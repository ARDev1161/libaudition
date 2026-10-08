include(GoogleTest)

add_executable(libaudition_tests
    core_time_test.cpp
    audio_buffer_test.cpp
    probability_geometry_test.cpp
    factory_registry_test.cpp
    bounded_queue_test.cpp
    model_registry_test.cpp
    public_headers_test.cpp
    memory_registry_test.cpp
    sound_prototype_matcher_test.cpp
    dsp_test.cpp
    audio_frontend_test.cpp
    quality_test.cpp
    speech_contracts_test.cpp
    speaker_contracts_test.cpp)
target_link_libraries(libaudition_tests PRIVATE
    audition::core
    audition::interfaces
    audition::model
    audition::memory
    audition::logging
    audition::dsp
    audition::pipeline
    GTest::gtest_main)
libaudition_configure_target(libaudition_tests)
gtest_discover_tests(libaudition_tests)

if(TARGET audition::backend_odas)
    target_sources(libaudition_tests PRIVATE odas_backend_test.cpp)
    target_link_libraries(libaudition_tests PRIVATE audition::backend_odas)
endif()

if(TARGET audition::backend_samplerate)
    target_sources(libaudition_tests PRIVATE samplerate_backend_test.cpp)
    target_link_libraries(libaudition_tests PRIVATE audition::backend_samplerate)
endif()

if(TARGET audition::backend_webrtc_aec3)
    target_sources(libaudition_tests PRIVATE webrtc_aec3_backend_test.cpp)
    target_link_libraries(libaudition_tests PRIVATE audition::backend_webrtc_aec3)
endif()


if(TARGET audition::backend_sherpa)
    target_sources(libaudition_tests PRIVATE sherpa_backend_test.cpp sherpa_speaker_backend_test.cpp)
    target_link_libraries(libaudition_tests PRIVATE audition::backend_sherpa)
endif()


if(TARGET audition::backend_clap)
    target_sources(libaudition_tests PRIVATE
        clap_backend_test.cpp
        ${PROJECT_SOURCE_DIR}/backends/clap/src/clap_features.cpp)
    target_link_libraries(libaudition_tests PRIVATE audition::backend_clap)
    target_include_directories(libaudition_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/backends/clap/src)
endif()
