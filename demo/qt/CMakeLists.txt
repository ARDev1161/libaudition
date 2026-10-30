option(LIBAUDITION_DEMO_WITH_SPEEXDSP_AGC
    "Enable SpeexDSP AGC in the Qt acoustic scene when available" ON)
set(LIBAUDITION_DEMO_HAS_SPEEXDSP_AGC OFF)
if(LIBAUDITION_DEMO_WITH_SPEEXDSP_AGC AND LIBAUDITION_WITH_SPEEXDSP_AGC)
    set(LIBAUDITION_DEMO_HAS_SPEEXDSP_AGC ON)
elseif(LIBAUDITION_DEMO_WITH_SPEEXDSP_AGC)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SPEEXDSP QUIET IMPORTED_TARGET speexdsp)
        if(SPEEXDSP_FOUND)
            set(LIBAUDITION_DEMO_HAS_SPEEXDSP_AGC ON)
        endif()
    endif()
    if(NOT LIBAUDITION_DEMO_HAS_SPEEXDSP_AGC)
        message(STATUS "SpeexDSP AGC requested but speexdsp was not found; using RMS AGC only")
    endif()
endif()

find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets Concurrent REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets Concurrent REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Multimedia QUIET)

add_executable(libaudition_qt_demo
    main.cpp
    demo_window.cpp
    acoustic_scene_page.cpp
    acoustic_scene_page.hpp
    alsa_live_capture.cpp
    alsa_live_capture.hpp
    live_audio_tagging_worker.cpp
    live_audio_tagging_worker.hpp
    source_track_lifecycle.hpp
    source_wav_recorder.hpp
    acoustic_sphere_widget.cpp
    acoustic_sphere_widget.hpp
    demo_window.hpp
    backend_workbench.cpp
    temporal_patterns_page.cpp
    temporal_patterns_page.hpp
    temporal_events_page.cpp
    temporal_events_page.hpp
    feature_catalog_page.cpp
    feature_catalog_page.hpp
    backend_workbench.hpp
    async_panel_runner.cpp
    async_panel_runner.hpp
    model_fixture_selftest.cpp
    model_fixture_selftest.hpp
    optional_backend_workbench.cpp
    optional_backend_workbench.hpp
    sherpa_extended_workbench.cpp
    sherpa_extended_workbench.hpp
    sherpa_speaker_workbench.cpp
    sherpa_speaker_workbench.hpp
    wav_io.hpp)

target_link_libraries(libaudition_qt_demo PRIVATE
    audition::core
    audition::io
    audition::interfaces
    audition::dsp
    audition::spatial
    audition::memory
    audition::pipeline
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Concurrent)

target_compile_definitions(libaudition_qt_demo PRIVATE
    LIBAUDITION_DEMO_HAS_ODAS=$<BOOL:${LIBAUDITION_WITH_ODAS}>
    LIBAUDITION_DEMO_HAS_SAMPLERATE=$<BOOL:${LIBAUDITION_WITH_LIBSAMPLERATE}>
    LIBAUDITION_DEMO_HAS_AEC3=$<BOOL:${LIBAUDITION_WITH_WEBRTC_AEC3}>
    LIBAUDITION_DEMO_HAS_SHERPA=$<BOOL:${LIBAUDITION_WITH_SHERPA}>
    LIBAUDITION_DEMO_HAS_WHISPERCPP=$<BOOL:${LIBAUDITION_WITH_WHISPERCPP}>
    LIBAUDITION_DEMO_HAS_SHERPA_TTS=$<AND:$<BOOL:${LIBAUDITION_WITH_SHERPA}>,$<BOOL:${LIBAUDITION_SHERPA_ENABLE_TTS}>>
    LIBAUDITION_DEMO_HAS_CLAP=$<BOOL:${LIBAUDITION_WITH_CLAP}>
    LIBAUDITION_DEMO_HAS_YAMNET=$<BOOL:${LIBAUDITION_WITH_YAMNET}>
    LIBAUDITION_DEMO_HAS_EFFICIENTAT=$<BOOL:${LIBAUDITION_WITH_EFFICIENTAT}>
    LIBAUDITION_DEMO_HAS_AASIST=$<BOOL:${LIBAUDITION_WITH_AASIST}>
    LIBAUDITION_DEMO_HAS_WORLD=$<BOOL:${LIBAUDITION_WITH_WORLD}>
    LIBAUDITION_DEMO_HAS_GTSAM=$<BOOL:${LIBAUDITION_WITH_GTSAM}>)

if(LIBAUDITION_WITH_YAMNET AND LIBAUDITION_DEMO_FETCH_YAMNET_MODEL)
    target_compile_definitions(libaudition_qt_demo PRIVATE
        LIBAUDITION_DEMO_YAMNET_MODEL="${LIBAUDITION_DEMO_YAMNET_MODEL}"
        LIBAUDITION_DEMO_YAMNET_LABELS="${LIBAUDITION_DEMO_YAMNET_LABELS}")
endif()

if(LIBAUDITION_WITH_SHERPA AND LIBAUDITION_DEMO_FETCH_SPEECH_MODELS)
    target_compile_definitions(libaudition_qt_demo PRIVATE
        LIBAUDITION_DEMO_SILERO_VAD_MODEL="${LIBAUDITION_DEMO_SILERO_VAD_MODEL}"
        LIBAUDITION_DEMO_SENSEVOICE_MODEL="${LIBAUDITION_DEMO_SENSEVOICE_MODEL}"
        LIBAUDITION_DEMO_SENSEVOICE_TOKENS="${LIBAUDITION_DEMO_SENSEVOICE_TOKENS}")
endif()

if(TARGET Qt${QT_VERSION_MAJOR}::Multimedia)
    target_link_libraries(libaudition_qt_demo PRIVATE Qt${QT_VERSION_MAJOR}::Multimedia)
    target_compile_definitions(libaudition_qt_demo PRIVATE LIBAUDITION_DEMO_HAS_MULTIMEDIA=1)
else()
    target_compile_definitions(libaudition_qt_demo PRIVATE LIBAUDITION_DEMO_HAS_MULTIMEDIA=0)
endif()

libaudition_configure_target(libaudition_qt_demo)

# ALSA is only a host-side capture adapter in this optional Qt executable.
# No ALSA dependency is added to libaudition or its installed targets.
find_package(Threads REQUIRED)
target_link_libraries(libaudition_qt_demo PRIVATE Threads::Threads)

if(LIBAUDITION_WITH_ODAS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(ALSA QUIET)
endif()

if(LIBAUDITION_WITH_ODAS AND ALSA_FOUND)
    target_compile_definitions(libaudition_qt_demo PRIVATE
        LIBAUDITION_DEMO_HAS_ALSA=1)
    target_link_libraries(libaudition_qt_demo PRIVATE ALSA::ALSA)
else()
    target_compile_definitions(libaudition_qt_demo PRIVATE
        LIBAUDITION_DEMO_HAS_ALSA=0)
endif()


if(LIBAUDITION_WITH_ODAS)
    target_link_libraries(libaudition_qt_demo PRIVATE audition::backend_odas)
endif()

if(LIBAUDITION_WITH_LIBSAMPLERATE)
    target_link_libraries(libaudition_qt_demo PRIVATE audition::backend_samplerate)
endif()

if(LIBAUDITION_WITH_WEBRTC_AEC3)
    target_link_libraries(libaudition_qt_demo PRIVATE audition::backend_webrtc_aec3)
endif()

if(LIBAUDITION_WITH_GTSAM)
    target_link_libraries(libaudition_qt_demo PRIVATE audition::backend_gtsam)
endif()

if(LIBAUDITION_WITH_SHERPA)
    target_link_libraries(libaudition_qt_demo PRIVATE audition::backend_sherpa)
endif()

if(LIBAUDITION_WITH_WHISPERCPP)
    target_link_libraries(libaudition_qt_demo PRIVATE audition::backend_whispercpp)
endif()

if(LIBAUDITION_WITH_CLAP)
    target_link_libraries(libaudition_qt_demo PRIVATE audition::backend_clap)
endif()

if(LIBAUDITION_WITH_YAMNET)
    target_link_libraries(libaudition_qt_demo PRIVATE audition::backend_yamnet)
endif()

if(LIBAUDITION_WITH_EFFICIENTAT)
    target_link_libraries(libaudition_qt_demo PRIVATE audition::backend_efficientat)
endif()

if(LIBAUDITION_WITH_AASIST)
    target_link_libraries(libaudition_qt_demo PRIVATE audition::backend_aasist)
endif()

if(LIBAUDITION_WITH_WORLD)
    target_link_libraries(libaudition_qt_demo PRIVATE audition::backend_world)
endif()


# GCC 13 can emit a false-positive -Wmaybe-uninitialized from the libstdc++
# std::map destructor while inlining the default ExecutionTarget inside CLAP
# option aggregates. Keep the diagnostic visible, but do not promote this one
# compiler false-positive to an error for the demo target.
if(LIBAUDITION_WITH_CLAP AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(libaudition_qt_demo PRIVATE
        -Wno-error=maybe-uninitialized)
endif()

# Headless application checks are first-class CTest cases, reusable by local
# developers and CI. Register them even when GTest is disabled (Qt-only builds).
# Keep model-weight dependent tests out of this basic suite.
if(BUILD_TESTING)
    function(libaudition_add_qt_check name argument)
        add_test(NAME "qt_${name}"
            COMMAND $<TARGET_FILE:libaudition_qt_demo> "${argument}")
        set_tests_properties("qt_${name}" PROPERTIES
            LABELS "qt;qt-smoke"
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            TIMEOUT 120)
    endfunction()

    libaudition_add_qt_check(self_test --self-test)
    libaudition_add_qt_check(playback_lifecycle --model-self-test=playback-lifecycle)
    libaudition_add_qt_check(source_track_lifecycle --model-self-test=source-track-lifecycle)
    if(LIBAUDITION_WITH_SHERPA)
        libaudition_add_qt_check(live_source_tagging_errors --model-self-test=live-source-tagging-errors)
    endif()
    if(LIBAUDITION_WITH_ODAS)
        libaudition_add_qt_check(odas_errors --model-self-test=odas-errors)
        if(ALSA_FOUND)
            libaudition_add_qt_check(alsa_errors --model-self-test=alsa-errors)
        endif()
    endif()
    if(LIBAUDITION_WITH_LIBSAMPLERATE)
        libaudition_add_qt_check(samplerate --model-self-test=samplerate)
    endif()
    if(LIBAUDITION_WITH_WEBRTC_AEC3)
        libaudition_add_qt_check(aec3 --model-self-test=aec3)
    endif()
    if(LIBAUDITION_WITH_WEBRTC_APM)
        libaudition_add_qt_check(noise_suppression_controls --model-self-test=ns-controls)
    endif()
    if(LIBAUDITION_WITH_SHERPA)
        libaudition_add_qt_check(sherpa_errors --model-self-test=sherpa-errors)
        libaudition_add_qt_check(vad_tuning_controls --model-self-test=vad-tuning)
    endif()
    if(LIBAUDITION_WITH_WORLD)
        libaudition_add_qt_check(world --model-self-test=world)
    endif()
    if(LIBAUDITION_WITH_GTSAM)
        libaudition_add_qt_check(gtsam --model-self-test=gtsam)
    endif()
endif()

# APM 2.x is a single public DSP API. Keep it separate from the old
# independently extracted WebRTC backends during the migration.
if(LIBAUDITION_WITH_WEBRTC_APM)
    target_link_libraries(libaudition_qt_demo PRIVATE WebRTCAPM::APM)
    target_compile_definitions(libaudition_qt_demo PRIVATE
        LIBAUDITION_WITH_WEBRTC_APM=1)
    target_compile_features(libaudition_qt_demo PRIVATE cxx_std_20)
else()
    target_compile_definitions(libaudition_qt_demo PRIVATE
        LIBAUDITION_WITH_WEBRTC_APM=0)
endif()

# The model-backed checks require independently downloaded real weights.
# Enable explicitly on CI jobs/local machines that provision those fixtures.
option(LIBAUDITION_REGISTER_REAL_QT_MODEL_TESTS
    "Register Qt integration tests that require external real model assets" OFF)
if(BUILD_TESTING AND LIBAUDITION_REGISTER_REAL_QT_MODEL_TESTS)
    function(libaudition_add_qt_real_check name)
        add_test(NAME "qt_real_${name}"
            COMMAND $<TARGET_FILE:libaudition_qt_demo>
                    "--model-self-test=${name}")
        set_tests_properties("qt_real_${name}" PROPERTIES
            LABELS "qt;real-model"
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            TIMEOUT 240)
    endfunction()
    if(LIBAUDITION_WITH_SHERPA)
        libaudition_add_qt_real_check(silero)
        libaudition_add_qt_real_check(whisper-real)
        libaudition_add_qt_real_check(speaker-real)
        if(LIBAUDITION_SHERPA_ENABLE_TTS)
            libaudition_add_qt_real_check(piper-real)
        endif()
    endif()
    if(LIBAUDITION_WITH_CLAP)
        libaudition_add_qt_real_check(clap)
    endif()
    if(LIBAUDITION_WITH_AASIST)
        libaudition_add_qt_real_check(aasist)
    endif()
endif()

if(LIBAUDITION_DEMO_HAS_SPEEXDSP_AGC)
    if(LIBAUDITION_WITH_SPEEXDSP_AGC)
        target_link_libraries(libaudition_qt_demo PRIVATE audition::backend_speexdsp)
    else()
        # Preserve the demo's historical optional auto-discovery even when
        # the installed SDK's SpeexDSP target is disabled.
        target_link_libraries(libaudition_qt_demo PRIVATE PkgConfig::SPEEXDSP)
        target_compile_definitions(libaudition_qt_demo PRIVATE LIBAUDITION_WITH_SPEEXDSP_AGC=1)
    endif()
else()
    target_compile_definitions(libaudition_qt_demo PRIVATE LIBAUDITION_WITH_SPEEXDSP_AGC=0)
endif()

