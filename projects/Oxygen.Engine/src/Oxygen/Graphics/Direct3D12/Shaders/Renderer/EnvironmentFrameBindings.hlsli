//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTFRAMEBINDINGS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTFRAMEBINDINGS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

struct EnvironmentFrameBindings
{
    uint environment_static_slot;
    uint environment_view_slot;
    uint environment_map_srv;
    uint irradiance_map_srv;
    uint prefiltered_map_srv;
    uint brdf_lut_srv;
    uint probe_revision;
    float ambient_intensity;
    float average_brightness;
    float blend_fraction;
    uint evaluation_flags;
    uint ambient_bridge_irradiance_map_srv;
    float ambient_bridge_ambient_intensity;
    float ambient_bridge_average_brightness;
    float ambient_bridge_blend_fraction;
    uint ambient_bridge_flags;
};

static EnvironmentFrameBindings LoadEnvironmentFrameBindings(uint slot)
{
    EnvironmentFrameBindings invalid_bindings = (EnvironmentFrameBindings)0;
    invalid_bindings.environment_static_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.environment_view_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.environment_map_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.irradiance_map_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.prefiltered_map_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.brdf_lut_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.ambient_bridge_irradiance_map_srv = K_INVALID_BINDLESS_INDEX;

    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        return invalid_bindings;
    }

    StructuredBuffer<EnvironmentFrameBindings> bindings_buffer =
        ResourceDescriptorHeap[slot];
    return bindings_buffer[0];
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTFRAMEBINDINGS_HLSLI
