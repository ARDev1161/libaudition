include(FetchContent)
include(ExternalProject)

function(libaudition_find_or_fetch_spdlog)
    find_package(spdlog 1.15 CONFIG QUIET)
    if(spdlog_FOUND)
        return()
    endif()
    if(NOT LIBAUDITION_FETCH_DEPENDENCIES)
        message(FATAL_ERROR "spdlog not found. Install spdlog or set LIBAUDITION_FETCH_DEPENDENCIES=ON")
    endif()
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.17.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(spdlog)
endfunction()

function(libaudition_find_or_fetch_gtest)
    find_package(GTest 1.16 CONFIG QUIET)
    if(GTest_FOUND)
        return()
    endif()
    if(NOT LIBAUDITION_FETCH_DEPENDENCIES)
        message(FATAL_ERROR "GoogleTest not found. Install it or set LIBAUDITION_FETCH_DEPENDENCIES=ON")
    endif()
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.18.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(googletest)
endfunction()

function(libaudition_find_or_fetch_odas)
    find_package(ODAS QUIET)
    if(TARGET ODAS::odas)
        return()
    endif()
    if(NOT LIBAUDITION_FETCH_DEPENDENCIES)
        message(FATAL_ERROR "ODAS not found. Install libodas or set LIBAUDITION_FETCH_DEPENDENCIES=ON")
    endif()

    set(_odas_install_dir "${CMAKE_BINARY_DIR}/_deps/odas-install")
    set(_odas_library
        "${_odas_install_dir}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}odas${CMAKE_SHARED_LIBRARY_SUFFIX}")
    file(MAKE_DIRECTORY
        "${_odas_install_dir}/include"
        "${_odas_install_dir}/include/odas"
        "${_odas_install_dir}/lib")

    ExternalProject_Add(libaudition_odas_external
        GIT_REPOSITORY https://github.com/introlab/odas.git
        GIT_TAG bcb845434495e293df3d48f1203b7a86e1852449
        GIT_SHALLOW FALSE
        UPDATE_DISCONNECTED TRUE
        INSTALL_DIR "${_odas_install_dir}"
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DODAS_DISABLE_INSTALL=OFF
            -DODAS_INSTALL_EXECUTABLES=OFF
            -DODAS_FORCE_BIN_AND_LIB_DIRS=OFF
        BUILD_BYPRODUCTS "${_odas_library}")

    add_library(ODAS::odas SHARED IMPORTED GLOBAL)
    set_target_properties(ODAS::odas PROPERTIES
        IMPORTED_LOCATION "${_odas_library}"
        INTERFACE_INCLUDE_DIRECTORIES
            "${_odas_install_dir}/include;${_odas_install_dir}/include/odas")
    add_dependencies(ODAS::odas libaudition_odas_external)

    install(DIRECTORY "${_odas_install_dir}/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(FILES "${_odas_library}"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endfunction()

function(libaudition_find_or_fetch_samplerate)
    find_package(SampleRate QUIET)
    if(TARGET SampleRate::samplerate)
        return()
    endif()
    if(NOT LIBAUDITION_FETCH_DEPENDENCIES)
        message(FATAL_ERROR "libsamplerate not found. Install it or set LIBAUDITION_FETCH_DEPENDENCIES=ON")
    endif()

    set(_samplerate_install_dir "${CMAKE_BINARY_DIR}/_deps/samplerate-install")
    set(_samplerate_library
        "${_samplerate_install_dir}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}samplerate${CMAKE_STATIC_LIBRARY_SUFFIX}")
    file(MAKE_DIRECTORY
        "${_samplerate_install_dir}/include"
        "${_samplerate_install_dir}/lib")

    ExternalProject_Add(libaudition_samplerate_external
        GIT_REPOSITORY https://github.com/libsndfile/libsamplerate.git
        GIT_TAG c96f5e3de9c4488f4e6c97f59f5245f22fda22f7
        GIT_SHALLOW FALSE
        UPDATE_DISCONNECTED TRUE
        INSTALL_DIR "${_samplerate_install_dir}"
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DBUILD_SHARED_LIBS=OFF
            -DBUILD_TESTING=OFF
            -DLIBSAMPLERATE_EXAMPLES=OFF
            -DLIBSAMPLERATE_INSTALL=ON
        BUILD_BYPRODUCTS "${_samplerate_library}")

    add_library(SampleRate::samplerate STATIC IMPORTED GLOBAL)
    set_target_properties(SampleRate::samplerate PROPERTIES
        IMPORTED_LOCATION "${_samplerate_library}"
        INTERFACE_INCLUDE_DIRECTORIES "${_samplerate_install_dir}/include")
    if(UNIX AND NOT APPLE)
        set_property(TARGET SampleRate::samplerate APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES m)
    endif()
    add_dependencies(SampleRate::samplerate libaudition_samplerate_external)

    install(FILES "${_samplerate_install_dir}/include/samplerate.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(FILES "${_samplerate_library}"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endfunction()

function(libaudition_find_or_fetch_webrtc_aec3)
    if(TARGET webrtc_aec3)
        return()
    endif()
    if(NOT LIBAUDITION_FETCH_DEPENDENCIES)
        message(FATAL_ERROR "WebRTC AEC3 backend requires the pinned standalone AEC3 source; set LIBAUDITION_FETCH_DEPENDENCIES=ON")
    endif()

    set(_webrtc_aec3_source_dir "${CMAKE_BINARY_DIR}/_deps/webrtc-aec3-src")
    set(_webrtc_aec3_binary_dir "${CMAKE_BINARY_DIR}/_deps/webrtc-aec3-build")
    set(_webrtc_aec3_library
        "${_webrtc_aec3_binary_dir}/${CMAKE_SHARED_LIBRARY_PREFIX}webrtc_aec3${CMAKE_SHARED_LIBRARY_SUFFIX}")

    ExternalProject_Add(libaudition_webrtc_aec3_external
        GIT_REPOSITORY https://github.com/Enaium/webrtc-aec3.git
        GIT_TAG 2cec2f52e26646f93bd2d5498bbabf59cba18da9
        GIT_SHALLOW FALSE
        UPDATE_DISCONNECTED TRUE
        SOURCE_DIR "${_webrtc_aec3_source_dir}"
        BINARY_DIR "${_webrtc_aec3_binary_dir}"
        PATCH_COMMAND
            "${CMAKE_COMMAND}"
            -DSOURCE_DIR=<SOURCE_DIR>
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/PatchWebrtcAec3.cmake"
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DBUILD_TESTS=OFF
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS "${_webrtc_aec3_library}")

    add_library(webrtc_aec3 SHARED IMPORTED GLOBAL)
    set_target_properties(webrtc_aec3 PROPERTIES
        IMPORTED_LOCATION "${_webrtc_aec3_library}")
    add_dependencies(webrtc_aec3 libaudition_webrtc_aec3_external)

    set(LIBAUDITION_WEBRTC_AEC3_SOURCE_DIR
        "${_webrtc_aec3_source_dir}" PARENT_SCOPE)

    install(FILES "${_webrtc_aec3_library}"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endfunction()


function(libaudition_find_or_fetch_sherpa)
    find_package(SherpaOnnx QUIET)
    if(TARGET SherpaOnnx::cxx_api)
        return()
    endif()
    if(NOT LIBAUDITION_FETCH_DEPENDENCIES)
        message(FATAL_ERROR "sherpa-onnx not found. Install it or set LIBAUDITION_FETCH_DEPENDENCIES=ON")
    endif()

    set(_sherpa_install_dir "${CMAKE_BINARY_DIR}/_deps/sherpa-onnx-install")
    set(_sherpa_cxx_api
        "${_sherpa_install_dir}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}sherpa-onnx-cxx-api${CMAKE_SHARED_LIBRARY_SUFFIX}")
    file(MAKE_DIRECTORY
        "${_sherpa_install_dir}/include"
        "${_sherpa_install_dir}/lib"
        "${_sherpa_install_dir}/bin")

    ExternalProject_Add(libaudition_sherpa_external
        GIT_REPOSITORY https://github.com/k2-fsa/sherpa-onnx.git
        GIT_TAG 99ddefaa92129858b80a71a426903dd4215c83fa
        GIT_SHALLOW FALSE
        UPDATE_DISCONNECTED TRUE
        INSTALL_DIR "${_sherpa_install_dir}"
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DBUILD_SHARED_LIBS=ON
            -DSHERPA_ONNX_ENABLE_PORTAUDIO=OFF
            -DSHERPA_ONNX_ENABLE_WEBSOCKET=OFF
            -DSHERPA_ONNX_ENABLE_BINARY=OFF
            -DSHERPA_ONNX_ENABLE_C_API=ON
            -DSHERPA_ONNX_ENABLE_TTS=OFF
            -DSHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION=ON
            -DSHERPA_ONNX_ENABLE_TESTS=OFF
            -DSHERPA_ONNX_ENABLE_CHECK=OFF
            -DSHERPA_ONNX_BUILD_C_API_EXAMPLES=OFF
            -DSHERPA_ONNX_USE_PRE_INSTALLED_ONNXRUNTIME_IF_AVAILABLE=OFF
            -DSHERPA_ONNX_ENABLE_GPU=${LIBAUDITION_SHERPA_ENABLE_GPU}
            -DSHERPA_ONNX_ENABLE_DIRECTML=${LIBAUDITION_SHERPA_ENABLE_DIRECTML}
            -DSHERPA_ONNX_ENABLE_RKNN=${LIBAUDITION_SHERPA_ENABLE_RKNN}
            -DSHERPA_ONNX_ENABLE_AXERA=${LIBAUDITION_SHERPA_ENABLE_AXERA}
            -DSHERPA_ONNX_ENABLE_AXCL=${LIBAUDITION_SHERPA_ENABLE_AXCL}
            -DSHERPA_ONNX_ENABLE_ASCEND_NPU=${LIBAUDITION_SHERPA_ENABLE_ASCEND_NPU}
            -DSHERPA_ONNX_ENABLE_QNN=${LIBAUDITION_SHERPA_ENABLE_QNN}
            -DSHERPA_ONNX_ENABLE_SPACEMIT=${LIBAUDITION_SHERPA_ENABLE_SPACEMIT}
        BUILD_BYPRODUCTS "${_sherpa_cxx_api}")

    add_library(SherpaOnnx::cxx_api SHARED IMPORTED GLOBAL)
    set_target_properties(SherpaOnnx::cxx_api PROPERTIES
        IMPORTED_LOCATION "${_sherpa_cxx_api}"
        INTERFACE_INCLUDE_DIRECTORIES "${_sherpa_install_dir}/include"
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_sherpa_install_dir}/include")
    add_dependencies(SherpaOnnx::cxx_api libaudition_sherpa_external)

    install(DIRECTORY "${_sherpa_install_dir}/lib/"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(DIRECTORY "${_sherpa_install_dir}/bin/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        OPTIONAL)
endfunction()
