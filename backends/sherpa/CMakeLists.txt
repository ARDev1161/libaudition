add_library(libaudition_backend_sherpa SHARED
    src/sherpa_config.cpp
    src/sherpa_vad.cpp
    src/sherpa_offline_asr.cpp
    src/sherpa_streaming_asr.cpp
    src/sherpa_keyword_spotter.cpp
    src/sherpa_language_identifier.cpp
    src/sherpa_audio_tagger.cpp
    src/sherpa_speech_denoiser_config.cpp
    src/sherpa_offline_speech_denoiser.cpp
    src/sherpa_streaming_speech_denoiser.cpp
    src/sherpa_speaker_config.cpp
    src/sherpa_speaker_embedder.cpp
    src/sherpa_speaker_verifier.cpp
    src/sherpa_speaker_identifier.cpp
    src/sherpa_speaker_diarizer.cpp)
add_library(audition::backend_sherpa ALIAS libaudition_backend_sherpa)

set_target_properties(libaudition_backend_sherpa PROPERTIES
    EXPORT_NAME backend_sherpa
    OUTPUT_NAME audition_backend_sherpa
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)

if(APPLE)
    set_target_properties(libaudition_backend_sherpa PROPERTIES
        INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(libaudition_backend_sherpa PROPERTIES
        INSTALL_RPATH "$ORIGIN")
endif()

target_compile_definitions(libaudition_backend_sherpa
    PRIVATE LIBAUDITION_BUILDING_LIBRARY
    PUBLIC LIBAUDITION_SHARED)

target_link_libraries(libaudition_backend_sherpa
    PUBLIC audition::interfaces
    PRIVATE SherpaOnnx::cxx_api)

target_include_directories(libaudition_backend_sherpa
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

if(TARGET libaudition_sherpa_external)
    add_dependencies(libaudition_backend_sherpa libaudition_sherpa_external)
endif()
if(LIBAUDITION_SHERPA_ENABLE_TTS)
    target_sources(libaudition_backend_sherpa PRIVATE
        src/sherpa_tts_config.cpp
        src/sherpa_tts.cpp)
    target_compile_definitions(libaudition_backend_sherpa
        PUBLIC LIBAUDITION_SHERPA_TTS_ENABLED=1)
endif()

libaudition_configure_target(libaudition_backend_sherpa)

install(TARGETS libaudition_backend_sherpa
    EXPORT libauditionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/audition
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
