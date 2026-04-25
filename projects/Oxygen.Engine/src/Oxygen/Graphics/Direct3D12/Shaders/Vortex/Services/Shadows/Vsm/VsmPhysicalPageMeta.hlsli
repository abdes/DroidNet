//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPHYSICALPAGEMETA_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPHYSICALPAGEMETA_HLSLI

#include "Vortex/Services/Shadows/Vsm/VsmCommon.hlsli"

// Persistent physical-page metadata ABI.
//
// Unlike page-table entries and virtual page flags, this record is intentionally
// shared with the CPU contract because it is already a narrow, leaf metadata
// shape. It describes one physical page in the shared pool and is carried across
// frames for reuse, invalidation, HZB gating, and diagnostics.
//
// Semantics:
// - *_invalidated bits are previous-frame invalidation state consumed by the
//   next reuse / initialization path
// - owner_* identifies the virtual page that last owned the physical page
// - last_touched_frame is a 64-bit frame generation split into uint2 for HLSL
struct VsmPhysicalPageMeta
{
    // 0 or 1. Physical storage exists for this slot.
    uint is_allocated;
    // 0 or 1. The page received raster output this frame.
    uint is_dirty;
    // 0 or 1. The page participated in the current frame build.
    uint used_this_frame;
    // 0 or 1. The page must run initialization work before reuse.
    uint view_uncached;
    // 0 or 1. Static slice/content is invalid and cannot be reused as-is.
    uint static_invalidated;
    // 0 or 1. Dynamic slice/content is invalid and cannot be reused as-is.
    uint dynamic_invalidated;
    // Age in frames since last active use, for retention/eviction policy.
    uint age;
    // Owning virtual map id for the last published mapping.
    uint owner_id;
    // Owning mip/clip level inside owner_id.
    uint owner_mip_level;
    // Owning page coordinate inside the virtual map.
    VsmVirtualPageCoord owner_page;
    // Low/high 32-bit words of the last touched frame generation.
    uint2 last_touched_frame;
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPHYSICALPAGEMETA_HLSLI
