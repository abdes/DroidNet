//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// The geometry view: the scene's opaque and masked geometry drawn straight to
// scene color with a view pixel shader instead of GBuffer packing. It serves
// the wireframe render modes and the geometry debug view modes, which show
// vertex attributes the GBuffer does not keep. None of this is compiled into
// the production base pass.

#include "Vortex/Stages/BasePass/BasePassGBuffer.hlsl"
#include "Vortex/Contracts/View/FrameExposureHelpers.hlsli"

struct WireframePassConstants
{
    float4 wire_color;
    float write_pre_exposed;
    float3 padding;
};

static inline float4 LoadWireframeColor()
{
    float4 color = float4(1.0f, 1.0f, 1.0f, 1.0f);
    float write_pre_exposed = 0.0f;
    if (BX_IsValidSlot(g_PassConstantsIndex))
    {
        StructuredBuffer<WireframePassConstants> constants =
            ResourceDescriptorHeap[g_PassConstantsIndex];
        const WireframePassConstants pc = constants[0];
        color = pc.wire_color;
        write_pre_exposed = pc.write_pre_exposed;
    }

    if (write_pre_exposed > 0.5f)
    {
        color.rgb *= GetPreExposure();
    }

    return color;
}

static inline void ClipMaskedGeometry(BasePassGBufferVSOutput input)
{
#if defined(ALPHA_TEST)
    const SamplerState linear_sampler = SamplerDescriptorHeap[K_SAMPLER_LINEAR_WRAP];
    ApplyMaskedAlphaClip(
        EvaluateMaskedAlphaTest(input.uv, input.draw_index, linear_sampler));
#endif
}

[shader("pixel")]
float4 BasePassWireframePS(BasePassGBufferVSOutput input) : SV_Target0
{
    ClipMaskedGeometry(input);
    return LoadWireframeColor();
}

#if defined(DEBUG_TEXTURE_COORDINATES)

// U in red and V in green, wrapped to [0, 1), with a line every 1/8 of a
// texture and blue where a coordinate lies outside [0, 1].
static inline float3 TextureCoordinatesColor(float2 uv)
{
    const float2 wrapped = frac(uv);
    const float2 cells = uv * 8.0f;
    const float2 width = fwidth(cells);
    const float2 to_line = min(frac(cells), 1.0f - frac(cells)) / max(width, 1e-6f);
    const float on_line = 1.0f - saturate(min(to_line.x, to_line.y) - 0.5f);
    const bool outside = any(uv < 0.0f) || any(uv > 1.0f);
    const float3 color = float3(wrapped, outside ? 1.0f : 0.0f);
    return lerp(color, color * 0.25f, on_line);
}

#elif defined(DEBUG_TANGENT_FRAME_CHECK)

// `space::texture`: T follows +dP/dU and B +dP/dV. Compares the interpolated
// vertex frame with the triangle's dP/dU and dP/dV from screen derivatives:
// green both agree, red T disagrees, blue B disagrees, magenta both, grey
// where the UVs are degenerate.
static inline float3 TangentFrameCheckColor(BasePassGBufferVSOutput input)
{
    const float3 dp_dx = ddx(input.world_pos);
    const float3 dp_dy = ddy(input.world_pos);
    const float2 duv_dx = ddx(input.uv);
    const float2 duv_dy = ddy(input.uv);
    const float det = duv_dx.x * duv_dy.y - duv_dx.y * duv_dy.x;
    if (abs(det) < 1e-12f)
    {
        return float3(0.5f, 0.5f, 0.5f);
    }
    const float3 n = SafeNormalize(input.world_normal);
    float3 dp_du = (dp_dx * duv_dy.y - dp_dy * duv_dx.y) / det;
    float3 dp_dv = (dp_dy * duv_dx.x - dp_dx * duv_dy.x) / det;
    dp_du = SafeNormalize(dp_du - n * dot(n, dp_du));
    dp_dv = SafeNormalize(dp_dv - n * dot(n, dp_dv));

    const float kAgreement = 0.7071f; // 45 degrees
    const bool tangent_ok
        = dot(SafeNormalize(input.world_tangent), dp_du) > kAgreement;
    const bool bitangent_ok
        = dot(SafeNormalize(input.world_bitangent), dp_dv) > kAgreement;
    return float3(tangent_ok ? 0.0f : 1.0f,
        tangent_ok && bitangent_ok ? 1.0f : 0.0f,
        bitangent_ok ? 0.0f : 1.0f);
}

#endif

#if defined(DEBUG_TEXTURE_COORDINATES) || defined(DEBUG_TANGENT_FRAME_CHECK)

[shader("pixel")]
float4 BasePassGeometryDebugPS(BasePassGBufferVSOutput input) : SV_Target0
{
    ClipMaskedGeometry(input);
#if defined(DEBUG_TEXTURE_COORDINATES)
    return float4(TextureCoordinatesColor(input.uv), 1.0f);
#else
    return float4(TangentFrameCheckColor(input), 1.0f);
#endif
}

#endif
