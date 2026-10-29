# Find the *unified* WebRTC Audio Processing Module (APM) 2.x package.
# WebRTC APM 3 uses an incompatible builder API and needs its own adapter.
# Deliberately do not accept standalone AEC3/AGC2 extraction targets.
include(FindPackageHandleStandardArgs)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_WEBRTC_APM QUIET webrtc-audio-processing-2)
endif()

find_path(WebRTCAPM_INCLUDE_DIR
    NAMES modules/audio_processing/include/audio_processing.h
    HINTS ${PC_WEBRTC_APM_INCLUDE_DIRS}
    PATH_SUFFIXES webrtc-audio-processing-2)

find_library(WebRTCAPM_LIBRARY
    NAMES webrtc-audio-processing-2
    HINTS ${PC_WEBRTC_APM_LIBRARY_DIRS})

find_package_handle_standard_args(WebRTCAPM
    REQUIRED_VARS WebRTCAPM_LIBRARY WebRTCAPM_INCLUDE_DIR)

if(WebRTCAPM_FOUND AND NOT TARGET WebRTCAPM::APM)
    add_library(WebRTCAPM::APM UNKNOWN IMPORTED)
    set_target_properties(WebRTCAPM::APM PROPERTIES
        IMPORTED_LOCATION "${WebRTCAPM_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${WebRTCAPM_INCLUDE_DIR}")
    if(PC_WEBRTC_APM_LINK_LIBRARIES)
        set_property(TARGET WebRTCAPM::APM APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES "${PC_WEBRTC_APM_LINK_LIBRARIES}")
    endif()
    if(PC_WEBRTC_APM_CFLAGS_OTHER)
        set_property(TARGET WebRTCAPM::APM APPEND PROPERTY
            INTERFACE_COMPILE_OPTIONS "${PC_WEBRTC_APM_CFLAGS_OTHER}")
    endif()
endif()
mark_as_advanced(WebRTCAPM_INCLUDE_DIR WebRTCAPM_LIBRARY)
