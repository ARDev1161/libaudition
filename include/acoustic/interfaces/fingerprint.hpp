#pragma once

#include <acoustic/backend/capabilities.hpp>
#include <acoustic/spatial/types.hpp>

namespace acoustic {

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

}  // namespace acoustic
