cmake_minimum_required(VERSION 3.20)

project(libacoustic VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

option(LIBACOUSTIC_BUILD_TESTS "Build GoogleTest unit tests" ${BUILD_TESTING})
option(LIBACOUSTIC_BUILD_EXAMPLES "Build examples" ON)
option(LIBACOUSTIC_BUILD_DOCS "Build Doxygen documentation" OFF)
option(LIBACOUSTIC_WITH_SPDLOG "Build the spdlog logging adapter" ON)
option(LIBACOUSTIC_FETCH_DEPENDENCIES "Fetch missing third-party build dependencies" ON)
option(LIBACOUSTIC_WARNINGS_AS_ERRORS "Treat libacoustic warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

function(libacoustic_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(LIBACOUSTIC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow)
        if(LIBACOUSTIC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_subdirectory(src)

if(LIBACOUSTIC_WITH_SPDLOG)
    libacoustic_find_or_fetch_spdlog()
    add_subdirectory(backends/spdlog)
endif()

if(LIBACOUSTIC_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(LIBACOUSTIC_BUILD_TESTS)
    enable_testing()
    libacoustic_find_or_fetch_gtest()
    add_subdirectory(tests)
endif()

if(LIBACOUSTIC_BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    set(DOXYGEN_IN "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in")
    set(DOXYGEN_OUT "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")
    configure_file("${DOXYGEN_IN}" "${DOXYGEN_OUT}" @ONLY)
    add_custom_target(docs
        COMMAND "${DOXYGEN_EXECUTABLE}" "${DOXYGEN_OUT}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating libacoustic API documentation")
endif()

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libacousticConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/libacousticConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/libacoustic")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/libacousticConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/libacousticConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/libacousticConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/libacoustic")
install(EXPORT libacousticTargets
    FILE libacousticTargets.cmake
    NAMESPACE acoustic::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/libacoustic")
install(FILES LICENSE NOTICE THIRD_PARTY.md DESTINATION "${CMAKE_INSTALL_DATADIR}/libacoustic")
