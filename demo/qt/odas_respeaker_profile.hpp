#pragma once

#if LIBAUDITION_DEMO_HAS_ODAS
#include <audition/backends/odas/odas_options.hpp>

namespace demo {

// Angular microphone response and spatial search mask transcribed from
// ODAS's upstream config/odaslive/respeaker_usb_4_mic_array.cfg.
// '80,100' is an angular transition relative to +Z, not a calibrated
// constraint on the source's actual elevation or a confidence threshold.
// The input mapping and XYZ coordinates remain separately editable.
inline void applyReSpeakerUsb4MicAngularProfile(audition::OdasOptions& options) {
    options.microphone_directivity.assign(
        options.microphone_array.size(),
        audition::OdasMicrophoneDirectivity{80.0, 100.0});
    options.spatial_filters = {
        audition::OdasSpatialFilter{
            audition::Direction3D::fromVector({0.0, 0.0, 1.0}),
            80.0, 100.0}
    };
}

}  // namespace demo
#endif
