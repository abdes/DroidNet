//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#if defined(_WIN32) || defined(_WIN64)
#  ifdef OXGN_CLP_STATIC
#    define OXGN_CLP_API
#  else
#    ifdef OXGN_CLP_EXPORTS
#      define OXGN_CLP_API __declspec(dllexport)
#    else
#      define OXGN_CLP_API __declspec(dllimport)
#    endif
#  endif
#elif defined(__APPLE__) || defined(__linux__)
#  ifdef OXGN_CLP_EXPORTS
#    define OXGN_CLP_API __attribute__((visibility("default")))
#  else
#    define OXGN_CLP_API
#  endif
#else
#  define OXGN_CLP_API
#endif

#define OXGN_CLP_NDAPI [[nodiscard]] OXGN_CLP_API
