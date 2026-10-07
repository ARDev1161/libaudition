#pragma once

#include <audition/backend/capabilities.hpp>
#include <audition/spatial/types.hpp>

namespace audition {

/**
 * @brief Produces an acoustic fingerprint for generic persistent-source association.
 *
 * A fingerprint is deliberately distinct from a speaker embedding: it may
 * describe a motor, alarm, animal, loudspeaker, person, or another recurring source.
 */
class ISourceFingerprintExtractor {
public:
    virtual ~ISourceFingerprintExtractor() = default;
    [[nodiscard]] virtual BackendInfo backendInfo() const = 0;
    [[nodiscard]] virtual SourceFingerprint extract(const TrackedAudioFrame& frame) const = 0;
};

}  // namespace audition
