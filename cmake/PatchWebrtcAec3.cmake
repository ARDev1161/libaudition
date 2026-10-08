if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(_cmake_file "${SOURCE_DIR}/CMakeLists.txt")
file(READ "${_cmake_file}" _content)

set(_old "add_compile_options(-Wall -fPIC -Wno-deprecated -m64 -fexceptions)")
set(_new "add_compile_options(-Wall -fPIC -Wno-deprecated -fexceptions)")
string(REPLACE "${_old}" "${_new}" _patched "${_content}")
if(_patched STREQUAL _content AND _content MATCHES "-m64")
    message(FATAL_ERROR "Could not remove architecture-specific -m64 from standalone AEC3")
endif()

set(_simd_patch [=[
# libaudition patch: the standalone extraction compiles SIMD translation units
# but does not assign the ISA flags required by their intrinsics.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    file(GLOB_RECURSE AEC3_AVX2_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/*_avx2.cc")
    file(GLOB_RECURSE AEC3_SSE2_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/*_sse2.cc")
    file(GLOB_RECURSE AEC3_SSE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/*_sse.cc")
    if(AEC3_AVX2_SRC)
        set_source_files_properties(${AEC3_AVX2_SRC} PROPERTIES COMPILE_OPTIONS "-mavx2")
    endif()
    if(AEC3_SSE2_SRC)
        set_source_files_properties(${AEC3_SSE2_SRC} PROPERTIES COMPILE_OPTIONS "-msse2")
    endif()
    if(AEC3_SSE_SRC)
        set_source_files_properties(${AEC3_SSE_SRC} PROPERTIES COMPILE_OPTIONS "-msse2")
    endif()
endif()
]=])

if(NOT _patched MATCHES "libaudition patch: the standalone extraction compiles SIMD")
    string(APPEND _patched "\n${_simd_patch}\n")
endif()

file(WRITE "${_cmake_file}" "${_patched}")
