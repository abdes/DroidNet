//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_DRAWCULLRECORD_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_DRAWCULLRECORD_HLSLI

// Mirrors oxygen::vortex::DrawCullFlagBits.
static const uint DRAW_CULL_OPAQUE = 1u << 0u;
static const uint DRAW_CULL_MASKED = 1u << 1u;
static const uint DRAW_CULL_TRANSPARENT = 1u << 2u;
static const uint DRAW_CULL_SHADOW_CASTER = 1u << 3u;
static const uint DRAW_CULL_MAIN_VIEW_VISIBLE = 1u << 4u;
static const uint DRAW_CULL_WORLD_SPACE_BOX = 1u << 5u;
static const uint DRAW_CULL_ALWAYS_VISIBLE = 1u << 6u;
static const uint DRAW_CULL_FRESH_HISTORY = 1u << 7u;

static const uint DRAW_CULL_NO_HISTORY_SLOT = 0xFFFFFFFFu;

// ABI: must match sizeof(oxygen::vortex::DrawCullRecord) == 32
struct DrawCullRecord
{
    float3 box_center;
    uint history_slot;
    float3 box_extent;
    uint flags;
};

#endif // OXYGEN_D3D12_SHADERS_RENDERER_DRAWCULLRECORD_HLSLI
