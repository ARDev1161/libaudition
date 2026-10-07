#include <audition/backends/odas/odas_spatial_engine.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>

#include <audition/core/error.hpp>

#include "detail/odas_options_detail.hpp"
#include "detail/odas_runtime.hpp"

namespace audition {
namespace {

constexpr const char* kBackendName = "odas";
constexpr const char* kBackendVersion = "libodas-direct";

Probability clampedProbability(double value) {
    if (!std::isfinite(value)) {
        return Probability::zero();
    }
    return Probability::from(std::clamp(value, 0.0, 1.0));
}

}  // namespace

class OdasSpatialEngine::Impl {
public:
    explicit Impl(OdasOptions options)
        : options_(std::move(options)), input_channels_(odas_detail::resolvedInputChannels(options_)) {
        validateOdasOptions(options_);
        runtime_ = std::make_unique<odas_detail::OdasRuntime>(options_);
    }

    void reset() {
        runtime_->reset();
        first_seen_.clear();
    }

    [[nodiscard]] SpatialCapabilities capabilities() const {
        SpatialCapabilities result{};
        result.localization = true;
        result.tracking = true;
        result.separation = options_.sss.enabled;
        result.supports_3d = true;
        result.audio.supported_sample_rates_hz = {options_.sample_rate_hz};
        result.audio.min_channels = static_cast<std::uint32_t>(
            *std::max_element(input_channels_.begin(), input_channels_.end()) + 1U);
        result.audio.max_channels = std::numeric_limits<std::uint32_t>::max();
        result.audio.supported_layouts = {AudioLayout::Interleaved, AudioLayout::Planar};
        result.audio.preferred_frame_count = options_.hop_size;
        return result;
    }

    [[nodiscard]] SpatialProcessingResult process(AudioView audio) {
        const auto raw = runtime_->process(audio);
        SpatialProcessingResult result{};
        result.tracks.reserve(raw.active_slots.size());
        result.separated_frames.reserve(raw.active_slots.size());

        for (const auto& slot : raw.active_slots) {
            if (!std::isfinite(slot.direction.x) || !std::isfinite(slot.direction.y) ||
                !std::isfinite(slot.direction.z) || slot.direction.squaredNorm() <= 1.0e-18) {
                throw Error{ErrorCode::ProcessingError,
                            "ODAS produced an invalid tracked direction"};
            }

            const auto id = SpatialTrackId{slot.track_id};
            const auto activity = clampedProbability(slot.activity);
            DirectionEstimate direction{};
            direction.direction = Direction3D::fromVector(slot.direction);
            direction.angular_variance_rad2.reset();
            direction.confidence = Probability::zero();

            auto inserted = first_seen_.emplace(slot.track_id, audio.captureTime());
            SpatialTrack track{};
            track.track_id = id;
            track.direction = direction;
            track.activity = activity;
            track.first_seen = inserted.first->second;
            track.last_seen = audio.captureTime();
            result.tracks.push_back(track);

            if (slot.separated_audio.has_value()) {
                TrackedAudioFrame frame{};
                frame.track_id = id;
                frame.direction = direction;
                frame.activity = activity;
                frame.audio = AudioBuffer{
                    *slot.separated_audio,
                    AudioFormat{options_.sample_rate_hz, 1U, AudioLayout::Interleaved},
                    audio.captureTime(), audio.sequenceNumber()};
                result.separated_frames.push_back(std::move(frame));
            }
        }

        for (auto it = first_seen_.begin(); it != first_seen_.end();) {
            if (std::find(raw.live_tracker_ids.begin(), raw.live_tracker_ids.end(), it->first) ==
                raw.live_tracker_ids.end()) {
                it = first_seen_.erase(it);
            } else {
                ++it;
            }
        }
        return result;
    }

    [[nodiscard]] const OdasOptions& options() const noexcept { return options_; }

private:
    OdasOptions options_{};
    std::vector<std::size_t> input_channels_{};
    std::unique_ptr<odas_detail::OdasRuntime> runtime_{};
    std::unordered_map<std::uint64_t, Timestamp> first_seen_{};
};

OdasSpatialEngine::OdasSpatialEngine(OdasOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
OdasSpatialEngine::~OdasSpatialEngine() = default;
OdasSpatialEngine::OdasSpatialEngine(OdasSpatialEngine&&) noexcept = default;
OdasSpatialEngine& OdasSpatialEngine::operator=(OdasSpatialEngine&&) noexcept = default;
BackendInfo OdasSpatialEngine::backendInfo() const { return {kBackendName, kBackendVersion}; }
SpatialCapabilities OdasSpatialEngine::capabilities() const { return impl_->capabilities(); }
void OdasSpatialEngine::reset() { impl_->reset(); }
SpatialProcessingResult OdasSpatialEngine::process(AudioView audio) { return impl_->process(audio); }
const OdasOptions& OdasSpatialEngine::options() const noexcept { return impl_->options(); }

}  // namespace audition
