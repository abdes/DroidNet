//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_MASKEDALPHATEST_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_MASKEDALPHATEST_HLSLI

#include "MaterialFlags.hlsli"
#include "Renderer/DrawHelpers.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"

static const float kDefaultMaskedAlphaCutoff = 0.5f;

struct MaskedAlphaTestResult {
  bool has_material_data;
  bool alpha_test_enabled;
  float alpha;
  float cutoff;
};

static inline float ResolveMaskedAlphaCutoff(MaterialShadingConstants material)
{
  return material.alpha_cutoff > 0.0f ? material.alpha_cutoff
                                      : kDefaultMaskedAlphaCutoff;
}

static inline bool TryLoadMaterialForDraw(
  uint draw_index, out MaterialShadingConstants material)
{
  material = (MaterialShadingConstants)0;

  const DrawFrameBindings draw_bindings = LoadResolvedDrawFrameBindings();
  if (draw_bindings.draw_metadata_slot == K_INVALID_BINDLESS_INDEX
    || draw_bindings.material_shading_constants_slot
      == K_INVALID_BINDLESS_INDEX) {
    return false;
  }

  StructuredBuffer<DrawMetadata> draw_meta_buffer
    = ResourceDescriptorHeap[draw_bindings.draw_metadata_slot];
  uint draw_count = 0u;
  uint draw_stride = 0u;
  draw_meta_buffer.GetDimensions(draw_count, draw_stride);
  if (draw_index >= draw_count) {
    return false;
  }

  const DrawMetadata meta = draw_meta_buffer[draw_index];

  StructuredBuffer<MaterialShadingConstants> materials
    = ResourceDescriptorHeap[draw_bindings.material_shading_constants_slot];
  uint material_count = 0u;
  uint material_stride = 0u;
  materials.GetDimensions(material_count, material_stride);
  if (meta.material_handle >= material_count) {
    return false;
  }

  material = materials[meta.material_handle];
  return true;
}

static inline float SampleMaskedMaterialAlpha(
  float2 uv0, MaterialShadingConstants material, SamplerState linear_sampler)
{
  float alpha = material.base_color.a;
  const bool no_texture_sampling
    = (material.flags & MATERIAL_FLAG_NO_TEXTURE_SAMPLING) != 0u;

  if (!no_texture_sampling
    && material.base_color_texture_index != K_INVALID_BINDLESS_INDEX) {
    const float2 uv = ApplyMaterialUv(uv0, material);
    Texture2D<float4> base_tex
      = ResourceDescriptorHeap[material.base_color_texture_index];
    alpha *= base_tex.Sample(linear_sampler, uv).a;
  }

  return alpha;
}

static inline MaskedAlphaTestResult EvaluateMaskedAlphaTest(
  float2 uv0, uint draw_index, SamplerState linear_sampler)
{
  MaskedAlphaTestResult result = (MaskedAlphaTestResult)0;

  MaterialShadingConstants material;
  if (!TryLoadMaterialForDraw(draw_index, material)) {
    return result;
  }

  result.has_material_data = true;
  result.alpha_test_enabled = (material.flags & MATERIAL_FLAG_ALPHA_TEST) != 0u;
  if (!result.alpha_test_enabled) {
    return result;
  }

  result.alpha = SampleMaskedMaterialAlpha(uv0, material, linear_sampler);
  result.cutoff = ResolveMaskedAlphaCutoff(material);
  return result;
}

static inline void ApplyMaskedAlphaClip(MaskedAlphaTestResult result)
{
  if (result.has_material_data && result.alpha_test_enabled) {
    clip(result.alpha - result.cutoff);
  }
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_MASKEDALPHATEST_HLSLI
