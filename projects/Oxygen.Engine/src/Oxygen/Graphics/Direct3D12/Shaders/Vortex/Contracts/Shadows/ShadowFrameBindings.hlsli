//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_CONTRACTS_SHADOWS_SHADOWFRAMEBINDINGS_HLSLI
#define OXYGEN_VORTEX_CONTRACTS_SHADOWS_SHADOWFRAMEBINDINGS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/Shadows/ShadowCascadeBinding.hlsli"

#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Contracts/View/ViewFrameBindings.hlsli"

static const uint VORTEX_SHADOW_TECHNIQUE_DIRECTIONAL_CONVENTIONAL = 1u << 0u;
static const uint VORTEX_SHADOW_TECHNIQUE_SPOT_CONVENTIONAL = 1u << 1u;
static const uint VORTEX_SHADOW_TECHNIQUE_POINT_CONVENTIONAL = 1u << 2u;

struct VortexSpotShadowBinding
{
    float4x4 light_view_projection;
    float4 position_and_inv_range;
    float4 direction_and_bias;
    float4 sampling_metadata0;
    float4 sampling_metadata1;
};

struct VortexPointShadowBinding
{
    float4x4 face_light_view_projection[6];
    float4 position_and_inv_range;
    float4 sampling_metadata0;
    float4 sampling_metadata1;
    float4 _padding0;
};

struct VortexShadowFrameBindings
{
    uint conventional_shadow_surface_handle;
    uint cascade_count;
    uint technique_flags;
    uint sampling_contract_flags;
    float4 light_direction_to_source;
    uint spot_shadow_surface_handle;
    uint spot_shadow_count;
    uint _padding0;
    uint _padding1;
    VortexShadowCascadeBinding cascades[4];
    VortexSpotShadowBinding spot_shadows[8];
    uint point_shadow_surface_handle;
    uint point_shadow_count;
    uint _padding2;
    uint _padding3;
    VortexPointShadowBinding point_shadows[4];
};

static inline VortexShadowFrameBindings MakeInvalidVortexShadowFrameBindings()
{
    VortexShadowFrameBindings bindings = (VortexShadowFrameBindings)0;
    bindings.conventional_shadow_surface_handle = K_INVALID_BINDLESS_INDEX;
    bindings.cascade_count = 0u;
    bindings.technique_flags = 0u;
    bindings.sampling_contract_flags = 0u;
    bindings.spot_shadow_surface_handle = K_INVALID_BINDLESS_INDEX;
    bindings.spot_shadow_count = 0u;
    bindings.point_shadow_surface_handle = K_INVALID_BINDLESS_INDEX;
    bindings.point_shadow_count = 0u;
    return bindings;
}

static inline VortexShadowFrameBindings LoadVortexShadowFrameBindings()
{
    const ViewFrameBindingsData view_bindings =
        LoadVortexViewFrameBindings(bindless_view_frame_bindings_slot);
    if (view_bindings.shadow_frame_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(view_bindings.shadow_frame_slot)) {
        return MakeInvalidVortexShadowFrameBindings();
    }

    StructuredBuffer<VortexShadowFrameBindings> bindings_buffer =
        ResourceDescriptorHeap[view_bindings.shadow_frame_slot];
    return bindings_buffer[0];
}

#endif // OXYGEN_VORTEX_CONTRACTS_SHADOWS_SHADOWFRAMEBINDINGS_HLSLI
