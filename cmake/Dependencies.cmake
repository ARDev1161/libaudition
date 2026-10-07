include(FetchContent)

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
    set(ODAS_DISABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(ODAS_INSTALL_EXECUTABLES OFF CACHE BOOL "" FORCE)
    set(ODAS_FORCE_BIN_AND_LIB_DIRS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        odas
        GIT_REPOSITORY https://github.com/introlab/odas.git
        GIT_TAG bcb845434495e293df3d48f1203b7a86e1852449
        GIT_SHALLOW FALSE)
    FetchContent_GetProperties(odas)
    if(NOT odas_POPULATED)
        FetchContent_Populate(odas)
        add_subdirectory("${odas_SOURCE_DIR}" "${odas_BINARY_DIR}" EXCLUDE_FROM_ALL)
    endif()
    if(NOT TARGET odas)
        message(FATAL_ERROR "Fetched ODAS did not define the expected 'odas' target")
    endif()
    target_include_directories(odas PUBLIC
        "${odas_SOURCE_DIR}/include"
        "${odas_SOURCE_DIR}/include/odas")
    if(NOT TARGET ODAS::odas)
        add_library(ODAS::odas ALIAS odas)
    endif()
endfunction()
