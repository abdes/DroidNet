//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_MATERIALS_GBUFFERMATERIALOUTPUT_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_MATERIALS_GBUFFERMATERIALOUTPUT_HLSLI

#include "Vortex/Contracts/View/FrameExposureHelpers.hlsli"
#include "Vortex/Contracts/View/HdrStoreChecks.hlsli"
#include "Vortex/Shared/MaskedAlphaTestView.hlsli"
#include "Vortex/Materials/ForwardMaterialEval.hlsli"

#include "Vortex/Contracts/Definitions/SceneDefinitions.hlsli"
#include "Vortex/Contracts/Scene/GBufferHelpers.hlsli"
#include "Vortex/Shared/BRDFCommon.hlsli"
#include "Vortex/Services/Shadows/ShadowSurfaceNormal.hlsli"

// The draw's material, loaded once per pixel, with its masked alpha test
// already applied (clip) under ALPHA_TEST.
struct BasePassMaterial
{
    bool has_material;
    DrawMaterial draw_material;
    uint procedural_grid_material_constants_slot;
    MaskedAlphaTestResult alpha_test;
};

static inline BasePassMaterial LoadClippedBasePassMaterial(
    float2 uv0, uint draw_index)
{
    BasePassMaterial material = (BasePassMaterial)0;
    const DrawFrameBindings draw_bindings = LoadResolvedDrawFrameBindings();
    material.procedural_grid_material_constants_slot
        = draw_bindings.procedural_grid_material_constants_slot;
    material.has_material = TryLoadDrawMaterialFromSlots(
        draw_bindings.draw_metadata_slot,
        draw_bindings.material_shading_constants_slot, draw_index,
        material.draw_material);
#if defined(ALPHA_TEST)
    if (material.has_material) {
        const SamplerState linear_sampler = SamplerDescriptorHeap[0];
        material.alpha_test = EvaluateMaskedAlphaTestForMaterial(
            uv0, material.draw_material.constants, linear_sampler);
    }
    ApplyMaskedAlphaClip(material.alpha_test);
#endif
    return material;
}

static inline MaterialSurface EvaluateBasePassSurface(BasePassMaterial material,
    float3 world_pos, float3 world_normal, float3 world_tangent,
    float3 world_bitangent, float2 uv0, bool is_front_face)
{
    return EvaluateMaterialSurfaceForMaterial(world_pos, world_normal,
        world_tangent, world_bitangent, uv0, material.has_material,
        material.draw_material,
        material.procedural_grid_material_constants_slot, is_front_face);
}

static inline uint ResolveVortexShadingModel(BasePassMaterial material)
{
    return material.has_material
            && (material.draw_material.constants.flags & MATERIAL_FLAG_UNLIT) != 0u
        ? SHADING_MODEL_UNLIT
        : SHADING_MODEL_DEFAULT_LIT;
}

static inline GBufferOutput PackGBufferOutput(MaterialSurface surface,
    uint shading_model, MaskedAlphaTestResult alpha_test,
    float3 geometric_normal)
{
    GBufferOutput output;
    output.gbuffer_normal = EncodeGBufferNormal(surface.N);
    output.gbuffer_material = EncodeGBufferMaterial(
        surface.metalness, kVortexDefaultSpecular, surface.roughness, shading_model);
    output.gbuffer_base_color
        = EncodeGBufferBaseColor(surface.base_rgb, surface.ao);
    output.gbuffer_custom_data = 0.0f.xxxx;
    if (alpha_test.has_material_data && alpha_test.alpha_test_enabled) {
        output.gbuffer_custom_data = float4(
            1.0f, alpha_test.alpha, alpha_test.cutoff, 0.0f);
    }
    // Per-instance receiver state is independent of shared material/shading data.
    output.gbuffer_normal.w = surface.receives_shadows ? 1.0f : 0.0f;
    // Preserve the geometric normal for metric contact bias without another MRT.
    const float2 geometric_oct = EncodeGBufferGeometricNormal(geometric_normal);
    output.gbuffer_normal.z = geometric_oct.x;
    output.gbuffer_custom_data.w = geometric_oct.y;
    // Surviving opaque/masked fragments have full foreground coverage.
#if defined(OXYGEN_DEPTH_COMPLETE)
    CheckHdrStoreRange(float4(surface.emissive, 1.0f), 1u,
        GetExposureStatusUav(), 0u, 1.0f);
#endif
    output.emissive_scene_color = float4(surface.emissive * GetPreExposure(), 1.0f);
#if defined(HAS_VELOCITY)
    output.velocity = float2(0.0f, 0.0f);
#endif
    return output;
}

static inline GBufferOutput EvaluateGBufferMaterialOutput(float3 world_pos,
    float3 world_normal, float3 world_tangent, float3 world_bitangent, float2 uv0,
    uint draw_index, bool is_front_face)
{
    const BasePassMaterial material = LoadClippedBasePassMaterial(uv0, draw_index);
    const MaterialSurface surface = EvaluateBasePassSurface(material, world_pos,
        world_normal, world_tangent, world_bitangent, uv0, is_front_face);
    const float3 geometric_normal = ComputeShadowSurfaceNormal(
        world_pos, world_normal, is_front_face);
    return PackGBufferOutput(surface, ResolveVortexShadingModel(material),
        material.alpha_test, geometric_normal);
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_MATERIALS_GBUFFERMATERIALOUTPUT_HLSLI
