#include <audition/audio/peak_normalizer.hpp>
#include <audition/audio/recording_timeline.hpp>
#include <audition/audio/recording_timeline_json.hpp>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdint>
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
    assert(json.find("\\"start_sample\\":16000") != std::string::npos);
    assert(json.find("\\"confidence\\":0.939") != std::string::npos);
    timeline.add(audition::TimelineSpeechEvent{12, 32000, 48000,
                                              "quote \\" slash \\\\ newline \\n", "ru"});
    const auto escaped = audition::recordingTimelineToJson(timeline);
    assert(escaped.find("quote \\\\" slash \\\\\\\\ newline \\\\n") != std::string::npos);
    bool rejected = false;
    try { timeline.add(audition::TimelineSpeechEvent{11, 0, 20, "wrong", ""}); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
}
