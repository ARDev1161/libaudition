add_library(libaudition_backend_samplerate
    src/samplerate_resampler.cpp)
add_library(audition::backend_samplerate ALIAS libaudition_backend_samplerate)

set_target_properties(libaudition_backend_samplerate PROPERTIES
    EXPORT_NAME backend_samplerate
    OUTPUT_NAME audition_backend_samplerate
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)

target_compile_definitions(libaudition_backend_samplerate PRIVATE LIBAUDITION_BUILDING_LIBRARY)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(libaudition_backend_samplerate PUBLIC LIBAUDITION_SHARED)
endif()

target_link_libraries(libaudition_backend_samplerate
    PUBLIC audition::interfaces
    PRIVATE audition::dsp SampleRate::samplerate)
target_include_directories(libaudition_backend_samplerate
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
libaudition_configure_target(libaudition_backend_samplerate)

install(TARGETS libaudition_backend_samplerate
    EXPORT libauditionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/audition
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
