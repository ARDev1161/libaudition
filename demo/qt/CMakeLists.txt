find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets REQUIRED)

add_executable(libaudition_qt_demo
    main.cpp
    demo_window.cpp
    demo_window.hpp)

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
