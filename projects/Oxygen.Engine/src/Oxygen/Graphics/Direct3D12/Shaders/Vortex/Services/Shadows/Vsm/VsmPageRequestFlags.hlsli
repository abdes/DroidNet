//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPAGEREQUESTFLAGS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPAGEREQUESTFLAGS_HLSLI

// Per-frame page-request flags.
//
// This buffer is separate from the persistent page-flag buffer used later in
// the page-management pipeline. It only answers the Stage 5 question:
// "which virtual pages does the current camera view demand?"

static const uint VSM_PAGE_REQUEST_FLAG_REQUIRED = 1u << 0;
static const uint VSM_PAGE_REQUEST_FLAG_COARSE = 1u << 1;
static const uint VSM_PAGE_REQUEST_FLAG_STATIC_ONLY = 1u << 2;

struct VsmShaderPageRequestFlags
{
    // Bitwise OR of VSM_PAGE_REQUEST_FLAG_* values.
    uint bits;
};

static bool VsmHasAnyPageRequestFlag(const VsmShaderPageRequestFlags flags, const uint bits)
{
    return (flags.bits & bits) != 0u;
}

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPAGEREQUESTFLAGS_HLSLI
