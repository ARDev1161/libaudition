add_library(libacoustic_backend_spdlog src/spdlog_sink.cpp)
add_library(acoustic::backend_spdlog ALIAS libacoustic_backend_spdlog)
set_target_properties(libacoustic_backend_spdlog PROPERTIES EXPORT_NAME backend_spdlog)
target_include_directories(libacoustic_backend_spdlog
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(libacoustic_backend_spdlog
    PUBLIC acoustic::logging
    PRIVATE spdlog::spdlog)
libacoustic_configure_target(libacoustic_backend_spdlog)

install(TARGETS libacoustic_backend_spdlog
    EXPORT libacousticTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/acoustic
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
