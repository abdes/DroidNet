//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

// Export macros of the oxygen::platform-input library (the platform-neutral
// input vocabulary), separate from oxygen::platform so that both can be shared
// libraries without duplicating the InputSlots registry.

#if defined(_WIN32) || defined(_WIN64)
#  ifdef OXGN_PLIN_STATIC
#    define OXGN_PLIN_API
#  else
#    ifdef OXGN_PLIN_EXPORTS
#      define OXGN_PLIN_API __declspec(dllexport)
#    else
#      define OXGN_PLIN_API __declspec(dllimport)
#    endif
#  endif
#elif defined(__APPLE__) || defined(__linux__)
#  ifdef OXGN_PLIN_EXPORTS
#    define OXGN_PLIN_API __attribute__((visibility("default")))
#  else
#    define OXGN_PLIN_API
#  endif
#else
#  define OXGN_PLIN_API
#endif

#define OXGN_PLIN_NDAPI [[nodiscard]] OXGN_PLIN_API
