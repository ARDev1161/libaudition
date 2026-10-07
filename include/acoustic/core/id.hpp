#pragma once

#include <cstdint>
#include <functional>
#include <type_traits>

namespace acoustic {

template <typename Tag>
class StrongId {
public:
    using value_type = std::uint64_t;

    constexpr StrongId() noexcept = default;
    explicit constexpr StrongId(value_type value) noexcept : value_(value) {}

    [[nodiscard]] constexpr value_type value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool valid() const noexcept { return value_ != 0; }
    explicit constexpr operator bool() const noexcept { return valid(); }

    friend constexpr bool operator==(StrongId lhs, StrongId rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }
    friend constexpr bool operator!=(StrongId lhs, StrongId rhs) noexcept { return !(lhs == rhs); }
    friend constexpr bool operator<(StrongId lhs, StrongId rhs) noexcept {
        return lhs.value_ < rhs.value_;
    }

private:
    value_type value_{0};
};

template <typename Id>
struct StrongIdHash {
    std::size_t operator()(Id id) const noexcept {
        static_assert(std::is_same_v<typename Id::value_type, std::uint64_t>, "Unsupported ID type");
        return std::hash<std::uint64_t>{}(id.value());
    }
};

struct SpatialTrackIdTag;
struct AcousticSourceIdTag;
struct SpeechSegmentIdTag;
struct AcousticEventIdTag;
struct SpeakerIdTag;
struct AudioStreamIdTag;
struct SoundPrototypeIdTag;

using SpatialTrackId = StrongId<SpatialTrackIdTag>;
using AcousticSourceId = StrongId<AcousticSourceIdTag>;
using SpeechSegmentId = StrongId<SpeechSegmentIdTag>;
using AcousticEventId = StrongId<AcousticEventIdTag>;
using SpeakerId = StrongId<SpeakerIdTag>;
using AudioStreamId = StrongId<AudioStreamIdTag>;
using SoundPrototypeId = StrongId<SoundPrototypeIdTag>;

}  // namespace acoustic
