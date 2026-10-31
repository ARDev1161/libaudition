#include <audition/audio/recording_timeline_json.hpp>
#include <audition/dsp/source_speech_chain.hpp>
#include <audition/io/wav_io.hpp>
#include <audition/recording/source_recording_session.hpp>
#include <audition/recording/source_wav_recorder.hpp>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class ScriptedGain final : public audition::dsp::ISourceGain {
public:
    explicit ScriptedGain(std::size_t fail_on = 0U)
        : fail_on_(fail_on) {}

    std::vector<float> process(const float* pcm, std::size_t count,
                               std::uint32_t rate) override {
        assert(rate == 16000U && pcm);
        ++calls_;
        if (calls_ == fail_on_)
            throw std::runtime_error("synthetic source gain failure");
        std::vector<float> result(count);
        for (std::size_t i = 0; i < count; ++i)
            result[i] = pcm[i] * 2.0F;
        return result;
    }
    double gainDb() const noexcept override { return 6.020599913; }
    void reset() noexcept override { calls_ = 0U; }

private:
    std::size_t fail_on_{0U};
    std::size_t calls_{0U};
};

void expectPcm(const std::vector<float>& pcm, std::size_t begin,
               std::size_t end, float target) {
    assert(begin <= end && end <= pcm.size());
    for (std::size_t i = begin; i < end; ++i)
        assert(std::abs(pcm[i] - target) < 0.0002F);
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using audition::recording::RecordingGapReason;
    using audition::recording::SourceRecordingSession;
    using audition::recording::SourceWavRecorder;

    const auto root = fs::temp_directory_path() /
        ("libaudition-speech-recording-" +
         std::to_string(std::chrono::steady_clock::now()
                            .time_since_epoch().count()));
    fs::create_directories(root);
    constexpr std::uint32_t rate = 16000U;
    constexpr std::size_t hop_size = 128U;
    const audition::Timestamp origin{
        1000000000LL, {audition::ClockDomain::Monotonic, 2U}};

    SourceRecordingSession session;
    SourceWavRecorder recorder(root, rate);
    ScriptedGain first_gain(2U);  // Only source 11 fails, on its second hop.
    ScriptedGain second_gain;
    ScriptedGain reused_gain;

    const std::vector<float> source_a(hop_size, 0.125F);
    const std::vector<float> source_b(hop_size, 0.25F);
    std::uint64_t first_id = 0U, second_id = 0U, reused_id = 0U;
    fs::path first_path, second_path, reused_path;

    const auto accept = [&](std::uint64_t track, const std::vector<float>& raw,
                            ScriptedGain& gain, std::uint64_t& id,
                            fs::path& path) {
        const auto hop = audition::dsp::processSourceSpeechHopOrMute(
            raw.data(), raw.size(), rate, nullptr, &gain);
        assert(hop.samples.size() == raw.size());
        const auto saved = recorder.append(track, hop.samples);
        assert(saved.recording_id != 0U);
        assert(saved.accepted_samples == hop.samples.size());
        assert(saved.lost_samples == 0U);
        if (!session.active(track)) {
            const auto entry = recorder.activeEntry(track);
            assert(entry && entry->recording_id == saved.recording_id);
            session.begin(track, entry->recording_id, entry->path, rate, origin);
            id = entry->recording_id;
            path = entry->path;
        }
        if (hop.failed) {
            assert(!hop.error.empty());
            for (float sample : hop.samples) assert(sample == 0.0F);
            session.addGap(track, saved.start_sample, saved.accepted_samples,
                           RecordingGapReason::SpeechPreprocessingFailure);
        } else {
            for (float sample : hop.samples) assert(sample > 0.0F);
        }
        return hop.failed;
    };

    assert(!accept(11U, source_a, first_gain, first_id, first_path));
    assert(!accept(22U, source_b, second_gain, second_id, second_path));
    assert(accept(11U, source_a, first_gain, first_id, first_path));
    assert(!accept(22U, source_b, second_gain, second_id, second_path));
    assert(!accept(11U, source_a, first_gain, first_id, first_path));

    // Track 11 retires while track 22 remains active. Reusing the numeric
    // track identity must open an independent WAV/JSON generation.
    recorder.retire(11U);
    session.retire(11U, std::nullopt);
    assert(!accept(11U, source_a, reused_gain, reused_id, reused_path));
    assert(reused_id != first_id);
    assert(reused_path != first_path);
    recorder.retire(11U);
    session.retire(11U, std::nullopt);
    recorder.retire(22U);
    session.retire(22U, std::nullopt);
    recorder.closeAll(); // Wait for queued audio and all WAV headers to flush.

    const auto first = audition::io::loadWav(first_path);
    const auto second = audition::io::loadWav(second_path);
    const auto reused = audition::io::loadWav(reused_path);
    assert(first.audio.samples().size() == hop_size * 3U);
    assert(second.audio.samples().size() == hop_size * 2U);
    assert(reused.audio.samples().size() == hop_size);
    expectPcm(first.audio.samples(), 0U, hop_size, 0.25F);
    expectPcm(first.audio.samples(), hop_size, hop_size * 2U, 0.0F);
    expectPcm(first.audio.samples(), hop_size * 2U, hop_size * 3U, 0.25F);
    expectPcm(second.audio.samples(), 0U, hop_size * 2U, 0.5F);
    expectPcm(reused.audio.samples(), 0U, hop_size, 0.25F);

    const auto first_json = audition::readRecordingTimelineJson(
        fs::path{first_path.string() + ".json"});
    const auto second_json = audition::readRecordingTimelineJson(
        fs::path{second_path.string() + ".json"});
    const auto reused_json = audition::readRecordingTimelineJson(
        fs::path{reused_path.string() + ".json"});
    assert(first_json.recordingId() == first_id);
    assert(second_json.recordingId() == second_id);
    assert(reused_json.recordingId() == reused_id);
    assert(first_json.gaps().size() == 1U);
    assert(first_json.gaps()[0].start_sample == hop_size);
    assert(first_json.gaps()[0].end_sample == hop_size * 2U);
    assert(first_json.gaps()[0].reason == "speech_preprocessing_failure");
    assert(second_json.gaps().empty());
    assert(reused_json.gaps().empty());

    fs::remove_all(root);
}
