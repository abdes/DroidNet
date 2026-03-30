//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPAGEFLAGS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPAGEFLAGS_HLSLI

// Virtual-page flags derived for the current frame.
//
// This is a separate buffer from the page table because these bits describe
// per-frame work and coverage state, not just mapping. Typical consumers:
// - request/management passes write or propagate the bits
// - raster and HZB passes select work from uncached/detail bits
// - projection may use allocated state for fast rejection

static const uint VSM_PAGE_FLAG_ALLOCATED = 1u << 0;
// Dynamic content in this page is not reusable and must be refreshed.
static const uint VSM_PAGE_FLAG_DYNAMIC_UNCACHED = 1u << 1;
// Static content in this page is not reusable and must be refreshed.
static const uint VSM_PAGE_FLAG_STATIC_UNCACHED = 1u << 2;
// The page intersects detailed geometry and may need finer-grained handling.
static const uint VSM_PAGE_FLAG_DETAIL_GEOMETRY = 1u << 3;
// A coarser page has at least one mapped descendant page in a finer level.
static const uint VSM_PAGE_FLAG_MAPPED_DESCENDANT = 1u << 4;

struct VsmShaderPageFlags
{
    // Bitwise OR of VSM_PAGE_FLAG_* values.
    uint bits;
};

static bool VsmHasAnyPageFlag(const VsmShaderPageFlags flags, const uint bits)
{
    return (flags.bits & bits) != 0u;
}

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPAGEFLAGS_HLSLI
