//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <expected>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxBrdf.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>

namespace oxygen::vortex::testing::reference {

using PackedGBufferNormal
  = NamedType<std::uint32_t, struct PackedNormalTag, Comparable>;
using PackedGBufferMaterial
  = NamedType<std::uint32_t, struct PackedMaterialTag, Comparable>;
using PackedGBufferBaseColor
  = NamedType<std::uint32_t, struct PackedBaseColorTag, Comparable>;
using ShadingModelCode
  = NamedType<std::uint8_t, struct ShadingModelCodeTag, Comparable>;

struct LinearRgb {
  double red { 0.0 };
  double green { 0.0 };
  double blue { 0.0 };
};

struct WorldNormal {
  double x { 0.0 };
  double y { 0.0 };
  double z { 1.0 };
};

struct StandardMaterial {
  LinearRgb base_color {};
  double metallic { 0.0 };
  double specular { 0.5 };
  PerceptualRoughness roughness { 1.0 };
};

//! Exact stored texel words, not a prediction of raster quantization/rounding.
struct PackedGBufferTexel {
  PackedGBufferNormal normal { 0U }; // R10G10B10A2_UNORM; octahedron in R/G.
  PackedGBufferMaterial material {
    0U
  }; // RGBA8_UNORM: metal/specular/rough/model.
  PackedGBufferBaseColor base_color {
    0U
  }; // RGBA8_UNORM_SRGB; alpha is linear AO.
};

struct DecodedGBufferTexel {
  WorldNormal normal {};
  StandardMaterial material {};
  double ambient_occlusion { 1.0 };
  ShadingModelCode shading_model { 0U };
};

struct MaterialBrdfChannels {
  BrdfReflectance red {};
  BrdfReflectance green {};
  BrdfReflectance blue {};
};

//! Ideal IEC sRGB decoding, independently evaluated in double precision.
[[nodiscard]] auto DecodeSrgb8(std::uint8_t code) -> double;

//! Does not interpret shading-model eligibility or change the decoded
//! roughness.
[[nodiscard]] auto DecodeGBufferTexel(const PackedGBufferTexel& texel)
  -> DecodedGBufferTexel;

//! Resolve F0=lerp(0.08*specular,base,metallic) and rho=base*(1-metallic).
[[nodiscard]] auto ResolveMaterialBrdf(const StandardMaterial& material)
  -> std::expected<MaterialBrdfChannels, BrdfReferenceError>;

} // namespace oxygen::vortex::testing::reference
