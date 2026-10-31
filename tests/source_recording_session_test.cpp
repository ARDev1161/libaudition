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
#include <limits>
#include <stdexcept>
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
    assert(records.boundAsrGenerations() == 0U);

    // Repeat the exact fallback path used when the speech retirement
    // table is saturated: the recording already has an ASR binding but
    // cannot be retired with a generation. The binding must not survive.
    for (std::uint64_t i = 0; i < 48U; ++i) {
        const auto path = root / ("fallback-" + std::to_string(i) + ".wav");
        const auto track_id = 400U + i;
        records.begin(track_id, 4000U + i, path, 16000U, origin);
        records.bindAsr(track_id, 200U);
        assert(records.boundAsrGenerations() == 1U);
        records.retire(track_id, std::nullopt);
        assert(records.boundAsrGenerations() == 0U);
        assert(records.activeRecordings() == 0U);
        assert(fs::exists(fs::path{path.string() + ".json"}));
    }
    // A duplicate generation passed to retire() must not steal an existing
    // recording's ASR result or retire the rejected active track.
    const auto collision_a = root / "collision-a.wav";
    const auto collision_b = root / "collision-b.wav";
    records.begin(70U, 700U, collision_a, 16000U, origin);
    records.begin(71U, 701U, collision_b, 16000U, origin);
    records.bindAsr(70U, 900U);
    records.retire(70U, 900U);
    bool collision_rejected = false;
    try { records.retire(71U, 900U); }
    catch (const std::invalid_argument&) { collision_rejected = true; }
    assert(collision_rejected);
    assert(records.active(71U));
    assert(records.recordingId(71U) == 701U);
    assert(records.pendingRetirements() == 1U);
    assert(records.boundAsrGenerations() == 1U);
    records.completeGeneration(900U);
    records.retire(71U, std::nullopt);
    assert(records.pendingRetirements() == 0U);
    assert(records.boundAsrGenerations() == 0U);
    assert(fs::exists(fs::path{collision_a.string() + ".json"}));
    assert(fs::exists(fs::path{collision_b.string() + ".json"}));


    // The producer can retire an old WAV before the worker finishes VAD.
    // A reused numeric ID must never steal late text or the retired WAV.
    const auto deferred_a = root / "deferred-a.wav";
    const auto deferred_b = root / "deferred-b.wav";
    records.begin(85U, 785U, deferred_a, 16000U, origin);
    const auto delayed = records.retireDeferred(85U);
    assert(delayed == 785U);
    assert(!records.active(85U) && records.pendingRetirements() == 1U);
    records.begin(85U, 786U, deferred_b, 16000U, origin);
    records.bindAsr(85U, 985U);
    records.bindRecordingAsr(785U, 984U);
    bool deferred_collision = false;
    try { records.acknowledgeRetired(*delayed,985U); }
    catch (const std::invalid_argument&) { deferred_collision = true; }
    assert(deferred_collision);
    assert(records.pendingRetirements() == 1U);
    records.acknowledgeRetired(*delayed,984U);
    audition::SourceSpeechResult delayed_text{};
    delayed_text.track_id=85U;
    delayed_text.generation=984U;
    delayed_text.start_time=origin;
    delayed_text.end_time=origin;
    delayed_text.transcript.text="old-utterance";
    records.addSpeech(delayed_text);
    records.completeGeneration(984U);
    const auto saved_deferred=audition::readRecordingTimelineJson(
        fs::path{deferred_a.string()+".json"});
    assert(saved_deferred.speech().size()==1U);
    assert(saved_deferred.speech()[0].text=="old-utterance");
    assert(records.active(85U));
    records.retire(85U,985U);
    records.completeGeneration(985U);
    const auto saved_new=audition::readRecordingTimelineJson(
        fs::path{deferred_b.string()+".json"});
    assert(saved_new.speech().empty());
    assert(records.boundAsrGenerations()==0U);

    const auto expired_path=root/"deferred-expired.wav";
    records.begin(86U,787U,expired_path,16000U,origin);
    const auto expired=records.retireDeferred(86U);
    assert(expired==787U);
    records.acknowledgeRetired(*expired,std::nullopt);
    assert(records.pendingRetirements()==0U);
    assert(fs::exists(fs::path{expired_path.string()+".json"}));
    bool double_ack_rejected=false;
    try {records.acknowledgeRetired(*expired,std::nullopt);}
    catch(const std::invalid_argument&) {double_ack_rejected=true;}
    assert(double_ack_rejected);

    // Safe acknowledgement ignores a delayed duplicate, but never binds
    // a retired WAV to a reused source ID's new ASR generation.
    assert(!records.tryAcknowledgeRetired(*expired,std::nullopt));
    assert(!records.tryAcknowledgeRetired(999999U,123U));
    const auto safe_old_path=root/"safe-old.wav";
    const auto safe_new_path=root/"safe-new.wav";
    records.begin(87U,788U,safe_old_path,16000U,origin);
    const auto safe_old=records.retireDeferred(87U);
    assert(safe_old && *safe_old==788U);
    records.begin(87U,789U,safe_new_path,16000U,origin);
    records.bindAsr(87U,990U);
    bool wrong_generation_rejected=false;
    try { (void)records.tryAcknowledgeRetired(*safe_old,990U); }
    catch(const std::invalid_argument&) { wrong_generation_rejected=true; }
    assert(wrong_generation_rejected);
    assert(records.pendingRetirements()==1U);
    assert(records.tryAcknowledgeRetired(*safe_old,std::nullopt));
    assert(!records.tryAcknowledgeRetired(*safe_old,std::nullopt));
    assert(records.active(87U));
    records.retire(87U,990U);
    records.completeGeneration(990U);

    // Invalid gap arithmetic must not create a wrapped or partial event.
    const auto overflow_path = root / "gap-overflow.wav";
    records.begin(72U, 702U, overflow_path, 16000U, origin);
    bool gap_rejected = false;
    try { records.addGap(72U, std::numeric_limits<std::uint64_t>::max(), 2U); }
    catch (const std::length_error&) { gap_rejected = true; }
    assert(gap_rejected && records.active(72U));
    records.addGap(72U, 1U, 2U);
    records.retire(72U, std::nullopt);
    const auto gap_record = audition::readRecordingTimelineJson(
        fs::path{overflow_path.string() + ".json"});
    assert(gap_record.gaps().size() == 1U);
    assert(gap_record.gaps()[0].start_sample == 1U);
    assert(gap_record.gaps()[0].end_sample == 3U);

    // A valid timestamp delta may exceed INT64_MAX nanoseconds. Conversion
    // must not overflow signed arithmetic or lose integer sample precision.
    const auto wide_path = root / "wide-timestamps.wav";
    const auto low = std::numeric_limits<std::int64_t>::min();
    const auto high = std::numeric_limits<std::int64_t>::max();
    records.begin(73U, 703U, wide_path, 16000U,
                  Timestamp{low, {ClockDomain::Monotonic, 2U}});
    records.bindAsr(73U, 903U);
    audition::SourceSpeechResult wide{};
    wide.generation = 903U;
    wide.start_time = Timestamp{high, {ClockDomain::Monotonic, 2U}};
    wide.end_time = wide.start_time;
    wide.transcript.text = "wide";
    records.addSpeech(wide);
    records.retire(73U, 903U);
    records.completeGeneration(903U);
    const auto wide_record = audition::readRecordingTimelineJson(
        fs::path{wide_path.string() + ".json"});
    assert(wide_record.speech().size() == 1U);
    assert(wide_record.speech()[0].start_sample ==
           static_cast<std::uint64_t>(
               ((static_cast<std::uint64_t>(high) -
                 static_cast<std::uint64_t>(low)) / 1000000000ULL) * 16000ULL +
               (((static_cast<std::uint64_t>(high) -
                  static_cast<std::uint64_t>(low)) % 1000000000ULL) *
                 16000ULL) / 1000000000ULL));

    // Excessive sample-rate * time spans cannot be represented in uint64;
    // discard the annotation rather than committing a wrapped offset.
    const auto out_of_range_path = root / "offset-overflow.wav";
    records.begin(74U, 704U, out_of_range_path,
                  std::numeric_limits<std::uint32_t>::max(),
                  Timestamp{low, {ClockDomain::Monotonic, 2U}});
    records.bindAsr(74U, 904U);
    wide.generation = 904U;
    records.addSpeech(wide);
    records.retire(74U, 904U);
    records.completeGeneration(904U);
    const auto out_of_range = audition::readRecordingTimelineJson(
        fs::path{out_of_range_path.string() + ".json"});
    assert(out_of_range.speech().empty());
    assert(records.boundAsrGenerations() == 0U);


    // Annotation memory is bounded per recording, even if the client does
    // not rotate a long capture and model result rates remain high.
    {
        audition::recording::SourceRecordingOptions limits{};
        limits.max_recordings = 1U;
        limits.max_speech_events = 1U;
        limits.max_sound_events = 1U;
        limits.max_gap_events = 1U;
        limits.max_annotation_text_bytes = 12U;
        SourceRecordingSession bounded{limits};
        const auto path = root / "bounded.wav";
        bounded.begin(880U, 880U, path, 16000U, origin);
        assert(bounded.retainedRecordings() == 1U);
        bool saturated = false;
        try { bounded.begin(881U, 881U, root / "too-many.wav", 16000U, origin); }
        catch (const std::length_error&) { saturated = true; }
        assert(saturated && !bounded.active(881U));
        // Repeat capacity refusal while keeping the existing timeline.
        for (int n = 0; n < 16; ++n) {
            assert(!bounded.tryBegin(881U, 881U,
                                     root / "denied.wav", 16000U, origin));
            assert(bounded.retainedRecordings() == 1U);
            assert(bounded.active(880U) && !bounded.active(881U));
        }

        bounded.bindAsr(880U, 880U);
        audition::SourceSpeechResult utterance{};
        utterance.generation = 880U;
        utterance.start_time = origin;
        utterance.end_time = origin;
        utterance.transcript.text = "yes";
        utterance.transcript.language = "en";
        bounded.addSpeech(utterance);
        bounded.addSpeech(utterance); // second speech result dropped
        bounded.addSound(880U, 1U, 2U, "tap", "m", .8, "q");
        bounded.addSound(880U, 3U, 4U, "tap", "m", .8, "q");
        bounded.addGap(880U, 0U, 2U);
        bounded.addGap(880U, 2U, 2U); // merged at event capacity
        bounded.addGap(880U, 10U, 2U); // unrelated gap dropped
        assert(bounded.droppedAnnotations().speech == 1U);
        assert(bounded.droppedAnnotations().sound == 1U);
        assert(bounded.droppedAnnotations().gaps == 1U);
        bounded.retire(880U, 880U);
        bounded.completeGeneration(880U);
        assert(bounded.retainedRecordings() == 0U);
        const auto saved = audition::readRecordingTimelineJson(
            fs::path{path.string() + ".json"});
        assert(saved.speech().size() == 1U);
        assert(saved.sounds().size() == 1U);
        assert(saved.gaps().size() == 1U);
        assert(saved.gaps()[0].end_sample == 4U);

        assert(bounded.tryBegin(881U, 881U, root / "recovered.wav",
                                16000U, origin));
        bounded.addSound(881U, 0U, 1U, "oversized-label", "m", .5, "q");
        assert(bounded.droppedAnnotations().sound == 2U);
        bounded.retire(881U, std::nullopt);
        assert(bounded.retainedRecordings() == 0U);
        bool invalid_options = false;
        try {
            limits.max_sound_events = 0U;
            (void)SourceRecordingSession{limits};
        } catch (const std::invalid_argument&) { invalid_options = true; }
        assert(invalid_options);
    }


    // A scenario may combine several model adapters on the same source.
    // De-dup only an exact model/stage/window, not a different classifier
    // or a distinct overlapping window that happens to share its end.
    const auto multimodel_path = root / "multi-model.wav";
    records.begin(91U, 791U, multimodel_path, 16000U, origin);
    records.addSound(91U, 0U, 8000U, "speech", "YAMNet", .90, "quick");
    records.addSound(91U, 0U, 8000U, "speech", "YAMNet", .90, "quick");
    records.addSound(91U, 0U, 8000U, "speech", "EfficientAT", .85, "quick");
    records.addSound(91U, 4000U, 8000U, "speech", "YAMNet", .80, "quick");
    records.addSound(91U, 0U, 8000U, "speech", "YAMNet", .95, "refined");
    records.retire(91U, std::nullopt);
    const auto multi = audition::readRecordingTimelineJson(
        fs::path{multimodel_path.string() + ".json"});
    assert(multi.sounds().size() == 4U);
    assert(multi.sounds()[0].model == "YAMNet" &&
           multi.sounds()[0].start_sample == 0U &&
           multi.sounds()[0].end_sample == 8000U);
    assert(multi.sounds()[1].model == "EfficientAT" &&
           multi.sounds()[1].stage == "quick");
    assert(multi.sounds()[2].model == "YAMNet" &&
           multi.sounds()[2].start_sample == 4000U);
    assert(multi.sounds()[3].stage == "refined");
    assert(records.retainedRecordings() == 0U);


    // Multi-label models legitimately return multiple classes for a
    // single model/stage/window. Duplicates must still be suppressed.
    const auto multilabel_path = root / "multi-label.wav";
    records.begin(92U, 792U, multilabel_path, 16000U, origin);
    const auto sound_drops_before=records.droppedAnnotations().sound;
    records.addSound(92U, 0U, 8000U, "speech", "YAMNet", .85, "quick");
    records.addSound(92U, 0U, 8000U, "music", "YAMNet", .70, "quick");
    records.addSound(92U, 0U, 8000U, "music", "YAMNet", .70, "quick");
    records.addSound(92U, 0U, 8000U, "music", "EfficientAT", .60, "quick");
    // Bad scores from an optional model must not interrupt recording.
    records.addSound(92U, 0U, 8000U, "invalid-nan", "YAMNet",
                     std::numeric_limits<double>::quiet_NaN(), "quick");
    records.addSound(92U, 0U, 8000U, "invalid-inf", "YAMNet",
                     std::numeric_limits<double>::infinity(), "quick");
    records.addSound(92U, 0U, 8000U, "invalid-negative", "YAMNet", -.1, "quick");
    records.addSound(92U, 0U, 8000U, "invalid-over", "YAMNet", 1.1, "quick");
    assert(records.droppedAnnotations().sound==sound_drops_before+4U);
    // An invalid score cannot reserve the duplicate key for its label.
    records.addSound(92U, 0U, 8000U, "invalid-over", "YAMNet", 1.0, "quick");
    records.addSound(92U, 0U, 8000U, "zero", "YAMNet", 0.0, "quick");
    records.retire(92U, std::nullopt);
    const auto multilabel=audition::readRecordingTimelineJson(
        fs::path{multilabel_path.string()+".json"});
    assert(multilabel.sounds().size()==5U);
    assert(multilabel.sounds()[0].label=="speech");
    assert(multilabel.sounds()[1].label=="music");
    assert(multilabel.sounds()[1].model=="YAMNet");
    assert(multilabel.sounds()[2].label=="music");
    assert(multilabel.sounds()[2].model=="EfficientAT");
    assert(multilabel.sounds()[3].label=="invalid-over");
    assert(multilabel.sounds()[3].confidence==1.0);
    assert(multilabel.sounds()[4].label=="zero");
    assert(multilabel.sounds()[4].confidence==0.0);
    assert(records.retainedRecordings()==0U);

    // A muted speech DSP hop is not a recorder queue drop. Adjacent gaps
    // coalesce only when their reasons agree; old callers keep the original
    // recorder_backpressure default.
    const auto gap_reasons_path = root / "gap-reasons.wav";
    records.begin(95U, 795U, gap_reasons_path, 16000U, origin);
    records.addGap(95U, 0U, 20U);
    records.addGap(95U, 20U, 15U,
        audition::recording::RecordingGapReason::SpeechPreprocessingFailure);
    records.addGap(95U, 35U, 25U,
        audition::recording::RecordingGapReason::SpeechPreprocessingFailure);
    records.addGap(95U, 60U, 10U);
    bool invalid_reason_rejected = false;
    try {
        records.addGap(95U, 70U, 5U,
            static_cast<audition::recording::RecordingGapReason>(255));
    } catch (const std::invalid_argument&) {
        invalid_reason_rejected = true;
    }
    assert(invalid_reason_rejected && records.active(95U));
    records.retire(95U, std::nullopt);
    const auto gap_reasons = audition::readRecordingTimelineJson(
        fs::path{gap_reasons_path.string() + ".json"});
    assert(gap_reasons.gaps().size() == 3U);
    assert(gap_reasons.gaps()[0].start_sample == 0U &&
           gap_reasons.gaps()[0].end_sample == 20U &&
           gap_reasons.gaps()[0].reason == "recorder_backpressure");
    assert(gap_reasons.gaps()[1].start_sample == 20U &&
           gap_reasons.gaps()[1].end_sample == 60U &&
           gap_reasons.gaps()[1].reason == "speech_preprocessing_failure");
    assert(gap_reasons.gaps()[2].start_sample == 60U &&
           gap_reasons.gaps()[2].end_sample == 70U &&
           gap_reasons.gaps()[2].reason == "recorder_backpressure");

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
