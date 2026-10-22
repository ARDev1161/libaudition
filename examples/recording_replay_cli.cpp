// Runnable WAV + annotation sidecar export/replay example.
// Uses synthetic PCM, but real library file writers/readers and recording IDs.
#include <audition/audio/recording_timeline_json.hpp>
#include <audition/io/wav_io.hpp>
#include <audition/recording/source_recording_session.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;
constexpr std::uint32_t kRate = 16000U;
constexpr std::uint64_t kTrack = 7U;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
fs::path wav(const fs::path& root, const char* name) {
    return root / name;
}
fs::path sidecar(const fs::path& audio) {
    return fs::path{audio.string() + ".json"};
}

void exportExample(const fs::path& root) {
    fs::create_directories(root);
    audition::recording::SourceRecordingSession session;
    const audition::Timestamp origin{
        1000000000LL, {audition::ClockDomain::Monotonic, 1U}};
    const auto first = wav(root, "source-generation-101.wav");
    session.begin(kTrack, 101U, first, kRate, origin);
    {
        audition::io::StreamingPcm16WavWriter writer(first, kRate);
        const std::array<std::int16_t, 8> positive{
            1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000};
        const std::array<std::int16_t, 8> negative{
            -1000, -1000, -1000, -1000, -1000, -1000, -1000, -1000};
        writer.append(positive.data(), positive.size());
        // A lost 4-frame interval is represented by silence in this WAV.
        // Its reason and sample offsets are preserved in the JSON sidecar.
        writer.appendSilence(4U);
        writer.append(negative.data(), negative.size());
        writer.finish();
    }
    session.addSound(kTrack, 0U, 8U, "synthetic-tone", "demo", 1.0, "quick");
    session.addGap(kTrack, 8U, 4U);
    session.retire(kTrack, std::nullopt);

    // The numeric source/track ID can be reused; the recording ID cannot.
    const auto second = wav(root, "source-generation-102.wav");
    session.begin(kTrack, 102U, second, kRate,
                  audition::Timestamp{2000000000LL,
                                      {audition::ClockDomain::Monotonic, 1U}});
    {
        audition::io::StreamingPcm16WavWriter writer(second, kRate);
        const std::array<std::int16_t, 8> samples{
            -2000, -2000, -2000, -2000, -2000, -2000, -2000, -2000};
        writer.append(samples.data(), samples.size());
        writer.finish();
    }
    session.addSound(kTrack, 0U, 8U, "new-generation", "demo", 1.0, "quick");
    session.retire(kTrack, std::nullopt);
    require(session.activeRecordings() == 0U, "recordings not retired");
}

void replayAndValidate(const fs::path& root) {
    const auto first = wav(root, "source-generation-101.wav");
    const auto second = wav(root, "source-generation-102.wav");
    const auto a = audition::io::loadWav(first);
    const auto b = audition::io::loadWav(second);
    const auto ta = audition::readRecordingTimelineJson(sidecar(first));
    const auto tb = audition::readRecordingTimelineJson(sidecar(second));
    require(a.audio.format().sample_rate_hz == kRate &&
                b.audio.format().sample_rate_hz == kRate &&
                a.audio.format().channel_count == 1U &&
                b.audio.format().channel_count == 1U,
            "unexpected WAV format");
    require(ta.sampleRateHz() == kRate && tb.sampleRateHz() == kRate,
            "sidecar/WAV sample rate mismatch");
    require(a.audio.frameCount() == 20U && b.audio.frameCount() == 8U,
            "unexpected WAV frame count");
    require(ta.recordingId() == 101U && tb.recordingId() == 102U,
            "recording generation identity mismatch");
    require(ta.gaps().size() == 1U &&
                ta.gaps()[0].start_sample == 8U &&
                ta.gaps()[0].end_sample == 12U &&
                ta.gaps()[0].reason == "recorder_backpressure" &&
                tb.gaps().empty(),
            "gap belongs to the wrong recording or interval");
    require(ta.sounds().size() == 1U && tb.sounds().size() == 1U &&
                ta.sounds()[0].label == "synthetic-tone" &&
                ta.sounds()[0].end_sample == 8U &&
                tb.sounds()[0].label == "new-generation" &&
                tb.sounds()[0].start_sample == 0U,
            "annotations crossed a recording generation");
    const auto firstChannel = a.audio.channel(0U);
    const auto secondChannel = b.audio.channel(0U);
    for (std::size_t i = 0; i < 8U; ++i) {
        require(firstChannel[i] > 0.02F && firstChannel[i + 12U] < -0.02F,
                "PCM segments not preserved");
        require(secondChannel[i] < -0.05F, "reused-source PCM mismatch");
    }
    for (std::size_t i = 8U; i < 12U; ++i)
        require(firstChannel[i] == 0.0F, "gap silence not preserved");
}

struct TempDir {
    fs::path path;
    ~TempDir() noexcept {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

void selfTest() {
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch().count();
    TempDir temp{fs::temp_directory_path() /
                 ("libaudition-recording-replay-" + std::to_string(nonce))};
    exportExample(temp.path);
    replayAndValidate(temp.path);
    // Negative regression: a syntactically valid but wrong-rate sidecar
    // must not be accepted as a replay of the exported WAV.
    const auto firstSidecar = sidecar(wav(temp.path, "source-generation-101.wav"));
    const auto original = audition::readRecordingTimelineJson(firstSidecar);
    audition::RecordingTimeline corrupted{original.recordingId(), 8000U};
    for (const auto& event : original.speech()) corrupted.add(event);
    for (const auto& event : original.sounds()) corrupted.add(event);
    for (const auto& event : original.gaps()) corrupted.add(event);
    require(corrupted.sounds().size() == original.sounds().size() &&
                corrupted.gaps().size() == original.gaps().size() &&
                corrupted.speech().size() == original.speech().size(),
            "rate-only corruption unexpectedly changed annotations");
    audition::writeRecordingTimelineJson(firstSidecar, corrupted);
    bool rejectedRate = false;
    try {
        replayAndValidate(temp.path);
    } catch (const std::runtime_error& error) {
        rejectedRate =
            std::string{error.what()} == "sidecar/WAV sample rate mismatch";
    }
    require(rejectedRate, "sidecar/WAV rate mismatch was not specifically rejected");
    std::cout << "recording-export-replay=ok\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string{argv[1]} == "--self-test") {
            selfTest();
        } else if (argc == 3 && std::string{argv[1]} == "--export") {
            exportExample(argv[2]);
            std::cout << "recording-export=ok\n";
        } else if (argc == 3 && std::string{argv[1]} == "--replay") {
            replayAndValidate(argv[2]);
            std::cout << "recording-replay=ok\n";
        } else {
            throw std::invalid_argument(
                "usage: recording_replay_cli --export DIR | --replay DIR | --self-test");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "recording-replay=error: " << error.what() << '\n';
        return 1;
    }
}
