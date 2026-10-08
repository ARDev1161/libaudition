add_library(libaudition_backend_world SHARED
    src/world_voice_traits_estimator.cpp)
add_library(audition::backend_world ALIAS libaudition_backend_world)

set_target_properties(libaudition_backend_world PROPERTIES
    EXPORT_NAME backend_world
    OUTPUT_NAME audition_backend_world
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)

target_compile_definitions(libaudition_backend_world
    PRIVATE LIBAUDITION_BUILDING_LIBRARY
    PUBLIC LIBAUDITION_SHARED)

target_link_libraries(libaudition_backend_world
    PUBLIC audition::interfaces
    PRIVATE World::world)

target_include_directories(libaudition_backend_world
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

libaudition_configure_target(libaudition_backend_world)

install(TARGETS libaudition_backend_world
    EXPORT libauditionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/audition
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
