#pragma once

#if LIBAUDITION_DEMO_HAS_ODAS
#include <audition/backends/odas/respeaker_usb4_profile.hpp>

namespace demo {

// Compatibility adapter: the reusable array profile is owned by libaudition,
// not the Qt playground.
inline void applyReSpeakerUsb4MicAngularProfile(audition::OdasOptions& options) {
    audition::applyReSpeakerUsb4MicAngularProfile(options);
}

}  // namespace demo
#endif
