#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(LIBAUDITION_BUILDING_LIBRARY)
#    define AUDITION_API __declspec(dllexport)
#  elif defined(LIBAUDITION_SHARED)
#    define AUDITION_API __declspec(dllimport)
#  else
#    define AUDITION_API
#  endif
#else
#  if __GNUC__ >= 4
#    define AUDITION_API __attribute__((visibility("default")))
#  else
#    define AUDITION_API
#  endif
#endif
