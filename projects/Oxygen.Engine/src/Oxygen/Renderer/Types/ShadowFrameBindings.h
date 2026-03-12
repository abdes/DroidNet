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
  ShaderVisibleIndex shadow_instance_metadata_slot {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex directional_shadow_metadata_slot {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex directional_shadow_texture_slot {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex virtual_shadow_page_table_slot {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex virtual_shadow_page_flags_slot {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex virtual_shadow_physical_pool_slot {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex virtual_directional_shadow_metadata_slot {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex virtual_shadow_physical_page_metadata_slot {
    kInvalidShaderVisibleIndex
  };
  ShaderVisibleIndex virtual_shadow_physical_page_lists_slot {
    kInvalidShaderVisibleIndex
  };
  std::uint32_t sun_shadow_index { 0xFFFFFFFFU };
  std::uint32_t _reserved0 { 0U };
  std::uint32_t _reserved1 { 0U };
};

static_assert(sizeof(ShadowFrameBindings) == 48);
static_assert(
  alignof(ShadowFrameBindings) == packing::kShaderDataFieldAlignment);
static_assert(
  sizeof(ShadowFrameBindings) % packing::kShaderDataFieldAlignment == 0);

} // namespace oxygen::engine
