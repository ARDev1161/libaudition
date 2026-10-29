cmake_minimum_required(VERSION 3.20)

project(libaudition VERSION 1.0.0 LANGUAGES CXX)
# CMake package versions are numeric; the public C++ API records prerelease identity.
set(LIBAUDITION_PUBLIC_VERSION "${PROJECT_VERSION}-rc.1")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

option(LIBAUDITION_BUILD_TESTS "Build GoogleTest unit tests" ${BUILD_TESTING})
option(LIBAUDITION_BUILD_EXAMPLES "Build examples" ON)
option(LIBAUDITION_BUILD_QT_DEMO "Build the optional Qt Widgets playground" OFF)
option(LIBAUDITION_BUILD_DOCS "Build Doxygen documentation" OFF)
option(LIBAUDITION_WITH_SPDLOG "Build the spdlog logging adapter" ON)
option(LIBAUDITION_WITH_ODAS "Build the direct libodas spatial-audio backend" OFF)
option(LIBAUDITION_WITH_LIBSAMPLERATE "Build the libsamplerate resampling backend" OFF)
option(LIBAUDITION_WITH_WEBRTC_AEC3 "Build the WebRTC AEC3 echo-cancellation backend" OFF)
option(LIBAUDITION_WITH_WEBRTC_APM
    "Enable unified WebRTC APM 2.x (AGC1/AGC2 and AEC3) backend"
    ${LIBAUDITION_WITH_WEBRTC_AEC3})
if(LIBAUDITION_WITH_WEBRTC_AEC3 AND NOT LIBAUDITION_WITH_WEBRTC_APM)
    message(STATUS "AEC3 now uses unified WebRTC APM; enabling APM")
    set(LIBAUDITION_WITH_WEBRTC_APM ON CACHE BOOL
        "Enable unified WebRTC APM" FORCE)
endif()
option(LIBAUDITION_WITH_SHERPA "Build sherpa-onnx speech backends" OFF)
option(LIBAUDITION_WITH_WHISPERCPP "Build optional whisper.cpp ASR backend (system library)" OFF)
option(LIBAUDITION_WITH_CLAP "Build the CLAP ONNX audio-embedding backend" OFF)
option(LIBAUDITION_WITH_YAMNET "Build the YAMNet ONNX AudioSet classifier" ${LIBAUDITION_BUILD_QT_DEMO})
option(LIBAUDITION_WITH_EFFICIENTAT "Build experimental EfficientAT log-mel ONNX adapter" OFF)
option(LIBAUDITION_DEMO_FETCH_YAMNET_MODEL "Download the demo YAMNet model and labels at configure time" ${LIBAUDITION_BUILD_QT_DEMO})
option(LIBAUDITION_DEMO_FETCH_SPEECH_MODELS "Download Silero VAD and SenseVoice INT8 for the Qt demo at configure time" ${LIBAUDITION_BUILD_QT_DEMO})
option(LIBAUDITION_DEMO_FETCH_EXTRA_SPEECH_MODELS "Download extended VAD and offline ASR demo model packs (large downloads)" ${LIBAUDITION_BUILD_QT_DEMO})
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
if(LIBAUDITION_WITH_WEBRTC_APM)
    libaudition_find_or_fetch_webrtc_apm()
endif()

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
    add_subdirectory(backends/webrtc_aec3)
endif()

if(LIBAUDITION_WITH_SHERPA)
    libaudition_find_or_fetch_sherpa()
    add_subdirectory(backends/sherpa)
endif()

if(LIBAUDITION_WITH_WHISPERCPP)
    add_subdirectory(backends/whispercpp)
endif()

if(LIBAUDITION_WITH_CLAP)
    libaudition_find_or_fetch_onnxruntime()
    libaudition_find_or_fetch_nlohmann_json()
    add_subdirectory(backends/clap)
endif()

if(LIBAUDITION_WITH_YAMNET)
    libaudition_find_or_fetch_onnxruntime()
    add_subdirectory(backends/yamnet)
endif()

if(LIBAUDITION_WITH_EFFICIENTAT)
    libaudition_find_or_fetch_onnxruntime()
    add_subdirectory(backends/efficientat)
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
    libaudition_find_gtsam()
    add_subdirectory(backends/gtsam)
endif()

if(LIBAUDITION_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(LIBAUDITION_BUILD_QT_DEMO)
    if(LIBAUDITION_WITH_YAMNET AND LIBAUDITION_DEMO_FETCH_YAMNET_MODEL)
        include(cmake/FetchDemoYamnet.cmake)
    endif()
    if(LIBAUDITION_WITH_SHERPA AND (LIBAUDITION_DEMO_FETCH_SPEECH_MODELS OR LIBAUDITION_DEMO_FETCH_EXTRA_SPEECH_MODELS))
        include(cmake/FetchDemoSpeechModels.cmake)
    endif()
    add_subdirectory(demo/qt)
endif()

if(LIBAUDITION_BUILD_TESTS)
    enable_testing()
    libaudition_find_or_fetch_gtest()
    add_subdirectory(tests)
endif()

# Installed package smoke can run in a minimal build with GTest disabled.
# CI installs into the configured prefix, then runs this CTest case.
option(LIBAUDITION_REGISTER_INSTALLED_PACKAGE_SMOKE
    "Register independent consumer test for the installed CMake package" OFF)
set(LIBAUDITION_PACKAGE_SMOKE_PREFIX "${CMAKE_BINARY_DIR}/install-default"
    CACHE PATH "libaudition install prefix to validate with the CTest consumer")
if(BUILD_TESTING AND LIBAUDITION_REGISTER_INSTALLED_PACKAGE_SMOKE)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME installed_package_cpp17_consumer
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/verify_installed_package.py"
            --prefix "${LIBAUDITION_PACKAGE_SMOKE_PREFIX}"
            --cmake "${CMAKE_COMMAND}"
            --generator "${CMAKE_GENERATOR}"
            --version "${PROJECT_VERSION}"
            --public-version "${LIBAUDITION_PUBLIC_VERSION}")
    set_tests_properties(installed_package_cpp17_consumer PROPERTIES
        LABELS "installed-package;package-consumer" TIMEOUT 180)
    if(LIBAUDITION_WITH_SHERPA)
        add_test(NAME installed_package_sherpa_cpp17_consumer
            COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/verify_installed_package.py"
                --prefix "${LIBAUDITION_PACKAGE_SMOKE_PREFIX}"
                --cmake "${CMAKE_COMMAND}"
                --generator "${CMAKE_GENERATOR}"
                --version "${PROJECT_VERSION}"
            --public-version "${LIBAUDITION_PUBLIC_VERSION}"
                --backend sherpa)
        set_tests_properties(installed_package_sherpa_cpp17_consumer PROPERTIES
            LABELS "installed-package;package-consumer;sherpa"
            TIMEOUT 180)
    endif()
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
