find_path(SampleRate_INCLUDE_DIR NAMES samplerate.h)
find_library(SampleRate_LIBRARY NAMES samplerate)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SampleRate
    REQUIRED_VARS SampleRate_INCLUDE_DIR SampleRate_LIBRARY)

if(SampleRate_FOUND AND NOT TARGET SampleRate::samplerate)
    add_library(SampleRate::samplerate UNKNOWN IMPORTED)
    set_target_properties(SampleRate::samplerate PROPERTIES
        IMPORTED_LOCATION "${SampleRate_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SampleRate_INCLUDE_DIR}")
    if(UNIX AND NOT APPLE)
        set_property(TARGET SampleRate::samplerate APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES m)
    endif()
endif()

mark_as_advanced(SampleRate_INCLUDE_DIR SampleRate_LIBRARY)
