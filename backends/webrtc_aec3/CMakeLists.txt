add_library(libaudition_backend_webrtc_aec3 SHARED
    src/webrtc_aec3_echo_canceller.cpp)
add_library(audition::backend_webrtc_aec3 ALIAS libaudition_backend_webrtc_aec3)

set_target_properties(libaudition_backend_webrtc_aec3 PROPERTIES
    EXPORT_NAME backend_webrtc_aec3
    OUTPUT_NAME audition_backend_webrtc_aec3
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)

if(APPLE)
    set_target_properties(libaudition_backend_webrtc_aec3 PROPERTIES
        INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(libaudition_backend_webrtc_aec3 PROPERTIES
        INSTALL_RPATH "$ORIGIN")
endif()

target_compile_definitions(libaudition_backend_webrtc_aec3
    PRIVATE LIBAUDITION_BUILDING_LIBRARY
    PUBLIC LIBAUDITION_SHARED)
target_link_libraries(libaudition_backend_webrtc_aec3
    PUBLIC audition::interfaces
    PRIVATE webrtc_aec3)
target_include_directories(libaudition_backend_webrtc_aec3
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
libaudition_configure_target(libaudition_backend_webrtc_aec3)

install(TARGETS libaudition_backend_webrtc_aec3
    EXPORT libauditionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/audition
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
