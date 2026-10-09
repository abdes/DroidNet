//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Vortex/Resources/CubeIlluminance.h>

namespace oxygen::vortex::resources {

namespace {
  constexpr std::uint32_t kCubeFaces = 6U;
  constexpr std::uint32_t kMeasuredMinimumSize = 32U;

  //! Hardware cube-space direction of a face texel; +Y is world up.
  auto FaceDirection(const std::uint32_t face, const double u, const double v)
    -> std::array<double, 3>
  {
    switch (face) {
    case 0:
      return { 1.0, -v, -u };
    case 1:
      return { -1.0, -v, u };
    case 2:
      return { u, 1.0, v };
    case 3:
      return { u, -1.0, -v };
    case 4:
      return { u, -v, 1.0 };
    default:
      return { -u, -v, -1.0 };
    }
  }

  auto TexelBytes(const Format format) noexcept -> std::size_t
  {
    switch (format) {
    case Format::kRGBA32Float:
      return 16U;
    case Format::kRGBA16Float:
      return 8U;
    case Format::kR11G11B10Float:
    case Format::kR9G9B9E5Float:
      return 4U;
    default:
      return 0U;
    }
  }

  //! Unsigned small float with a 5-bit exponent (bias 15), as in R11G11B10.
  auto SmallFloat(const std::uint32_t bits, const std::uint32_t mantissa_bits)
    -> double
  {
    const auto mantissa = bits & ((1U << mantissa_bits) - 1U);
    const auto exponent = static_cast<int>((bits >> mantissa_bits) & 0x1FU);
    const auto fraction = static_cast<double>(mantissa)
      / static_cast<double>(1U << mantissa_bits);
    if (exponent == 0) {
      return std::ldexp(fraction, -14);
    }
    if (exponent == 31) {
      // Infinite or NaN radiance carries no measurable illuminance.
      return 0.0;
    }
    return std::ldexp(1.0 + fraction, exponent - 15);
  }

  auto ReadRgb(const std::uint8_t* texel, const Format format)
    -> std::array<double, 3>
  {
    if (format == Format::kRGBA32Float) {
      std::array<float, 3> rgb {};
      std::memcpy(rgb.data(), texel, sizeof(rgb));
      return { rgb[0], rgb[1], rgb[2] };
    }
    if (format == Format::kR11G11B10Float || format == Format::kR9G9B9E5Float) {
      std::uint32_t packed {};
      std::memcpy(&packed, texel, sizeof(packed));
      if (format == Format::kR11G11B10Float) {
        return { SmallFloat(packed & 0x7FFU, 6U),
          SmallFloat((packed >> 11U) & 0x7FFU, 6U),
          SmallFloat((packed >> 22U) & 0x3FFU, 5U) };
      }
      // Three 9-bit mantissas share a 5-bit exponent (bias 15).
      const auto scale = std::ldexp(1.0, static_cast<int>(packed >> 27U) - 24);
      return { (packed & 0x1FFU) * scale, ((packed >> 9U) & 0x1FFU) * scale,
        ((packed >> 18U) & 0x1FFU) * scale };
    }
    std::array<std::uint16_t, 3> bits {};
    std::memcpy(bits.data(), texel, sizeof(bits));
    return { data::HalfFloat { bits[0] }.ToFloat(),
      data::HalfFloat { bits[1] }.ToFloat(),
      data::HalfFloat { bits[2] }.ToFloat() };
  }
} // namespace

auto MeasureCubeIlluminance(const data::TextureResource& texture)
  -> std::optional<float>
{
  const auto format = texture.GetFormat();
  const auto texel_bytes = TexelBytes(format);
  const auto mips = static_cast<std::uint32_t>(texture.GetMipCount());
  const auto layouts = texture.GetSubresourceLayouts();
  if (texel_bytes == 0U || texture.GetTextureType() != TextureType::kTextureCube
    || texture.GetArrayLayers() != kCubeFaces
    || texture.GetWidth() != texture.GetHeight() || texture.GetWidth() == 0U
    || mips == 0U || layouts.size() != std::size_t { kCubeFaces } * mips) {
    return std::nullopt;
  }

  // The smallest mip still at least kMeasuredMinimumSize wide, or mip 0.
  auto mip = 0U;
  while (mip + 1U < mips
    && (texture.GetWidth() >> (mip + 1U)) >= kMeasuredMinimumSize) {
    ++mip;
  }
  const auto size = (std::max)(texture.GetWidth() >> mip, 1U);
  const auto data = texture.GetData();
  const double texel_extent = 2.0 / size;
  double illuminance = 0.0;
  for (auto face = 0U; face < kCubeFaces; ++face) {
    const auto& layout = layouts[(face * mips) + mip];
    const std::size_t row_bytes = std::size_t { size } * texel_bytes;
    if (layout.row_pitch_bytes < row_bytes
      || std::size_t { layout.offset_bytes }
          + (std::size_t { layout.row_pitch_bytes } * (size - 1U)) + row_bytes
        > data.size()) {
      return std::nullopt;
    }
    for (auto y = 0U; y < size; ++y) {
      const auto* row = data.data() + layout.offset_bytes
        + (std::size_t { layout.row_pitch_bytes } * y);
      const double v = ((y + 0.5) * texel_extent) - 1.0;
      for (auto x = 0U; x < size; ++x) {
        const double u = ((x + 0.5) * texel_extent) - 1.0;
        const auto direction = FaceDirection(face, u, v);
        if (direction[1] <= 0.0) {
          continue;
        }
        const auto [r, g, b] = ReadRgb(row + (x * texel_bytes), format);
        const double luminance = (0.2126 * r) + (0.7152 * g) + (0.0722 * b);
        if (!std::isfinite(luminance) || luminance <= 0.0) {
          continue;
        }
        // A texel at (u, v) on the unit face spans a solid angle of
        // texel_area / length^3; its cosine to up is direction.y / length.
        const double length_squared = 1.0 + (u * u) + (v * v);
        const double cosine_solid_angle = direction[1] * texel_extent
          * texel_extent / (length_squared * length_squared);
        illuminance += luminance * cosine_solid_angle;
      }
    }
  }
  return static_cast<float>(illuminance);
}

} // namespace oxygen::vortex::resources
