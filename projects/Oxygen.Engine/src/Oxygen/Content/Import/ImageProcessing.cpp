//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/ImageProcessing.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace oxygen::content::import {

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
    return 1.055F * std::pow(linear, 1.0F / 2.4F) - 0.055F;
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
      const auto& meta = image.Meta();
      const auto format = meta.format;

      // Only support RGBA8 and RGBA32Float
      if (format != Format::kRGBA8UNorm && format != Format::kRGBA32Float) {
        return;
      }

      for (uint16_t layer = 0; layer < meta.array_layers; ++layer) {
        for (uint16_t mip = 0; mip < meta.mip_levels; ++mip) {
          auto pixels = image.GetMutablePixels(layer, mip);
          const auto view = image.GetImage(layer, mip);

          if (format == Format::kRGBA32Float) {
            auto* float_data = reinterpret_cast<float*>(pixels.data());
            const auto pixel_count
              = static_cast<size_t>(view.width) * view.height;

            for (size_t i = 0; i < pixel_count; ++i) {
              const size_t offset = i * 4;
              float_data[offset + 0] = convert_fn(float_data[offset + 0]);
              float_data[offset + 1] = convert_fn(float_data[offset + 1]);
              float_data[offset + 2] = convert_fn(float_data[offset + 2]);
              // Alpha unchanged
            }
          } else { // RGBA8
            auto* byte_data = reinterpret_cast<uint8_t*>(pixels.data());
            const auto pixel_count
              = static_cast<size_t>(view.width) * view.height;

            for (size_t i = 0; i < pixel_count; ++i) {
              const size_t offset = i * 4;
              for (size_t c = 0; c < 3; ++c) { // RGB only
                const float normalized
                  = static_cast<float>(byte_data[offset + c]) / 255.0F;
                const float converted = convert_fn(normalized);
                const float clamped = std::clamp(converted, 0.0F, 1.0F);
                byte_data[offset + c]
                  = static_cast<uint8_t>(std::round(clamped * 255.0F));
              }
            }
          }
        }
      }
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
      const float numerator = x * (kA * x + kB);
      const float denominator = x * (kC * x + kD) + kE;
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

    for (uint16_t layer = 0; layer < meta.array_layers; ++layer) {
      for (uint16_t mip = 0; mip < meta.mip_levels; ++mip) {
        const auto src_view = hdr_image.GetImage(layer, mip);
        auto dst_pixels = ldr_image.GetMutablePixels(layer, mip);

        const auto* src_data
          = reinterpret_cast<const float*>(src_view.pixels.data());
        auto* dst_data = reinterpret_cast<uint8_t*>(dst_pixels.data());

        const auto pixel_count
          = static_cast<size_t>(src_view.width) * src_view.height;

        for (size_t i = 0; i < pixel_count; ++i) {
          const size_t src_offset = i * 4;
          const size_t dst_offset = i * 4;

          // Read HDR pixel
          std::array<float, 4> hdr_pixel = {
            src_data[src_offset + 0],
            src_data[src_offset + 1],
            src_data[src_offset + 2],
            src_data[src_offset + 3],
          };

          // Apply exposure
          auto exposed = ApplyExposure(hdr_pixel, exposure);

          // Apply tonemapping
          auto tonemapped = AcesTonemap(exposed);

          // Quantize to 8-bit
          dst_data[dst_offset + 0]
            = static_cast<uint8_t>(std::round(tonemapped[0] * 255.0F));
          dst_data[dst_offset + 1]
            = static_cast<uint8_t>(std::round(tonemapped[1] * 255.0F));
          dst_data[dst_offset + 2]
            = static_cast<uint8_t>(std::round(tonemapped[2] * 255.0F));
          dst_data[dst_offset + 3]
            = static_cast<uint8_t>(std::round(tonemapped[3] * 255.0F));
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

  auto BesselI0(const float x) noexcept -> float
  {
    // Modified Bessel function I0 using polynomial approximation
    const float ax = std::abs(x);

    if (ax < 3.75F) {
      const float t = x / 3.75F;
      const float t2 = t * t;
      return 1.0F
        + t2
        * (3.5156229F
          + t2
            * (3.0899424F
              + t2
                * (1.2067492F
                  + t2 * (0.2659732F + t2 * (0.0360768F + t2 * 0.0045813F)))));
    }

    const float t = 3.75F / ax;
    return (std::exp(ax) / std::sqrt(ax))
      * (0.39894228F
        + t
          * (0.01328592F
            + t
              * (0.00225319F
                + t
                  * (-0.00157565F
                    + t
                      * (0.00916281F
                        + t
                          * (-0.02057706F
                            + t
                              * (0.02635537F
                                + t * (-0.01647633F + t * 0.00392377F))))))));
  }

  auto KaiserWindow(const float x, const float alpha) noexcept -> float
  {
    if (std::abs(x) > 1.0F) {
      return 0.0F;
    }

    const float arg = alpha * std::sqrt(1.0F - x * x);
    return BesselI0(arg) / BesselI0(alpha);
  }

  auto LanczosKernel(const float x, const int a) noexcept -> float
  {
    if (std::abs(x) < 1e-6F) {
      return 1.0F;
    }

    const float a_float = static_cast<float>(a);
    if (std::abs(x) >= a_float) {
      return 0.0F;
    }

    const float pi_x = std::numbers::pi_v<float> * x;
    const float sinc = std::sin(pi_x) / pi_x;
    const float lanczos_window = std::sin(pi_x / a_float) / (pi_x / a_float);
    return sinc * lanczos_window;
  }

  auto ComputeMipCount(const uint32_t width, const uint32_t height) noexcept
    -> uint32_t
  {
    const uint32_t max_dim = (std::max)(width, height);
    if (max_dim == 0) {
      return 0;
    }

    // floor(log2(max_dim)) + 1
    uint32_t count = 1;
    uint32_t dim = max_dim;
    while (dim > 1) {
      dim >>= 1;
      ++count;
    }
    return count;
  }

  namespace {

    //=== Box Filter Implementation
    //===------------------------------------------//

    void DownsampleBox2D(const ImageView& src, std::span<std::byte> dst,
      uint32_t dst_width, uint32_t dst_height, Format format)
    {
      if (format == Format::kRGBA32Float) {
        const auto* src_data
          = reinterpret_cast<const float*>(src.pixels.data());
        auto* dst_data = reinterpret_cast<float*>(dst.data());
        const uint32_t src_stride = src.row_pitch_bytes / sizeof(float);

        for (uint32_t y = 0; y < dst_height; ++y) {
          for (uint32_t x = 0; x < dst_width; ++x) {
            const uint32_t sx = x * 2;
            const uint32_t sy = y * 2;

            // Clamp source coordinates
            const uint32_t sx1 = (std::min)(sx + 1, src.width - 1);
            const uint32_t sy1 = (std::min)(sy + 1, src.height - 1);

            std::array<float, 4> sum = { 0, 0, 0, 0 };

            // Sample 2x2 block
            for (uint32_t py : { sy, sy1 }) {
              for (uint32_t px : { sx, sx1 }) {
                const size_t src_offset = static_cast<size_t>(py) * src_stride
                  + static_cast<size_t>(px) * 4;
                for (int c = 0; c < 4; ++c) {
                  sum[c] += src_data[src_offset + c];
                }
              }
            }

            // Average
            const size_t dst_offset
              = (static_cast<size_t>(y) * dst_width + x) * 4;
            for (int c = 0; c < 4; ++c) {
              dst_data[dst_offset + c] = sum[c] * 0.25F;
            }
          }
        }
      } else { // RGBA8
        const auto* src_data
          = reinterpret_cast<const uint8_t*>(src.pixels.data());
        auto* dst_data = reinterpret_cast<uint8_t*>(dst.data());
        const uint32_t src_stride = src.row_pitch_bytes;

        for (uint32_t y = 0; y < dst_height; ++y) {
          for (uint32_t x = 0; x < dst_width; ++x) {
            const uint32_t sx = x * 2;
            const uint32_t sy = y * 2;

            const uint32_t sx1 = (std::min)(sx + 1, src.width - 1);
            const uint32_t sy1 = (std::min)(sy + 1, src.height - 1);

            std::array<uint32_t, 4> sum = { 0, 0, 0, 0 };

            for (uint32_t py : { sy, sy1 }) {
              for (uint32_t px : { sx, sx1 }) {
                const size_t src_offset
                  = static_cast<size_t>(py) * src_stride + px * 4;
                for (int c = 0; c < 4; ++c) {
                  sum[c] += src_data[src_offset + c];
                }
              }
            }

            const size_t dst_offset
              = (static_cast<size_t>(y) * dst_width + x) * 4;
            for (int c = 0; c < 4; ++c) {
              dst_data[dst_offset + c] = static_cast<uint8_t>((sum[c] + 2) / 4);
            }
          }
        }
      }
    }

    //=== Separable Filter Implementation
    //===------------------------------------//

    using KernelFunc = float (*)(float, float);

    [[nodiscard]] auto KaiserKernel(float x, float /*param*/) noexcept -> float
    {
      // Kaiser window with alpha=4.0, width=3 (radius)
      constexpr float kAlpha = 4.0F;
      constexpr float kWidth = 3.0F;
      if (std::abs(x) >= kWidth) {
        return 0.0F;
      }
      return KaiserWindow(x / kWidth, kAlpha);
    }

    [[nodiscard]] auto LanczosKernelWrapper(float x, float /*param*/) noexcept
      -> float
    {
      return LanczosKernel(x, 3); // Lanczos-3
    }

    void DownsampleSeparable2D(const ImageView& src, std::span<std::byte> dst,
      uint32_t dst_width, uint32_t dst_height, Format format,
      KernelFunc /*kernel*/, float /*kernel_param*/)
    {
      // For simplicity, use the box filter approach for now
      // A full separable implementation would require an intermediate buffer
      // TODO: Implement proper separable filtering for higher quality
      DownsampleBox2D(src, dst, dst_width, dst_height, format);

      // Apply kernel weights (simplified - proper implementation would do
      // horizontal then vertical pass)
    }

  } // namespace

  auto GenerateChain2D(const ScratchImage& source, const MipFilter filter,
    const ColorSpace color_space) -> ScratchImage
  {
    const auto& src_meta = source.Meta();

    if (!source.IsValid() || src_meta.mip_levels != 1) {
      return {}; // Invalid input
    }

    const uint32_t mip_count = ComputeMipCount(src_meta.width, src_meta.height);

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
      std::copy(
        src_view.pixels.begin(), src_view.pixels.end(), dst_pixels.begin());
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
        auto curr_pixels = result.GetMutablePixels(layer, mip);

        const uint32_t curr_width
          = ScratchImage::ComputeMipDimension(src_meta.width, mip);
        const uint32_t curr_height
          = ScratchImage::ComputeMipDimension(src_meta.height, mip);

        switch (filter) {
        case MipFilter::kBox:
          DownsampleBox2D(
            prev_view, curr_pixels, curr_width, curr_height, dst_meta.format);
          break;

        case MipFilter::kKaiser:
          DownsampleSeparable2D(prev_view, curr_pixels, curr_width, curr_height,
            dst_meta.format, KaiserKernel, 4.0F);
          break;

        case MipFilter::kLanczos:
          DownsampleSeparable2D(prev_view, curr_pixels, curr_width, curr_height,
            dst_meta.format, LanczosKernelWrapper, 3.0F);
          break;
        }
      }
    }

    return result;
  }

  auto GenerateChain3D(const ScratchImage& source, const MipFilter /*filter*/,
    const ColorSpace /*color_space*/) -> ScratchImage
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
    const float nx = rgba[0] * 2.0F - 1.0F;
    const float ny = rgba[1] * 2.0F - 1.0F;
    const float nz = rgba[2] * 2.0F - 1.0F;

    // Normalize
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len < 1e-6F) {
      return { 0.5F, 0.5F, 1.0F, rgba[3] }; // Default up normal
    }

    const float inv_len = 1.0F / len;

    // Pack back to [0,1]
    return {
      (nx * inv_len + 1.0F) * 0.5F,
      (ny * inv_len + 1.0F) * 0.5F,
      (nz * inv_len + 1.0F) * 0.5F,
      rgba[3],
    };
  }

  auto GenerateNormalMapMips(const ScratchImage& source, const bool renormalize)
    -> ScratchImage
  {
    // Generate mips using box filter (averaging normals)
    auto result = image::mip::GenerateChain2D(
      source, MipFilter::kBox, ColorSpace::kLinear);

    if (!result.IsValid()) {
      return result;
    }

    if (!renormalize) {
      return result;
    }

    // Renormalize each pixel in each mip (skip mip 0 as it's copied)
    const auto& meta = result.Meta();
    const bool is_float = (meta.format == Format::kRGBA32Float);

    for (uint16_t layer = 0; layer < meta.array_layers; ++layer) {
      for (uint16_t mip = 1; mip < meta.mip_levels; ++mip) {
        auto pixels = result.GetMutablePixels(layer, mip);
        const auto view = result.GetImage(layer, mip);
        const auto pixel_count = static_cast<size_t>(view.width) * view.height;

        if (is_float) {
          auto* float_data = reinterpret_cast<float*>(pixels.data());
          for (size_t i = 0; i < pixel_count; ++i) {
            const size_t offset = i * 4;
            std::array<float, 4> pixel = {
              float_data[offset + 0],
              float_data[offset + 1],
              float_data[offset + 2],
              float_data[offset + 3],
            };

            auto normalized = RenormalizeNormal(pixel);

            float_data[offset + 0] = normalized[0];
            float_data[offset + 1] = normalized[1];
            float_data[offset + 2] = normalized[2];
            float_data[offset + 3] = normalized[3];
          }
        } else { // RGBA8
          auto* byte_data = reinterpret_cast<uint8_t*>(pixels.data());
          for (size_t i = 0; i < pixel_count; ++i) {
            const size_t offset = i * 4;
            std::array<float, 4> pixel = {
              static_cast<float>(byte_data[offset + 0]) / 255.0F,
              static_cast<float>(byte_data[offset + 1]) / 255.0F,
              static_cast<float>(byte_data[offset + 2]) / 255.0F,
              static_cast<float>(byte_data[offset + 3]) / 255.0F,
            };

            auto normalized = RenormalizeNormal(pixel);

            byte_data[offset + 0]
              = static_cast<uint8_t>(std::round(normalized[0] * 255.0F));
            byte_data[offset + 1]
              = static_cast<uint8_t>(std::round(normalized[1] * 255.0F));
            byte_data[offset + 2]
              = static_cast<uint8_t>(std::round(normalized[2] * 255.0F));
            byte_data[offset + 3]
              = static_cast<uint8_t>(std::round(normalized[3] * 255.0F));
          }
        }
      }
    }

    return result;
  }

  void FlipNormalGreen(ScratchImage& image)
  {
    const auto& meta = image.Meta();
    const bool is_float = (meta.format == Format::kRGBA32Float);

    for (uint16_t layer = 0; layer < meta.array_layers; ++layer) {
      for (uint16_t mip = 0; mip < meta.mip_levels; ++mip) {
        auto pixels = image.GetMutablePixels(layer, mip);
        const auto view = image.GetImage(layer, mip);
        const auto pixel_count = static_cast<size_t>(view.width) * view.height;

        if (is_float) {
          auto* float_data = reinterpret_cast<float*>(pixels.data());
          for (size_t i = 0; i < pixel_count; ++i) {
            const size_t offset = i * 4 + 1; // Green channel
            // Flip: [0,1] -> [1,0]
            float_data[offset] = 1.0F - float_data[offset];
          }
        } else { // RGBA8
          auto* byte_data = reinterpret_cast<uint8_t*>(pixels.data());
          for (size_t i = 0; i < pixel_count; ++i) {
            const size_t offset = i * 4 + 1; // Green channel
            byte_data[offset] = 255 - byte_data[offset];
          }
        }
      }
    }
  }

} // namespace image::content

} // namespace oxygen::content::import
