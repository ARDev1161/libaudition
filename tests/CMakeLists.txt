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
    source_identity_resolver_test.cpp
    spatial_identity_coordinator_test.cpp
    level_range_prior_test.cpp
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
    audition::spatial
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
        clap_open_vocabulary_test.cpp
        ${PROJECT_SOURCE_DIR}/backends/clap/src/clap_features.cpp
        ${PROJECT_SOURCE_DIR}/backends/clap/src/roberta_tokenizer.cpp)
    target_link_libraries(libaudition_tests PRIVATE
        audition::backend_clap
        nlohmann_json::nlohmann_json)
    target_include_directories(libaudition_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/backends/clap/src)
endif()


if(TARGET audition::backend_aasist)
    target_sources(libaudition_tests PRIVATE aasist_backend_test.cpp)
    target_link_libraries(libaudition_tests PRIVATE audition::backend_aasist)
endif()


if(TARGET audition::backend_world)
    target_sources(libaudition_tests PRIVATE
        world_backend_test.cpp
        world_acoustic_features_test.cpp)
    target_link_libraries(libaudition_tests PRIVATE audition::backend_world)
endif()

if(TARGET audition::backend_gtsam)
    target_sources(libaudition_tests PRIVATE gtsam_spatial_fusion_test.cpp)
    target_link_libraries(libaudition_tests PRIVATE audition::backend_gtsam)
endif()
