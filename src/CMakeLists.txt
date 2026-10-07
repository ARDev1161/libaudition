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
libacoustic_configure_target(libacoustic_core)

install(TARGETS libacoustic_core
    EXPORT libacousticTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/acoustic
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

add_library(libacoustic_interfaces INTERFACE)
add_library(acoustic::interfaces ALIAS libacoustic_interfaces)
set_target_properties(libacoustic_interfaces PROPERTIES EXPORT_NAME interfaces)
target_link_libraries(libacoustic_interfaces INTERFACE acoustic::core)
target_include_directories(libacoustic_interfaces INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
install(TARGETS libacoustic_interfaces EXPORT libacousticTargets)

add_library(libacoustic_model model/in_memory_model_registry.cpp)
add_library(acoustic::model ALIAS libacoustic_model)
set_target_properties(libacoustic_model PROPERTIES EXPORT_NAME model)
target_link_libraries(libacoustic_model PUBLIC acoustic::interfaces)
libacoustic_configure_target(libacoustic_model)
target_include_directories(libacoustic_model PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

add_library(libacoustic_logging logging/logging.cpp)
add_library(acoustic::logging ALIAS libacoustic_logging)
set_target_properties(libacoustic_logging PROPERTIES EXPORT_NAME logging)
target_link_libraries(libacoustic_logging PUBLIC acoustic::core)
libacoustic_configure_target(libacoustic_logging)
target_include_directories(libacoustic_logging PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

add_library(libacoustic_pipeline INTERFACE)
add_library(acoustic::pipeline ALIAS libacoustic_pipeline)
set_target_properties(libacoustic_pipeline PROPERTIES EXPORT_NAME pipeline)
target_link_libraries(libacoustic_pipeline INTERFACE acoustic::core acoustic::interfaces)
target_compile_features(libacoustic_pipeline INTERFACE cxx_std_17)
target_include_directories(libacoustic_pipeline INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

install(TARGETS libacoustic_model libacoustic_logging libacoustic_pipeline
    EXPORT libacousticTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
