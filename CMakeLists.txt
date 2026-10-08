cmake_minimum_required(VERSION 3.20)

project(libaudition VERSION 0.2.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

option(LIBAUDITION_BUILD_TESTS "Build GoogleTest unit tests" ${BUILD_TESTING})
option(LIBAUDITION_BUILD_EXAMPLES "Build examples" ON)
option(LIBAUDITION_BUILD_DOCS "Build Doxygen documentation" OFF)
option(LIBAUDITION_WITH_SPDLOG "Build the spdlog logging adapter" ON)
option(LIBAUDITION_WITH_ODAS "Build the direct libodas spatial-audio backend" OFF)
option(LIBAUDITION_WITH_LIBSAMPLERATE "Build the libsamplerate resampling backend" OFF)
option(LIBAUDITION_WITH_WEBRTC_AEC3 "Build the WebRTC AEC3 echo-cancellation backend" OFF)
option(LIBAUDITION_WITH_SHERPA "Build sherpa-onnx speech backends" OFF)
option(LIBAUDITION_WITH_CLAP "Build the CLAP ONNX audio-embedding backend" OFF)
option(LIBAUDITION_WITH_AASIST "Build the AASIST ONNX authenticity backend" OFF)
option(LIBAUDITION_WITH_WORLD "Build the WORLD voice-traits backend" OFF)
option(LIBAUDITION_WITH_GTSAM "Build the GTSAM spatial-fusion backend" OFF)
option(LIBAUDITION_SHERPA_ENABLE_TTS "Enable sherpa-onnx TTS support and libaudition TTS adapter" OFF)
option(LIBAUDITION_SHERPA_ENABLE_GPU "Build fetched sherpa-onnx with its GPU provider support" OFF)
option(LIBAUDITION_SHERPA_ENABLE_DIRECTML "Build fetched sherpa-onnx with DirectML support" OFF)
option(LIBAUDITION_SHERPA_ENABLE_RKNN "Build fetched sherpa-onnx with RKNN support" OFF)
option(LIBAUDITION_SHERPA_ENABLE_AXERA "Build fetched sherpa-onnx with Axera support" OFF)
option(LIBAUDITION_SHERPA_ENABLE_AXCL "Build fetched sherpa-onnx with AXCL support" OFF)
option(LIBAUDITION_SHERPA_ENABLE_ASCEND_NPU "Build fetched sherpa-onnx with Ascend NPU support" OFF)
option(LIBAUDITION_SHERPA_ENABLE_QNN "Build fetched sherpa-onnx with Qualcomm QNN support" OFF)
option(LIBAUDITION_SHERPA_ENABLE_SPACEMIT "Build fetched sherpa-onnx with SpacemiT support" OFF)
option(LIBAUDITION_FETCH_DEPENDENCIES "Fetch missing third-party build dependencies" ON)
option(LIBAUDITION_WARNINGS_AS_ERRORS "Treat libaudition warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

function(libaudition_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_17)
    get_target_property(_libaudition_target_type ${target} TYPE)
    if(_libaudition_target_type STREQUAL "STATIC_LIBRARY" OR
       _libaudition_target_type STREQUAL "OBJECT_LIBRARY")
        set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(LIBAUDITION_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow)
        if(LIBAUDITION_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_subdirectory(src)

if(LIBAUDITION_WITH_SPDLOG)
    libaudition_find_or_fetch_spdlog()
    add_subdirectory(backends/spdlog)
endif()

if(LIBAUDITION_WITH_ODAS)
    libaudition_find_or_fetch_odas()
    add_subdirectory(backends/odas)
endif()

if(LIBAUDITION_WITH_LIBSAMPLERATE)
    libaudition_find_or_fetch_samplerate()
    add_subdirectory(backends/samplerate)
endif()

if(LIBAUDITION_WITH_WEBRTC_AEC3)
    libaudition_find_or_fetch_webrtc_aec3()
    add_subdirectory(backends/webrtc_aec3)
endif()

if(LIBAUDITION_WITH_SHERPA)
    libaudition_find_or_fetch_sherpa()
    add_subdirectory(backends/sherpa)
endif()

if(LIBAUDITION_WITH_CLAP)
    libaudition_find_or_fetch_onnxruntime()
    libaudition_find_or_fetch_nlohmann_json()
    add_subdirectory(backends/clap)
endif()

if(LIBAUDITION_WITH_AASIST)
    libaudition_find_or_fetch_onnxruntime()
    add_subdirectory(backends/aasist)
endif()

if(LIBAUDITION_WITH_WORLD)
    libaudition_find_or_fetch_world()
    add_subdirectory(backends/world)
endif()

if(LIBAUDITION_WITH_GTSAM)
    libaudition_find_or_fetch_gtsam()
    add_subdirectory(backends/gtsam)
endif()

if(LIBAUDITION_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(LIBAUDITION_BUILD_TESTS)
    enable_testing()
    libaudition_find_or_fetch_gtest()
    add_subdirectory(tests)
endif()

if(LIBAUDITION_BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    set(DOXYGEN_IN "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in")
    set(DOXYGEN_OUT "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")
    configure_file("${DOXYGEN_IN}" "${DOXYGEN_OUT}" @ONLY)
    add_custom_target(docs
        COMMAND "${DOXYGEN_EXECUTABLE}" "${DOXYGEN_OUT}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating libaudition API documentation")
endif()

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libauditionConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/libauditionConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/libaudition")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/libauditionConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/libauditionConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/libauditionConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/libaudition")
install(EXPORT libauditionTargets
    FILE libauditionTargets.cmake
    NAMESPACE audition::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/libaudition")
install(FILES LICENSE NOTICE THIRD_PARTY.md DESTINATION "${CMAKE_INSTALL_DATADIR}/libaudition")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/LICENSES" DESTINATION "${CMAKE_INSTALL_DATADIR}/libaudition")
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindODAS.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindSampleRate.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindSherpaOnnx.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/libaudition")
