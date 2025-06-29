//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#if defined(_WIN32) || defined(_WIN64)
#  ifdef OXGN_CNTT_STATIC
#    define OXGN_CNTT_API
#  else
#    ifdef OXGN_CNTT_EXPORTS
#      define OXGN_CNTT_API __declspec(dllexport)
#    else
#      define OXGN_CNTT_API __declspec(dllimport)
#    endif
#  endif
#elif defined(__APPLE__) || defined(__linux__)
#  ifdef OXGN_CNTT_EXPORTS
#    define OXGN_CNTT_API __attribute__((visibility("default")))
#  else
#    define OXGN_CNTT_API
#  endif
#else
#  define OXGN_CNTT_API
#endif

#define OXGN_CNTT_NDAPI [[nodiscard]] OXGN_CNTT_API
