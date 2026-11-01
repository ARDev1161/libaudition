# Resolve a system SpeexDSP package for the optional per-source AGC.
# The default libaudition build does not need this dependency.
include(FindPackageHandleStandardArgs)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_SPEEXDSP QUIET speexdsp)
endif()

find_path(SpeexDSP_INCLUDE_DIR
    NAMES speex/speex_preprocess.h
    HINTS ${PC_SPEEXDSP_INCLUDE_DIRS})

find_library(SpeexDSP_LIBRARY
    NAMES speexdsp
    HINTS ${PC_SPEEXDSP_LIBRARY_DIRS})

find_package_handle_standard_args(SpeexDSP
    REQUIRED_VARS SpeexDSP_INCLUDE_DIR SpeexDSP_LIBRARY)

if(SpeexDSP_FOUND AND NOT TARGET SpeexDSP::SpeexDSP)
    add_library(SpeexDSP::SpeexDSP UNKNOWN IMPORTED)
    set_target_properties(SpeexDSP::SpeexDSP PROPERTIES
        IMPORTED_LOCATION "${SpeexDSP_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SpeexDSP_INCLUDE_DIR}")
endif()

mark_as_advanced(SpeexDSP_INCLUDE_DIR SpeexDSP_LIBRARY)
