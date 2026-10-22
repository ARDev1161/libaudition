find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets Concurrent REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets Concurrent REQUIRED)

add_executable(libaudition_qt_demo
    main.cpp
    demo_window.cpp
    acoustic_scene_page.cpp
    acoustic_scene_page.hpp
    alsa_live_capture.cpp
    alsa_live_capture.hpp
    live_audio_tagging_worker.cpp
    live_audio_tagging_worker.hpp
    acoustic_sphere_widget.cpp
    acoustic_sphere_widget.hpp
    demo_window.hpp
    backend_workbench.cpp
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
    wav_io.cpp
    wav_io.hpp)

target_link_libraries(libaudition_qt_demo PRIVATE
    audition::core
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
    LIBAUDITION_DEMO_HAS_SHERPA_TTS=$<AND:$<BOOL:${LIBAUDITION_WITH_SHERPA}>,$<BOOL:${LIBAUDITION_SHERPA_ENABLE_TTS}>>
    LIBAUDITION_DEMO_HAS_CLAP=$<BOOL:${LIBAUDITION_WITH_CLAP}>
    LIBAUDITION_DEMO_HAS_YAMNET=$<BOOL:${LIBAUDITION_WITH_YAMNET}>
    LIBAUDITION_DEMO_HAS_AASIST=$<BOOL:${LIBAUDITION_WITH_AASIST}>
    LIBAUDITION_DEMO_HAS_WORLD=$<BOOL:${LIBAUDITION_WITH_WORLD}>
    LIBAUDITION_DEMO_HAS_GTSAM=$<BOOL:${LIBAUDITION_WITH_GTSAM}>)

if(LIBAUDITION_WITH_YAMNET AND LIBAUDITION_DEMO_FETCH_YAMNET_MODEL)
    target_compile_definitions(libaudition_qt_demo PRIVATE
        LIBAUDITION_DEMO_YAMNET_MODEL="${LIBAUDITION_DEMO_YAMNET_MODEL}"
        LIBAUDITION_DEMO_YAMNET_LABELS="${LIBAUDITION_DEMO_YAMNET_LABELS}")
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

if(LIBAUDITION_WITH_CLAP)
    target_link_libraries(libaudition_qt_demo PRIVATE audition::backend_clap)
endif()

if(LIBAUDITION_WITH_YAMNET)
    target_link_libraries(libaudition_qt_demo PRIVATE audition::backend_yamnet)
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
