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
