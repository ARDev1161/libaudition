add_library(libacoustic_core
    core/error.cpp
    core/time.cpp
    core/audio_buffer.cpp)
add_library(acoustic::core ALIAS libacoustic_core)
set_target_properties(libacoustic_core PROPERTIES
    EXPORT_NAME core
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)
target_compile_definitions(libacoustic_core PRIVATE LIBACOUSTIC_BUILDING_LIBRARY)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(libacoustic_core PUBLIC LIBACOUSTIC_SHARED)
endif()
target_include_directories(libacoustic_core
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(libacoustic_core
    PUBLIC acoustic::project_options
    PRIVATE acoustic::project_warnings)

install(TARGETS libacoustic_core
    EXPORT libacousticTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/acoustic
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
