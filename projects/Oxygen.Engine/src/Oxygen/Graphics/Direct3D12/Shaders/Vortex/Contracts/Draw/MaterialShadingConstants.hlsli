//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_MATERIALSHADINGCONSTANTS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_MATERIALSHADINGCONSTANTS_HLSLI

// TODO: Add support for Tier 1/2 glTF material capabilities.
// This requires expanding the ABI and adding support for the following missing features:
// - Textures: specular, sheen_color, clearcoat, clearcoat_normal, transmission, thickness
// - Scalars: ior, specular_factor, sheen_color_factor, clearcoat_factor, clearcoat_roughness,
//            transmission_factor, thickness_factor, attenuation_color, attenuation_distance

// How a material samples one of its textures.
// ABI: must match sizeof(oxygen::vortex::MaterialSlotSampling) == 32
struct MaterialSlotSampling
{
    float2 uv_basis_u;
    float2 uv_basis_v;
    float2 uv_offset;
    uint uv_set;
    uint sampler_index;
};

// ABI: must match sizeof(oxygen::vortex::MaterialShadingConstants) == 272
struct MaterialShadingConstants
{
    // Register 0
    float4 base_color;

    // Register 1
    float3 emissive_factor;
    uint flags;

    // Register 2
    float metalness;
    float roughness;
    float normal_scale;
    float ambient_occlusion;

    // Register 3
    uint base_color_texture_index;
    uint normal_texture_index;
    uint metallic_texture_index;
    uint roughness_texture_index;

    // Register 4
    uint ambient_occlusion_texture_index;
    uint emissive_texture_index;
    float alpha_cutoff;
    uint _pad2;

    // Registers 5 to 16, one record per sampled texture slot.
    MaterialSlotSampling base_color_sampling;
    MaterialSlotSampling normal_sampling;
    MaterialSlotSampling metallic_sampling;
    MaterialSlotSampling roughness_sampling;
    MaterialSlotSampling ambient_occlusion_sampling;
    MaterialSlotSampling emissive_sampling;
};

// The texture coordinate a slot reads. `uvs` holds set 0 in xy and set 1 in zw.
// The record is already scale, then counter-clockwise rotation around the UV
// origin, then offset.
float2 ResolveSlotUv(float4 uvs, MaterialSlotSampling slot)
{
    const float2 uv = slot.uv_set == 1u ? uvs.zw : uvs.xy;
    return slot.uv_basis_u * uv.x + slot.uv_basis_v * uv.y + slot.uv_offset;
}

float4 SampleSlot(uint texture_index, MaterialSlotSampling slot, float4 uvs)
{
    Texture2D<float4> texture = ResourceDescriptorHeap[texture_index];
    SamplerState samp = SamplerDescriptorHeap[slot.sampler_index];
    return texture.Sample(samp, ResolveSlotUv(uvs, slot));
}

// A normal map's xy runs along its texture's axes, and the tangent frame along
// the mesh's. Takes xy back through the slot's rotation and mirror, not its
// tiling, whose only effect on a direction is to stretch it.
float2 SlotNormalToMeshFrame(float2 xy, MaterialSlotSampling slot)
{
    const float2 u_axis = slot.uv_basis_u / max(length(slot.uv_basis_u), 1e-6f);
    const float2 v_axis = slot.uv_basis_v / max(length(slot.uv_basis_v), 1e-6f);
    return float2(dot(u_axis, xy), dot(v_axis, xy));
}

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_MATERIALSHADINGCONSTANTS_HLSLI
