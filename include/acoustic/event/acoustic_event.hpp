#pragma once

#include <optional>
#include <vector>

#include <acoustic/authenticity/types.hpp>
#include <acoustic/classify/types.hpp>
#include <acoustic/core/geometry.hpp>
#include <acoustic/core/id.hpp>
#include <acoustic/speaker/types.hpp>
#include <acoustic/speech/types.hpp>
#include <acoustic/voice/types.hpp>

namespace acoustic {

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

}  // namespace acoustic
