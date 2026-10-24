find_path(WHISPERCPP_INCLUDE_DIR NAMES whisper.h PATH_SUFFIXES whisper)
find_library(WHISPERCPP_LIBRARY NAMES whisper)
if(NOT WHISPERCPP_INCLUDE_DIR OR NOT WHISPERCPP_LIBRARY)
    message(FATAL_ERROR "LIBAUDITION_WITH_WHISPERCPP requires installed whisper.cpp (whisper.h and libwhisper)")
endif()

add_library(libaudition_backend_whispercpp SHARED src/whispercpp_asr.cpp)
add_library(audition::backend_whispercpp ALIAS libaudition_backend_whispercpp)
set_target_properties(libaudition_backend_whispercpp PROPERTIES
    EXPORT_NAME backend_whispercpp OUTPUT_NAME audition_backend_whispercpp
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_compile_definitions(libaudition_backend_whispercpp PRIVATE
    LIBAUDITION_BUILDING_LIBRARY PUBLIC LIBAUDITION_SHARED)
target_link_libraries(libaudition_backend_whispercpp PUBLIC audition::interfaces PRIVATE ${WHISPERCPP_LIBRARY})
target_include_directories(libaudition_backend_whispercpp
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${WHISPERCPP_INCLUDE_DIR})
libaudition_configure_target(libaudition_backend_whispercpp)
install(TARGETS libaudition_backend_whispercpp EXPORT libauditionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/audition DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
