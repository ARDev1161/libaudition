add_library(libaudition_backend_yamnet SHARED src/yamnet_audio_tagger.cpp)
add_library(audition::backend_yamnet ALIAS libaudition_backend_yamnet)

set_target_properties(libaudition_backend_yamnet PROPERTIES
    EXPORT_NAME backend_yamnet
    OUTPUT_NAME audition_backend_yamnet
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)
if(APPLE)
    set_target_properties(libaudition_backend_yamnet PROPERTIES INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(libaudition_backend_yamnet PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()
target_compile_definitions(libaudition_backend_yamnet PRIVATE LIBAUDITION_BUILDING_LIBRARY PUBLIC LIBAUDITION_SHARED)
target_link_libraries(libaudition_backend_yamnet PUBLIC audition::interfaces PRIVATE OnnxRuntime::onnxruntime)
target_include_directories(libaudition_backend_yamnet PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
libaudition_configure_target(libaudition_backend_yamnet)
install(TARGETS libaudition_backend_yamnet EXPORT libauditionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/audition DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
