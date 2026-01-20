//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/TextureCooker.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <optional>

#include <glm/gtc/packing.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/ImageDecode.h>
#include <Oxygen/Content/Import/ImageProcessing.h>
#include <Oxygen/Content/Import/TextureSourceAssembly.h>
#include <Oxygen/Content/Import/bc7/Bc7Encoder.h>
#include <Oxygen/Content/Import/util/Signature.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::import {

namespace {

  //! Convert RGBA32Float pixels to RGBA16Float in-place.
  inline void ConvertRgba32FloatToRgba16Float(
    std::span<const float> src, std::span<uint16_t> dst, size_t pixel_count)
  {
    for (size_t i = 0; i < pixel_count * 4; ++i) {
      dst[i] = glm::packHalf1x16(src[i]);
    }
  }

  //=== Pre-Decode Validation ===---------------------------------------------//

  [[nodiscard]] auto IsFloatFormat(Format format) noexcept -> bool;

  [[nodiscard]] constexpr auto IsHdrIntent(TextureIntent intent) noexcept
    -> bool
  {
    return intent == TextureIntent::kHdrEnvironment
      || intent == TextureIntent::kHdrLightProbe;
  }

  [[nodiscard]] constexpr auto IsBc7Format(const Format format) noexcept -> bool
  {
    return format == Format::kBC7UNorm || format == Format::kBC7UNormSRGB;
  }

  //! Validate settings that apply before decoding.
  /*!
   Validates the user-provided descriptor for correctness before we spend any
   CPU time decoding or processing image data.
  */
  [[nodiscard]] auto ValidatePreDecode(const TextureImportDesc& desc) noexcept
    -> std::optional<TextureImportError>
  {
    // Dimensions may be inferred from the decoded image. However, if the user
    // provides one dimension, they must provide both.
    if ((desc.width == 0) != (desc.height == 0)) {
      LOG_F(WARNING,
        "TextureCooker: invalid dimensions - width and height must both be "
        "specified or both be zero (got {}x{}) for '{}'",
        desc.width, desc.height, desc.source_id);
      return TextureImportError::kInvalidDimensions;
    }

    // Depth is only meaningful for 3D textures.
    if (desc.texture_type != TextureType::kTexture3D && desc.depth != 1) {
      LOG_F(WARNING,
        "TextureCooker: depth {} specified for non-3D texture type for '{}'",
        desc.depth, desc.source_id);
      return TextureImportError::kDepthInvalidFor2D;
    }

    // Mip policy configuration.
    if (desc.mip_policy == MipPolicy::kMaxCount && desc.max_mip_levels == 0) {
      LOG_F(WARNING,
        "TextureCooker: mip_policy is kMaxCount but max_mip_levels is 0 for "
        "'{}'",
        desc.source_id);
      return TextureImportError::kInvalidMipPolicy;
    }

    // HDR content vs output format. If the intent implies HDR and the user
    // did not request baking, the output must be float.
    if (IsHdrIntent(desc.intent) && !desc.bake_hdr_to_ldr
      && !IsFloatFormat(desc.output_format)
      && desc.hdr_handling != HdrHandling::kKeepFloat) {
      LOG_F(WARNING,
        "TextureCooker: HDR intent {} requires float output format, "
        "but got {} (set bake_hdr_to_ldr=true or use float format) for '{}'",
        to_string(desc.intent), to_string(desc.output_format), desc.source_id);
      return TextureImportError::kHdrRequiresFloatFormat;
    }

    // BC7 quality vs output format consistency.
    if (desc.bc7_quality != Bc7Quality::kNone
      && !IsBc7Format(desc.output_format)) {
      LOG_F(WARNING,
        "TextureCooker: bc7_quality is {} but output_format is {} "
        "(not BC7) for '{}'",
        to_string(desc.bc7_quality), to_string(desc.output_format),
        desc.source_id);
      return TextureImportError::kIntentFormatMismatch;
    }
    if (IsBc7Format(desc.output_format)
      && desc.bc7_quality == Bc7Quality::kNone) {
      LOG_F(WARNING,
        "TextureCooker: output_format is {} but bc7_quality is kNone "
        "(BC7 format requires compression quality) for '{}'",
        to_string(desc.output_format), desc.source_id);
      return TextureImportError::kIntentFormatMismatch;
    }

    return std::nullopt;
  }

  //! Validate settings that depend on decoded or assembled image metadata.
  [[nodiscard]] auto ValidatePostDecode(const TextureImportDesc& desc,
    const ScratchImageMeta& decoded_meta) noexcept
    -> std::optional<TextureImportError>
  {
    if (decoded_meta.width == 0 || decoded_meta.height == 0) {
      LOG_F(WARNING,
        "TextureCooker: decoded image has zero dimensions ({}x{}) for '{}'",
        decoded_meta.width, decoded_meta.height, desc.source_id);
      return TextureImportError::kInvalidDimensions;
    }

    // If the user provided explicit dimensions, require them to match.
    if (desc.width != 0 && desc.height != 0) {
      if (decoded_meta.width != desc.width
        || decoded_meta.height != desc.height) {
        LOG_F(WARNING,
          "TextureCooker: dimension mismatch - descriptor specifies {}x{} "
          "but decoded image is {}x{} for '{}'",
          desc.width, desc.height, decoded_meta.width, decoded_meta.height,
          desc.source_id);
        return TextureImportError::kDimensionMismatch;
      }
    }

    // Validate the fully-resolved descriptor using decoded metadata.
    // This covers array-layer rules and non-3D depth constraints.
    auto resolved = desc;
    resolved.width = decoded_meta.width;
    resolved.height = decoded_meta.height;
    resolved.depth = decoded_meta.depth;
    resolved.array_layers = decoded_meta.array_layers;
    return resolved.Validate();
  }

  //=== Format Helpers ===----------------------------------------------------//

  [[nodiscard]] auto IsCompressedFormat(const Format format) noexcept -> bool
  {
    const auto& info = graphics::detail::GetFormatInfo(format);
    return info.block_size > 1;
  }

  [[nodiscard]] auto IsFloatFormat(const Format format) noexcept -> bool
  {
    const auto& info = graphics::detail::GetFormatInfo(format);
    return info.kind == graphics::detail::FormatKind::kFloat;
  }

  [[nodiscard]] auto IsSrgbFormat(const Format format) noexcept -> bool
  {
    const auto& info = graphics::detail::GetFormatInfo(format);
    return info.is_srgb;
  }

  [[nodiscard]] auto ToPackingPolicyId(const std::string_view id)
    -> std::optional<data::pak::TexturePackingPolicyId>
  {
    if (id == D3D12PackingPolicy::Instance().Id()) {
      return data::pak::TexturePackingPolicyId::kD3D12;
    }
    if (id == TightPackedPolicy::Instance().Id()) {
      return data::pak::TexturePackingPolicyId::kTightPacked;
    }
    return std::nullopt;
  }

  struct DecodedSource {
    ScratchImage image;
    SubresourceId subresource;
    std::string source_id;
  };

  /*!
   Assemble a non-cube array texture from pre-decoded subresources.

  Each source must provide exactly one mip level (mip 0 in the decoded
  ScratchImage). The target mip index is taken from subresource
  metadata, and all mip levels for every array layer must be present.

   @param sources Pre-decoded sources with subresource metadata.
   @param type Target texture type (non-cube only).
   @return A single ScratchImage containing all array layers and mips.

  ### Performance Characteristics

  - Time Complexity: $O(n + L \cdot M)$ for $n$ sources, $L$ layers,
    $M$ mips.
  - Memory: $O(L \cdot M)$ for presence tracking plus destination storage.
  - Optimization: Single-pass validation and direct pixel copies.

  ### Usage Examples

   ```cpp
   auto assembled = AssembleArrayTexture(
     decoded_sources, desc.texture_type);
   if (!assembled) {
     return ::oxygen::Err(assembled.error());
   }
   ```

   @note Cube maps are rejected and handled by the dedicated cube path.
   @see CookTexture
  */
  [[nodiscard]] auto AssembleArrayTexture(
    const std::span<const DecodedSource> sources, const TextureType type)
    -> oxygen::Result<ScratchImage, TextureImportError>
  {
    if (sources.empty()) {
      return ::oxygen::Err(TextureImportError::kFileNotFound);
    }

    if (type == TextureType::kTextureCube
      || type == TextureType::kTextureCubeArray) {
      return ::oxygen::Err(TextureImportError::kUnsupportedFormat);
    }
    if (type != TextureType::kTexture2D
      && type != TextureType::kTexture2DArray) {
      return ::oxygen::Err(TextureImportError::kUnsupportedFormat);
    }

    uint16_t max_layer = 0;
    uint16_t max_mip = 0;
    uint32_t base_width = 0;
    uint32_t base_height = 0;
    Format format = Format::kUnknown;

    for (const auto& source : sources) {
      if (!source.image.IsValid()) {
        return ::oxygen::Err(TextureImportError::kInvalidDimensions);
      }

      const auto& meta = source.image.Meta();
      if (meta.mip_levels != 1) {
        return ::oxygen::Err(TextureImportError::kInvalidMipPolicy);
      }

      if (source.subresource.depth_slice != 0) {
        return ::oxygen::Err(TextureImportError::kUnsupportedFormat);
      }

      max_layer = (std::max)(max_layer, source.subresource.array_layer);
      max_mip = (std::max)(max_mip, source.subresource.mip_level);

      if (format == Format::kUnknown) {
        format = meta.format;
      } else if (meta.format != format) {
        return ::oxygen::Err(TextureImportError::kOutputFormatInvalid);
      }

      if (source.subresource.mip_level == 0) {
        if (base_width == 0 && base_height == 0) {
          base_width = meta.width;
          base_height = meta.height;
        } else if (meta.width != base_width || meta.height != base_height) {
          return ::oxygen::Err(TextureImportError::kDimensionMismatch);
        }
      }
    }

    if (base_width == 0 || base_height == 0) {
      return ::oxygen::Err(TextureImportError::kInvalidMipPolicy);
    }

    const auto array_layers = static_cast<uint16_t>(max_layer + 1);
    const auto mip_levels = static_cast<uint16_t>(max_mip + 1);

    ScratchImageMeta meta {
      .texture_type = type,
      .width = base_width,
      .height = base_height,
      .depth = 1,
      .array_layers = array_layers,
      .mip_levels = mip_levels,
      .format = format,
    };

    ScratchImage assembled = ScratchImage::Create(meta);
    if (!assembled.IsValid()) {
      return ::oxygen::Err(TextureImportError::kOutOfMemory);
    }

    std::vector<bool> present(
      static_cast<size_t>(array_layers) * mip_levels, false);

    for (const auto& source : sources) {
      const auto layer = source.subresource.array_layer;
      const auto mip = source.subresource.mip_level;
      const auto index
        = ScratchImage::ComputeSubresourceIndex(layer, mip, mip_levels);
      if (index >= present.size() || present[index]) {
        return ::oxygen::Err(TextureImportError::kInvalidMipPolicy);
      }
      present[index] = true;

      const auto expected_width
        = ScratchImage::ComputeMipDimension(base_width, mip);
      const auto expected_height
        = ScratchImage::ComputeMipDimension(base_height, mip);

      const auto src_view = source.image.GetImage(0, 0);
      if (src_view.width != expected_width
        || src_view.height != expected_height) {
        return ::oxygen::Err(TextureImportError::kDimensionMismatch);
      }

      const auto expected_row_bytes = ComputeRowBytes(expected_width, format);
      if (src_view.row_pitch_bytes != expected_row_bytes) {
        return ::oxygen::Err(TextureImportError::kOutputFormatInvalid);
      }

      auto dst_pixels = assembled.GetMutablePixels(layer, mip);
      if (dst_pixels.size() != src_view.pixels.size()) {
        return ::oxygen::Err(TextureImportError::kDimensionMismatch);
      }

      std::copy(
        src_view.pixels.begin(), src_view.pixels.end(), dst_pixels.data());
    }

    for (size_t layer = 0; layer < array_layers; ++layer) {
      for (size_t mip = 0; mip < mip_levels; ++mip) {
        const auto index = ScratchImage::ComputeSubresourceIndex(
          static_cast<uint16_t>(layer), static_cast<uint16_t>(mip), mip_levels);
        if (index >= present.size() || !present[index]) {
          return ::oxygen::Err(TextureImportError::kInvalidMipPolicy);
        }
      }
    }

    return ::oxygen::Ok(std::move(assembled));
  }

  //=== Format Conversion Helpers
  //===------------------------------------------//

  //! Convert RGBA32Float image to RGBA8UNorm.
  /*!
   Clamps float values to [0, 1] and quantizes to 8-bit.

   \param source Float format image
   \return RGBA8UNorm image, or invalid image on failure
  */
  [[nodiscard]] auto ConvertFloat32ToRgba8(const ScratchImage& source)
    -> ScratchImage
  {
    if (!source.IsValid() || source.Meta().format != Format::kRGBA32Float) {
      return {};
    }

    const auto& meta = source.Meta();
    auto result = ScratchImage::Create(ScratchImageMeta {
      .width = meta.width,
      .height = meta.height,
      .depth = meta.depth,
      .array_layers = meta.array_layers,
      .mip_levels = meta.mip_levels,
      .format = Format::kRGBA8UNorm,
    });
    if (!result.IsValid()) {
      return {};
    }

    // Convert each subresource
    for (uint16_t layer = 0; layer < meta.array_layers; ++layer) {
      for (uint16_t mip = 0; mip < meta.mip_levels; ++mip) {
        const auto src_view = source.GetImage(layer, mip);
        auto dst_pixels = result.GetMutablePixels(layer, mip);

        const auto* src_ptr
          = reinterpret_cast<const float*>(src_view.pixels.data());
        auto* dst_ptr = reinterpret_cast<uint8_t*>(dst_pixels.data());

        const size_t pixel_count = src_view.width * src_view.height;
        for (size_t i = 0; i < pixel_count; ++i) {
          for (size_t c = 0; c < 4; ++c) {
            const float value = src_ptr[i * 4 + c];
            const float clamped = (std::max)(0.0f, (std::min)(1.0f, value));
            dst_ptr[i * 4 + c] = static_cast<uint8_t>(clamped * 255.0f + 0.5f);
          }
        }
      }
    }

    return result;
  }

  //! Convert RGBA8UNorm image to RGBA32Float.
  /*!
   Converts 8-bit values to [0, 1] float range.

   \param source RGBA8 format image
   \return RGBA32Float image, or invalid image on failure
  */
  [[nodiscard]] auto ConvertRgba8ToFloat32(const ScratchImage& source)
    -> ScratchImage
  {
    if (!source.IsValid()
      || (source.Meta().format != Format::kRGBA8UNorm
        && source.Meta().format != Format::kRGBA8UNormSRGB)) {
      return {};
    }

    const auto& meta = source.Meta();
    auto result = ScratchImage::Create(ScratchImageMeta {
      .width = meta.width,
      .height = meta.height,
      .depth = meta.depth,
      .array_layers = meta.array_layers,
      .mip_levels = meta.mip_levels,
      .format = Format::kRGBA32Float,
    });
    if (!result.IsValid()) {
      return {};
    }

    // Convert each subresource
    for (uint16_t layer = 0; layer < meta.array_layers; ++layer) {
      for (uint16_t mip = 0; mip < meta.mip_levels; ++mip) {
        const auto src_view = source.GetImage(layer, mip);
        auto dst_pixels = result.GetMutablePixels(layer, mip);

        const auto* src_ptr = src_view.pixels.data();
        auto* dst_ptr = reinterpret_cast<float*>(dst_pixels.data());

        const size_t pixel_count = src_view.width * src_view.height;
        for (size_t i = 0; i < pixel_count; ++i) {
          for (size_t c = 0; c < 4; ++c) {
            const uint8_t byte_val = static_cast<uint8_t>(src_ptr[i * 4 + c]);
            dst_ptr[i * 4 + c] = static_cast<float>(byte_val) / 255.0f;
          }
        }
      }
    }

    return result;
  }

  //! Convert RGBA32Float image to RGBA16Float.
  /*!
   Converts 32-bit float values to 16-bit half float using GLM.

   \param source RGBA32Float format image
   \return RGBA16Float image, or invalid image on failure
  */
  [[nodiscard]] auto ConvertFloat32ToFloat16(const ScratchImage& source)
    -> ScratchImage
  {
    if (!source.IsValid() || source.Meta().format != Format::kRGBA32Float) {
      return {};
    }

    const auto& meta = source.Meta();
    auto result = ScratchImage::Create(ScratchImageMeta {
      .width = meta.width,
      .height = meta.height,
      .depth = meta.depth,
      .array_layers = meta.array_layers,
      .mip_levels = meta.mip_levels,
      .format = Format::kRGBA16Float,
    });
    if (!result.IsValid()) {
      return {};
    }

    // Convert each subresource
    for (uint16_t layer = 0; layer < meta.array_layers; ++layer) {
      for (uint16_t mip = 0; mip < meta.mip_levels; ++mip) {
        const auto src_view = source.GetImage(layer, mip);
        auto dst_pixels = result.GetMutablePixels(layer, mip);

        const auto* src_ptr
          = reinterpret_cast<const float*>(src_view.pixels.data());
        auto* dst_ptr = reinterpret_cast<uint16_t*>(dst_pixels.data());

        const size_t pixel_count = src_view.width * src_view.height;
        ConvertRgba32FloatToRgba16Float(std::span { src_ptr, pixel_count * 4 },
          std::span { dst_ptr, pixel_count * 4 }, pixel_count);
      }
    }

    return result;
  }

} // namespace

//===----------------------------------------------------------------------===//
// Detail Pipeline Stages
//===----------------------------------------------------------------------===//

namespace detail {

  [[nodiscard]] auto CheckCancelled(const TextureImportDesc& desc) noexcept
    -> std::optional<TextureImportError>
  {
    if (desc.stop_token.stop_requested()) {
      return TextureImportError::kCancelled;
    }
    return std::nullopt;
  }

  auto DecodeSource(const std::span<const std::byte> source_bytes,
    const TextureImportDesc& desc)
    -> oxygen::Result<ScratchImage, TextureImportError>
  {
    if (auto cancelled = CheckCancelled(desc)) {
      return ::oxygen::Err(*cancelled);
    }

    DCHECK_F(
      !source_bytes.empty(), "DecodeSource: source_bytes must not be empty");

    DecodeOptions options {
      .flip_y = desc.flip_y_on_decode,
      .force_rgba = desc.force_rgba_on_decode,
      .extension_hint = {},
    };

    // Extract extension hint from source_id if present
    if (const auto dot_pos = desc.source_id.rfind('.');
      dot_pos != std::string::npos) {
      options.extension_hint = desc.source_id.substr(dot_pos);
    }

    auto result = DecodeToScratchImage(source_bytes, options);
    if (!result) {
      LOG_F(WARNING, "TextureCooker: failed to decode source '{}' (error: {})",
        desc.source_id, to_string(result.error()));
    } else {
      DLOG_F(INFO, "TextureCooker: decoded '{}' as {}x{} {}", desc.source_id,
        result->Meta().width, result->Meta().height,
        to_string(result->Meta().format));
    }
    return result;
  }

  auto ConvertToWorkingFormat(
    ScratchImage&& image, const TextureImportDesc& desc)
    -> oxygen::Result<ScratchImage, TextureImportError>
  {
    if (auto cancelled = CheckCancelled(desc)) {
      return ::oxygen::Err(*cancelled);
    }

    // For now, the decoder already produces RGBA8 or RGBA32Float
    // which are valid working formats.
    // Future: may need to expand channels or convert between formats

    (void)desc; // Currently unused but may be needed for future conversions

    if (!image.IsValid()) {
      LOG_F(WARNING,
        "TextureCooker: ConvertToWorkingFormat received invalid image for '{}'",
        desc.source_id);
      return ::oxygen::Err(TextureImportError::kDecodeFailed);
    }

    return ::oxygen::Ok(std::move(image));
  }

  auto ApplyContentProcessing(
    ScratchImage&& image, const TextureImportDesc& desc)
    -> oxygen::Result<ScratchImage, TextureImportError>
  {
    if (auto cancelled = CheckCancelled(desc)) {
      return ::oxygen::Err(*cancelled);
    }

    if (!image.IsValid()) {
      LOG_F(WARNING,
        "TextureCooker: ApplyContentProcessing received invalid image for '{}'",
        desc.source_id);
      return ::oxygen::Err(TextureImportError::kDecodeFailed);
    }

    ScratchImage result = std::move(image);
    const bool is_hdr_input = (result.Meta().format == Format::kRGBA32Float);
    const bool is_ldr_output = !IsFloatFormat(desc.output_format);

    // HDR processing: handle based on hdr_handling policy
    if (is_hdr_input && is_ldr_output) {
      // HDR input with LDR output - need to resolve
      switch (desc.hdr_handling) {
      case HdrHandling::kTonemapAuto:
        // Auto-tonemap: always bake HDR to LDR for LDR output
        result = image::hdr::BakeToLdr(result, desc.exposure_ev);
        if (!result.IsValid()) {
          return ::oxygen::Err(TextureImportError::kMipGenerationFailed);
        }
        break;

      case HdrHandling::kError:
        // Explicit user choice to bake - use the bake_hdr_to_ldr flag
        if (desc.bake_hdr_to_ldr) {
          result = image::hdr::BakeToLdr(result, desc.exposure_ev);
          if (!result.IsValid()) {
            return ::oxygen::Err(TextureImportError::kMipGenerationFailed);
          }
        }
        // If bake_hdr_to_ldr is false, let ConvertToOutputFormat handle error
        break;

      case HdrHandling::kKeepFloat:
        // Will be handled by ConvertToOutputFormat overriding to float
        // (not yet implemented - would require modifying desc)
        break;
      }
    } else if (is_hdr_input && desc.bake_hdr_to_ldr) {
      // User explicitly requested baking even for float output
      result = image::hdr::BakeToLdr(result, desc.exposure_ev);
      if (!result.IsValid()) {
        return ::oxygen::Err(TextureImportError::kMipGenerationFailed);
      }
    }

    // Normal map processing
    if (desc.intent == TextureIntent::kNormalTS) {
      if (desc.flip_normal_green) {
        image::content::FlipNormalGreen(result);
      }
    }

    // Color space conversion for sRGB content
    if (desc.source_color_space == ColorSpace::kSRGB
      && result.Meta().format == Format::kRGBA8UNorm) {
      // Convert to linear for processing if needed
      // Note: Mip generation will handle color space internally
    }

    return ::oxygen::Ok(std::move(result));
  }

  auto GenerateMips(ScratchImage&& image, const TextureImportDesc& desc)
    -> oxygen::Result<ScratchImage, TextureImportError>
  {
    if (auto cancelled = CheckCancelled(desc)) {
      return ::oxygen::Err(*cancelled);
    }

    if (!image.IsValid()) {
      return ::oxygen::Err(TextureImportError::kDecodeFailed);
    }

    if (desc.mip_policy == MipPolicy::kNone) {
      return ::oxygen::Ok(std::move(image));
    }

    // Compute target mip count
    const uint32_t full_mip_count
      = image::mip::ComputeMipCount(image.Meta().width, image.Meta().height);

    uint32_t target_mip_count = full_mip_count;
    if (desc.mip_policy == MipPolicy::kMaxCount) {
      target_mip_count = (std::min)(static_cast<uint32_t>(desc.max_mip_levels),
        full_mip_count);
    }

    // Skip if already has enough mips or only need 1
    if (target_mip_count <= 1 || image.Meta().mip_levels >= target_mip_count) {
      return ::oxygen::Ok(std::move(image));
    }

    // Generate mip chain based on content intent
    ScratchImage result;
    if (desc.intent == TextureIntent::kNormalTS) {
      result = image::content::GenerateNormalMapMips(
        image, desc.renormalize_normals_in_mips, target_mip_count);
    } else if (desc.texture_type == TextureType::kTexture3D) {
      result = image::mip::GenerateChain3D(
        image, desc.mip_filter, desc.mip_filter_space, target_mip_count);
    } else {
      result = image::mip::GenerateChain2D(
        image, desc.mip_filter, desc.mip_filter_space, target_mip_count);
    }

    if (!result.IsValid()) {
      return ::oxygen::Err(TextureImportError::kMipGenerationFailed);
    }

    return ::oxygen::Ok(std::move(result));
  }

  auto ConvertToOutputFormat(
    ScratchImage&& image, const TextureImportDesc& desc)
    -> oxygen::Result<ScratchImage, TextureImportError>
  {
    if (auto cancelled = CheckCancelled(desc)) {
      return ::oxygen::Err(*cancelled);
    }

    if (!image.IsValid()) {
      return ::oxygen::Err(TextureImportError::kDecodeFailed);
    }

    const Format output_format = desc.output_format;
    const Format current_format = image.Meta().format;

    // BC7 compression
    if (output_format == Format::kBC7UNorm
      || output_format == Format::kBC7UNormSRGB) {
      if (desc.bc7_quality == Bc7Quality::kNone) {
        return ::oxygen::Err(TextureImportError::kCompressionFailed);
      }

      // Convert float to RGBA8 first if needed
      ScratchImage input_image;
      if (current_format == Format::kRGBA32Float) {
        input_image = ConvertFloat32ToRgba8(image);
        if (!input_image.IsValid()) {
          return ::oxygen::Err(TextureImportError::kOutputFormatInvalid);
        }
      } else if (current_format == Format::kRGBA8UNorm
        || current_format == Format::kRGBA8UNormSRGB) {
        input_image = std::move(image);
      } else {
        return ::oxygen::Err(TextureImportError::kOutputFormatInvalid);
      }

      bc7::InitializeEncoder();
      auto compressed
        = bc7::EncodeTexture(input_image, desc.bc7_quality, desc.stop_token);
      if (!compressed.IsValid()) {
        if (desc.stop_token.stop_requested()) {
          return ::oxygen::Err(TextureImportError::kCancelled);
        }
        return ::oxygen::Err(TextureImportError::kCompressionFailed);
      }
      return ::oxygen::Ok(std::move(compressed));
    }

    // Float format output - handle RGBA16Float and RGBA32Float separately
    if (output_format == Format::kRGBA16Float) {
      // RGBA16Float: convert from RGBA32Float or RGBA8
      if (current_format == Format::kRGBA32Float) {
        auto half_image = ConvertFloat32ToFloat16(image);
        if (!half_image.IsValid()) {
          return ::oxygen::Err(TextureImportError::kOutputFormatInvalid);
        }
        return ::oxygen::Ok(std::move(half_image));
      }
      // Convert RGBA8 to float32 first, then to float16
      if (current_format == Format::kRGBA8UNorm
        || current_format == Format::kRGBA8UNormSRGB) {
        auto float32_image = ConvertRgba8ToFloat32(image);
        if (!float32_image.IsValid()) {
          return ::oxygen::Err(TextureImportError::kOutputFormatInvalid);
        }
        auto half_image = ConvertFloat32ToFloat16(float32_image);
        if (!half_image.IsValid()) {
          return ::oxygen::Err(TextureImportError::kOutputFormatInvalid);
        }
        return ::oxygen::Ok(std::move(half_image));
      }
      return ::oxygen::Err(TextureImportError::kOutputFormatInvalid);
    }

    if (output_format == Format::kRGBA32Float) {
      if (current_format == Format::kRGBA32Float) {
        // Already in float32 format
        return ::oxygen::Ok(std::move(image));
      }
      // Convert RGBA8 to float32
      if (current_format == Format::kRGBA8UNorm
        || current_format == Format::kRGBA8UNormSRGB) {
        auto float_image = ConvertRgba8ToFloat32(image);
        if (!float_image.IsValid()) {
          return ::oxygen::Err(TextureImportError::kOutputFormatInvalid);
        }
        return ::oxygen::Ok(std::move(float_image));
      }
      return ::oxygen::Err(TextureImportError::kOutputFormatInvalid);
    }

    // LDR format - ensure we have RGBA8
    if (output_format == Format::kRGBA8UNorm
      || output_format == Format::kRGBA8UNormSRGB) {
      if (current_format == Format::kRGBA8UNorm
        || current_format == Format::kRGBA8UNormSRGB) {
        return ::oxygen::Ok(std::move(image));
      }
      // HDR input without bake_hdr_to_ldr - error
      if (current_format == Format::kRGBA32Float) {
        return ::oxygen::Err(TextureImportError::kHdrRequiresFloatFormat);
      }
    }

    // Pass through for matching formats
    if (current_format == output_format) {
      return ::oxygen::Ok(std::move(image));
    }

    return ::oxygen::Err(TextureImportError::kOutputFormatInvalid);
  }

  //! Pack all subresources into a contiguous buffer with proper alignment.
  /*!
    CRITICAL: Subresource ordering MUST be LAYER-MAJOR to match D3D12
    subresource indexing.

    D3D12 subresource indexing formula:
      SubresourceIndex = MipSlice + (ArraySlice * MipLevels)

    This means we iterate: for (layer) { for (mip) { ... } }

    Data layout in output buffer:
      Layer0/Mip0, Layer0/Mip1, ..., Layer0/MipN,
      Layer1/Mip0, Layer1/Mip1, ..., Layer1/MipN,
      ...

    This ordering MUST match:
      - ComputeSubresourceLayouts() in TexturePackingPolicy.cpp
      - BuildTexture2DUploadLayout() in TextureBinder.cpp
  */
  auto PackSubresources(const ScratchImage& image,
    const ITexturePackingPolicy& policy) -> std::vector<std::byte>
  {
    if (!image.IsValid()) {
      return {};
    }

    // Compute layouts (uses layer-major ordering)
    auto layouts = ComputeSubresourceLayouts(image.Meta(), policy);
    const uint64_t total_size = ComputeTotalPayloadSize(layouts);

    // Allocate output buffer
    std::vector<std::byte> payload(total_size);

    // Copy each subresource with proper alignment.
    // IMPORTANT: Must iterate layer-major (layer outer, mip inner) to match
    // the layout order from ComputeSubresourceLayouts and D3D12 indexing.
    size_t layout_index = 0;
    const auto& meta = image.Meta();

    for (uint16_t layer = 0; layer < meta.array_layers; ++layer) {
      for (uint16_t mip = 0; mip < meta.mip_levels; ++mip) {
        const auto& layout = layouts[layout_index++];
        const auto src_view = image.GetImage(layer, mip);

        // Get source and destination pointers
        const auto* src_ptr = src_view.pixels.data();
        auto* dst_ptr = payload.data() + layout.offset;

        // Copy row by row with potential padding
        const auto& format_info = graphics::detail::GetFormatInfo(meta.format);
        const uint32_t src_row_bytes
          = ComputeRowBytes(layout.width, meta.format);

        if (format_info.block_size == 1) {
          // Uncompressed format
          for (uint32_t row = 0; row < layout.height; ++row) {
            std::memcpy(dst_ptr + row * layout.row_pitch,
              src_ptr + row * src_view.row_pitch_bytes, src_row_bytes);
          }
        } else {
          // Block-compressed format
          const uint32_t blocks_y = (layout.height + format_info.block_size - 1)
            / format_info.block_size;
          for (uint32_t block_row = 0; block_row < blocks_y; ++block_row) {
            std::memcpy(dst_ptr + block_row * layout.row_pitch,
              src_ptr + block_row * src_view.row_pitch_bytes, src_row_bytes);
          }
        }
      }
    }

    return payload;
  }

  auto ComputeContentHash(const std::span<const std::byte> payload) noexcept
    -> uint64_t
  {
    return util::ComputeContentHash(payload);
  }

} // namespace detail

//===----------------------------------------------------------------------===//
// Main Cooker API
//===----------------------------------------------------------------------===//

namespace {

  //! Common implementation for cooking from an already-decoded ScratchImage.
  /*!
   This handles stages 2-6 of the pipeline and builds the final payload.
  */
  [[nodiscard]] auto CookFromScratchImage(ScratchImage&& image,
    const TextureImportDesc& desc, const ITexturePackingPolicy& policy,
    const bool with_content_hashing)
    -> oxygen::Result<CookedTexturePayload, TextureImportError>
  {
    auto resolved_desc = desc;
    DLOG_F(INFO, "CookFromScratchImage: {}x{} layers={} mips={} format={}",
      image.Meta().width, image.Meta().height, image.Meta().array_layers,
      image.Meta().mip_levels, static_cast<int>(image.Meta().format));

    const bool is_hdr_input = image.Meta().format == Format::kRGBA32Float;
    if (is_hdr_input && resolved_desc.hdr_handling == HdrHandling::kKeepFloat
      && !IsFloatFormat(resolved_desc.output_format)) {
      resolved_desc.output_format = Format::kRGBA32Float;
      resolved_desc.bc7_quality = Bc7Quality::kNone;
      resolved_desc.bake_hdr_to_ldr = false;
    }

    // Post-decode validation (uses decoded/assembled image metadata).
    if (auto error = ValidatePostDecode(resolved_desc, image.Meta())) {
      return ::oxygen::Err(*error);
    }

    // Stage 2: Convert to working format
    auto working
      = detail::ConvertToWorkingFormat(std::move(image), resolved_desc);
    if (!working) {
      return ::oxygen::Err(working.error());
    }

    const bool is_working_hdr_input
      = working->Meta().format == Format::kRGBA32Float;
    if (is_working_hdr_input
      && resolved_desc.hdr_handling == HdrHandling::kKeepFloat
      && !IsFloatFormat(resolved_desc.output_format)) {
      resolved_desc.output_format = Format::kRGBA32Float;
      resolved_desc.bc7_quality = Bc7Quality::kNone;
      resolved_desc.bake_hdr_to_ldr = false;
    }

    // Stage 3: Apply content-specific processing
    auto processed
      = detail::ApplyContentProcessing(std::move(*working), resolved_desc);
    if (!processed) {
      return ::oxygen::Err(processed.error());
    }

    // Stage 4: Generate mips
    auto with_mips = detail::GenerateMips(std::move(*processed), resolved_desc);
    if (!with_mips) {
      return ::oxygen::Err(with_mips.error());
    }

    // Stage 5: Convert to output format
    auto output
      = detail::ConvertToOutputFormat(std::move(*with_mips), resolved_desc);
    if (!output) {
      return ::oxygen::Err(output.error());
    }

    DLOG_F(INFO, "CookFromScratchImage: output {}x{} layers={} mips={} fmt={}",
      output->Meta().width, output->Meta().height, output->Meta().array_layers,
      output->Meta().mip_levels, static_cast<int>(output->Meta().format));

    // Stage 6: Pack subresources (data region only)
    auto payload_data = detail::PackSubresources(*output, policy);

    // Compute layouts
    const auto raw_layouts = ComputeSubresourceLayouts(output->Meta(), policy);

    // Determine final format.
    // The ScratchImage format may differ from desc.output_format in cases where
    // the data is bit-identical but the format interpretation differs (e.g.,
    // kRGBA8UNorm vs kRGBA8UNormSRGB). Use the requested output_format when
    // the storage is compatible.
    Format final_format = output->Meta().format;
    const Format requested = resolved_desc.output_format;

    // Handle sRGB reinterpretation for RGBA8 formats
    if ((output->Meta().format == Format::kRGBA8UNorm
          || output->Meta().format == Format::kRGBA8UNormSRGB)
      && (requested == Format::kRGBA8UNorm
        || requested == Format::kRGBA8UNormSRGB)) {
      final_format = requested;
    }
    // Handle BC7 sRGB variants (encoder sets the correct format)
    if ((output->Meta().format == Format::kBC7UNorm
          || output->Meta().format == Format::kBC7UNormSRGB)
      && (requested == Format::kBC7UNorm
        || requested == Format::kBC7UNormSRGB)) {
      final_format = requested;
    }

    // Map layouts to PAK representation (32-bit offsets/pitches)
    std::vector<data::pak::SubresourceLayout> layouts;
    layouts.reserve(raw_layouts.size());
    for (const auto& layout : raw_layouts) {
      if (layout.offset > std::numeric_limits<uint32_t>::max()
        || layout.row_pitch > std::numeric_limits<uint32_t>::max()
        || layout.size_bytes > std::numeric_limits<uint32_t>::max()) {
        return ::oxygen::Err(TextureImportError::kOutputFormatInvalid);
      }

      layouts.push_back(data::pak::SubresourceLayout {
        .offset_bytes = static_cast<uint32_t>(layout.offset),
        .row_pitch_bytes = static_cast<uint32_t>(layout.row_pitch),
        .size_bytes = static_cast<uint32_t>(layout.size_bytes),
      });
    }

    const auto policy_id_opt = ToPackingPolicyId(policy.Id());
    if (!policy_id_opt.has_value()) {
      return ::oxygen::Err(TextureImportError::kOutputFormatInvalid);
    }

    const uint32_t layouts_offset
      = static_cast<uint32_t>(sizeof(data::pak::TexturePayloadHeader));
    const uint64_t layouts_bytes64 = static_cast<uint64_t>(layouts.size())
      * sizeof(data::pak::SubresourceLayout);
    if (layouts_bytes64 > std::numeric_limits<uint32_t>::max()) {
      return ::oxygen::Err(TextureImportError::kOutputFormatInvalid);
    }
    const auto layouts_bytes = static_cast<uint32_t>(layouts_bytes64);

    const auto data_offset64
      = policy.AlignSubresourceOffset(layouts_offset + layouts_bytes);
    if (data_offset64 > std::numeric_limits<uint32_t>::max()) {
      return ::oxygen::Err(TextureImportError::kOutputFormatInvalid);
    }
    const auto data_offset_bytes = static_cast<uint32_t>(data_offset64);

    const auto total_payload64
      = data_offset64 + static_cast<uint64_t>(payload_data.size());
    if (total_payload64 > std::numeric_limits<uint32_t>::max()) {
      return ::oxygen::Err(TextureImportError::kOutputFormatInvalid);
    }
    const auto total_payload_size = static_cast<uint32_t>(total_payload64);

    data::pak::TexturePayloadHeader header {};
    header.magic = data::pak::kTexturePayloadMagic;
    header.packing_policy = static_cast<uint8_t>(*policy_id_opt);
    header.flags = static_cast<uint8_t>(data::pak::TexturePayloadFlags::kNone);
    header.subresource_count = static_cast<uint16_t>(layouts.size());
    header.total_payload_size = total_payload_size;
    header.layouts_offset_bytes = layouts_offset;
    header.data_offset_bytes = data_offset_bytes;

    std::vector<std::byte> final_payload(total_payload_size, std::byte { 0 });
    std::memcpy(final_payload.data(), &header, sizeof(header));
    std::memcpy(
      final_payload.data() + layouts_offset, layouts.data(), layouts_bytes);
    std::memcpy(final_payload.data() + data_offset_bytes, payload_data.data(),
      payload_data.size());

    if (with_content_hashing) {
      header.content_hash = detail::ComputeContentHash(final_payload);
      std::memcpy(final_payload.data(), &header, sizeof(header));
    }

    // Build result
    CookedTexturePayload result;
    result.desc.texture_type = desc.texture_type;
    result.desc.width = output->Meta().width;
    result.desc.height = output->Meta().height;
    result.desc.depth = output->Meta().depth;
    result.desc.array_layers = output->Meta().array_layers;
    result.desc.mip_levels = output->Meta().mip_levels;
    result.desc.format = final_format;
    result.desc.packing_policy_id = std::string(policy.Id());
    result.desc.content_hash = header.content_hash;
    result.payload = std::move(final_payload);
    result.layouts = std::move(layouts);

    return ::oxygen::Ok(std::move(result));
  }

} // namespace

auto CookTexture(const std::span<const std::byte> source_bytes,
  const TextureImportDesc& desc, const ITexturePackingPolicy& policy,
  const bool with_content_hashing)
  -> oxygen::Result<CookedTexturePayload, TextureImportError>
{
  // Pre-decode validation - dimensions come from the decoded image
  if (auto error = ValidatePreDecode(desc)) {
    return ::oxygen::Err(*error);
  }

  // Stage 1: Decode
  auto decoded = detail::DecodeSource(source_bytes, desc);
  if (!decoded) {
    return ::oxygen::Err(decoded.error());
  }

  return CookFromScratchImage(
    std::move(*decoded), desc, policy, with_content_hashing);
}

auto CookTexture(ScratchImage&& image, const TextureImportDesc& desc,
  const ITexturePackingPolicy& policy, const bool with_content_hashing)
  -> oxygen::Result<CookedTexturePayload, TextureImportError>
{
  // Pre-decode validation - dimensions come from the ScratchImage
  if (auto error = ValidatePreDecode(desc)) {
    return ::oxygen::Err(*error);
  }

  if (!image.IsValid()) {
    return ::oxygen::Err(TextureImportError::kDecodeFailed);
  }

  return CookFromScratchImage(
    std::move(image), desc, policy, with_content_hashing);
}

auto CookTexture(const TextureSourceSet& sources, const TextureImportDesc& desc,
  const ITexturePackingPolicy& policy, const bool with_content_hashing)
  -> oxygen::Result<CookedTexturePayload, TextureImportError>
{
  // Pre-decode validation - dimensions come from decoded images
  if (auto error = ValidatePreDecode(desc)) {
    return ::oxygen::Err(*error);
  }

  if (sources.IsEmpty()) {
    return ::oxygen::Err(TextureImportError::kFileNotFound);
  }

  // For cube maps, we need exactly 6 sources
  if (desc.texture_type == TextureType::kTextureCube
    && sources.Count() != kCubeFaceCount) {
    return ::oxygen::Err(TextureImportError::kArrayLayerCountInvalid);
  }

  // Decode all sources first
  std::vector<DecodedSource> decoded_sources;
  decoded_sources.reserve(sources.Count());

  DecodeOptions decode_opts {
    .flip_y = desc.flip_y_on_decode,
    .force_rgba = desc.force_rgba_on_decode,
    .extension_hint = {},
  };

  for (const auto& source : sources.Sources()) {
    // Update extension hint from source_id
    if (const auto dot_pos = source.source_id.rfind('.');
      dot_pos != std::string::npos) {
      decode_opts.extension_hint = source.source_id.substr(dot_pos);
    }

    auto decoded = DecodeToScratchImage(source.bytes, decode_opts);
    if (!decoded) {
      return ::oxygen::Err(decoded.error());
    }

    decoded_sources.push_back(DecodedSource {
      .image = std::move(*decoded),
      .subresource = source.subresource,
      .source_id = source.source_id,
    });
  }

  // For cube maps, use the assembly helper
  if (desc.texture_type == TextureType::kTextureCube) {
    // Build array of 6 faces in order
    std::array<ScratchImage, kCubeFaceCount> faces;
    for (const auto& source : sources.Sources()) {
      const auto face_idx = source.subresource.array_layer;
      if (face_idx >= kCubeFaceCount) {
        return ::oxygen::Err(TextureImportError::kArrayLayerCountInvalid);
      }
      // Find the corresponding decoded image
      size_t src_idx = 0;
      for (const auto& s : sources.Sources()) {
        if (&s == &source) {
          break;
        }
        ++src_idx;
      }
      faces[face_idx] = std::move(decoded_sources[src_idx].image);
    }

    auto cube = AssembleCubeFromFaces(
      std::span<const ScratchImage, kCubeFaceCount>(faces));
    if (!cube) {
      return ::oxygen::Err(cube.error());
    }

    return CookFromScratchImage(
      std::move(*cube), desc, policy, with_content_hashing);
  }

  // For array textures, assemble into a single ScratchImage
  auto assembled = AssembleArrayTexture(decoded_sources, desc.texture_type);
  if (!assembled) {
    return ::oxygen::Err(assembled.error());
  }
  return CookFromScratchImage(
    std::move(*assembled), desc, policy, with_content_hashing);
}

} // namespace oxygen::content::import
