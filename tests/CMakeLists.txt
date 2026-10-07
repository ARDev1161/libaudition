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
    dsp_test.cpp)
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
