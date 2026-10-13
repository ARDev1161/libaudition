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
    acoustic_event_assembler_test.cpp
    acoustic_pipeline_scenario_test.cpp
    level_range_prior_test.cpp
    spl_calibration_test.cpp
    temporal_spatial_smoother_test.cpp
    dsp_test.cpp
    audio_frontend_test.cpp
    quality_test.cpp
    speech_contracts_test.cpp
    speaker_contracts_test.cpp
    classification_runtime_test.cpp
    per_source_speech_runtime_test.cpp)
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

# Synthetic streaming stress: no ONNX weights, Qt, ALSA or ODAS required.
# Uses explicit simulated inference costs to expose queue pressure and
# produces machine-readable per-stage diagnostics on stdout.
add_executable(progressive_multisource_stress progressive_multisource_stress.cpp)
target_link_libraries(progressive_multisource_stress PRIVATE
    audition::pipeline audition::core audition::interfaces)
libaudition_configure_target(progressive_multisource_stress)
add_test(NAME progressive_stress_5_sources
    COMMAND progressive_multisource_stress 5 8 4 12)
add_test(NAME progressive_stress_10_sources
    COMMAND progressive_multisource_stress 10 8 8 24)
set_tests_properties(progressive_stress_5_sources progressive_stress_10_sources
    PROPERTIES TIMEOUT 40)

if(TARGET audition::backend_odas)
    # Real libaudition integration: deterministic 6-channel ReSpeaker-layout
    # fixture generation, ODAS SSL/SST/SSS, recorded mono WAV/JSON and
    # independent ground-truth validation. No ALSA/Qt or model weights.
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_executable(audition_offline_integration
        integration/offline_pipeline_runner.cpp)
    target_link_libraries(audition_offline_integration PRIVATE
        audition::backend_odas audition::io audition::pipeline audition::core)
    libaudition_configure_target(audition_offline_integration)

    set(_spatial_fixtures "${CMAKE_CURRENT_BINARY_DIR}/spatial_audio_fixtures")
    set(_spatial_results "${CMAKE_CURRENT_BINARY_DIR}/audio_integration_results")
    add_test(NAME audio_fixture_generate COMMAND
        ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/generate_spatial_fixtures.py"
        --output "${_spatial_fixtures}")
    set_tests_properties(audio_fixture_generate PROPERTIES
        FIXTURES_SETUP spatial_audio_fixtures LABELS "audio-integration"
        TIMEOUT 60)
    foreach(_scene silence static moving two)
        add_test(NAME audio_integration_${_scene} COMMAND
            ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/validate_spatial_fixture.py"
            --runner "$<TARGET_FILE:audition_offline_integration>"
            --fixtures "${_spatial_fixtures}"
            --scene "${_scene}"
            --results "${_spatial_results}")
        set_tests_properties(audio_integration_${_scene} PROPERTIES
            FIXTURES_REQUIRED spatial_audio_fixtures
            LABELS "audio-integration;odas"
            TIMEOUT 180)
    endforeach()
    # Explicit known-elevation planar scenes: quantitative quality gates
    # compare directly against generated ground truth (no output projection).
    # Keep unconstrained ReSpeaker profiles above as 3D diagnostics.
    foreach(_scene static moving two)
        set(_planar_gates
            --max-median-error-deg 50
            --max-track-near-vertical-fraction 0.1
            --min-track-coverage 0.7)
        if(_scene STREQUAL "static")
            list(APPEND _planar_gates --require-static-track)
        elseif(_scene STREQUAL "two")
            list(APPEND _planar_gates --min-simultaneous-two-track-hops 40
                                       --min-matched-two-source-hops 40)
        endif()
        add_test(NAME audio_integration_near_plane_${_scene} COMMAND
            ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/validate_spatial_fixture.py"
            --runner "$<TARGET_FILE:audition_offline_integration>"
            --fixtures "${_spatial_fixtures}"
            --scene "${_scene}"
            --results "${_spatial_results}/horizontal"
            --horizontal-sources
            ${_planar_gates})
        set_tests_properties(audio_integration_near_plane_${_scene} PROPERTIES
            FIXTURES_REQUIRED spatial_audio_fixtures
            LABELS "audio-integration;odas-quality"
            TIMEOUT 180)
    endforeach()
endif()

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
    # Genuine Silero/SenseVoice inference on a real PCM WAV; deliberately
    # no fake recognizer and no GUI fixture. A pinned model is mandatory
    # for the CI invocation; missing files produce a nonzero exit.
    add_executable(real_speech_model_replay
        integration/real_speech_model_replay.cpp)
    target_link_libraries(real_speech_model_replay PRIVATE
        audition::backend_sherpa audition::io audition::core)
    libaudition_configure_target(real_speech_model_replay)

    target_sources(libaudition_tests PRIVATE sherpa_backend_test.cpp sherpa_speaker_backend_test.cpp)
    target_link_libraries(libaudition_tests PRIVATE audition::backend_sherpa)
    add_executable(sherpa_per_source_speech_smoke sherpa_per_source_speech_smoke.cpp)
    target_link_libraries(sherpa_per_source_speech_smoke PRIVATE
        audition::backend_sherpa audition::pipeline audition::core)
    libaudition_configure_target(sherpa_per_source_speech_smoke)
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


if(TARGET audition::backend_yamnet)
    add_executable(real_yamnet_wav_replay
        integration/real_yamnet_wav_replay.cpp)
    target_link_libraries(real_yamnet_wav_replay PRIVATE
        audition::backend_yamnet audition::io audition::core)
    libaudition_configure_target(real_yamnet_wav_replay)

    add_executable(yamnet_pcm16_probe yamnet_pcm16_probe.cpp)
    target_link_libraries(yamnet_pcm16_probe PRIVATE audition::backend_yamnet audition::core)
    libaudition_configure_target(yamnet_pcm16_probe)
    add_executable(yamnet_wav_benchmark yamnet_wav_benchmark.cpp)
    target_link_libraries(yamnet_wav_benchmark PRIVATE audition::backend_yamnet audition::core)
    libaudition_configure_target(yamnet_wav_benchmark)
    add_executable(yamnet_progressive_smoke yamnet_progressive_smoke.cpp)
    target_link_libraries(yamnet_progressive_smoke PRIVATE
        audition::backend_yamnet audition::pipeline audition::core)
    libaudition_configure_target(yamnet_progressive_smoke)
    add_executable(yamnet_progressive_multisource yamnet_progressive_multisource.cpp)
    target_link_libraries(yamnet_progressive_multisource PRIVATE
        audition::backend_yamnet audition::pipeline audition::core)
    libaudition_configure_target(yamnet_progressive_multisource)
endif()

if(TARGET audition::backend_efficientat)
    add_executable(efficientat_wav_benchmark efficientat_wav_benchmark.cpp)
    target_link_libraries(efficientat_wav_benchmark PRIVATE audition::backend_efficientat audition::core)
    libaudition_configure_target(efficientat_wav_benchmark)
    add_executable(efficientat_real_model_smoke efficientat_real_model_smoke.cpp)
    target_link_libraries(efficientat_real_model_smoke PRIVATE audition::backend_efficientat audition::core)
    libaudition_configure_target(efficientat_real_model_smoke)
    add_executable(efficientat_frontend_dump efficientat_frontend_dump.cpp)
    target_link_libraries(efficientat_frontend_dump PRIVATE
        audition::backend_efficientat audition::core)
    libaudition_configure_target(efficientat_frontend_dump)
    target_sources(libaudition_tests PRIVATE efficientat_backend_test.cpp)
    target_link_libraries(libaudition_tests PRIVATE audition::backend_efficientat)
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

# Pure C++17 disk recorder regression, no Qt or third-party models required.
add_executable(source_wav_recorder_test source_wav_recorder_test.cpp)
target_compile_features(source_wav_recorder_test PRIVATE cxx_std_17)
find_package(Threads REQUIRED)
target_link_libraries(source_wav_recorder_test PRIVATE Threads::Threads audition::io)
add_test(NAME source_wav_recorder_test COMMAND source_wav_recorder_test)

# Standalone public audio APIs: no Qt and no model assets.
add_executable(recording_audio_apis_test recording_audio_apis_test.cpp)
target_link_libraries(recording_audio_apis_test PRIVATE audition::core audition::io)
libaudition_configure_target(recording_audio_apis_test)
add_test(NAME recording_audio_apis_test COMMAND recording_audio_apis_test)

# Recording session acceptance: reusable identity, timeline, gap and JSON APIs.
add_executable(source_recording_session_test source_recording_session_test.cpp)
target_link_libraries(source_recording_session_test PRIVATE
    audition::io audition::pipeline audition::core)
libaudition_configure_target(source_recording_session_test)
add_test(NAME source_recording_session_test COMMAND source_recording_session_test)
