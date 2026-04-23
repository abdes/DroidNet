//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_MATERIALS_GBUFFERMATERIALOUTPUT_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_MATERIALS_GBUFFERMATERIALOUTPUT_HLSLI

#include "Vortex/Shared/MaskedAlphaTest.hlsli"
#include "Vortex/Materials/ForwardMaterialEval.hlsli"

#include "Vortex/Contracts/Definitions/SceneDefinitions.hlsli"
#include "Vortex/Contracts/Scene/GBufferHelpers.hlsli"
#include "Vortex/Shared/BRDFCommon.hlsli"

static inline uint ResolveVortexShadingModel(uint draw_index)
{
    MaterialShadingConstants material;
    if (TryLoadMaterialForDraw(draw_index, material)
        && (material.flags & MATERIAL_FLAG_UNLIT) != 0u) {
        return SHADING_MODEL_UNLIT;
    }

    return SHADING_MODEL_DEFAULT_LIT;
}

static inline GBufferOutput PackGBufferOutput(
    MaterialSurface surface, uint shading_model)
{
    GBufferOutput output;
    output.gbuffer_normal = EncodeGBufferNormal(surface.N);
    output.gbuffer_material = EncodeGBufferMaterial(
        surface.metalness, kVortexDefaultSpecular, surface.roughness, shading_model);
    output.gbuffer_base_color
        = EncodeGBufferBaseColor(surface.base_rgb, surface.ao);
    output.gbuffer_custom_data = 0.0f.xxxx;
    output.emissive_scene_color = float4(surface.emissive, surface.base_a);
#if defined(HAS_VELOCITY)
    output.velocity = float2(0.0f, 0.0f);
#endif
    return output;
}

static inline GBufferOutput EvaluateGBufferMaterialOutput(float3 world_pos,
    float3 world_normal, float3 world_tangent, float3 world_bitangent, float2 uv0,
    uint draw_index, bool is_front_face)
{
    const MaterialSurface surface = EvaluateMaterialSurface(world_pos,
        world_normal, world_tangent, world_bitangent, uv0, draw_index,
        is_front_face);
    const uint shading_model = ResolveVortexShadingModel(draw_index);
    return PackGBufferOutput(surface, shading_model);
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_MATERIALS_GBUFFERMATERIALOUTPUT_HLSLI
