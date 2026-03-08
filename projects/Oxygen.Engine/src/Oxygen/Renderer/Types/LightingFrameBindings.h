//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Renderer/Types/LightCullingConfig.h>
#include <Oxygen/Renderer/Types/SyntheticSunData.h>

namespace oxygen::engine {

//! Bindless lighting-system routing payload for a single view.
struct alignas(packing::kShaderDataFieldAlignment) LightingFrameBindings {
  ShaderVisibleIndex directional_lights_slot { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex positional_lights_slot { kInvalidShaderVisibleIndex };
  std::uint32_t _pad0 { 0 };
  std::uint32_t _pad1 { 0 };
  LightCullingConfig light_culling {};
  SyntheticSunData sun {};
};

static_assert(sizeof(LightingFrameBindings) == 112);
static_assert(
  alignof(LightingFrameBindings) == packing::kShaderDataFieldAlignment);
static_assert(
  sizeof(LightingFrameBindings) % packing::kShaderDataFieldAlignment == 0);

} // namespace oxygen::engine
