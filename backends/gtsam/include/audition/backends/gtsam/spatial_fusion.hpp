#pragma once

#include <memory>
#include <optional>

#include <audition/backends/gtsam/options.hpp>
#include <audition/core/export.hpp>
#include <audition/interfaces/spatial.hpp>

namespace audition {

class AUDITION_API GtsamSpatialFusion final : public ISpatialFusion {
public:
    explicit GtsamSpatialFusion(GtsamSpatialFusionOptions options = {});
    ~GtsamSpatialFusion() override;

    GtsamSpatialFusion(const GtsamSpatialFusion&) = delete;
    GtsamSpatialFusion& operator=(const GtsamSpatialFusion&) = delete;
    GtsamSpatialFusion(GtsamSpatialFusion&&) noexcept;
    GtsamSpatialFusion& operator=(GtsamSpatialFusion&&) noexcept;

    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] std::optional<PositionEstimate> fuse(
        const SpatialFusionInput& input) const override;

    [[nodiscard]] const GtsamSpatialFusionOptions& options() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition
