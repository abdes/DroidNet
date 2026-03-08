//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>

namespace oxygen::engine {

//! Bindless shadow-system routing payload for a single view.
struct alignas(packing::kShaderDataFieldAlignment) ShadowFrameBindings {
  ShaderVisibleIndex directional_shadow_metadata_slot {
    kInvalidShaderVisibleIndex
  };
  std::array<std::uint32_t, 3> _pad_to_16 {};
};

static_assert(sizeof(ShadowFrameBindings) == 16);
static_assert(
  alignof(ShadowFrameBindings) == packing::kShaderDataFieldAlignment);
static_assert(
  sizeof(ShadowFrameBindings) % packing::kShaderDataFieldAlignment == 0);

} // namespace oxygen::engine
