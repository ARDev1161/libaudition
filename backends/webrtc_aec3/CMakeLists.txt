if(LIBAUDITION_WITH_WEBRTC_APM)
    set(_audition_aec3_adapter_source src/webrtc_apm_echo_canceller.cpp)
else()
    set(_audition_aec3_adapter_source src/webrtc_aec3_echo_canceller.cpp)
endif()
add_library(libaudition_backend_webrtc_aec3 SHARED
    ${_audition_aec3_adapter_source})
add_library(audition::backend_webrtc_aec3 ALIAS libaudition_backend_webrtc_aec3)

set_target_properties(libaudition_backend_webrtc_aec3 PROPERTIES
    EXPORT_NAME backend_webrtc_aec3
    OUTPUT_NAME audition_backend_webrtc_aec3
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)

if(APPLE)
    set_target_properties(libaudition_backend_webrtc_aec3 PROPERTIES
        INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(libaudition_backend_webrtc_aec3 PROPERTIES
        INSTALL_RPATH "$ORIGIN")
endif()

target_compile_definitions(libaudition_backend_webrtc_aec3
    PRIVATE LIBAUDITION_BUILDING_LIBRARY
    PUBLIC LIBAUDITION_SHARED)
target_link_libraries(libaudition_backend_webrtc_aec3
    PUBLIC audition::interfaces)
if(LIBAUDITION_WITH_WEBRTC_APM)
    target_link_libraries(libaudition_backend_webrtc_aec3
        PRIVATE WebRTCAPM::APM)
else()
    target_link_libraries(libaudition_backend_webrtc_aec3
        PRIVATE webrtc_aec3)
endif()
target_include_directories(libaudition_backend_webrtc_aec3
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
if(NOT LIBAUDITION_WITH_WEBRTC_APM)
target_include_directories(libaudition_backend_webrtc_aec3 SYSTEM PRIVATE
    "${LIBAUDITION_WEBRTC_AEC3_SOURCE_DIR}/src"
    "${LIBAUDITION_WEBRTC_AEC3_SOURCE_DIR}/src/api"
    "${LIBAUDITION_WEBRTC_AEC3_SOURCE_DIR}/src/base/rtc_base"
    "${LIBAUDITION_WEBRTC_AEC3_SOURCE_DIR}/src/base"
    "${LIBAUDITION_WEBRTC_AEC3_SOURCE_DIR}/src/base/system_wrappers/include"
    "${LIBAUDITION_WEBRTC_AEC3_SOURCE_DIR}/src/audio_processing"
    "${LIBAUDITION_WEBRTC_AEC3_SOURCE_DIR}/src/absl"
    "${LIBAUDITION_WEBRTC_AEC3_SOURCE_DIR}/src/base/jsoncpp/include"
    "${LIBAUDITION_WEBRTC_AEC3_SOURCE_DIR}/src/audio_processing/aec3"
    "${LIBAUDITION_WEBRTC_AEC3_SOURCE_DIR}/src/audio_processing/resampler"
    "${LIBAUDITION_WEBRTC_AEC3_SOURCE_DIR}/src/audio_processing/logging"
    "${LIBAUDITION_WEBRTC_AEC3_SOURCE_DIR}/src/audio_processing/include"
    "${LIBAUDITION_WEBRTC_AEC3_SOURCE_DIR}/src/audio_processing/utility"
    "${LIBAUDITION_WEBRTC_AEC3_SOURCE_DIR}/src/rtc_base/experiments"
    "${LIBAUDITION_WEBRTC_AEC3_SOURCE_DIR}/src/common_audio"
    "${LIBAUDITION_WEBRTC_AEC3_SOURCE_DIR}/src/common_audio/third_party/ooura/fft_size_128")
add_dependencies(libaudition_backend_webrtc_aec3
    libaudition_webrtc_aec3_external)
endif()
libaudition_configure_target(libaudition_backend_webrtc_aec3)
target_compile_features(libaudition_backend_webrtc_aec3 PRIVATE cxx_std_20)

install(TARGETS libaudition_backend_webrtc_aec3
    EXPORT libauditionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/audition
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
