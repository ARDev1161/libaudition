add_library(libaudition_backend_spdlog src/spdlog_sink.cpp)
add_library(audition::backend_spdlog ALIAS libaudition_backend_spdlog)
set_target_properties(libaudition_backend_spdlog PROPERTIES EXPORT_NAME backend_spdlog OUTPUT_NAME audition_backend_spdlog)
target_include_directories(libaudition_backend_spdlog
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(libaudition_backend_spdlog
    PUBLIC audition::logging
    PRIVATE spdlog::spdlog)
libaudition_configure_target(libaudition_backend_spdlog)

install(TARGETS libaudition_backend_spdlog
    EXPORT libauditionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/audition
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
