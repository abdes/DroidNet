//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTFRAMEBINDINGS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTFRAMEBINDINGS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

static const uint ENVIRONMENT_CONTRACT_FLAG_INTEGRATED_LIGHT_SCATTERING_VALID = 1u << 8u;

struct EnvironmentFrameBindings
{
    struct EnvironmentProbeBindings
    {
        uint environment_map_srv;
        uint diffuse_sh_srv;
        uint irradiance_map_srv;
        uint prefiltered_map_srv;
        uint probe_revision;
        uint product_metadata_srv;
    };

    uint environment_static_slot;
    uint environment_view_slot;
    uint atmosphere_model_slot;
    uint height_fog_model_slot;
    uint sky_light_model_slot;
    uint volumetric_fog_model_slot;
    uint environment_view_products_slot;
    uint contract_flags;
    uint transmittance_lut_srv;
    uint multi_scattering_lut_srv;
    uint sky_view_lut_srv;
    uint camera_aerial_perspective_srv;
    EnvironmentProbeBindings probes;
    uint brdf_lut_srv;
    uint padding;
};

static EnvironmentFrameBindings LoadEnvironmentFrameBindings(uint slot)
{
    EnvironmentFrameBindings invalid_bindings = (EnvironmentFrameBindings)0;
    invalid_bindings.environment_static_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.environment_view_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.atmosphere_model_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.height_fog_model_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.sky_light_model_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.volumetric_fog_model_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.environment_view_products_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.transmittance_lut_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.multi_scattering_lut_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.sky_view_lut_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.camera_aerial_perspective_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.probes.environment_map_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.probes.diffuse_sh_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.probes.irradiance_map_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.probes.prefiltered_map_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.probes.probe_revision = 0u;
    invalid_bindings.probes.product_metadata_srv = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.brdf_lut_srv = K_INVALID_BINDLESS_INDEX;

    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        return invalid_bindings;
    }

    StructuredBuffer<EnvironmentFrameBindings> bindings_buffer =
        ResourceDescriptorHeap[slot];
    return bindings_buffer[0];
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTFRAMEBINDINGS_HLSLI
