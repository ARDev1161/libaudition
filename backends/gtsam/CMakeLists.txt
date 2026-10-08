add_library(libaudition_backend_gtsam SHARED
    src/gtsam_spatial_fusion.cpp)
add_library(audition::backend_gtsam ALIAS libaudition_backend_gtsam)

set_target_properties(libaudition_backend_gtsam PROPERTIES
    EXPORT_NAME backend_gtsam
    OUTPUT_NAME audition_backend_gtsam
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)

target_compile_definitions(libaudition_backend_gtsam
    PRIVATE LIBAUDITION_BUILDING_LIBRARY
    PUBLIC LIBAUDITION_SHARED)

target_link_libraries(libaudition_backend_gtsam
    PUBLIC audition::interfaces
    PRIVATE GTSAM::gtsam)

target_include_directories(libaudition_backend_gtsam
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

libaudition_configure_target(libaudition_backend_gtsam)

install(TARGETS libaudition_backend_gtsam
    EXPORT libauditionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/audition
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
