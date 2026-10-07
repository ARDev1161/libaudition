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
