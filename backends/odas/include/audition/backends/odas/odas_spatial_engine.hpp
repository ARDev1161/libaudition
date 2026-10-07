#pragma once

#include <memory>

#include <audition/backends/odas/odas_options.hpp>
#include <audition/core/export.hpp>
#include <audition/interfaces/spatial.hpp>

namespace audition {

class AUDITION_API OdasSpatialEngine final : public ISpatialAudioEngine {
public:
    explicit OdasSpatialEngine(OdasOptions options);
    ~OdasSpatialEngine() override;
    OdasSpatialEngine(const OdasSpatialEngine&) = delete;
    OdasSpatialEngine& operator=(const OdasSpatialEngine&) = delete;
    OdasSpatialEngine(OdasSpatialEngine&&) noexcept;
    OdasSpatialEngine& operator=(OdasSpatialEngine&&) noexcept;
    [[nodiscard]] BackendInfo backendInfo() const override;
    [[nodiscard]] SpatialCapabilities capabilities() const override;
    void reset() override;
    [[nodiscard]] SpatialProcessingResult process(AudioView multichannel_audio) override;
    [[nodiscard]] const OdasOptions& options() const noexcept;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

AUDITION_API void validateOdasOptions(const OdasOptions& options);

}  // namespace audition
