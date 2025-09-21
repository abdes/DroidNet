//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// ImGui shader (engine-managed, bindless-friendly)
// - Vertex input uses a classic input layout (UI-only exception)
// - Texture sampling uses SM 6.6 descriptor heaps with a bindless texture index
//   provided via root constant b2. Sampler index uses slot 0 by convention
//   (Bilinear Clamp recommended).

#ifndef SM66
    #define SM66 1
#endif

// Local bindless descriptor heap declarations (SM 6.6)
// These must match the engine's generated root signature tables:
// - SRV table:  t0, space0 (unbounded)
// - Sampler table: s0, space0 (unbounded)
Texture2D<float4> ImGuiTextures[] : register(t0, space0);
SamplerState       ImGuiSamplers[] : register(s0, space0);

// Root constant b2 (shared root param index with engine)
// Packed value layout (no RS change needed):
//   bits [20:0]   -> Texture SRV index (max ~2,097,151)
//   bits [31:21]  -> Sampler index     (max 2047)
cbuffer ImGuiPackedIndices : register(b2)
{
    uint g_TexSampPacked;
};

struct VSInput
{
    float2 pos : POSITION;  // Already pre-transformed to clip space on CPU
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};

[shader("vertex")]
PSInput VS(VSInput v)
{
    PSInput o;
    // Positions are in clip space already (x,y in [-1,1])
    o.pos = float4(v.pos, 0.0f, 1.0f);
    o.uv  = v.uv;
    o.col = v.col;
    return o;
}

[shader("pixel")]
float4 PS(PSInput input) : SV_Target
{
    const uint texIndex = (g_TexSampPacked & 0x001FFFFFu); // 21 bits
    const uint sampIndex = (g_TexSampPacked >> 21);         // 11 bits

    // Sample with provided indices (assumed valid per backend packing)
    float4 texel = ImGuiTextures[texIndex].Sample(ImGuiSamplers[sampIndex], input.uv);
    return input.col * texel;
}
