//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_SKY_DIFFUSE_IRRADIANCE_HLSLI
#define OXYGEN_VORTEX_SKY_DIFFUSE_IRRADIANCE_HLSLI

// Pure sky-light diffuse evaluation from the environment data passed in.

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/Environment/EnvironmentStaticData.hlsli"
#include "Vortex/Contracts/Environment/IblProductMetadata.hlsli"

static float3 EvaluatePackedSkyDiffuseSh(StructuredBuffer<float4> sh, float3 normal_ws)
{
    const float normal_len_sq = dot(normal_ws, normal_ws);
    if (normal_len_sq <= 1.0e-8f) {
        return 0.0f.xxx;
    }

    const float4 normal = float4(normal_ws * rsqrt(normal_len_sq), 1.0f);
    const float3 intermediate0 = float3(
        dot(sh[0], normal),
        dot(sh[1], normal),
        dot(sh[2], normal));

    const float4 v_b = normal.xyzz * normal.yzzx;
    const float3 intermediate1 = float3(
        dot(sh[3], v_b),
        dot(sh[4], v_b),
        dot(sh[5], v_b));

    const float v_c = normal.x * normal.x - normal.y * normal.y;
    const float3 intermediate2 = sh[6].xyz * v_c;
    return max(0.0f.xxx, intermediate0 + intermediate1 + intermediate2);
}

static inline float3 EvaluateSkyDiffuseIrradiance(EnvironmentStaticData env_data, float3 normal_ws)
{
    if (env_data.sky_light.enabled == 0u
        || env_data.sky_light.diffuse_sh_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(env_data.sky_light.diffuse_sh_slot)) return 0.0.xxx;
    StructuredBuffer<float4> sh = ResourceDescriptorHeap[env_data.sky_light.diffuse_sh_slot];
    if (!BX_IN_GLOBAL_SRV(env_data.sky_light.product_metadata_srv)) return 0.0.xxx;
    StructuredBuffer<IblProductMetadata> metadata = ResourceDescriptorHeap[env_data.sky_light.product_metadata_srv];
    const IblProductMetadata product = metadata[0];
    if (!IsIblProductReady(product, env_data.sky_light.ibl_generation)) return 0.0.xxx;
    return EvaluatePackedSkyDiffuseSh(sh, normal_ws) * product.source_radiance_scale;
}

#endif // OXYGEN_VORTEX_SKY_DIFFUSE_IRRADIANCE_HLSLI
