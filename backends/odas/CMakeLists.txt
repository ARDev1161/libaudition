add_library(libaudition_backend_odas
    src/odas_options.cpp
    src/odas_config.cpp
    src/odas_runtime.cpp
    src/odas_spatial_engine.cpp)
add_library(audition::backend_odas ALIAS libaudition_backend_odas)
set_target_properties(libaudition_backend_odas PROPERTIES
    EXPORT_NAME backend_odas
    OUTPUT_NAME audition_backend_odas
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)
target_compile_definitions(libaudition_backend_odas PRIVATE LIBAUDITION_BUILDING_LIBRARY)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(libaudition_backend_odas PUBLIC LIBAUDITION_SHARED)
endif()
target_link_libraries(libaudition_backend_odas
    PUBLIC audition::interfaces
    PRIVATE ODAS::odas)
target_include_directories(libaudition_backend_odas
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
libaudition_configure_target(libaudition_backend_odas)

install(TARGETS libaudition_backend_odas
    EXPORT libauditionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/audition
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
