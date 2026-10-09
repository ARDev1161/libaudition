find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets REQUIRED)

add_executable(libaudition_qt_demo
    main.cpp
    demo_window.cpp
    demo_window.hpp
    backend_workbench.cpp
    backend_workbench.hpp
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
    Qt${QT_VERSION_MAJOR}::Widgets)

target_compile_definitions(libaudition_qt_demo PRIVATE
    LIBAUDITION_DEMO_HAS_ODAS=$<BOOL:${LIBAUDITION_WITH_ODAS}>
    LIBAUDITION_DEMO_HAS_SAMPLERATE=$<BOOL:${LIBAUDITION_WITH_LIBSAMPLERATE}>
    LIBAUDITION_DEMO_HAS_AEC3=$<BOOL:${LIBAUDITION_WITH_WEBRTC_AEC3}>
    LIBAUDITION_DEMO_HAS_SHERPA=$<BOOL:${LIBAUDITION_WITH_SHERPA}>
    LIBAUDITION_DEMO_HAS_SHERPA_TTS=$<AND:$<BOOL:${LIBAUDITION_WITH_SHERPA}>,$<BOOL:${LIBAUDITION_SHERPA_ENABLE_TTS}>>
    LIBAUDITION_DEMO_HAS_CLAP=$<BOOL:${LIBAUDITION_WITH_CLAP}>
    LIBAUDITION_DEMO_HAS_AASIST=$<BOOL:${LIBAUDITION_WITH_AASIST}>
    LIBAUDITION_DEMO_HAS_WORLD=$<BOOL:${LIBAUDITION_WITH_WORLD}>
    LIBAUDITION_DEMO_HAS_GTSAM=$<BOOL:${LIBAUDITION_WITH_GTSAM}>)

libaudition_configure_target(libaudition_qt_demo)


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
