#include <audition/audition.hpp>

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <utility>

namespace {

audition::Timestamp ts(std::int64_t nanoseconds) {
    return audition::Timestamp{
        nanoseconds,
        {audition::ClockDomain::Monotonic, 0U}};
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 3) {
            std::cerr
                << "usage: libaudition_event_cli [label] [transcript]\n";
            return 2;
        }

        const std::string label =
            argc > 1 ? argv[1] : "alarm";
        const std::string text =
            argc > 2 ? argv[2] : "help";

        audition::AcousticEventAssembler assembler;

        audition::SpatialTrack track{};
        track.track_id = audition::SpatialTrackId{7U};
        track.source_id = audition::AcousticSourceId{3U};
        track.first_seen = ts(0);
        track.last_seen = ts(10);
        track.direction.direction =
            audition::Direction3D::fromVector({1.0, 0.0, 0.0});
        track.range = audition::RangeEstimate{
            {2.5, 0.25},
            audition::Probability::zero(),
            audition::RangeEstimate::Method::Fused,
        };

        const auto event_id = assembler.begin(ts(0));
        assembler.updateFromTrack(event_id, track);

        audition::AcousticEventPatch classification_patch{};
        classification_patch.timestamp = ts(20);
        audition::ClassificationResult classification{};
        classification.classes.push_back(
            {label, audition::Probability::from(0.90)});
        classification_patch.classification =
            std::move(classification);
        assembler.update(event_id, classification_patch);

        audition::AcousticEventPatch transcript_patch{};
        transcript_patch.timestamp = ts(30);
        audition::Transcript transcript{};
        transcript.segment_id = audition::SpeechSegmentId{1U};
        transcript.text = text;
        transcript.language = "en";
        transcript.confidence = audition::Probability::from(0.85);
        transcript_patch.transcript = std::move(transcript);
        assembler.update(event_id, transcript_patch);

        const audition::AcousticEvent event =
            assembler.finish(event_id, ts(40));

        std::cout << "event_id=" << event.event_id.value() << '\n';
        std::cout << "track_id=" << event.track_id->value() << '\n';
        std::cout << "source_id=" << event.source_id->value() << '\n';
        std::cout << "label="
                  << event.classification->classes.front().label << '\n';
        std::cout << "transcript=" << event.transcript->text << '\n';
        std::cout << "range_m=" << event.range->distance_m.mean << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
