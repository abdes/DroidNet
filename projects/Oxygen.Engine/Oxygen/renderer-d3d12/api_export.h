//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#ifdef OXYGEN_RENDERER_D3D12_STATIC_DEFINE
#  define OXYGEN_RENDERER_D3D12_API
#  define OXYGEN_RENDERER_D3D12_NO_EXPORT
#else
#  ifndef OXYGEN_RENDERER_D3D12_API
#    ifdef OXYGEN_RENDERER_D3D12_EXPORTS
/* We are building this library */
#      define OXYGEN_RENDERER_D3D12_API __declspec(dllexport)
#    else
/* We are using this library */
#      define OXYGEN_RENDERER_D3D12_API __declspec(dllimport)
#    endif
#  endif
#  ifndef OXYGEN_RENDERER_D3D12_NO_EXPORT
#    define OXYGEN_RENDERER_D3D12_NO_EXPORT
#  endif
#endif
