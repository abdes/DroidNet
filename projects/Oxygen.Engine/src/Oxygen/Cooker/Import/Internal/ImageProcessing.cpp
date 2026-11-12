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
#include <functional>
#include <numbers>
#include <numeric>
#include <span>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/ImageProcessing.h>
#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>

namespace oxygen::content::import {

//===----------------------------------------------------------------------===//
// Texels - RGBA8 and RGBA32Float texel access
//===----------------------------------------------------------------------===//

namespace {

  //! One texel's four channels in its format's own range: 0..255 for RGBA8,
  //! the stored value for RGBA32Float.
  using Texel = std::array<float, 4>;

  //! Whether `format` stores float channels; anything else is RGBA8.
  [[nodiscard]] auto IsFloat(const Format format) noexcept -> bool
  {
    return format == Format::kRGBA32Float;
  }

  //! The largest channel value of `format`: an opaque alpha.
  [[nodiscard]] auto ChannelMax(const Format format) noexcept -> float
  {
    return IsFloat(format) ? 1.0F : 255.0F;
  }

  //! The byte offset of texel (`x`, `y`) of a subresource whose rows lie
  //! `row_pitch` bytes apart.
  [[nodiscard]] auto TexelOffset(const Format format, const uint64_t row_pitch,
    const uint32_t x, const uint32_t y) -> std::size_t
  {
    return static_cast<std::size_t>(
      (uint64_t { y } * row_pitch) + graphics::detail::RowBytes(format, x));
  }

  [[nodiscard]] auto ReadTexel(const std::span<const std::byte> bytes,
    const Format format, const std::size_t offset) -> Texel
  {
    if (IsFloat(format)) {
      auto texel = Texel {};
      std::memcpy(texel.data(), bytes.subspan(offset, sizeof(texel)).data(),
        sizeof(texel));
      return texel;
    }
    auto raw = std::array<uint8_t, 4> {};
    std::memcpy(
      raw.data(), bytes.subspan(offset, sizeof(raw)).data(), sizeof(raw));
    auto texel = Texel {};
    std::ranges::transform(raw, texel.begin(),
      [](const uint8_t value) -> float { return static_cast<float>(value); });
    return texel;
  }

  //! Writes `texel`; an RGBA8 channel is rounded to the nearest value in
  //! 0..255.
  auto WriteTexel(const std::span<std::byte> bytes, const Format format,
    const std::size_t offset, const Texel& texel) -> void
  {
    if (IsFloat(format)) {
      std::memcpy(bytes.subspan(offset, sizeof(texel)).data(), texel.data(),
        sizeof(texel));
      return;
    }
    auto raw = std::array<uint8_t, 4> {};
    std::ranges::transform(
      texel, raw.begin(), [](const float value) -> uint8_t {
        return static_cast<uint8_t>(
          std::lround(std::clamp(value, 0.0F, 255.0F)));
      });
    std::memcpy(
      bytes.subspan(offset, sizeof(raw)).data(), raw.data(), sizeof(raw));
  }

  //! Applies `edit` to every texel of every layer of `image`, from mip
  //! `first_mip` on.
  template <typename Edit>
  auto ForEachTexel(
    ScratchImage& image, const uint16_t first_mip, const Edit& edit) -> void
  {
    const auto& meta = image.Meta();
    for (uint16_t layer = 0; layer < meta.array_layers; ++layer) {
      for (uint16_t mip = first_mip; mip < meta.mip_levels; ++mip) {
        const auto view = image.GetImage(layer, mip);
        const auto pixels = image.GetMutablePixels(layer, mip);
        for (uint32_t y = 0; y < view.height; ++y) {
          for (uint32_t x = 0; x < view.width; ++x) {
            const auto offset
              = TexelOffset(meta.format, view.row_pitch_bytes, x, y);
            auto texel = ReadTexel(pixels, meta.format, offset);
            edit(texel);
            WriteTexel(pixels, meta.format, offset, texel);
          }
        }
      }
    }
  }

} // namespace

//===----------------------------------------------------------------------===//
// image::color - Color Space Conversion Implementation
//===----------------------------------------------------------------------===//

namespace image::color {

  auto SrgbToLinear(const float srgb) noexcept -> float
  {
    if (srgb <= 0.04045F) {
      return srgb / 12.92F;
    }
    return std::pow((srgb + 0.055F) / 1.055F, 2.4F);
  }

  auto LinearToSrgb(const float linear) noexcept -> float
  {
    if (linear <= 0.0031308F) {
      return linear * 12.92F;
    }
    return (1.055F * std::pow(linear, 1.0F / 2.4F)) - 0.055F;
  }

  auto SrgbToLinear(const std::array<float, 4> rgba) noexcept
    -> std::array<float, 4>
  {
    return {
      SrgbToLinear(rgba[0]),
      SrgbToLinear(rgba[1]),
      SrgbToLinear(rgba[2]),
      rgba[3], // Alpha unchanged
    };
  }

  auto LinearToSrgb(const std::array<float, 4> rgba) noexcept
    -> std::array<float, 4>
  {
    return {
      LinearToSrgb(rgba[0]),
      LinearToSrgb(rgba[1]),
      LinearToSrgb(rgba[2]),
      rgba[3], // Alpha unchanged
    };
  }

  namespace {

    void ConvertImageColorSpace(
      ScratchImage& image, float (*convert_fn)(float) noexcept)
    {
      const auto format = image.Meta().format;

      // Only support RGBA8 and RGBA32Float
      if (format != Format::kRGBA8UNorm && format != Format::kRGBA32Float) {
        return;
      }

      // An RGBA8 channel converts normalized and stays in range.
      const auto max = ChannelMax(format);
      const auto convert = [&](float& channel) -> void {
        channel = IsFloat(format)
          ? convert_fn(channel)
          : std::clamp(convert_fn(channel / max), 0.0F, 1.0F) * max;
      };
      ForEachTexel(image, 0U, [&](Texel& texel) -> void {
        std::ranges::for_each(std::span(texel).first<3>(), convert);
        // Alpha unchanged
      });
    }

  } // namespace

  void ConvertSrgbToLinear(ScratchImage& image)
  {
    ConvertImageColorSpace(image, SrgbToLinear);
  }

  void ConvertLinearToSrgb(ScratchImage& image)
  {
    ConvertImageColorSpace(image, LinearToSrgb);
  }

} // namespace image::color

//===----------------------------------------------------------------------===//
// image::hdr - HDR Processing Implementation
//===----------------------------------------------------------------------===//

namespace image::hdr {

  auto ApplyExposure(const std::array<float, 4> rgba,
    const float exposure) noexcept -> std::array<float, 4>
  {
    const float multiplier = std::exp2(exposure);
    return {
      rgba[0] * multiplier,
      rgba[1] * multiplier,
      rgba[2] * multiplier,
      rgba[3], // Alpha unchanged
    };
  }

  auto AcesTonemap(const std::array<float, 4> rgba) noexcept
    -> std::array<float, 4>
  {
    // ACES fitted curve from Krzysztof Narkowicz
    // https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
    constexpr float kA = 2.51F;
    constexpr float kB = 0.03F;
    constexpr float kC = 2.43F;
    constexpr float kD = 0.59F;
    constexpr float kE = 0.14F;

    const auto tonemap = [](float x) noexcept -> float {
      // Clamp negative values
      x = (std::max)(x, 0.0F);
      const float numerator = x * ((kA * x) + kB);
      const float denominator = (x * ((kC * x) + kD)) + kE;
      return std::clamp(numerator / denominator, 0.0F, 1.0F);
    };

    return {
      tonemap(rgba[0]),
      tonemap(rgba[1]),
      tonemap(rgba[2]),
      rgba[3], // Alpha unchanged
    };
  }

  auto BakeToLdr(const ScratchImage& hdr_image, const float exposure)
    -> ScratchImage
  {
    const auto& meta = hdr_image.Meta();

    // Only process HDR images
    if (meta.format != Format::kRGBA32Float) {
      // Return a copy if not HDR (degrade gracefully)
      ScratchImageMeta ldr_meta = meta;
      ldr_meta.format = Format::kRGBA8UNorm;
      return ScratchImage::Create(ldr_meta);
    }

    // Create LDR output image
    ScratchImageMeta ldr_meta = meta;
    ldr_meta.format = Format::kRGBA8UNorm;
    ScratchImage ldr_image = ScratchImage::Create(ldr_meta);
    if (!ldr_image.IsValid()) {
      return {};
    }

    const auto ldr_max = ChannelMax(ldr_meta.format);
    for (uint16_t layer = 0; layer < meta.array_layers; ++layer) {
      for (uint16_t mip = 0; mip < meta.mip_levels; ++mip) {
        const auto src_view = hdr_image.GetImage(layer, mip);
        const auto dst_view = ldr_image.GetImage(layer, mip);
        const auto dst_pixels = ldr_image.GetMutablePixels(layer, mip);

        for (uint32_t y = 0; y < src_view.height; ++y) {
          for (uint32_t x = 0; x < src_view.width; ++x) {
            const auto hdr_pixel = ReadTexel(src_view.pixels, meta.format,
              TexelOffset(meta.format, src_view.row_pitch_bytes, x, y));

            auto ldr_pixel = AcesTonemap(ApplyExposure(hdr_pixel, exposure));

            // Quantize to 8-bit
            std::ranges::transform(ldr_pixel, ldr_pixel.begin(),
              [ldr_max](
                const float value) -> float { return value * ldr_max; });
            WriteTexel(dst_pixels, ldr_meta.format,
              TexelOffset(ldr_meta.format, dst_view.row_pitch_bytes, x, y),
              ldr_pixel);
          }
        }
      }
    }

    return ldr_image;
  }

} // namespace image::hdr

//===----------------------------------------------------------------------===//
// image::mip - Mip Filter Kernels Implementation
//===----------------------------------------------------------------------===//

namespace image::mip {

  // clang-format off
  auto BesselI0(const float x) noexcept -> float
  {
    // Modified Bessel function I0, by the polynomial approximations of
    // Abramowitz & Stegun 9.8.1 (|x| < 3.75) and 9.8.2 (|x| >= 3.75).
    // Coefficients run from the constant term up.
    constexpr auto kBelow = std::array {
      1.0F, 3.5156229F, 3.0899424F, 1.2067492F, 0.2659732F, 0.0360768F,
      0.0045813F,
    };
    constexpr auto kAbove = std::array {
      0.39894228F,  0.01328592F, 0.00225319F, -0.00157565F, 0.00916281F,
      -0.02057706F, 0.02635537F, -0.01647633F, 0.00392377F,
    };
    constexpr auto kSplit = 3.75F;

    // c0 + t * (c1 + t * (c2 + ...)), innermost term first.
    const auto polynomial
      = [](const auto& coefficients, const float t) -> float {
          return std::accumulate(coefficients.rbegin(), coefficients.rend(),
            0.0F, [t](const float sum, const float c) -> float {
              return (sum * t) + c;
            });
        };

    const float ax = std::abs(x);
    if (ax < kSplit) {
      const float t = x / kSplit;
      return polynomial(kBelow, t * t);
    }
    return (std::exp(ax) / std::sqrt(ax)) * polynomial(kAbove, kSplit / ax);
  }
  // clang-format on

  auto KaiserWindow(const float x, const float alpha) noexcept -> float
  {
    if (std::abs(x) > 1.0F) {
      return 0.0F;
    }

    const float arg = alpha * std::sqrt(1.0F - (x * x));
    return BesselI0(arg) / BesselI0(alpha);
  }

  auto LanczosKernel(const float x) noexcept -> float
  {
    constexpr auto kLobes = 3.0F;
    if (std::abs(x) < 1e-6F) {
      return 1.0F;
    }

    if (std::abs(x) >= kLobes) {
      return 0.0F;
    }

    const float pi_x = std::numbers::pi_v<float> * x;
    const float sinc = std::sin(pi_x) / pi_x;
    const float lanczos_window = std::sin(pi_x / kLobes) / (pi_x / kLobes);
    return sinc * lanczos_window;
  }

  namespace {

    //! The mip a downsample writes: its texels and where they lie.
    struct MipTarget final {
      std::span<std::byte> bytes;
      ImageView view;
    };

    //=== Box Filter Implementation
    //===------------------------------------------//

    void DownsampleBox2D(const ImageView& src, const MipTarget& dst)
    {
      const auto format = src.format;
      for (uint32_t y = 0; y < dst.view.height; ++y) {
        for (uint32_t x = 0; x < dst.view.width; ++x) {
          const uint32_t sx = x * 2;
          const uint32_t sy = y * 2;

          // Clamp source coordinates
          const uint32_t sx1 = (std::min)(sx + 1, src.width - 1);
          const uint32_t sy1 = (std::min)(sy + 1, src.height - 1);

          // Sample 2x2 block
          auto sum = Texel {};
          for (uint32_t py : { sy, sy1 }) {
            for (uint32_t px : { sx, sx1 }) {
              const auto texel = ReadTexel(src.pixels, format,
                TexelOffset(format, src.row_pitch_bytes, px, py));
              std::ranges::transform(sum, texel, sum.begin(), std::plus {});
            }
          }

          // Average
          std::ranges::transform(sum, sum.begin(),
            [](const float value) -> float { return value * 0.25F; });
          WriteTexel(dst.bytes, format,
            TexelOffset(format, dst.view.row_pitch_bytes, x, y), sum);
        }
      }
    }

    //=== Separable Filter Implementation
    //===------------------------------------//

    using KernelFunc = float (*)(float);

    [[nodiscard]] auto KaiserKernel(const float x) noexcept -> float
    {
      // Kaiser window with alpha=4.0, width=3 (radius)
      constexpr float kAlpha = 4.0F;
      constexpr float kWidth = 3.0F;
      if (std::abs(x) >= kWidth) {
        return 0.0F;
      }
      return KaiserWindow(x / kWidth, kAlpha);
    }

    void DownsampleSeparable2D(
      const ImageView& src, const MipTarget& dst, const KernelFunc kernel)
    {
      const auto format = src.format;
      const auto dst_width = dst.view.width;
      const auto dst_height = dst.view.height;
      if (dst_width == 0 || dst_height == 0) {
        return;
      }

      // 1. Allocate intermediate buffer for separable (two-pass) filtering.
      // We use float precision for intermediate results to minimize error
      // accumulation. Buffer size is DstWidth * SrcHeight (after X pass, before
      // Y pass).
      const uint32_t temp_width = dst_width;
      const uint32_t temp_height = src.height;
      std::vector<Texel> temp_buffer(
        static_cast<size_t>(temp_width) * temp_height);
      const auto temp_at = [&](const uint32_t x, const uint32_t y) -> Texel& {
        return temp_buffer.at((static_cast<size_t>(y) * temp_width) + x);
      };

      // Both Kaiser and Lanczos-3 implementations in this file utilize a radius
      // approx 3.0 pixels.
      // - Kaiser: kWidth = 3.0 (hardcoded)
      // - Lanczos: kLobes = 3
      constexpr float kernel_width = 3.0F;

      // Helper for 1D resampling
      auto resample_1d = [&](uint32_t src_size, uint32_t dst_size,
                           auto sample_src, auto write_dst) -> auto {
        const float scale
          = static_cast<float>(dst_size) / static_cast<float>(src_size);
        const float support_scale = (scale < 1.0F) ? (1.0F / scale) : 1.0F;
        const float radius = kernel_width * support_scale;

        for (uint32_t i = 0; i < dst_size; ++i) {
          const float center = (static_cast<float>(i) + 0.5F) / scale;
          const int start = static_cast<int>(std::floor(center - radius));
          const int end = static_cast<int>(std::ceil(center + radius));

          auto val = Texel {};
          float weight_sum = 0.0F;

          for (int j = start; j < end; ++j) {
            const float dist = (static_cast<float>(j) + 0.5F - center);
            const float weight = kernel(dist / support_scale);

            const int src_idx
              = std::clamp(j, 0, static_cast<int>(src_size) - 1);

            const Texel pixel = sample_src(static_cast<uint32_t>(src_idx));
            std::ranges::transform(pixel, val, val.begin(),
              [weight](const float sample, const float accumulated) -> float {
                return accumulated + (sample * weight);
              });
            weight_sum += weight;
          }

          if (std::abs(weight_sum) > 1e-6F) {
            const float inv_w = 1.0F / weight_sum;
            std::ranges::transform(val, val.begin(),
              [inv_w](const float value) -> float { return value * inv_w; });
          }

          write_dst(i, val);
        }
      };

      // 2. Pass 1: Horizontal Resampling (Rows)
      //    Src (W x H) -> Temp (DstW x H). RGBA8 keeps its 0..255 range for
      //    precision.
      for (uint32_t y = 0; y < src.height; ++y) {
        resample_1d(
          src.width, temp_width,
          [&](uint32_t src_x) -> Texel {
            return ReadTexel(src.pixels, format,
              TexelOffset(format, src.row_pitch_bytes, src_x, y));
          },
          [&](uint32_t dst_x, const Texel& in) -> void {
            temp_at(dst_x, y) = in;
          });
      }

      // 3. Pass 2: Vertical Resampling (Columns)
      //    Temp (DstW x H) -> Dst (DstW x DstH)
      for (uint32_t x = 0; x < dst_width; ++x) {
        resample_1d(
          temp_height, dst_height,
          [&](uint32_t src_y) -> Texel { return temp_at(x, src_y); },
          [&](uint32_t dst_y, const Texel& in) -> void {
            WriteTexel(dst.bytes, format,
              TexelOffset(format, dst.view.row_pitch_bytes, x, dst_y), in);
          });
      }
    }

  } // namespace

  auto GenerateChain2D(const ScratchImage& source, const MipFilter filter,
    const ColorSpace color_space, const uint32_t target_mip_levels)
    -> ScratchImage
  {
    const auto& src_meta = source.Meta();

    if (!source.IsValid() || src_meta.mip_levels != 1) {
      return {}; // Invalid input
    }

    const uint32_t full_mip_count
      = MipChainLength(src_meta.width, src_meta.height);
    const uint32_t mip_count = (target_mip_levels == 0)
      ? full_mip_count
      : (std::min)(target_mip_levels, full_mip_count);

    // Create output with full mip chain
    ScratchImageMeta dst_meta = src_meta;
    dst_meta.mip_levels = static_cast<uint16_t>(mip_count);

    ScratchImage result = ScratchImage::Create(dst_meta);
    if (!result.IsValid()) {
      return {};
    }

    // Copy the base mip (mip 0)
    for (uint16_t layer = 0; layer < src_meta.array_layers; ++layer) {
      const auto src_view = source.GetImage(layer, 0);
      auto dst_pixels = result.GetMutablePixels(layer, 0);
      std::ranges::copy(src_view.pixels, dst_pixels.begin());
    }

    // Convert to linear if sRGB
    const bool is_srgb = (color_space == ColorSpace::kSRGB);
    if (is_srgb) {
      // Convert base mip to linear for filtering
      // Note: We work on result directly
    }

    // Generate each subsequent mip level
    for (uint16_t layer = 0; layer < dst_meta.array_layers; ++layer) {
      for (uint16_t mip = 1; mip < dst_meta.mip_levels; ++mip) {
        const auto prev_view = result.GetImage(layer, mip - 1);
        const auto target = MipTarget {
          .bytes = result.GetMutablePixels(layer, mip),
          .view = result.GetImage(layer, mip),
        };

        switch (filter) {
        case MipFilter::kBox:
          DownsampleBox2D(prev_view, target);
          break;

        case MipFilter::kKaiser:
          DownsampleSeparable2D(prev_view, target, KaiserKernel);
          break;

        case MipFilter::kLanczos:
          DownsampleSeparable2D(prev_view, target, LanczosKernel);
          break;
        }
      }
    }

    return result;
  }

  auto GenerateChain3D(const ScratchImage& source, const MipFilter /*filter*/,
    const ColorSpace /*color_space*/, const uint32_t /*target_mip_levels*/)
    -> ScratchImage
  {
    // 3D texture mip generation - simplified implementation
    // TODO: Implement proper 3D mip chain generation with depth downsampling
    const auto& src_meta = source.Meta();

    if (!source.IsValid() || src_meta.texture_type != TextureType::kTexture3D) {
      return {}; // Invalid input
    }

    // For now, return a copy (3D mip generation is complex)
    return ScratchImage::Create(src_meta);
  }

} // namespace image::mip

//===----------------------------------------------------------------------===//
// image::content - Content-Specific Processing Implementation
//===----------------------------------------------------------------------===//

namespace image::content {

  auto RenormalizeNormal(const std::array<float, 4> rgba) noexcept
    -> std::array<float, 4>
  {
    // Unpack from [0,1] to [-1,1]
    const float nx = (rgba[0] * 2.0F) - 1.0F;
    const float ny = (rgba[1] * 2.0F) - 1.0F;
    const float nz = (rgba[2] * 2.0F) - 1.0F;

    // Normalize
    const float len = std::sqrt((nx * nx) + (ny * ny) + (nz * nz));
    if (len < 1e-6F) {
      return { 0.5F, 0.5F, 1.0F, rgba[3] }; // Default up normal
    }

    const float inv_len = 1.0F / len;

    // Pack back to [0,1]
    return {
      ((nx * inv_len) + 1.0F) * 0.5F,
      ((ny * inv_len) + 1.0F) * 0.5F,
      ((nz * inv_len) + 1.0F) * 0.5F,
      rgba[3],
    };
  }

  auto GenerateNormalMapMips(const ScratchImage& source, const bool renormalize,
    const uint32_t target_mip_levels) -> ScratchImage
  {
    // Generate mips using box filter (averaging normals)
    auto result = mip::GenerateChain2D(
      source, MipFilter::kBox, ColorSpace::kLinear, target_mip_levels);

    if (!result.IsValid()) {
      return result;
    }

    if (!renormalize) {
      return result;
    }

    // Renormalize each pixel in each mip (skip mip 0 as it's copied), in
    // [0,1]
    const auto max = ChannelMax(result.Meta().format);
    ForEachTexel(result, 1U, [max](Texel& texel) -> void {
      std::ranges::transform(texel, texel.begin(),
        [max](const float value) -> float { return value / max; });
      texel = RenormalizeNormal(texel);
      std::ranges::transform(texel, texel.begin(),
        [max](const float value) -> float { return value * max; });
    });

    return result;
  }

  void FlipNormalGreen(ScratchImage& image)
  {
    // Flip: [0,1] -> [1,0]
    const auto max = ChannelMax(image.Meta().format);
    ForEachTexel(image, 0U, [max](Texel& texel) -> void {
      auto& green = std::get<1>(texel);
      green = max - green;
    });
  }

  void MoveValueToRed(ScratchImage& image, const TextureChannel channel)
  {
    const auto max = ChannelMax(image.Meta().format);
    const auto source = static_cast<size_t>(channel);
    ForEachTexel(image, 0U, [max, source](Texel& texel) -> void {
      const auto value = texel.at(source);
      texel = { value, value, value, max };
    });
  }

  // A model never says which channel of its opacity map holds the value, so
  // the image decides: any alpha below opaque means the alpha holds it (a
  // cut-out PNG, or a base colour map that is also its own opacity map),
  // else the red holds a grey mask. A mask whose alpha holds something else,
  // even one stray translucent texel, is read from that alpha instead. The
  // check scans the whole map once per cook, stopping at the first
  // translucent texel.
  auto CoverageChannel(const ScratchImage& coverage) -> TextureChannel
  {
    const auto& meta = coverage.Meta();
    const auto opaque = ChannelMax(meta.format);
    const auto view = coverage.GetImage(0, 0);
    for (uint32_t y = 0; y < view.height; ++y) {
      for (uint32_t x = 0; x < view.width; ++x) {
        const auto texel = ReadTexel(view.pixels, meta.format,
          TexelOffset(meta.format, view.row_pitch_bytes, x, y));
        if (texel.at(3) < opaque) {
          return TextureChannel::kAlpha;
        }
      }
    }
    return TextureChannel::kRed;
  }

  auto ReplaceAlpha(ScratchImage& image, const ScratchImage& coverage,
    const CoverageMapping& mapping) -> bool
  {
    const auto& meta = image.Meta();
    const auto& coverage_meta = coverage.Meta();
    const auto supported = [](const Format format) -> bool {
      return format == Format::kRGBA8UNorm || format == Format::kRGBA32Float;
    };
    const auto same_extent = meta.width == coverage_meta.width
      && meta.height == coverage_meta.height;
    const auto single_image = meta.depth == 1U && meta.array_layers == 1U
      && meta.mip_levels == 1U && coverage_meta.depth == 1U
      && coverage_meta.array_layers == 1U && coverage_meta.mip_levels == 1U;
    if (!image.IsValid() || !coverage.IsValid() || !supported(meta.format)
      || !supported(coverage_meta.format) || !same_extent || !single_image) {
      return false;
    }

    const auto image_max = ChannelMax(meta.format);
    const auto coverage_max = ChannelMax(coverage_meta.format);
    const auto source = static_cast<size_t>(CoverageChannel(coverage));
    const auto view = image.GetImage(0, 0);
    const auto pixels = image.GetMutablePixels(0, 0);
    const auto coverage_view = coverage.GetImage(0, 0);
    for (uint32_t y = 0; y < view.height; ++y) {
      for (uint32_t x = 0; x < view.width; ++x) {
        const auto offset
          = TexelOffset(meta.format, view.row_pitch_bytes, x, y);
        auto texel = ReadTexel(pixels, meta.format, offset);
        const auto covered
          = ReadTexel(coverage_view.pixels, coverage_meta.format,
            TexelOffset(
              coverage_meta.format, coverage_view.row_pitch_bytes, x, y));
        texel.at(3)
          = mapping.Alpha(covered.at(source) / coverage_max) * image_max;
        WriteTexel(pixels, meta.format, offset, texel);
      }
    }
    return true;
  }

} // namespace image::content

} // namespace oxygen::content::import
