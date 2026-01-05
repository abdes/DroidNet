//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Renderer/Types/SceneConstants.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine {

//! Per-frame environment payload consumed directly by shaders.
/*!
 This structure is intended to be bound as a **root CBV** (register b3) and is
 therefore kept small and frequently updated ("hot").

 The payload contains:

 - View-exposure values computed by the renderer (potentially from authored
   post-process settings + auto-exposure).
 - Bindless SRV slots and dimensions for clustered lighting buffers.

 Layout mirrors the HLSL struct `EnvironmentDynamicData`.

 @warning This struct must remain 16-byte aligned for D3D12 root CBV
          bindings.
*/
struct alignas(16) EnvironmentDynamicData {
  float exposure { 1.0F };
  float white_point { 1.0F };

  uint32_t bindless_cluster_grid_slot { kInvalidDescriptorSlot };
  uint32_t bindless_cluster_index_list_slot { kInvalidDescriptorSlot };

  uint32_t cluster_dim_x { 0 };
  uint32_t cluster_dim_y { 0 };
  uint32_t cluster_dim_z { 0 };
  uint32_t _pad0 { 0 };
};
static_assert(sizeof(EnvironmentDynamicData) % 16 == 0,
  "EnvironmentDynamicData size must be 16-byte aligned");
static_assert(sizeof(EnvironmentDynamicData) == 32,
  "EnvironmentDynamicData size must match HLSL packing");

} // namespace oxygen::engine
