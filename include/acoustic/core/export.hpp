#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(LIBACOUSTIC_BUILDING_LIBRARY)
#    define ACOUSTIC_API __declspec(dllexport)
#  elif defined(LIBACOUSTIC_SHARED)
#    define ACOUSTIC_API __declspec(dllimport)
#  else
#    define ACOUSTIC_API
#  endif
#else
#  if __GNUC__ >= 4
#    define ACOUSTIC_API __attribute__((visibility("default")))
#  else
#    define ACOUSTIC_API
#  endif
#endif
