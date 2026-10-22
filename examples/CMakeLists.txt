add_executable(libaudition_example_core core_audio.cpp)
target_link_libraries(libaudition_example_core PRIVATE audition::core audition::pipeline)
libaudition_configure_target(libaudition_example_core)

add_executable(libaudition_example_custom_classifier custom_classifier.cpp)
target_link_libraries(libaudition_example_custom_classifier PRIVATE audition::interfaces audition::pipeline)
libaudition_configure_target(libaudition_example_custom_classifier)


add_executable(libaudition_spl_cli spl_range_cli.cpp)
target_link_libraries(libaudition_spl_cli PRIVATE audition::spatial)
libaudition_configure_target(libaudition_spl_cli)

add_executable(libaudition_smoothing_cli smoothing_cli.cpp)
target_link_libraries(libaudition_smoothing_cli PRIVATE audition::spatial)
libaudition_configure_target(libaudition_smoothing_cli)

add_executable(libaudition_identity_cli identity_cli.cpp)
target_link_libraries(libaudition_identity_cli PRIVATE audition::memory)
libaudition_configure_target(libaudition_identity_cli)

add_executable(libaudition_event_cli event_cli.cpp)
target_link_libraries(libaudition_event_cli PRIVATE audition::pipeline)
libaudition_configure_target(libaudition_event_cli)

add_executable(libaudition_pipeline_cli pipeline_cli.cpp)
target_link_libraries(libaudition_pipeline_cli PRIVATE
    audition::spatial
    audition::memory
    audition::pipeline)
libaudition_configure_target(libaudition_pipeline_cli)

add_executable(libaudition_temporal_cli temporal_patterns_cli.cpp)
target_link_libraries(libaudition_temporal_cli PRIVATE audition::core)
libaudition_configure_target(libaudition_temporal_cli)

add_executable(libaudition_temporal_events_cli temporal_events_cli.cpp)
target_link_libraries(libaudition_temporal_events_cli PRIVATE audition::core audition::pipeline)
libaudition_configure_target(libaudition_temporal_events_cli)

add_executable(libaudition_temporal_association_cli temporal_association_cli.cpp)
target_link_libraries(libaudition_temporal_association_cli PRIVATE audition::core)
libaudition_configure_target(libaudition_temporal_association_cli)

add_executable(libaudition_registries_cli registries_cli.cpp)
target_link_libraries(libaudition_registries_cli PRIVATE
    audition::memory audition::model)
libaudition_configure_target(libaudition_registries_cli)

add_executable(libaudition_factory_registry_cli factory_registry_cli.cpp)
target_link_libraries(libaudition_factory_registry_cli PRIVATE audition::core audition::interfaces)
libaudition_configure_target(libaudition_factory_registry_cli)

add_executable(libaudition_recording_replay_cli recording_replay_cli.cpp)
target_link_libraries(libaudition_recording_replay_cli PRIVATE audition::io)
libaudition_configure_target(libaudition_recording_replay_cli)

if(LIBAUDITION_BUILD_TESTS)
    add_test(NAME cli-recording-export-replay-smoke
        COMMAND libaudition_recording_replay_cli --self-test)
    set_tests_properties(cli-recording-export-replay-smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "recording-export-replay=ok")
    add_test(NAME cli-factory-registry-smoke COMMAND libaudition_factory_registry_cli)
    set_tests_properties(cli-factory-registry-smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "factory=ok")
    add_test(NAME cli-factory-registry-unknown COMMAND libaudition_factory_registry_cli --unknown)
    set_tests_properties(cli-factory-registry-unknown PROPERTIES
        PASS_REGULAR_EXPRESSION "factory-unknown=blocked")
    add_test(NAME cli-factory-registry-duplicate COMMAND libaudition_factory_registry_cli --duplicate)
    set_tests_properties(cli-factory-registry-duplicate PROPERTIES
        PASS_REGULAR_EXPRESSION "factory-duplicate=blocked")
    add_test(NAME cli-registries-smoke
        COMMAND libaudition_registries_cli)
    set_tests_properties(cli-registries-smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "registries=ok")

    add_test(NAME cli-temporal-association-smoke
        COMMAND libaudition_temporal_association_cli)
    set_tests_properties(cli-temporal-association-smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "temporal-association=ok")

    add_test(NAME cli-temporal-events-smoke
        COMMAND libaudition_temporal_events_cli)
    set_tests_properties(cli-temporal-events-smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "temporal-events=ok")

    add_test(NAME cli-temporal-patterns-smoke
        COMMAND libaudition_temporal_cli)
    set_tests_properties(cli-temporal-patterns-smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "temporal=ok")

    add_test(NAME cli-spl-range-smoke
        COMMAND libaudition_spl_cli -40 -20 94 80)
    set_tests_properties(cli-spl-range-smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "range_m=")

    add_test(NAME cli-smoothing-smoke
        COMMAND libaudition_smoothing_cli 2.0 2.4 1.8 2.2)
    set_tests_properties(cli-smoothing-smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "smoothed_m=")

    add_test(NAME cli-identity-smoke
        COMMAND libaudition_identity_cli)
    set_tests_properties(cli-identity-smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "reacquired=yes")

    add_test(NAME cli-event-smoke
        COMMAND libaudition_event_cli alarm help)
    set_tests_properties(cli-event-smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "label=alarm")

    add_test(NAME cli-pipeline-smoke
        COMMAND libaudition_pipeline_cli)
    set_tests_properties(cli-pipeline-smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "pipeline=ok")
endif()
