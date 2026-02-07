//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// ImGui shader (engine-managed)
//
// Decision note: ImGui rendering remains on the upstream D3D12 backend pipeline
// (`src/Oxygen/Graphics/Direct3D12/ImGui/imgui_impl_dx12.*`), which uses its
// own root signature/PSO and binds textures via ImTextureID.
//
// As a result, this shader is not authoritative for the runtime ImGui backend
// and is excluded from the engine ABI/reflection validation gates.
//
// If an engine-owned UI pass later adopts this shader, it must be brought into
// compliance with the engine ABI and bindless-only rules (notably: no
// register-bound SRV/sampler arrays; `b2, space0` root constants must keep the
// fixed meaning of {g_DrawIndex, g_PassConstantsIndex-as-CBV-index}).
//
// Current behavior in this shader:
// - Vertex input uses a classic input layout (UI-only exception)
// - Texture sampling uses register-bound arrays and a packed payload in `b2`
//   (g_PassConstantsIndex), which is intentionally non-compliant.

#ifndef SM66
#define SM66 1
#endif

// Local bindless descriptor heap declarations (SM 6.6)
// These must match the engine's generated root signature tables:
// - SRV table:  t0, space0 (unbounded)
// - Sampler table: s0, space0 (unbounded)
Texture2D<float4> ImGuiTextures[] : register(t0, space0);
SamplerState       ImGuiSamplers[] : register(s0, space0);

// Root constants b2 (shared root param index with engine)
// ABI layout:
//   g_DrawIndex          : per-draw draw index
//   g_PassConstantsIndex : per-pass payload (future: heap index for pass CBV)
//
// For now, ImGui uses g_PassConstantsIndex as a packed texture/sampler value:
//   bits [20:0]   -> Texture SRV index (max ~2,097,151)
//   bits [31:21]  -> Sampler index     (max 2047)
cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
};

struct VSInput {
    float2 pos : POSITION;  // Already pre-transformed to clip space on CPU
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};

struct PSInput {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};

[shader("vertex")]
PSInput VS(VSInput v) {
    PSInput o;
    // Positions are in clip space already (x,y in [-1,1])
    o.pos = float4(v.pos, 0.0f, 1.0f);
    o.uv  = v.uv;
    o.col = v.col;
    return o;
}

[shader("pixel")]
float4 PS(PSInput input) : SV_Target {
    const uint texIndex = (g_PassConstantsIndex & 0x001FFFFFu); // 21 bits
    const uint sampIndex = (g_PassConstantsIndex >> 21);         // 11 bits

    // Sample with provided indices (assumed valid per backend packing)
    float4 texel = ImGuiTextures[texIndex].Sample(ImGuiSamplers[sampIndex], input.uv);
    return input.col * texel;
}
