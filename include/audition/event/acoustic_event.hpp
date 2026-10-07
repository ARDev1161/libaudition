#pragma once

#include <optional>
#include <vector>

#include <audition/authenticity/types.hpp>
#include <audition/classify/types.hpp>
#include <audition/core/geometry.hpp>
#include <audition/core/id.hpp>
#include <audition/speaker/types.hpp>
#include <audition/speech/types.hpp>
#include <audition/voice/types.hpp>

namespace audition {

struct AcousticEvent {
    AcousticEventId event_id{};
    Timestamp start_time{};
    Timestamp end_time{};
    std::optional<SpatialTrackId> track_id{};
    std::optional<AcousticSourceId> source_id{};

    std::optional<DirectionEstimate> direction{};
    std::optional<RangeEstimate> range{};
    std::optional<PositionEstimate> position{};

    std::optional<ClassificationResult> classification{};
    std::optional<Transcript> transcript{};
    std::optional<SpeakerIdentity> speaker{};
    std::optional<VoiceTraits> voice_traits{};
    std::optional<VoiceState> voice_state{};
    std::optional<AuthenticityResult> authenticity{};
};

}  // namespace audition
