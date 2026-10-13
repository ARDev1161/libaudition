#include <audition/audio/peak_normalizer.hpp>
#include <audition/audio/recording_timeline.hpp>
#include <audition/audio/recording_timeline_json.hpp>
#include <audition/io/wav_io.hpp>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <stdexcept>
#include <vector>

int main() {
    const std::vector<std::int16_t> quiet{0, 100, -100, 1000, -1000};
    const auto boosted = audition::peakNormalizePcm16(quiet);
    assert(boosted.pcm.size() == quiet.size());
    assert(boosted.pcm[0] == 0);
    assert(boosted.applied_gain_db > 20.0);
    assert(boosted.output_peak_dbfs <= -2.9);
    assert(quiet[3] == 1000); // original remains unchanged
    const auto silent = audition::peakNormalizePcm16({0,0,0});
    assert(silent.applied_gain_db == 0);
    assert(silent.pcm.size() == 3);
    const auto loud = audition::peakNormalizePcm16({32767, -32768});
    assert(loud.applied_gain_db == 0); // default never attenuates
    audition::RecordingTimeline timeline(12, 16000);
    timeline.add(audition::TimelineSpeechEvent{12, 16000, 32000, "hello", "en"});
    timeline.add(audition::TimelineSoundEvent{12, 8000, 24000, "Speech", "YAMNet", 0.94, "refined"});
    assert(timeline.speech().size() == 1);
    assert(timeline.sounds().size() == 1);
    const auto json = audition::recordingTimelineToJson(timeline);
    assert(json.find("libaudition.recording-timeline.v1") != std::string::npos);
    assert(json.find(R"("start_sample":16000)") != std::string::npos);
    assert(json.find(R"("confidence":0.939)") != std::string::npos);
    timeline.add(audition::TimelineSpeechEvent{12, 32000, 48000,
                                              "quote \" slash \\ newline \n", "ru"});
    const auto escaped = audition::recordingTimelineToJson(timeline);
    assert(escaped.find(R"(quote \" slash \\ newline \n)") != std::string::npos);
    const auto sidecar = std::filesystem::temp_directory_path() /
                         "libaudition-recording-api-test.json";
    audition::writeRecordingTimelineJson(sidecar, timeline);
    {
        std::ifstream saved(sidecar);
        assert(saved.good());
        const std::string content((std::istreambuf_iterator<char>(saved)),
                                  std::istreambuf_iterator<char>());
        assert(content == escaped);
    }
    std::filesystem::remove(sidecar);
    const auto decoded = audition::recordingTimelineFromJson(escaped);
    assert(decoded.speech().size() == timeline.speech().size());
    assert(decoded.sounds().size() == 1);
    assert(decoded.recordingId() == timeline.recordingId());
    const auto wav = std::filesystem::temp_directory_path() /
                     "libaudition-streaming-test.wav";
    const auto normalizedPath = std::filesystem::temp_directory_path() /
                                "libaudition-streaming-normalized.wav";
    {
        audition::io::StreamingPcm16WavWriter writer(wav, 16000);
        const std::vector<std::int16_t> values(32000U, 100);
        writer.append(values.data(), values.size());
        writer.appendSilence(16000U);
        writer.finish();
        assert(writer.sampleCount() == 48000U);
    }
    const auto normalizedFile = audition::io::normalizePcm16Wav(wav, normalizedPath);
    assert(normalizedFile.samples_written == 48000U);
    assert(normalizedFile.gain_db > 35.0);
    assert(normalizedFile.gain_db <= 40.0);
    const auto originalWav = audition::io::loadWav(wav);
    const auto changedWav = audition::io::loadWav(normalizedPath);
    assert(originalWav.audio.sampleCount() == changedWav.audio.sampleCount());
    assert(std::abs(originalWav.audio.samples()[0] - 100.F / 32768.F) < 1e-6);
    assert(changedWav.audio.samples()[0] > originalWav.audio.samples()[0]);
    assert(changedWav.audio.samples()[40000] == 0.0F);
    std::filesystem::remove(wav);
    std::filesystem::remove(normalizedPath);
    bool rejected = false;
    try { timeline.add(audition::TimelineSpeechEvent{11, 0, 20, "wrong", ""}); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
}
