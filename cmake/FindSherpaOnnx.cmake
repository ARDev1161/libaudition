find_path(SherpaOnnx_INCLUDE_DIR
    NAMES sherpa-onnx/c-api/cxx-api.h)

find_library(SherpaOnnx_CXX_API_LIBRARY
    NAMES sherpa-onnx-cxx-api)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SherpaOnnx
    REQUIRED_VARS SherpaOnnx_INCLUDE_DIR SherpaOnnx_CXX_API_LIBRARY)

if(SherpaOnnx_FOUND AND NOT TARGET SherpaOnnx::cxx_api)
    add_library(SherpaOnnx::cxx_api SHARED IMPORTED)
    set_target_properties(SherpaOnnx::cxx_api PROPERTIES
        IMPORTED_LOCATION "${SherpaOnnx_CXX_API_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SherpaOnnx_INCLUDE_DIR}"
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${SherpaOnnx_INCLUDE_DIR}")
endif()

mark_as_advanced(SherpaOnnx_INCLUDE_DIR SherpaOnnx_CXX_API_LIBRARY)
