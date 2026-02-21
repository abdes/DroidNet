
#pragma once

#if defined(_WIN32) || defined(_WIN64)
#  ifdef OXGN_PHSYNC_STATIC
#    define OXGN_PHSYNC_API
#  else
#    ifdef OXGN_PHSYNC_EXPORTS
#      define OXGN_PHSYNC_API __declspec(dllexport)
#    else
#      define OXGN_PHSYNC_API __declspec(dllimport)
#    endif
#  endif
#elif defined(__APPLE__) || defined(__linux__)
#  ifdef OXGN_PHSYNC_EXPORTS
#    define OXGN_PHSYNC_API __attribute__((visibility("default")))
#  else
#    define OXGN_PHSYNC_API
#  endif
#else
#  define OXGN_PHSYNC_API
#endif

#define OXGN_PHSYNC_NDAPI [[nodiscard]] OXGN_PHSYNC_API
