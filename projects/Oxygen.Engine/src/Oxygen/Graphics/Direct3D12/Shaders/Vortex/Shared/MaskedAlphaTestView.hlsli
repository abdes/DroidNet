//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_MASKEDALPHATESTVIEW_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_MASKEDALPHATESTVIEW_HLSLI

// Camera-view masked alpha test: resolves the draw's material through the bound
// view. Non-camera passes use EvaluateMaskedAlphaTestFromSlots with their own
// inputs record.

#include "Vortex/Contracts/Draw/DrawHelpers.hlsli"
#include "Vortex/Shared/MaskedAlphaTest.hlsli"

// Resolves the slots through the bound view's frame bindings.
static inline bool TryLoadMaterialForDraw(
  uint draw_index, out MaterialShadingConstants material)
{
  const DrawFrameBindings draw_bindings = LoadResolvedDrawFrameBindings();
  return TryLoadMaterialFromSlots(draw_bindings.draw_metadata_slot,
    draw_bindings.material_shading_constants_slot, draw_index, material);
}

static inline MaskedAlphaTestResult EvaluateMaskedAlphaTest(
  float2 uv0, uint draw_index, SamplerState linear_sampler)
{
  MaterialShadingConstants material;
  if (!TryLoadMaterialForDraw(draw_index, material)) {
    return (MaskedAlphaTestResult)0;
  }
  return EvaluateMaskedAlphaTestForMaterial(uv0, material, linear_sampler);
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_MASKEDALPHATESTVIEW_HLSLI
