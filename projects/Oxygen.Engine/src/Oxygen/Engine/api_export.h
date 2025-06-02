//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#if defined(_WIN32) || defined(_WIN64)
#  ifdef OXYGEN_NGIN_STATIC
#    define OXYGEN_NGIN_API
#  else
#    ifdef OXYGEN_NGIN_EXPORTS
#      define OXYGEN_NGIN_API __declspec(dllexport)
#    else
#      define OXYGEN_NGIN_API __declspec(dllimport)
#    endif
#  endif
#elif defined(__APPLE__) || defined(__linux__)
#  ifdef OXYGEN_NGIN_EXPORTS
#    define OXYGEN_NGIN_API __attribute__((visibility("default")))
#  else
#    define OXYGEN_NGIN_API
#  endif
#else
#  define OXYGEN_NGIN_API
#endif
