#pragma once

#include <audition/backends/odas/odas_options.hpp>

namespace audition {

// ODAS upstream config/odaslive/respeaker_usb_4_mic_array.cfg:
// each microphone and the SSL spatial mask accept directions around
// 90 degrees from the +Z microphone axis (in the microphone XY plane).
// Geometry/channel mapping must still be configured by the caller.
// This is an ARRAY-SPECIFIC prior, not a general 3D localization fix.
// In particular it must not be applied to sources significantly above
// or below the array plane.
inline void applyReSpeakerUsb4MicAngularProfile(OdasOptions& options) {
    options.microphone_directivity.assign(
        options.microphone_array.size(),
        OdasMicrophoneDirectivity{80.0, 100.0});
    options.spatial_filters = {
        OdasSpatialFilter{
            Direction3D::fromVector({0.0, 0.0, 1.0}),
            80.0, 100.0}
    };
}

}  // namespace audition
