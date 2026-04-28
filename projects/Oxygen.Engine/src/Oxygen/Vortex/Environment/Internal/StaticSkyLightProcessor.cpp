//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/Internal/StaticSkyLightProcessor.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Data/TextureResource.h>

namespace oxygen::vortex::environment::internal {

namespace {

  inline constexpr std::uint32_t kCubeFaceCount = 6U;
  inline constexpr float kFp16MaxFinite = 65504.0F;

  struct CubeFaceBasis {
    glm::vec3 center;
    glm::vec3 right;
    glm::vec3 up;
  };

  // Oxygen cube face order is +X, -X, +Y, -Y, +Z, -Z. The basis is duplicated
  // here to keep runtime Vortex independent from Cooker while preserving the
  // same +Z-up face convention recorded by the import pipeline.
  // clang-format off
  inline constexpr std::array<CubeFaceBasis, kCubeFaceCount> kCubeFaceBases {{
    { { +1.0F,  0.0F,  0.0F }, {  0.0F, +1.0F,  0.0F }, { 0.0F,  0.0F, +1.0F } },
    { { -1.0F,  0.0F,  0.0F }, {  0.0F, -1.0F,  0.0F }, { 0.0F,  0.0F, +1.0F } },
    { {  0.0F, +1.0F,  0.0F }, { -1.0F,  0.0F,  0.0F }, { 0.0F,  0.0F, +1.0F } },
    { {  0.0F, -1.0F,  0.0F }, { +1.0F,  0.0F,  0.0F }, { 0.0F,  0.0F, +1.0F } },
    { {  0.0F,  0.0F, +1.0F }, { +1.0F,  0.0F,  0.0F }, { 0.0F, -1.0F,  0.0F } },
    { {  0.0F,  0.0F, -1.0F }, { +1.0F,  0.0F,  0.0F }, { 0.0F, +1.0F,  0.0F } },
  }};
  // clang-format on

  struct ShVector3 {
    std::array<float, 9> r {};
    std::array<float, 9> g {};
    std::array<float, 9> b {};
  };

  struct SourceTexel {
    std::uint32_t face { 0U };
    std::uint32_t x { 0U };
    std::uint32_t y { 0U };
  };

  [[nodiscard]] auto TexelDirection(const std::uint32_t face,
    const std::uint32_t x, const std::uint32_t y, const std::uint32_t face_size)
    -> glm::vec3
  {
    const auto inv_size = 1.0F / static_cast<float>(face_size);
    const auto s = ((static_cast<float>(x) + 0.5F) * inv_size * 2.0F) - 1.0F;
    const auto t = ((static_cast<float>(y) + 0.5F) * inv_size * 2.0F) - 1.0F;
    const auto& basis = kCubeFaceBases.at(face);
    return glm::normalize(basis.center + s * basis.right + t * basis.up);
  }

  [[nodiscard]] auto RotateYaw(
    const glm::vec3 direction, const float radians) noexcept -> glm::vec3
  {
    const auto c = std::cos(radians);
    const auto s = std::sin(radians);
    return {
      (c * direction.x) - (s * direction.y),
      (s * direction.x) + (c * direction.y),
      direction.z,
    };
  }

  [[nodiscard]] auto SelectSourceTexel(const glm::vec3 direction_ws,
    const std::uint32_t source_size) noexcept -> SourceTexel
  {
    auto best_face = std::uint32_t { 0U };
    auto best_axis = -1.0F;
    for (std::uint32_t face = 0U; face < kCubeFaceCount; ++face) {
      const auto axis = glm::dot(direction_ws, kCubeFaceBases[face].center);
      if (axis > best_axis) {
        best_axis = axis;
        best_face = face;
      }
    }

    const auto& basis = kCubeFaceBases[best_face];
    const auto denominator = (std::max)(best_axis, 1.0e-6F);
    const auto s = glm::dot(direction_ws, basis.right) / denominator;
    const auto t = glm::dot(direction_ws, basis.up) / denominator;
    const auto u = glm::clamp((s + 1.0F) * 0.5F, 0.0F, 0.999999F);
    const auto v = glm::clamp((t + 1.0F) * 0.5F, 0.0F, 0.999999F);
    return {
      .face = best_face,
      .x = static_cast<std::uint32_t>(u * static_cast<float>(source_size)),
      .y = static_cast<std::uint32_t>(v * static_cast<float>(source_size)),
    };
  }

  [[nodiscard]] auto SolidAngleElement(const float x, const float y) noexcept
    -> float
  {
    return std::atan2(x * y, std::sqrt((x * x) + (y * y) + 1.0F));
  }

  [[nodiscard]] auto TexelSolidAngle(const std::uint32_t x,
    const std::uint32_t y, const std::uint32_t face_size) noexcept -> float
  {
    const auto inv_size = 1.0F / static_cast<float>(face_size);
    const auto x0 = (2.0F * static_cast<float>(x) * inv_size) - 1.0F;
    const auto y0 = (2.0F * static_cast<float>(y) * inv_size) - 1.0F;
    const auto x1 = (2.0F * static_cast<float>(x + 1U) * inv_size) - 1.0F;
    const auto y1 = (2.0F * static_cast<float>(y + 1U) * inv_size) - 1.0F;
    return SolidAngleElement(x1, y1) - SolidAngleElement(x0, y1)
      - SolidAngleElement(x1, y0) + SolidAngleElement(x0, y0);
  }

  [[nodiscard]] auto ShBasis3(const glm::vec3 direction) noexcept
    -> std::array<float, 9>
  {
    const auto squared = direction * direction;
    return {
      0.282095F,
      -0.488603F * direction.y,
      0.488603F * direction.z,
      -0.488603F * direction.x,
      1.092548F * direction.x * direction.y,
      -1.092548F * direction.y * direction.z,
      0.315392F * ((3.0F * squared.z) - 1.0F),
      -1.092548F * direction.x * direction.z,
      0.546274F * (squared.x - squared.y),
    };
  }

  auto AccumulateSh(ShVector3& sh, const glm::vec3 radiance,
    const glm::vec3 direction, const float weight) noexcept -> void
  {
    const auto basis = ShBasis3(direction);
    for (std::size_t index = 0U; index < basis.size(); ++index) {
      const auto weighted_basis = basis[index] * weight;
      sh.r[index] += radiance.r * weighted_basis;
      sh.g[index] += radiance.g * weighted_basis;
      sh.b[index] += radiance.b * weighted_basis;
    }
  }

  [[nodiscard]] auto PackDiffuseSh(
    const ShVector3& sh, const float average_brightness)
    -> std::array<glm::vec4, kStaticSkyLightDiffuseShElementCount>
  {
    const auto sqrt_pi = std::sqrt(std::numbers::pi_v<float>);
    const auto coefficient0 = 1.0F / (2.0F * sqrt_pi);
    const auto coefficient1 = std::sqrt(3.0F) / (3.0F * sqrt_pi);
    const auto coefficient2 = std::sqrt(15.0F) / (8.0F * sqrt_pi);
    const auto coefficient3 = std::sqrt(5.0F) / (16.0F * sqrt_pi);
    const auto coefficient4 = 0.5F * coefficient2;

    std::array<glm::vec4, kStaticSkyLightDiffuseShElementCount> packed {};
    packed[0] = { -coefficient1 * sh.r[3], -coefficient1 * sh.r[1],
      coefficient1 * sh.r[2], coefficient0 * sh.r[0] - coefficient3 * sh.r[6] };
    packed[1] = { -coefficient1 * sh.g[3], -coefficient1 * sh.g[1],
      coefficient1 * sh.g[2], coefficient0 * sh.g[0] - coefficient3 * sh.g[6] };
    packed[2] = { -coefficient1 * sh.b[3], -coefficient1 * sh.b[1],
      coefficient1 * sh.b[2], coefficient0 * sh.b[0] - coefficient3 * sh.b[6] };
    packed[3] = { coefficient2 * sh.r[4], -coefficient2 * sh.r[5],
      3.0F * coefficient3 * sh.r[6], -coefficient2 * sh.r[7] };
    packed[4] = { coefficient2 * sh.g[4], -coefficient2 * sh.g[5],
      3.0F * coefficient3 * sh.g[6], -coefficient2 * sh.g[7] };
    packed[5] = { coefficient2 * sh.b[4], -coefficient2 * sh.b[5],
      3.0F * coefficient3 * sh.b[6], -coefficient2 * sh.b[7] };
    packed[6] = { coefficient4 * sh.r[8], coefficient4 * sh.g[8],
      coefficient4 * sh.b[8], 1.0F };
    packed[7] = glm::vec4 { average_brightness };
    return packed;
  }

  [[nodiscard]] auto ReadRgba32Float(const std::span<const std::uint8_t> data,
    const data::pak::render::SubresourceLayout& layout, const std::uint32_t x,
    const std::uint32_t y) noexcept -> glm::vec4
  {
    glm::vec4 value {};
    const auto offset = static_cast<std::size_t>(layout.offset_bytes)
      + (static_cast<std::size_t>(y) * layout.row_pitch_bytes)
      + (static_cast<std::size_t>(x) * sizeof(float) * 4U);
    std::memcpy(&value, data.data() + offset, sizeof(value));
    return value;
  }

  [[nodiscard]] auto ReadRgba16Float(const std::span<const std::uint8_t> data,
    const data::pak::render::SubresourceLayout& layout, const std::uint32_t x,
    const std::uint32_t y) noexcept -> glm::vec4
  {
    const auto offset = static_cast<std::size_t>(layout.offset_bytes)
      + (static_cast<std::size_t>(y) * layout.row_pitch_bytes)
      + (static_cast<std::size_t>(x) * sizeof(std::uint16_t) * 4U);
    std::array<std::uint16_t, 4> encoded {};
    std::memcpy(encoded.data(), data.data() + offset,
      encoded.size() * sizeof(std::uint16_t));
    return {
      data::HalfFloat { encoded[0] }.ToFloat(),
      data::HalfFloat { encoded[1] }.ToFloat(),
      data::HalfFloat { encoded[2] }.ToFloat(),
      data::HalfFloat { encoded[3] }.ToFloat(),
    };
  }

  [[nodiscard]] auto ReadSourcePixel(const data::TextureResource& texture,
    const std::uint32_t face, const std::uint32_t x, const std::uint32_t y)
    -> std::expected<glm::vec4, StaticSkyLightProcessFailure>
  {
    const auto mip_count = static_cast<std::uint32_t>(texture.GetMipCount());
    const auto layout_index = (face * mip_count);
    const auto layouts = texture.GetSubresourceLayouts();
    if (layout_index >= layouts.size()) {
      return std::unexpected(
        StaticSkyLightProcessFailure::kInvalidPayloadLayout);
    }

    const auto data = texture.GetData();
    switch (texture.GetFormat()) {
    case Format::kRGBA32Float:
      return ReadRgba32Float(data, layouts[layout_index], x, y);
    case Format::kRGBA16Float:
      return ReadRgba16Float(data, layouts[layout_index], x, y);
    default:
      return std::unexpected(StaticSkyLightProcessFailure::kUnsupportedFormat);
    }
  }

  [[nodiscard]] auto ProcessedColor(const glm::vec4 source,
    const glm::vec3 sample_direction_ws,
    const SkyLightEnvironmentModel& sky_light,
    const float source_radiance_scale) noexcept -> glm::vec3
  {
    auto color = glm::vec3(source) / source_radiance_scale;
    if (sky_light.lower_hemisphere_is_solid_color
      && sample_direction_ws.z < 0.0F) {
      const auto alpha
        = glm::clamp(sky_light.lower_hemisphere_blend_alpha, 0.0F, 1.0F);
      color = glm::mix(color, sky_light.lower_hemisphere_color, alpha);
    }
    return color;
  }

} // namespace

auto ProcessStaticSkyLightCubemapCpu(
  const data::TextureResource& source_cubemap,
  const SkyLightEnvironmentModel& sky_light,
  const std::uint32_t output_face_size)
  -> std::expected<StaticSkyLightCpuProducts, StaticSkyLightProcessFailure>
{
  if (output_face_size == 0U || source_cubemap.GetWidth() == 0U
    || source_cubemap.GetHeight() == 0U
    || source_cubemap.GetWidth() != source_cubemap.GetHeight()
    || source_cubemap.GetArrayLayers() != kCubeFaceCount
    || source_cubemap.GetMipCount() == 0U) {
    return std::unexpected(StaticSkyLightProcessFailure::kInvalidDimensions);
  }

  if (source_cubemap.GetFormat() != Format::kRGBA32Float
    && source_cubemap.GetFormat() != Format::kRGBA16Float) {
    return std::unexpected(StaticSkyLightProcessFailure::kUnsupportedFormat);
  }

  const auto source_size = source_cubemap.GetWidth();
  auto max_luminance = 0.0F;
  for (std::uint32_t face = 0U; face < kCubeFaceCount; ++face) {
    for (std::uint32_t y = 0U; y < source_size; ++y) {
      for (std::uint32_t x = 0U; x < source_size; ++x) {
        const auto source = ReadSourcePixel(source_cubemap, face, x, y);
        if (!source.has_value()) {
          return std::unexpected(source.error());
        }
        max_luminance = (std::max)(max_luminance, source->r);
        max_luminance = (std::max)(max_luminance, source->g);
        max_luminance = (std::max)(max_luminance, source->b);
      }
    }
  }

  const auto source_radiance_scale
    = (std::max)(1.0F, max_luminance / kFp16MaxFinite);

  StaticSkyLightCpuProducts products {};
  products.output_face_size = output_face_size;
  products.source_radiance_scale = source_radiance_scale;
  products.processed_rgba.resize(static_cast<std::size_t>(kCubeFaceCount)
    * output_face_size * output_face_size);

  ShVector3 sh {};
  auto accumulated_solid_angle = 0.0F;
  auto accumulated_brightness = 0.0F;

  for (std::uint32_t face = 0U; face < kCubeFaceCount; ++face) {
    for (std::uint32_t y = 0U; y < output_face_size; ++y) {
      for (std::uint32_t x = 0U; x < output_face_size; ++x) {
        const auto direction_ws = TexelDirection(face, x, y, output_face_size);
        const auto source_direction_ws
          = RotateYaw(direction_ws, sky_light.source_cubemap_angle_radians);

        const auto source_texel
          = SelectSourceTexel(source_direction_ws, source_size);
        const auto source = ReadSourcePixel(
          source_cubemap, source_texel.face, source_texel.x, source_texel.y);
        if (!source.has_value()) {
          return std::unexpected(source.error());
        }

        const auto color = ProcessedColor(
          *source, source_direction_ws, sky_light, source_radiance_scale);
        const auto index = (static_cast<std::size_t>(face) * output_face_size
                             * output_face_size)
          + (static_cast<std::size_t>(y) * output_face_size) + x;
        products.processed_rgba[index] = glm::vec4(color, source->a);

        const auto weight = TexelSolidAngle(x, y, output_face_size);
        AccumulateSh(sh, color, direction_ws, weight);
        accumulated_solid_angle += weight;
        accumulated_brightness
          += ((color.r + color.g + color.b) / 3.0F) * weight;
      }
    }
  }

  const auto solid_angle_scale = (accumulated_solid_angle > 0.0F)
    ? ((4.0F * std::numbers::pi_v<float>) / accumulated_solid_angle)
    : 1.0F;
  for (auto* channel : { &sh.r, &sh.g, &sh.b }) {
    for (auto& coefficient : *channel) {
      coefficient *= solid_angle_scale;
    }
  }

  products.average_brightness = accumulated_solid_angle > 0.0F
    ? (accumulated_brightness / accumulated_solid_angle)
    : 0.0F;
  products.diffuse_irradiance_sh
    = PackDiffuseSh(sh, products.average_brightness);
  return products;
}

auto EvaluatePackedStaticSkyLightDiffuseSh(
  const std::span<const glm::vec4, kStaticSkyLightDiffuseShElementCount> sh,
  const glm::vec3 normal_ws) -> glm::vec3
{
  const auto normal = glm::vec4(glm::normalize(normal_ws), 1.0F);
  const auto intermediate0 = glm::vec3 {
    glm::dot(sh[0], normal),
    glm::dot(sh[1], normal),
    glm::dot(sh[2], normal),
  };

  const auto v_b = glm::vec4(normal.x, normal.y, normal.z, normal.z)
    * glm::vec4(normal.y, normal.z, normal.z, normal.x);
  const auto intermediate1 = glm::vec3 {
    glm::dot(sh[3], v_b),
    glm::dot(sh[4], v_b),
    glm::dot(sh[5], v_b),
  };

  const auto v_c = (normal.x * normal.x) - (normal.y * normal.y);
  const auto intermediate2 = glm::vec3(sh[6]) * v_c;
  return glm::max(
    glm::vec3(0.0F), intermediate0 + intermediate1 + intermediate2);
}

} // namespace oxygen::vortex::environment::internal
