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

if(LIBAUDITION_BUILD_TESTS)
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
