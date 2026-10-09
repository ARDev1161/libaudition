add_library(libaudition_core
    core/error.cpp
    core/time.cpp
    core/audio_buffer.cpp)
add_library(audition::core ALIAS libaudition_core)
set_target_properties(libaudition_core PROPERTIES
    EXPORT_NAME core
    OUTPUT_NAME audition_core
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)
target_compile_definitions(libaudition_core PRIVATE LIBAUDITION_BUILDING_LIBRARY)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(libaudition_core PUBLIC LIBAUDITION_SHARED)
endif()
target_include_directories(libaudition_core
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
libaudition_configure_target(libaudition_core)

install(TARGETS libaudition_core
    EXPORT libauditionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/audition
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

add_library(libaudition_interfaces INTERFACE)
add_library(audition::interfaces ALIAS libaudition_interfaces)
set_target_properties(libaudition_interfaces PROPERTIES EXPORT_NAME interfaces)
target_link_libraries(libaudition_interfaces INTERFACE audition::core)
target_include_directories(libaudition_interfaces INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
install(TARGETS libaudition_interfaces EXPORT libauditionTargets)

add_library(libaudition_spatial
    spatial/level_range_prior.cpp
    spatial/spl_calibration.cpp
    spatial/temporal_smoother.cpp)
add_library(audition::spatial ALIAS libaudition_spatial)
set_target_properties(libaudition_spatial PROPERTIES
    EXPORT_NAME spatial
    OUTPUT_NAME audition_spatial)
target_compile_definitions(libaudition_spatial PRIVATE LIBAUDITION_BUILDING_LIBRARY)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(libaudition_spatial PUBLIC LIBAUDITION_SHARED)
endif()
target_link_libraries(libaudition_spatial PUBLIC audition::interfaces)
libaudition_configure_target(libaudition_spatial)
target_include_directories(libaudition_spatial PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
install(TARGETS libaudition_spatial
    EXPORT libauditionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

add_library(libaudition_model model/in_memory_model_registry.cpp)
add_library(audition::model ALIAS libaudition_model)
set_target_properties(libaudition_model PROPERTIES EXPORT_NAME model OUTPUT_NAME audition_model)
target_link_libraries(libaudition_model PUBLIC audition::interfaces)
libaudition_configure_target(libaudition_model)
target_include_directories(libaudition_model PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

add_library(libaudition_logging logging/logging.cpp)
add_library(audition::logging ALIAS libaudition_logging)
set_target_properties(libaudition_logging PROPERTIES EXPORT_NAME logging OUTPUT_NAME audition_logging)
target_link_libraries(libaudition_logging PUBLIC audition::core)
libaudition_configure_target(libaudition_logging)
target_include_directories(libaudition_logging PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

add_library(libaudition_pipeline
    pipeline/spatial_identity.cpp)
add_library(audition::pipeline ALIAS libaudition_pipeline)
set_target_properties(libaudition_pipeline PROPERTIES
    EXPORT_NAME pipeline
    OUTPUT_NAME audition_pipeline)
target_compile_definitions(libaudition_pipeline PRIVATE LIBAUDITION_BUILDING_LIBRARY)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(libaudition_pipeline PUBLIC LIBAUDITION_SHARED)
endif()
target_link_libraries(libaudition_pipeline PUBLIC audition::core audition::interfaces)
libaudition_configure_target(libaudition_pipeline)
target_include_directories(libaudition_pipeline PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

install(TARGETS libaudition_model libaudition_logging libaudition_pipeline
    EXPORT libauditionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

add_library(libaudition_memory
    memory/in_memory_registries.cpp
    memory/sound_prototype_matcher.cpp
    memory/source_identity_resolver.cpp)
add_library(audition::memory ALIAS libaudition_memory)
set_target_properties(libaudition_memory PROPERTIES EXPORT_NAME memory OUTPUT_NAME audition_memory)
target_link_libraries(libaudition_memory PUBLIC audition::interfaces)
libaudition_configure_target(libaudition_memory)
target_include_directories(libaudition_memory PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
install(TARGETS libaudition_memory
    EXPORT libauditionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

add_library(libaudition_dsp
    dsp/basic.cpp
    dsp/audio_frontend.cpp
    dsp/quality.cpp)
add_library(audition::dsp ALIAS libaudition_dsp)
set_target_properties(libaudition_dsp PROPERTIES EXPORT_NAME dsp OUTPUT_NAME audition_dsp)
target_compile_definitions(libaudition_dsp PRIVATE LIBAUDITION_BUILDING_LIBRARY)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(libaudition_dsp PUBLIC LIBAUDITION_SHARED)
endif()
target_link_libraries(libaudition_dsp PUBLIC audition::core)
libaudition_configure_target(libaudition_dsp)
target_include_directories(libaudition_dsp PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
install(TARGETS libaudition_dsp
    EXPORT libauditionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
