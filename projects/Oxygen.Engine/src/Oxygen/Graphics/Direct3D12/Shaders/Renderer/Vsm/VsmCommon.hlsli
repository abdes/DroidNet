//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMCOMMON_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMCOMMON_HLSLI

// Shared low-level VSM constants and tiny value types.
//
// This header intentionally contains only policy-free ABI constants used by
// multiple VSM passes. It does not declare resource bindings or pass-specific
// logic. Future VSM compute/graphics passes include this file when they need:
// - canonical page-table encoding constants
// - a common virtual-page coordinate shape
// - conservative compile-time defaults for standalone contract compilation

// Runtime pool configuration remains authoritative. These compile-time values
// exist for standalone shader-contract helpers and can be overridden by the
// future VSM pipeline if needed.
#ifndef OXYGEN_VSM_PAGE_SIZE_TEXELS
#define OXYGEN_VSM_PAGE_SIZE_TEXELS 128u
#endif

#ifndef OXYGEN_VSM_MAX_MIP_LEVELS
#define OXYGEN_VSM_MAX_MIP_LEVELS 16u
#endif

static const uint VSM_PAGE_SIZE_TEXELS = OXYGEN_VSM_PAGE_SIZE_TEXELS;
static const uint VSM_MAX_MIP_LEVELS = OXYGEN_VSM_MAX_MIP_LEVELS;
static const uint VSM_PAGE_TABLE_MAPPED_BIT = 0x80000000u;
static const uint VSM_PAGE_TABLE_PHYSICAL_PAGE_MASK = 0x7fffffffu;

struct VsmVirtualPageCoord
{
    // Mip/clip level within one virtual map.
    uint level;
    // Page-space coordinates inside that level.
    uint page_x;
    uint page_y;
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMCOMMON_HLSLI
