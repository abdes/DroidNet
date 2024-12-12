//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#ifdef OXYGEN_SDL3_STATIC_DEFINE
#  define OXYGEN_SDL3_API
#  define OXYGEN_SDL3_NO_EXPORT
#else
#  ifndef OXYGEN_SDL3_API
#    ifdef OXYGEN_SDL3_EXPORTS
/* We are building this library */
#      define OXYGEN_SDL3_API __declspec(dllexport)
#    else
/* We are using this library */
#      define OXYGEN_SDL3_API __declspec(dllimport)
#    endif
#  endif
#  ifndef OXYGEN_SDL3_NO_EXPORT
#    define OXYGEN_SDL3_NO_EXPORT
#  endif
#endif
