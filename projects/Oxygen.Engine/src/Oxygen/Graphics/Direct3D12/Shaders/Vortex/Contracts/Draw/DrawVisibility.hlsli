//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_DRAWVISIBILITY_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_DRAWVISIBILITY_HLSLI

// Per-draw visibility bits of one culling view, written by the cull kernel.
// Mirrors oxygen::vortex::DrawVisibilityBit.
static const uint DRAW_VISIBILITY_IN_FRUSTUM = 1u << 0u;
static const uint DRAW_VISIBILITY_PHASE1_DRAWN = 1u << 1u;
static const uint DRAW_VISIBILITY_PHASE2_DRAWN = 1u << 2u;
static const uint DRAW_VISIBILITY_VISIBLE = 1u << 3u;

#endif // OXYGEN_D3D12_SHADERS_RENDERER_DRAWVISIBILITY_HLSLI
