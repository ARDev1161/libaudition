find_path(ODAS_INCLUDE_DIR NAMES odas/odas.h)
find_library(ODAS_LIBRARY NAMES odas)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ODAS REQUIRED_VARS ODAS_INCLUDE_DIR ODAS_LIBRARY)
if(ODAS_FOUND AND NOT TARGET ODAS::odas)
    add_library(ODAS::odas UNKNOWN IMPORTED)
    set_target_properties(ODAS::odas PROPERTIES
        IMPORTED_LOCATION "${ODAS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ODAS_INCLUDE_DIR};${ODAS_INCLUDE_DIR}/odas")
endif()
mark_as_advanced(ODAS_INCLUDE_DIR ODAS_LIBRARY)
