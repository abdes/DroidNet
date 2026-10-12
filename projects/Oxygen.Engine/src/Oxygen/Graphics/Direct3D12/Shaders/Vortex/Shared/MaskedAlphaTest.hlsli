//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_MASKEDALPHATEST_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_MASKEDALPHATEST_HLSLI

// Pure masked alpha test: callers pass the draw-metadata and material slots or
// the material. Camera passes resolve them through MaskedAlphaTestView.hlsli.

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/Definitions/MaterialFlags.hlsli"
#include "Vortex/Contracts/Draw/DrawMetadata.hlsli"
#include "Vortex/Contracts/Draw/MaterialShadingConstants.hlsli"

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

// A draw's material with the draw-metadata fields material evaluation needs.
struct DrawMaterial {
  MaterialShadingConstants constants;
  uint material_handle;
  uint primitive_flags;
};

static inline bool TryLoadDrawMaterialFromSlots(uint draw_metadata_slot,
  uint material_shading_constants_slot, uint draw_index,
  out DrawMaterial material)
{
  material = (DrawMaterial)0;

  if (draw_metadata_slot == K_INVALID_BINDLESS_INDEX
    || material_shading_constants_slot == K_INVALID_BINDLESS_INDEX) {
    return false;
  }

  StructuredBuffer<DrawMetadata> draw_meta_buffer
    = ResourceDescriptorHeap[draw_metadata_slot];
  uint draw_count = 0u;
  uint draw_stride = 0u;
  draw_meta_buffer.GetDimensions(draw_count, draw_stride);
  if (draw_index >= draw_count) {
    return false;
  }

  const DrawMetadata meta = draw_meta_buffer[draw_index];

  StructuredBuffer<MaterialShadingConstants> materials
    = ResourceDescriptorHeap[material_shading_constants_slot];
  uint material_count = 0u;
  uint material_stride = 0u;
  materials.GetDimensions(material_count, material_stride);
  if (meta.material_handle >= material_count) {
    return false;
  }

  material.constants = materials[meta.material_handle];
  material.material_handle = meta.material_handle;
  material.primitive_flags = meta.primitive_flags;
  return true;
}

static inline bool TryLoadMaterialFromSlots(uint draw_metadata_slot,
  uint material_shading_constants_slot, uint draw_index,
  out MaterialShadingConstants material)
{
  DrawMaterial draw_material;
  const bool loaded = TryLoadDrawMaterialFromSlots(draw_metadata_slot,
    material_shading_constants_slot, draw_index, draw_material);
  material = draw_material.constants;
  return loaded;
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

static inline MaskedAlphaTestResult EvaluateMaskedAlphaTestForMaterial(
  float2 uv0, MaterialShadingConstants material, SamplerState linear_sampler)
{
  MaskedAlphaTestResult result = (MaskedAlphaTestResult)0;
  result.has_material_data = true;
  result.alpha_test_enabled = (material.flags & MATERIAL_FLAG_ALPHA_TEST) != 0u;
  if (!result.alpha_test_enabled) {
    return result;
  }

  result.alpha = SampleMaskedMaterialAlpha(uv0, material, linear_sampler);
  result.cutoff = ResolveMaskedAlphaCutoff(material);
  return result;
}

static inline MaskedAlphaTestResult EvaluateMaskedAlphaTestFromSlots(
  float2 uv0, uint draw_metadata_slot, uint material_shading_constants_slot,
  uint draw_index, SamplerState linear_sampler)
{
  MaterialShadingConstants material;
  if (!TryLoadMaterialFromSlots(draw_metadata_slot,
        material_shading_constants_slot, draw_index, material)) {
    return (MaskedAlphaTestResult)0;
  }
  return EvaluateMaskedAlphaTestForMaterial(uv0, material, linear_sampler);
}

static inline void ApplyMaskedAlphaClip(MaskedAlphaTestResult result)
{
  if (result.has_material_data && result.alpha_test_enabled) {
    clip(result.alpha - result.cutoff);
  }
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_MASKEDALPHATEST_HLSLI
