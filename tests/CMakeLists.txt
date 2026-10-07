include(GoogleTest)

add_executable(libacoustic_tests
    core_time_test.cpp
    audio_buffer_test.cpp
    probability_geometry_test.cpp
    factory_registry_test.cpp
    bounded_queue_test.cpp
    model_registry_test.cpp
    public_headers_test.cpp
    memory_registry_test.cpp
    dsp_test.cpp)
target_link_libraries(libacoustic_tests PRIVATE
    acoustic::core
    acoustic::interfaces
    acoustic::model
    acoustic::memory
    acoustic::logging
    acoustic::dsp
    acoustic::pipeline
    GTest::gtest_main)
libacoustic_configure_target(libacoustic_tests)
gtest_discover_tests(libacoustic_tests)
