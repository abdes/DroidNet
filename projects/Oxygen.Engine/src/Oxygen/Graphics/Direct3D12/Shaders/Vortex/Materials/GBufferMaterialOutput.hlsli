//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_MATERIALS_GBUFFERMATERIALOUTPUT_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_MATERIALS_GBUFFERMATERIALOUTPUT_HLSLI

#include "Vortex/Contracts/View/FrameExposureHelpers.hlsli"
#include "Vortex/Contracts/View/HdrStoreChecks.hlsli"
#include "Vortex/Shared/MaskedAlphaTest.hlsli"
#include "Vortex/Materials/ForwardMaterialEval.hlsli"

#include "Vortex/Contracts/Definitions/SceneDefinitions.hlsli"
#include "Vortex/Contracts/Scene/GBufferHelpers.hlsli"
#include "Vortex/Shared/BRDFCommon.hlsli"
#include "Vortex/Services/Shadows/ShadowSurfaceNormal.hlsli"

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
    MaterialSurface surface, uint shading_model, float2 uv0, uint draw_index,
    float3 geometric_normal)
{
    GBufferOutput output;
    output.gbuffer_normal = EncodeGBufferNormal(surface.N);
    output.gbuffer_material = EncodeGBufferMaterial(
        surface.metalness, kVortexDefaultSpecular, surface.roughness, shading_model);
    output.gbuffer_base_color
        = EncodeGBufferBaseColor(surface.base_rgb, surface.ao);
    output.gbuffer_custom_data = 0.0f.xxxx;
#if defined(ALPHA_TEST)
    const SamplerState linear_sampler = SamplerDescriptorHeap[0];
    const MaskedAlphaTestResult alpha_test
        = EvaluateMaskedAlphaTest(uv0, draw_index, linear_sampler);
    if (alpha_test.has_material_data && alpha_test.alpha_test_enabled) {
        output.gbuffer_custom_data = float4(
            1.0f, alpha_test.alpha, alpha_test.cutoff, 0.0f);
    }
#endif
    // Per-instance receiver state is independent of shared material/shading data.
    output.gbuffer_normal.w = surface.receives_shadows ? 1.0f : 0.0f;
    // Preserve the geometric normal for metric contact bias without another MRT.
    const float2 geometric_oct = OctahedronEncode(geometric_normal) * 0.5f + 0.5f;
    output.gbuffer_normal.z = geometric_oct.x;
    output.gbuffer_custom_data.w = geometric_oct.y;
    // Surviving opaque/masked fragments have full foreground coverage.
#if defined(OXYGEN_DEPTH_COMPLETE)
    const ViewFrameBindings bindings = LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    CheckHdrStoreRange(float4(surface.emissive, 1.0f), 1u,
        bindings.exposure_status_uav, 0u, 1.0f);
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
    const MaterialSurface surface = EvaluateMaterialSurface(world_pos,
        world_normal, world_tangent, world_bitangent, uv0, draw_index,
        is_front_face);
    const uint shading_model = ResolveVortexShadingModel(draw_index);
    const float3 geometric_normal = ComputeShadowSurfaceNormal(
        world_pos, world_normal, is_front_face);
    return PackGBufferOutput(surface, shading_model, uv0, draw_index, geometric_normal);
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_MATERIALS_GBUFFERMATERIALOUTPUT_HLSLI
