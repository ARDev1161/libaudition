add_library(libaudition_backend_clap SHARED
    src/clap_features.cpp
    src/clap_audio_embedder.cpp)
add_library(audition::backend_clap ALIAS libaudition_backend_clap)

set_target_properties(libaudition_backend_clap PROPERTIES
    EXPORT_NAME backend_clap
    OUTPUT_NAME audition_backend_clap
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)

if(APPLE)
    set_target_properties(libaudition_backend_clap PROPERTIES
        INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(libaudition_backend_clap PROPERTIES
        INSTALL_RPATH "$ORIGIN")
endif()

target_compile_definitions(libaudition_backend_clap
    PRIVATE LIBAUDITION_BUILDING_LIBRARY
    PUBLIC LIBAUDITION_SHARED)

target_link_libraries(libaudition_backend_clap
    PUBLIC audition::interfaces
    PRIVATE OnnxRuntime::onnxruntime)

target_include_directories(libaudition_backend_clap
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

libaudition_configure_target(libaudition_backend_clap)

install(TARGETS libaudition_backend_clap
    EXPORT libauditionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/audition
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
