//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_SHADOWINSTANCEMETADATA_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_SHADOWINSTANCEMETADATA_HLSLI

static const uint SHADOW_DOMAIN_DIRECTIONAL = 0u;
static const uint SHADOW_DOMAIN_SPOT = 1u;
static const uint SHADOW_DOMAIN_POINT = 2u;

static const uint SHADOW_IMPLEMENTATION_NONE = 0u;
static const uint SHADOW_IMPLEMENTATION_CONVENTIONAL = 1u;
static const uint SHADOW_IMPLEMENTATION_VIRTUAL = 2u;

static const uint SHADOW_PRODUCT_FLAG_VALID = 1u << 0;
static const uint SHADOW_PRODUCT_FLAG_CONTACT_SHADOWS = 1u << 1;
static const uint SHADOW_PRODUCT_FLAG_SUN_LIGHT = 1u << 2;

struct ShadowInstanceMetadata
{
    uint light_index;
    uint payload_index;
    uint domain;
    uint implementation_kind;

    uint flags;
    uint _reserved0;
    uint _reserved1;
    uint _reserved2;
};

#endif // OXYGEN_D3D12_SHADERS_RENDERER_SHADOWINSTANCEMETADATA_HLSLI
