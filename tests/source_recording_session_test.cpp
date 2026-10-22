#include <audition/recording/source_recording_session.hpp>
#include <audition/audio/recording_timeline_json.hpp>
#include <audition/dsp/streaming_level_meter.hpp>
#include <audition/classify/progressive_window_policy.hpp>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>

int main() {
    namespace fs = std::filesystem;
    using audition::Timestamp;
    using audition::ClockDomain;
    using audition::recording::SourceRecordingSession;
    const auto root = fs::temp_directory_path() /
        ("audition-session-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    const auto first = root / "first.wav";
    const auto second = root / "second.wav";
    SourceRecordingSession records;
    const Timestamp origin{1000000000LL, {ClockDomain::Monotonic, 2U}};
    records.begin(3U, 10U, first, 16000U, origin);
    records.bindAsr(3U, 100U);
    records.addSound(3U, 0U, 16000U, "Speech", "YAMNet", .9, "quick");
    records.addSound(3U, 0U, 16000U, "Speech", "YAMNet", .9, "quick"); // dedup
    records.addGap(3U, 16000U, 400U);
    records.addGap(3U, 16400U, 600U);
    records.retire(3U, 100U);
    assert(records.pendingRetirements() == 1U);
    records.begin(3U, 11U, second, 16000U,
                  Timestamp{2000000000LL, {ClockDomain::Monotonic, 2U}});
    records.bindAsr(3U, 101U);
    audition::SourceSpeechResult old{};
    old.track_id = 3U;
    old.generation = 100U;
    old.start_time = Timestamp{1000000000LL, {ClockDomain::Monotonic, 2U}};
    old.end_time = Timestamp{2000000000LL, {ClockDomain::Monotonic, 2U}};
    old.transcript.text = "Привет\n\"робот\"";
    old.transcript.language = "ru";
    records.addSpeech(old);
    records.completeGeneration(100U);
    assert(records.pendingRetirements() == 0U);
    const auto loaded = audition::readRecordingTimelineJson(
        fs::path{first.string() + ".json"});
    assert(loaded.recordingId() == 10U);
    assert(loaded.sounds().size() == 1U);
    assert(loaded.speech().size() == 1U);
    assert(loaded.speech()[0].text == old.transcript.text);
    assert(loaded.speech()[0].start_sample == 0U);
    assert(loaded.speech()[0].end_sample == 16000U);
    assert(loaded.gaps().size() == 1U);
    assert(loaded.gaps()[0].start_sample == 16000U);
    assert(loaded.gaps()[0].end_sample == 17000U);
    records.retire(3U, 101U);
    records.completeGeneration(101U);
    const auto next = audition::readRecordingTimelineJson(
        fs::path{second.string() + ".json"});
    assert(next.speech().empty());
    assert(next.gaps().empty());
    assert(records.activeRecordings() == 0U);
    auto meter = audition::dsp::StreamingLevelMeter{};
    meter.push(0.5f);
    meter.push(-0.5f);
    const auto peak = meter.snapshotAndReset();
    assert(peak.sample_count == 2U);
    assert(std::abs(peak.peak_dbfs - (-6.020599913)) < .001);
    assert(meter.snapshotAndReset().sample_count == 0U);
    const auto windows = audition::makeFastRefinedWindowPolicy(16000,15600,80000);
    assert(windows.fast_hop == 7800U);
    assert(windows.refined_hop == 16000U);
    bool invalid = false;
    try { (void)audition::recordingTimelineFromJson("{bad}"); }
    catch (const std::invalid_argument&) { invalid = true; }
    assert(invalid);
    fs::remove_all(root);
}
