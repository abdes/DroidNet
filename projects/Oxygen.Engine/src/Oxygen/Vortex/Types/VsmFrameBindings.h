//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>

namespace oxygen::vortex {

//! Per-view VSM routing payload.
/*!
 Published separately from conventional shadow bindings so the renderer can
 expose VSM diagnostics and later full VSM lighting inputs without overloading
 the conventional shadow product ABI.
*/
struct alignas(16) VsmFrameBindings {
  ShaderVisibleIndex directional_shadow_mask_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex screen_shadow_mask_slot { kInvalidShaderVisibleIndex };
  std::array<std::uint32_t, 2> _pad_to_16 {};
};

static_assert(sizeof(VsmFrameBindings) == 16);
static_assert(alignof(VsmFrameBindings) == 16);
static_assert(sizeof(VsmFrameBindings) % 16 == 0);

} // namespace oxygen::vortex
