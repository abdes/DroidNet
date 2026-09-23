//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstdint>
#include <expected>

#include <Oxygen/Vortex/Test/Lighting/Reference/GgxBrdf.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/MaterialDecode.h>

namespace oxygen::vortex::testing::reference {
namespace {
  constexpr double kUnorm8Maximum = 255.0;
  constexpr std::uint32_t kByteMask = 0xFFU;
  constexpr std::uint32_t kBitsPerByte = 8U;
  constexpr std::uint32_t kNormalBits = 10U;
  constexpr std::uint32_t kNormalMask = (1U << kNormalBits) - 1U;

  auto Channel(const std::uint32_t packed, const std::uint32_t lane)
    -> std::uint8_t
  {
    return static_cast<std::uint8_t>(
      (packed >> (lane * kBitsPerByte)) & kByteMask);
  }
  auto Unorm(const std::uint8_t code) -> double
  {
    return static_cast<double>(code) / kUnorm8Maximum;
  }
  auto Reflectance(const double value) -> bool
  {
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
  }
} // namespace

auto DecodeSrgb8(const std::uint8_t code) -> double
{
  const auto encoded = Unorm(code);
  return encoded <= 0.04045 ? encoded / 12.92
                            : std::pow((encoded + 0.055) / 1.055, 2.4);
}

auto DecodeGBufferTexel(const PackedGBufferTexel& texel) -> DecodedGBufferTexel
{
  const auto packed_normal = texel.normal.get();
  auto x
    = ((2.0 * static_cast<double>(packed_normal & kNormalMask)) - kNormalMask)
    / kNormalMask;
  auto y
    = ((2.0 * static_cast<double>((packed_normal >> kNormalBits) & kNormalMask))
        - kNormalMask)
    / kNormalMask;
  const auto z = 1.0 - std::abs(x) - std::abs(y);
  if (z < 0.0) {
    const auto folded_x = std::copysign(1.0 - std::abs(y), x);
    y = std::copysign(1.0 - std::abs(x), y);
    x = folded_x;
  }
  const auto length = std::hypot(x, y, z);
  const auto material = texel.material.get();
  const auto color = texel.base_color.get();
  return {
    .normal = { .x=x/length, .y=y/length, .z=z/length },
    .material = {
      .base_color = { .red=DecodeSrgb8(Channel(color,0U)), .green=DecodeSrgb8(Channel(color,1U)), .blue=DecodeSrgb8(Channel(color,2U)) },
      .metallic = Unorm(Channel(material,0U)),
      .specular = Unorm(Channel(material,1U)),
      .roughness = PerceptualRoughness { Unorm(Channel(material,2U)) },
    },
    .ambient_occlusion = Unorm(Channel(color,3U)),
    .shading_model = ShadingModelCode { Channel(material,3U) },
  };
}

auto ResolveMaterialBrdf(const StandardMaterial& material)
  -> std::expected<MaterialBrdfChannels, BrdfReferenceError>
{
  if (!Reflectance(material.base_color.red)
    || !Reflectance(material.base_color.green)
    || !Reflectance(material.base_color.blue) || !Reflectance(material.metallic)
    || !Reflectance(material.specular)
    || !Reflectance(material.roughness.get())) {
    return std::unexpected(BrdfReferenceError::kInvalidInput);
  }
  const auto resolve = [&](const double base) -> BrdfReflectance {
    return {
      .f0 = ((1.0 - material.metallic) * 0.08 * material.specular)
        + (material.metallic * base),
      .diffuse = base * (1.0 - material.metallic),
    };
  };
  return MaterialBrdfChannels {
    .red = resolve(material.base_color.red),
    .green = resolve(material.base_color.green),
    .blue = resolve(material.base_color.blue),
  };
}

} // namespace oxygen::vortex::testing::reference
