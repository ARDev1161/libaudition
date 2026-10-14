include(GoogleTest)

if(LIBAUDITION_REGISTER_MODEL_TOOL_TESTS)
    if(DEFINED ENV{RUNNER_TEMP} AND NOT "$ENV{RUNNER_TEMP}" STREQUAL "")
        set(_real_model_asset_default "$ENV{RUNNER_TEMP}")
    else()
        set(_real_model_asset_default "${CMAKE_BINARY_DIR}/model-assets")
    endif()
    set(LIBAUDITION_REAL_MODEL_ASSET_DIR "${_real_model_asset_default}"
        CACHE PATH "Directory containing real-model CI fixtures")
endif()

# Register model-tool verification scripts with CTest rather than invoking
# them only from GitHub Actions. This suite is opt-in because it needs the
# Python environment provisioned by the real-model CI job.
option(LIBAUDITION_REGISTER_MODEL_TOOL_TESTS
    "Register real-model Python validation scripts in CTest" OFF)
if(LIBAUDITION_REGISTER_MODEL_TOOL_TESTS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    foreach(_test IN ITEMS
            test_efficientat_wav_evaluator
            test_compare_efficientat_wavs
            test_audioset_vocabulary_audit
            test_compare_audio_taggers
            test_analyze_yamnet_event_timeline
            test_yamnet_esc10_extended
            test_review_audio_taggers_report)
        add_test(NAME "model_tool_${_test}"
            COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/${_test}.py")
        set_tests_properties("model_tool_${_test}" PROPERTIES
            LABELS "model-tools;python"
            TIMEOUT 180
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    endforeach()

    if(TARGET audition::backend_yamnet OR TARGET audition::backend_efficientat)
        add_test(NAME model_fixture_benchmark_wav
            COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/integration/generate_model_benchmark_wav.py"
                --output "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/efficientat-benchmark.wav")
        set_tests_properties(model_fixture_benchmark_wav PROPERTIES
            LABELS "model-fixture;wav-integration"
            FIXTURES_SETUP real_model_benchmark_wav
            TIMEOUT 60)
    endif()
endif()

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
# With the optional reference suite, the synthesized ONNX fixture is
# created before every discovered GTest, including tests of the model adapter.
# Avoid relying on CI step ordering or a pre-existing fixture directory.
if(LIBAUDITION_REGISTER_EFFICIENTAT_REFERENCE_TESTS AND TARGET audition::backend_efficientat)
    gtest_discover_tests(libaudition_tests
        PROPERTIES FIXTURES_REQUIRED efficientat_synthetic_onnx)
else()
    gtest_discover_tests(libaudition_tests)
endif()

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

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    if(LIBAUDITION_REGISTER_REAL_QT_MODEL_TESTS)
        # Real model fixtures live in the configured build directory.
    # Their absence is a failed test (never a silent skip).
    add_test(NAME speech_real_whisper_two_sources
        COMMAND $<TARGET_FILE:sherpa_per_source_speech_smoke>
            "${CMAKE_BINARY_DIR}/whisper/tiny.en-encoder.int8.onnx"
            "${CMAKE_BINARY_DIR}/whisper/tiny.en-decoder.int8.onnx"
            "${CMAKE_BINARY_DIR}/whisper/tiny.en-tokens.txt")
    set_tests_properties(speech_real_whisper_two_sources PROPERTIES
        LABELS "real-model;speech-integration" TIMEOUT 180)

    add_test(NAME speech_real_silero_sensevoice_wav
        COMMAND $<TARGET_FILE:real_speech_model_replay>
            "${CMAKE_BINARY_DIR}/silero_vad.onnx"
            "${CMAKE_BINARY_DIR}/sensevoice/model.int8.onnx"
            "${CMAKE_BINARY_DIR}/sensevoice/tokens.txt"
            "${CMAKE_BINARY_DIR}/audio-fixtures/utterance.wav"
            "${CMAKE_BINARY_DIR}/audio-fixtures/real-speech-report.txt")
    set_tests_properties(speech_real_silero_sensevoice_wav PROPERTIES
        LABELS "real-model;speech-integration"
        FIXTURES_SETUP speech_real_replay
        TIMEOUT 180)
    add_test(NAME speech_real_sensevoice_quality
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/score_speech_report.py"
            --report "${CMAKE_BINARY_DIR}/audio-fixtures/real-speech-report.txt"
            --reference "Hello robot. Please go forward and stop at the door."
            --output "${CMAKE_BINARY_DIR}/audio-fixtures/asr-quality.json")
    set_tests_properties(speech_real_sensevoice_quality PROPERTIES
        LABELS "real-model;speech-integration"
        FIXTURES_REQUIRED speech_real_replay
        TIMEOUT 60)
    endif()
    add_test(NAME speech_report_scoring_selftest
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/score_speech_report.py" --self-test)
    set_tests_properties(speech_report_scoring_selftest PROPERTIES
        LABELS "speech-integration;python" TIMEOUT 60)

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

    if(LIBAUDITION_REGISTER_MODEL_TOOL_TESTS)
        add_test(NAME real_yamnet_wav_replay_ctest
            COMMAND $<TARGET_FILE:real_yamnet_wav_replay>
                "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/yamnet/yamnet.onnx"
                "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/yamnet/yamnet_class_map.csv"
                "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/yamnet-parity-5s.wav"
                "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/yamnet-real-wav-report.txt")
        set_tests_properties(real_yamnet_wav_replay_ctest PROPERTIES
            LABELS "real-model;yamnet;wav-integration"
            FIXTURES_REQUIRED yamnet_parity_audio TIMEOUT 180)
    endif()

    add_executable(yamnet_pcm16_probe yamnet_pcm16_probe.cpp)
    target_link_libraries(yamnet_pcm16_probe PRIVATE audition::backend_yamnet audition::core)
    libaudition_configure_target(yamnet_pcm16_probe)

    if(LIBAUDITION_REGISTER_MODEL_TOOL_TESTS)
        add_test(NAME real_yamnet_parity_fixture
            COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/integration/generate_yamnet_parity_fixture.py"
                --output-dir "${LIBAUDITION_REAL_MODEL_ASSET_DIR}")
        set_tests_properties(real_yamnet_parity_fixture PROPERTIES
            LABELS "model-fixture;yamnet" FIXTURES_SETUP yamnet_parity_audio
            TIMEOUT 60)

        foreach(_version IN ITEMS short long)
            if(_version STREQUAL "short")
                set(_input_raw "yamnet-parity.raw")
                set(_output_json "yamnet-onnx-cpp-parity.json")
            else()
                set(_input_raw "yamnet-parity-5s.raw")
                set(_output_json "yamnet-onnx-cpp-5s-parity.json")
            endif()
            add_test(NAME "real_yamnet_onnx_cpp_parity_${_version}"
                COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/verify_yamnet_onnx_cpp.py"
                    --model "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/yamnet/yamnet.onnx"
                    --labels "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/yamnet/yamnet_class_map.csv"
                    --pcm16 "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/${_input_raw}"
                    --cpp-probe $<TARGET_FILE:yamnet_pcm16_probe>
                    --output "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/${_output_json}")
            set_tests_properties("real_yamnet_onnx_cpp_parity_${_version}" PROPERTIES
                LABELS "real-model;yamnet;parity" FIXTURES_REQUIRED yamnet_parity_audio
                TIMEOUT 180)
        endforeach()

        add_test(NAME real_yamnet_label_rejection
            COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/verify_yamnet_label_rejection.py"
                --model "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/yamnet/yamnet.onnx"
                --labels "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/yamnet/yamnet_class_map.csv"
                --pcm16 "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/yamnet-parity.raw"
                --cpp-probe $<TARGET_FILE:yamnet_pcm16_probe>)
        set_tests_properties(real_yamnet_label_rejection PROPERTIES
            LABELS "real-model;yamnet;negative" FIXTURES_REQUIRED yamnet_parity_audio
            TIMEOUT 180)

        add_test(NAME real_yamnet_behavior
            COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/verify_yamnet_behavior.py"
                --model "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/yamnet/yamnet.onnx"
                --labels "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/yamnet/yamnet_class_map.csv"
                --output "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/yamnet-behavior.json")
        set_tests_properties(real_yamnet_behavior PROPERTIES
            LABELS "real-model;yamnet;behavior" TIMEOUT 180)
    endif()
    add_executable(yamnet_wav_benchmark yamnet_wav_benchmark.cpp)
    target_link_libraries(yamnet_wav_benchmark PRIVATE audition::backend_yamnet audition::core)
    libaudition_configure_target(yamnet_wav_benchmark)

    if(LIBAUDITION_REGISTER_MODEL_TOOL_TESTS)
        add_test(NAME real_yamnet_wav_benchmark
            COMMAND $<TARGET_FILE:yamnet_wav_benchmark>
                "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/yamnet/yamnet.onnx"
                "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/yamnet/yamnet_class_map.csv"
                "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/efficientat-benchmark.wav")
        set_tests_properties(real_yamnet_wav_benchmark PROPERTIES
            LABELS "real-model;yamnet;wav-integration"
            FIXTURES_REQUIRED real_model_benchmark_wav
            TIMEOUT 180)
    endif()
    add_executable(yamnet_progressive_smoke yamnet_progressive_smoke.cpp)
    target_link_libraries(yamnet_progressive_smoke PRIVATE
        audition::backend_yamnet audition::pipeline audition::core)
    libaudition_configure_target(yamnet_progressive_smoke)

    if(LIBAUDITION_REGISTER_MODEL_TOOL_TESTS)
        if(NOT DEFINED LIBAUDITION_REAL_MODEL_ASSET_DIR)
            if(DEFINED ENV{RUNNER_TEMP} AND NOT "$ENV{RUNNER_TEMP}" STREQUAL "")
                set(_yamnet_asset_default "$ENV{RUNNER_TEMP}")
            else()
                set(_yamnet_asset_default "${CMAKE_BINARY_DIR}/model-assets")
            endif()
            set(LIBAUDITION_REAL_MODEL_ASSET_DIR "${_yamnet_asset_default}"
                CACHE PATH "Directory holding downloaded real-model test fixtures")
        endif()
        add_test(NAME real_yamnet_progressive_smoke
            COMMAND $<TARGET_FILE:yamnet_progressive_smoke>
                "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/yamnet/yamnet.onnx"
                "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/yamnet/yamnet_class_map.csv")
        set_tests_properties(real_yamnet_progressive_smoke PROPERTIES
            LABELS "real-model;yamnet" TIMEOUT 180)
    endif()
    add_executable(yamnet_progressive_multisource yamnet_progressive_multisource.cpp)
    target_link_libraries(yamnet_progressive_multisource PRIVATE
        audition::backend_yamnet audition::pipeline audition::core)
    libaudition_configure_target(yamnet_progressive_multisource)

    if(LIBAUDITION_REGISTER_MODEL_TOOL_TESTS)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        foreach(_sources IN ITEMS 5 10)
            add_test(NAME "real_yamnet_stress_${_sources}"
                COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/integration/capture_model_benchmark.py"
                    --output "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/yamnet-stream-${_sources}.json"
                    --
                    $<TARGET_FILE:yamnet_progressive_multisource>
                    "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/yamnet/yamnet.onnx"
                    "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/yamnet/yamnet_class_map.csv"
                    "${_sources}" 8 real-time)
            set_tests_properties("real_yamnet_stress_${_sources}" PROPERTIES
                LABELS "real-model;yamnet;stress" TIMEOUT 180)
        endforeach()
    endif()
endif()

if(TARGET audition::backend_efficientat)
    add_executable(efficientat_wav_benchmark efficientat_wav_benchmark.cpp)
    target_link_libraries(efficientat_wav_benchmark PRIVATE audition::backend_efficientat audition::core)
    libaudition_configure_target(efficientat_wav_benchmark)

    if(LIBAUDITION_REGISTER_MODEL_TOOL_TESTS)
        foreach(_variant IN ITEMS mn10_as dymn10_as)
            add_test(NAME "real_efficientat_wav_benchmark_${_variant}"
                COMMAND $<TARGET_FILE:efficientat_wav_benchmark>
                    "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/eat-real/${_variant}.onnx"
                    "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/eat-real/${_variant}_labels.txt"
                    "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/efficientat-benchmark.wav")
            set_tests_properties("real_efficientat_wav_benchmark_${_variant}" PROPERTIES
                LABELS "real-model;efficientat;wav-integration"
                FIXTURES_REQUIRED real_model_benchmark_wav
                TIMEOUT 180)
        endforeach()
    endif()
    add_executable(efficientat_real_model_smoke efficientat_real_model_smoke.cpp)
    target_link_libraries(efficientat_real_model_smoke PRIVATE audition::backend_efficientat audition::core)
    libaudition_configure_target(efficientat_real_model_smoke)

    # The local path is configurable; CI uses the runner's asset directory.
    if(DEFINED ENV{RUNNER_TEMP} AND NOT "$ENV{RUNNER_TEMP}" STREQUAL "")
        set(_default_real_model_asset_dir "$ENV{RUNNER_TEMP}")
    else()
        set(_default_real_model_asset_dir "${CMAKE_BINARY_DIR}/model-assets")
    endif()
    set(LIBAUDITION_REAL_MODEL_ASSET_DIR "${_default_real_model_asset_dir}"
        CACHE PATH "Directory holding downloaded real-model test fixtures")
    # Genuine EfficientAT weights are provisioned by the model CI job.
    if(LIBAUDITION_REGISTER_MODEL_TOOL_TESTS)
        foreach(_variant IN ITEMS mn10_as dymn10_as)
            add_test(NAME "real_efficientat_${_variant}"
                COMMAND $<TARGET_FILE:efficientat_real_model_smoke>
                    "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/eat-real/${_variant}.onnx"
                    "${LIBAUDITION_REAL_MODEL_ASSET_DIR}/eat-real/${_variant}_labels.txt")
            set_tests_properties("real_efficientat_${_variant}" PROPERTIES
                LABELS "real-model;efficientat"
                TIMEOUT 180)
        endforeach()
    endif()
    add_executable(efficientat_frontend_dump efficientat_frontend_dump.cpp)
    target_link_libraries(efficientat_frontend_dump PRIVATE
        audition::backend_efficientat audition::core)
    libaudition_configure_target(efficientat_frontend_dump)

    option(LIBAUDITION_REGISTER_EFFICIENTAT_REFERENCE_TESTS
        "Register PyTorch frontend parity and generated ONNX fixture with CTest" OFF)
    if(LIBAUDITION_REGISTER_EFFICIENTAT_REFERENCE_TESTS)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        add_test(NAME efficientat_synthetic_onnx_fixture
            COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/make_efficientat_onnx.py"
                "${CMAKE_BINARY_DIR}/fixture")
        set_tests_properties(efficientat_synthetic_onnx_fixture PROPERTIES
            LABELS "efficientat;model-fixture"
            FIXTURES_SETUP efficientat_synthetic_onnx
            TIMEOUT 90)

        add_test(NAME efficientat_frontend_reference_dump
            COMMAND $<TARGET_FILE:efficientat_frontend_dump>
                "${CMAKE_BINARY_DIR}/efficientat_mel.f32")
        set_tests_properties(efficientat_frontend_reference_dump PROPERTIES
            LABELS "efficientat;parity;model-fixture"
            FIXTURES_SETUP efficientat_frontend_mel
            TIMEOUT 90)

        add_test(NAME efficientat_frontend_torch_parity
            COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/compare_efficientat_frontend.py"
                "${CMAKE_BINARY_DIR}/efficientat_mel.f32")
        set_tests_properties(efficientat_frontend_torch_parity PROPERTIES
            LABELS "efficientat;parity"
            FIXTURES_REQUIRED efficientat_frontend_mel
            TIMEOUT 180)
    endif()
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


# Entire real AudioSet/ESC-10 suite: CTest owns orchestration and fixtures.
# Local users can point LIBAUDITION_REAL_MODEL_ASSET_DIR at downloaded assets.
# No test in this opt-in suite silently skips missing weights or ESC-10 audio.
if(LIBAUDITION_REGISTER_MODEL_TOOL_TESTS AND
   TARGET audition::backend_efficientat AND TARGET audition::backend_yamnet)
    set(_assets "${LIBAUDITION_REAL_MODEL_ASSET_DIR}")
    set(_eat_upstream "${_assets}/EfficientAT")
    set(_esc10 "${_assets}/esc10-eval")
    set(_esc10_80 "${_assets}/esc10-yamnet-extended")
    set(_eat_source_csv "${_eat_upstream}/metadata/class_labels_indices.csv")
    set(_yamnet_csv "${_assets}/yamnet/yamnet_class_map.csv")
    set(_mn_model "${_assets}/eat-real/mn10_as.onnx")
    set(_mn_labels "${_assets}/eat-real/mn10_as_labels.txt")
    set(_dymn_model "${_assets}/eat-real/dymn10_as.onnx")
    set(_dymn_labels "${_assets}/eat-real/dymn10_as_labels.txt")
    set(_yamnet_model "${_assets}/yamnet/yamnet.onnx")
    set(_eat_cli "$<TARGET_FILE:efficientat_wav_benchmark>")
    set(_yamnet_cli "$<TARGET_FILE:yamnet_wav_benchmark>")

    add_test(NAME real_audioset_vocabulary_audit
        COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/audit_audioset_vocabularies.py"
            --efficientat-csv "${_eat_source_csv}"
            --yamnet-csv "${_assets}/yamnet_class_map.csv"
            --output "${_assets}/audioset_vocab_audit.json")
    set_tests_properties(real_audioset_vocabulary_audit PROPERTIES
        LABELS "real-model;audioset;vocabulary" TIMEOUT 90)

    add_test(NAME real_audioset_smoke_manifest
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/write_audioset_manifest.py"
            --wav "${_assets}/efficientat-benchmark.wav"
            --output "${_assets}/benchmark-manifest.csv")
    set_tests_properties(real_audioset_smoke_manifest PROPERTIES
        LABELS "real-model;audioset;model-fixture"
        FIXTURES_REQUIRED real_model_benchmark_wav
        FIXTURES_SETUP audioset_smoke_manifest TIMEOUT 60)

    set(_compare_tool "${PROJECT_SOURCE_DIR}/tools/compare_audio_taggers.py")
    set(_common_comparison_args
        --mn-model "${_mn_model}" --mn-labels "${_mn_labels}"
        --dymn-model "${_dymn_model}" --dymn-labels "${_dymn_labels}"
        --yamnet-model "${_yamnet_model}" --yamnet-labels "${_yamnet_csv}"
        --efficientat-csv "${_eat_source_csv}" --yamnet-csv "${_yamnet_csv}"
        --efficientat-benchmark "${_eat_cli}"
        --yamnet-benchmark "${_yamnet_cli}")

    add_test(NAME real_audioset_smoke_comparison
        COMMAND ${Python3_EXECUTABLE} "${_compare_tool}"
            --manifest "${_assets}/benchmark-manifest.csv"
            ${_common_comparison_args}
            --output "${_assets}/three-model-comparison.json")
    set_tests_properties(real_audioset_smoke_comparison PROPERTIES
        LABELS "real-model;audioset;model-comparison"
        FIXTURES_REQUIRED audioset_smoke_manifest TIMEOUT 240)

    add_test(NAME real_esc10_prepare_basic
        COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tools/prepare_esc10_evaluation.py"
            --output "${_esc10}"
            --efficientat-csv "${_eat_source_csv}"
            --yamnet-csv "${_yamnet_csv}")
    set_tests_properties(real_esc10_prepare_basic PROPERTIES
        LABELS "real-model;esc10;model-fixture;network"
        FIXTURES_SETUP real_esc10_basic TIMEOUT 600)

    add_test(NAME real_esc10_diagnose_mn10_wav
        COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tools/diagnose_efficientat_wav.py"
            --wav "${_esc10}/chainsaw.wav"
            --model mn10_as --onnx "${_mn_model}"
            --cpp-mel-dump "$<TARGET_FILE:efficientat_frontend_dump>"
            --cpp-benchmark "${_eat_cli}" --cpp-labels "${_mn_labels}"
            --output "${_esc10}/mn10-chainsaw-parity.json")
    set_tests_properties(real_esc10_diagnose_mn10_wav PROPERTIES
        LABELS "real-model;esc10;efficientat;parity"
        FIXTURES_REQUIRED real_esc10_basic
        WORKING_DIRECTORY "${_eat_upstream}"
        ENVIRONMENT "PYTHONPATH=${_eat_upstream}" TIMEOUT 300)

    add_test(NAME real_esc10_diagnose_mn10_duration
        COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tools/diagnose_efficientat_duration.py"
            --model mn10_as
            --input "${_esc10}/chainsaw-original.wav"
            --output "${_esc10}/mn10-duration.json")
    set_tests_properties(real_esc10_diagnose_mn10_duration PROPERTIES
        LABELS "real-model;esc10;efficientat;duration"
        FIXTURES_REQUIRED real_esc10_basic
        WORKING_DIRECTORY "${_eat_upstream}"
        ENVIRONMENT "PYTHONPATH=${_eat_upstream}" TIMEOUT 300)

    add_test(NAME real_esc10_compare_models
        COMMAND ${Python3_EXECUTABLE} "${_compare_tool}"
            --manifest "${_esc10}/manifest.csv"
            ${_common_comparison_args}
            --output "${_esc10}/three-model-real-events.json")
    set_tests_properties(real_esc10_compare_models PROPERTIES
        LABELS "real-model;esc10;model-comparison"
        FIXTURES_REQUIRED real_esc10_basic
        FIXTURES_SETUP real_esc10_comparison TIMEOUT 300)

    add_test(NAME real_esc10_yamnet_timeline
        COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tools/analyze_yamnet_event_timeline.py"
            --manifest "${_esc10}/manifest.csv"
            --model "${_yamnet_model}" --labels "${_yamnet_csv}"
            --output "${_esc10}/yamnet-event-timeline.json")
    set_tests_properties(real_esc10_yamnet_timeline PROPERTIES
        LABELS "real-model;esc10;yamnet;timeline"
        FIXTURES_REQUIRED real_esc10_basic TIMEOUT 300)

    add_test(NAME real_esc10_review_scores
        COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tools/review_audio_taggers_report.py"
            "${_esc10}/three-model-real-events.json"
            --output "${_esc10}/review-findings.json")
    set_tests_properties(real_esc10_review_scores PROPERTIES
        LABELS "real-model;esc10;model-comparison"
        FIXTURES_REQUIRED real_esc10_comparison TIMEOUT 120)

    add_test(NAME real_esc10_prepare_extended
        COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tools/prepare_esc10_evaluation.py"
            --output "${_esc10_80}" --clips-per-category 20
            --efficientat-csv "${_eat_source_csv}"
            --yamnet-csv "${_yamnet_csv}")
    set_tests_properties(real_esc10_prepare_extended PROPERTIES
        LABELS "real-model;esc10;model-fixture;network"
        FIXTURES_SETUP real_esc10_extended TIMEOUT 900)

    add_test(NAME real_esc10_yamnet_80
        COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tools/evaluate_yamnet_esc10.py"
            --manifest "${_esc10_80}/manifest.csv"
            --model "${_yamnet_model}" --labels "${_yamnet_csv}"
            --expected-clips 80
            --output "${_esc10_80}/yamnet-esc10-80.json")
    set_tests_properties(real_esc10_yamnet_80 PROPERTIES
        LABELS "real-model;esc10;yamnet;quality"
        FIXTURES_REQUIRED real_esc10_extended TIMEOUT 600)
endif()


# Assert that configured algorithm and backend test contracts remain discoverable.
# This explicitly audits registration, not code or hardware-accuracy coverage.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME algorithm_coverage_audit_selftest
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/test_ctest_coverage_audit.py")
set_tests_properties(algorithm_coverage_audit_selftest PROPERTIES
    LABELS "coverage-audit;ci-contract" TIMEOUT 60)

add_test(NAME algorithm_coverage_contract
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/audit_ctest_algorithms.py"
        --build "${CMAKE_BINARY_DIR}"
        --source "${PROJECT_SOURCE_DIR}"
        --ctest "${CMAKE_CTEST_COMMAND}"
        --output "${CMAKE_BINARY_DIR}/ctest-algorithm-coverage.json")
set_tests_properties(algorithm_coverage_contract PROPERTIES
    LABELS "coverage-audit;ci-contract"
    TIMEOUT 60)
