//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <optional>
#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Async/Pipelines/TexturePipeline.h>
#include <Oxygen/Content/Import/Internal/TextureSourceAssemblyInternal.h>
#include <Oxygen/Content/Import/TextureCooker.h>
#include <Oxygen/Content/Import/TextureImportError.h>
#include <Oxygen/Content/Import/emit/TextureEmissionUtils.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::import {

namespace {

  [[nodiscard]] auto MakeErrorDiagnostic(
    TextureImportError error, std::string_view source_id) -> ImportDiagnostic
  {
    ImportDiagnostic diag {
      .severity = ImportSeverity::kError,
      .code = "texture.cook_failed",
      .message = std::string("Texture cook failed: ") + to_string(error) + " ("
        + std::string(source_id) + ")",
      .source_path = std::string(source_id),
      .object_path = {},
    };
    return diag;
  }

  [[nodiscard]] auto MakePackingPolicyDiagnostic(std::string_view policy_id,
    std::string_view fallback_id, std::string_view source_id)
    -> ImportDiagnostic
  {
    ImportDiagnostic diag {
      .severity = ImportSeverity::kWarning,
      .code = "texture.packing_policy_unknown",
      .message = std::string("Unknown packing policy '")
        + std::string(policy_id) + "'; using '" + std::string(fallback_id)
        + "'.",
      .source_path = std::string(source_id),
      .object_path = {},
    };
    return diag;
  }

  [[nodiscard]] auto ParseLayouts(std::span<const std::byte> payload)
    -> std::vector<data::pak::SubresourceLayout>
  {
    if (payload.size() < sizeof(data::pak::TexturePayloadHeader)) {
      return {};
    }

    data::pak::TexturePayloadHeader header {};
    std::memcpy(&header, payload.data(), sizeof(header));
    const auto count = header.subresource_count;
    const auto layouts_offset = header.layouts_offset_bytes;
    const auto layouts_size
      = static_cast<size_t>(count) * sizeof(data::pak::SubresourceLayout);
    if (layouts_offset > payload.size()
      || layouts_size > payload.size() - layouts_offset) {
      return {};
    }

    std::vector<data::pak::SubresourceLayout> layouts(count);
    std::memcpy(layouts.data(), payload.data() + layouts_offset, layouts_size);
    return layouts;
  }

  [[nodiscard]] auto ConvertToFloatImage(ScratchImage&& image)
    -> oxygen::Result<ScratchImage, TextureImportError>
  {
    if (!image.IsValid()) {
      return ::oxygen::Err(TextureImportError::kInvalidDimensions);
    }

    const auto& meta = image.Meta();
    if (meta.format == Format::kRGBA32Float) {
      return ::oxygen::Ok(std::move(image));
    }

    if (meta.format != Format::kRGBA8UNorm
      && meta.format != Format::kRGBA8UNormSRGB) {
      return ::oxygen::Err(TextureImportError::kInvalidOutputFormat);
    }

    ScratchImage float_image = ScratchImage::Create(ScratchImageMeta {
      .texture_type = TextureType::kTexture2D,
      .width = meta.width,
      .height = meta.height,
      .depth = 1,
      .array_layers = 1,
      .mip_levels = 1,
      .format = Format::kRGBA32Float,
    });

    if (!float_image.IsValid()) {
      return ::oxygen::Err(TextureImportError::kOutOfMemory);
    }

    const auto src_view = image.GetImage(0, 0);
    auto dst_pixels = float_image.GetMutablePixels(0, 0);
    const auto* src_ptr = src_view.pixels.data();
    auto* dst_ptr = reinterpret_cast<float*>(dst_pixels.data());

    const size_t pixel_count
      = static_cast<size_t>(meta.width) * static_cast<size_t>(meta.height);
    const size_t expected_bytes = pixel_count * 4U;
    if (src_view.pixels.size() < expected_bytes) {
      return ::oxygen::Err(TextureImportError::kInvalidDimensions);
    }
    for (size_t i = 0; i < pixel_count; ++i) {
      for (size_t c = 0; c < 4; ++c) {
        const uint8_t byte_val = static_cast<uint8_t>(src_ptr[i * 4 + c]);
        dst_ptr[i * 4 + c] = static_cast<float>(byte_val) / 255.0F;
      }
    }

    return ::oxygen::Ok(std::move(float_image));
  }

  [[nodiscard]] auto ConvertEquirectangularToCube(
    ScratchImage&& equirect, const EquirectToCubeOptions& options)
    -> oxygen::Result<ScratchImage, TextureImportError>
  {
    if (!equirect.IsValid()) {
      return ::oxygen::Err(TextureImportError::kDecodeFailed);
    }

    const auto& src_meta = equirect.Meta();
    const float aspect = static_cast<float>(src_meta.width)
      / static_cast<float>(src_meta.height);
    if (aspect < 1.5F || aspect > 2.5F) {
      return ::oxygen::Err(TextureImportError::kInvalidDimensions);
    }

    if (src_meta.format != Format::kRGBA32Float) {
      return ::oxygen::Err(TextureImportError::kInvalidOutputFormat);
    }

    if (options.face_size == 0) {
      return ::oxygen::Err(TextureImportError::kInvalidDimensions);
    }

    ScratchImageMeta cube_meta {
      .texture_type = TextureType::kTextureCube,
      .width = options.face_size,
      .height = options.face_size,
      .depth = 1,
      .array_layers = kCubeFaceCount,
      .mip_levels = 1,
      .format = Format::kRGBA32Float,
    };

    ScratchImage cube = ScratchImage::Create(cube_meta);
    if (!cube.IsValid()) {
      return ::oxygen::Err(TextureImportError::kOutOfMemory);
    }

    const auto src_view = equirect.GetImage(0, 0);
    const bool use_bicubic = (options.sample_filter == MipFilter::kKaiser
      || options.sample_filter == MipFilter::kLanczos);
    const uint32_t face_size = options.face_size;

    for (uint32_t face_idx = 0; face_idx < kCubeFaceCount; ++face_idx) {
      const auto face = static_cast<CubeFace>(face_idx);
      detail::ConvertEquirectangularFace(equirect, src_meta, src_view.pixels,
        face, face_size, use_bicubic, cube);
    }

    return ::oxygen::Ok(std::move(cube));
  }

  [[nodiscard]] auto ExtractCubeFacesFromLayoutImage(
    const ScratchImage& layout_image, CubeMapImageLayout layout)
    -> oxygen::Result<ScratchImage, TextureImportError>
  {
    if (!layout_image.IsValid()) {
      return ::oxygen::Err(TextureImportError::kDecodeFailed);
    }

    if (layout == CubeMapImageLayout::kAuto) {
      const auto detection = DetectCubeMapLayout(layout_image);
      if (!detection.has_value()) {
        return ::oxygen::Err(TextureImportError::kDimensionMismatch);
      }
      layout = detection->layout;
    }

    if (layout == CubeMapImageLayout::kUnknown) {
      return ::oxygen::Err(TextureImportError::kInvalidDimensions);
    }

    const auto& meta = layout_image.Meta();
    const auto detection = DetectCubeMapLayout(meta.width, meta.height);
    if (!detection.has_value() || detection->layout != layout) {
      return ::oxygen::Err(TextureImportError::kDimensionMismatch);
    }

    const uint32_t face_size = detection->face_size;
    const std::size_t bytes_per_pixel = detail::GetBytesPerPixel(meta.format);
    if (bytes_per_pixel == 0) {
      return ::oxygen::Err(TextureImportError::kUnsupportedFormat);
    }

    ScratchImageMeta cube_meta {
      .texture_type = TextureType::kTextureCube,
      .width = face_size,
      .height = face_size,
      .depth = 1,
      .array_layers = kCubeFaceCount,
      .mip_levels = 1,
      .format = meta.format,
    };

    ScratchImage cube = ScratchImage::Create(cube_meta);
    if (!cube.IsValid()) {
      return ::oxygen::Err(TextureImportError::kOutOfMemory);
    }

    const auto src_view = layout_image.GetImage(0, 0);
    for (uint32_t face_idx = 0; face_idx < kCubeFaceCount; ++face_idx) {
      const auto face = static_cast<CubeFace>(face_idx);
      detail::ExtractCubeFaceFromLayout(
        src_view, layout, face_size, bytes_per_pixel, face, cube);
    }

    return ::oxygen::Ok(std::move(cube));
  }

  //! Assemble a 3D volume texture from depth slices.
  /*!
   @param slices Decoded slice images.
   @param subresources Subresource identifiers for the slices.
   @return Assembled volume image or a texture import error.

  ### Performance Characteristics

  - Time Complexity: O(depth * width * height)
  - Memory: O(depth * width * height)
  - Optimization: Single contiguous copy per slice

   @note All slices must be single-mip, array_layer 0, and contiguous depth.
  */
  [[nodiscard]] auto AssembleVolumeFromSlices(
    std::span<ScratchImage> slices, std::span<const SubresourceId> subresources)
    -> oxygen::Result<ScratchImage, TextureImportError>
  {
    if (slices.empty() || slices.size() != subresources.size()) {
      return ::oxygen::Err(TextureImportError::kInvalidDimensions);
    }

    const auto& first_meta = slices.front().Meta();
    const auto format = first_meta.format;

    uint16_t max_depth = 0;
    for (const auto& subresource : subresources) {
      if (subresource.array_layer != 0) {
        return ::oxygen::Err(TextureImportError::kArrayLayerCountInvalid);
      }
      if (subresource.mip_level != 0) {
        return ::oxygen::Err(TextureImportError::kInvalidMipPolicy);
      }
      max_depth = (std::max)(max_depth, subresource.depth_slice);
    }

    const auto depth = static_cast<uint16_t>(max_depth + 1);
    std::vector<size_t> source_by_depth(depth, slices.size());
    std::vector<bool> present(depth, false);

    for (size_t i = 0; i < slices.size(); ++i) {
      const auto& meta = slices[i].Meta();
      if (meta.width != first_meta.width || meta.height != first_meta.height) {
        return ::oxygen::Err(TextureImportError::kDimensionMismatch);
      }
      if (meta.depth != 1 || meta.array_layers != 1 || meta.mip_levels != 1) {
        return ::oxygen::Err(TextureImportError::kInvalidDimensions);
      }
      if (meta.format != format) {
        return ::oxygen::Err(TextureImportError::kOutputFormatInvalid);
      }

      const auto depth_index = subresources[i].depth_slice;
      if (depth_index >= depth || present[depth_index]) {
        return ::oxygen::Err(TextureImportError::kInvalidMipPolicy);
      }
      present[depth_index] = true;
      source_by_depth[depth_index] = i;
    }

    for (const auto has_slice : present) {
      if (!has_slice) {
        return ::oxygen::Err(TextureImportError::kInvalidMipPolicy);
      }
    }

    const auto bytes_per_pixel = detail::GetBytesPerPixel(format);
    if (bytes_per_pixel == 0) {
      return ::oxygen::Err(TextureImportError::kUnsupportedFormat);
    }

    ScratchImageMeta volume_meta {
      .texture_type = TextureType::kTexture3D,
      .width = first_meta.width,
      .height = first_meta.height,
      .depth = depth,
      .array_layers = 1,
      .mip_levels = 1,
      .format = format,
    };

    ScratchImage volume = ScratchImage::Create(volume_meta);
    if (!volume.IsValid()) {
      return ::oxygen::Err(TextureImportError::kOutOfMemory);
    }

    auto dst_pixels = volume.GetMutablePixels(0, 0);
    const auto slice_size = static_cast<size_t>(first_meta.width)
      * static_cast<size_t>(first_meta.height) * bytes_per_pixel;

    if (dst_pixels.size() < slice_size * depth) {
      return ::oxygen::Err(TextureImportError::kOutOfMemory);
    }

    for (uint16_t slice = 0; slice < depth; ++slice) {
      const auto source_index = source_by_depth[slice];
      if (source_index >= slices.size()) {
        return ::oxygen::Err(TextureImportError::kInvalidMipPolicy);
      }

      const auto src_view = slices[source_index].GetImage(0, 0);
      const auto expected_row_bytes = ComputeRowBytes(src_view.width, format);
      if (src_view.row_pitch_bytes != expected_row_bytes) {
        return ::oxygen::Err(TextureImportError::kOutputFormatInvalid);
      }

      if (src_view.pixels.size() != slice_size) {
        return ::oxygen::Err(TextureImportError::kDimensionMismatch);
      }

      std::copy(src_view.pixels.begin(), src_view.pixels.end(),
        dst_pixels.data() + slice_size * slice);
    }

    return ::oxygen::Ok(std::move(volume));
  }

  [[nodiscard]] auto BuildPlaceholderPayload(std::string_view texture_id,
    std::string_view packing_policy_id) -> CookedTexturePayload
  {
    emit::CookerConfig config {};
    config.packing_policy_id = std::string(packing_policy_id);

    auto placeholder
      = emit::CreatePlaceholderForMissingTexture(texture_id, config);

    CookedTexturePayload cooked {};
    cooked.payload = std::move(placeholder.payload);
    cooked.layouts = ParseLayouts(cooked.payload);
    cooked.desc.texture_type = TextureType::kTexture2D;
    cooked.desc.width = 1;
    cooked.desc.height = 1;
    cooked.desc.depth = 1;
    cooked.desc.array_layers = 1;
    cooked.desc.mip_levels = 1;
    cooked.desc.format = Format::kRGBA8UNorm;
    cooked.desc.packing_policy_id = std::string(packing_policy_id);

    if (cooked.payload.size() >= sizeof(data::pak::TexturePayloadHeader)) {
      data::pak::TexturePayloadHeader header {};
      std::memcpy(&header, cooked.payload.data(), sizeof(header));
      cooked.desc.content_hash = header.content_hash;
    }

    return cooked;
  }

  struct CookOutcome {
    oxygen::Result<CookedTexturePayload, TextureImportError> cooked;
    std::optional<std::chrono::microseconds> decode_duration;
  };

  //! Cook a decoded image with optional output format override.
  [[nodiscard]] auto CookDecodedImage(ScratchImage&& image,
    TextureImportDesc desc, const ITexturePackingPolicy& policy,
    const bool output_format_is_override,
    std::optional<std::chrono::microseconds> decode_duration) -> CookOutcome
  {
    if (!output_format_is_override) {
      const auto& meta = image.Meta();
      desc.output_format = meta.format;
      desc.bc7_quality = Bc7Quality::kNone;
    }

    return { .cooked = CookTexture(std::move(image), desc, policy),
      .decode_duration = decode_duration };
  }

  //! Decode and cook a single source payload.
  /*!
   Converts an encoded texture payload into a cooked texture. Supports
   optional equirectangular-to-cubemap conversion and cubemap layout
   extraction.

   @param source Encoded payload bytes.
   @param desc Import description for the texture.
   @param policy Packing policy used by the cooker.
   @param output_format_is_override Whether output format is forced.
   @param equirect_to_cubemap Whether to convert from equirectangular.
   @param cubemap_face_size Output face size for cubemap conversion.
   @param cubemap_layout Layout for cubemap face extraction.
   @return Cooked texture payload with decode duration if applicable.

  ### Performance Characteristics

  - Time Complexity: O(width * height) plus decode cost.
  - Memory: O(width * height) for decoded images.
  - Optimization: Decode and cook performed once per payload.

   @note Decode cost depends on the input format and decoder.
  */
  [[nodiscard]] auto CookFromBytes(TexturePipeline::SourceBytes source,
    TextureImportDesc desc, const ITexturePackingPolicy& policy,
    const bool output_format_is_override, const bool equirect_to_cubemap,
    const uint32_t cubemap_face_size, const CubeMapImageLayout cubemap_layout)
    -> CookOutcome
  {
    if (source.bytes.empty()) {
      return {
        .cooked = ::oxygen::Err(TextureImportError::kFileNotFound),
        .decode_duration = {},
      };
    }

    if (equirect_to_cubemap) {
      const auto decode_start = std::chrono::steady_clock::now();
      auto decoded = detail::DecodeSource(source.bytes, desc);
      const auto decode_end = std::chrono::steady_clock::now();
      const auto decode_duration
        = std::chrono::duration_cast<std::chrono::microseconds>(
          decode_end - decode_start);
      if (!decoded) {
        return { .cooked = ::oxygen::Err(decoded.error()),
          .decode_duration = decode_duration };
      }

      auto float_image = ConvertToFloatImage(std::move(*decoded));
      if (!float_image) {
        return { .cooked = ::oxygen::Err(float_image.error()),
          .decode_duration = decode_duration };
      }

      EquirectToCubeOptions options {
        .face_size = cubemap_face_size,
        .sample_filter = desc.mip_filter,
      };
      auto cube
        = ConvertEquirectangularToCube(std::move(float_image.value()), options);
      if (!cube) {
        return { .cooked = ::oxygen::Err(cube.error()),
          .decode_duration = decode_duration };
      }

      return CookDecodedImage(std::move(*cube), desc, policy,
        output_format_is_override, decode_duration);
    }

    if (cubemap_layout != CubeMapImageLayout::kUnknown) {
      const auto decode_start = std::chrono::steady_clock::now();
      auto decoded = detail::DecodeSource(source.bytes, desc);
      const auto decode_end = std::chrono::steady_clock::now();
      const auto decode_duration
        = std::chrono::duration_cast<std::chrono::microseconds>(
          decode_end - decode_start);
      if (!decoded) {
        return { .cooked = ::oxygen::Err(decoded.error()),
          .decode_duration = decode_duration };
      }

      auto cube = ExtractCubeFacesFromLayoutImage(*decoded, cubemap_layout);
      if (!cube) {
        return { .cooked = ::oxygen::Err(cube.error()),
          .decode_duration = decode_duration };
      }

      return CookDecodedImage(std::move(*cube), desc, policy,
        output_format_is_override, decode_duration);
    }

    const auto decode_start = std::chrono::steady_clock::now();
    auto decoded = detail::DecodeSource(source.bytes, desc);
    const auto decode_end = std::chrono::steady_clock::now();
    const auto decode_duration
      = std::chrono::duration_cast<std::chrono::microseconds>(
        decode_end - decode_start);
    if (!decoded) {
      return { .cooked = ::oxygen::Err(decoded.error()),
        .decode_duration = decode_duration };
    }

    return CookDecodedImage(std::move(*decoded), desc, policy,
      output_format_is_override, decode_duration);
  }

  //! Decode, validate, assemble, and cook a set of source slices.
  /*!
   Decodes a source set, validates subresource metadata, and assembles the
   target texture (2D array, cube, or 3D volume) before cooking.

   @param source_set Source set containing decoded subresources.
   @param desc Import description for the texture.
   @param policy Packing policy used by the cooker.
   @param output_format_is_override Whether output format is forced.
   @return Cooked texture payload with accumulated decode duration.

  ### Performance Characteristics

  - Time Complexity: O(n * width * height) plus decode cost.
  - Memory: O(n * width * height) for decoded sources.
  - Optimization: Validates and assembles in a single pass.

   @note All sources must be single-mip and dimensionally consistent.
  */
  [[nodiscard]] auto CookFromSourceSet(TextureSourceSet source_set,
    TextureImportDesc desc, const ITexturePackingPolicy& policy,
    const bool output_format_is_override) -> CookOutcome
  {
    if (source_set.IsEmpty()) {
      return {
        .cooked = ::oxygen::Err(TextureImportError::kFileNotFound),
        .decode_duration = {},
      };
    }

    std::vector<ScratchImage> decoded_images;
    decoded_images.reserve(source_set.Count());
    std::vector<SubresourceId> subresources;
    subresources.reserve(source_set.Count());

    std::chrono::microseconds decode_accum { 0 };

    for (const auto& source : source_set.Sources()) {
      if (source.bytes.empty()) {
        return {
          .cooked = ::oxygen::Err(TextureImportError::kFileNotFound),
          .decode_duration = {},
        };
      }

      auto per_source_desc = desc;
      per_source_desc.source_id = source.source_id;
      const auto decode_start = std::chrono::steady_clock::now();
      auto decoded = detail::DecodeSource(source.bytes, per_source_desc);
      const auto decode_end = std::chrono::steady_clock::now();
      decode_accum += std::chrono::duration_cast<std::chrono::microseconds>(
        decode_end - decode_start);
      if (!decoded) {
        return { .cooked = ::oxygen::Err(decoded.error()),
          .decode_duration = decode_accum };
      }

      const auto& meta = decoded->Meta();
      if (meta.mip_levels != 1) {
        return { .cooked = ::oxygen::Err(TextureImportError::kInvalidMipPolicy),
          .decode_duration = decode_accum };
      }

      if (source.subresource.depth_slice != 0
        && desc.texture_type != TextureType::kTexture3D) {
        return { .cooked
          = ::oxygen::Err(TextureImportError::kUnsupportedFormat),
          .decode_duration = decode_accum };
      }

      decoded_images.push_back(std::move(*decoded));
      subresources.push_back(source.subresource);
    }

    const auto& first_meta = decoded_images[0].Meta();
    for (size_t i = 1; i < decoded_images.size(); ++i) {
      const auto& meta = decoded_images[i].Meta();
      if (meta.width != first_meta.width || meta.height != first_meta.height) {
        return { .cooked
          = ::oxygen::Err(TextureImportError::kDimensionMismatch),
          .decode_duration = decode_accum };
      }
      if (meta.format != first_meta.format) {
        return { .cooked
          = ::oxygen::Err(TextureImportError::kOutputFormatInvalid),
          .decode_duration = decode_accum };
      }
    }

    if (desc.texture_type == TextureType::kTextureCube) {
      std::array<ScratchImage, kCubeFaceCount> faces;
      std::array<bool, kCubeFaceCount> filled {};
      for (size_t i = 0; i < decoded_images.size(); ++i) {
        const auto face_idx = subresources[i].array_layer;
        if (face_idx >= kCubeFaceCount || filled[face_idx]) {
          return { .cooked
            = ::oxygen::Err(TextureImportError::kArrayLayerCountInvalid),
            .decode_duration = decode_accum };
        }
        faces[face_idx] = std::move(decoded_images[i]);
        filled[face_idx] = true;
      }

      for (const auto present : filled) {
        if (!present) {
          return { .cooked
            = ::oxygen::Err(TextureImportError::kArrayLayerCountInvalid),
            .decode_duration = decode_accum };
        }
      }

      auto cube = AssembleCubeFromFaces(
        std::span<const ScratchImage, kCubeFaceCount>(faces));
      if (!cube) {
        return { .cooked = ::oxygen::Err(cube.error()),
          .decode_duration = decode_accum };
      }

      return CookDecodedImage(std::move(*cube), desc, policy,
        output_format_is_override, decode_accum);
    }

    if (desc.texture_type == TextureType::kTextureCubeArray) {
      return { .cooked = ::oxygen::Err(TextureImportError::kUnsupportedFormat),
        .decode_duration = decode_accum };
    }
    if (desc.texture_type == TextureType::kTexture3D) {
      auto volume = AssembleVolumeFromSlices(decoded_images, subresources);
      if (!volume) {
        return { .cooked = ::oxygen::Err(volume.error()),
          .decode_duration = decode_accum };
      }

      return CookDecodedImage(std::move(*volume), desc, policy,
        output_format_is_override, decode_accum);
    }
    if (desc.texture_type != TextureType::kTexture2D
      && desc.texture_type != TextureType::kTexture2DArray) {
      return { .cooked = ::oxygen::Err(TextureImportError::kUnsupportedFormat),
        .decode_duration = decode_accum };
    }

    uint16_t max_layer = 0;
    uint16_t max_mip = 0;
    uint32_t base_width = 0;
    uint32_t base_height = 0;
    Format format = Format::kUnknown;

    for (size_t i = 0; i < decoded_images.size(); ++i) {
      const auto& meta = decoded_images[i].Meta();
      const auto& subresource = subresources[i];

      max_layer = (std::max)(max_layer, subresource.array_layer);
      max_mip = (std::max)(max_mip, subresource.mip_level);

      if (format == Format::kUnknown) {
        format = meta.format;
      } else if (meta.format != format) {
        return { .cooked
          = ::oxygen::Err(TextureImportError::kOutputFormatInvalid),
          .decode_duration = decode_accum };
      }

      if (subresource.mip_level == 0) {
        if (base_width == 0 && base_height == 0) {
          base_width = meta.width;
          base_height = meta.height;
        } else if (meta.width != base_width || meta.height != base_height) {
          return { .cooked
            = ::oxygen::Err(TextureImportError::kDimensionMismatch),
            .decode_duration = decode_accum };
        }
      }
    }

    if (base_width == 0 || base_height == 0) {
      return { .cooked = ::oxygen::Err(TextureImportError::kInvalidMipPolicy),
        .decode_duration = decode_accum };
    }

    const auto array_layer_count = static_cast<uint16_t>(max_layer + 1);
    const auto mip_level_count = static_cast<uint16_t>(max_mip + 1);
    auto array_type = desc.texture_type;
    if (array_type == TextureType::kTexture2D && array_layer_count > 1) {
      array_type = TextureType::kTexture2DArray;
    }

    ScratchImageMeta array_meta {
      .texture_type = array_type,
      .width = base_width,
      .height = base_height,
      .depth = 1,
      .array_layers = array_layer_count,
      .mip_levels = mip_level_count,
      .format = format,
    };

    ScratchImage assembled = ScratchImage::Create(array_meta);
    if (!assembled.IsValid()) {
      return { .cooked = ::oxygen::Err(TextureImportError::kOutOfMemory),
        .decode_duration = decode_accum };
    }

    std::vector<bool> present(
      static_cast<size_t>(array_layer_count) * mip_level_count, false);

    for (size_t i = 0; i < decoded_images.size(); ++i) {
      const auto& subresource = subresources[i];
      const auto layer = subresource.array_layer;
      const auto mip = subresource.mip_level;
      const auto index
        = ScratchImage::ComputeSubresourceIndex(layer, mip, mip_level_count);
      if (index >= present.size() || present[index]) {
        return { .cooked = ::oxygen::Err(TextureImportError::kInvalidMipPolicy),
          .decode_duration = decode_accum };
      }
      present[index] = true;

      const auto expected_width
        = ScratchImage::ComputeMipDimension(base_width, mip);
      const auto expected_height
        = ScratchImage::ComputeMipDimension(base_height, mip);

      const auto src_view = decoded_images[i].GetImage(0, 0);
      if (src_view.width != expected_width
        || src_view.height != expected_height) {
        return { .cooked
          = ::oxygen::Err(TextureImportError::kDimensionMismatch),
          .decode_duration = decode_accum };
      }

      const auto expected_row_bytes = ComputeRowBytes(expected_width, format);
      if (src_view.row_pitch_bytes != expected_row_bytes) {
        return { .cooked
          = ::oxygen::Err(TextureImportError::kOutputFormatInvalid),
          .decode_duration = decode_accum };
      }

      auto dst_pixels = assembled.GetMutablePixels(layer, mip);
      if (dst_pixels.size() != src_view.pixels.size()) {
        return { .cooked
          = ::oxygen::Err(TextureImportError::kDimensionMismatch),
          .decode_duration = decode_accum };
      }

      std::copy(
        src_view.pixels.begin(), src_view.pixels.end(), dst_pixels.data());
    }

    for (size_t layer = 0; layer < array_layer_count; ++layer) {
      for (size_t mip = 0; mip < mip_level_count; ++mip) {
        const auto index
          = ScratchImage::ComputeSubresourceIndex(static_cast<uint16_t>(layer),
            static_cast<uint16_t>(mip), mip_level_count);
        if (index >= present.size() || !present[index]) {
          return { .cooked
            = ::oxygen::Err(TextureImportError::kInvalidMipPolicy),
            .decode_duration = decode_accum };
        }
      }
    }

    desc.texture_type = array_type;
    return CookDecodedImage(std::move(assembled), desc, policy,
      output_format_is_override, decode_accum);
  }

  [[nodiscard]] auto CookFromSourceContent(
    TexturePipeline::SourceContent source, TextureImportDesc desc,
    const ITexturePackingPolicy& policy, const bool output_format_is_override,
    const bool equirect_to_cubemap, const uint32_t cubemap_face_size,
    const CubeMapImageLayout cubemap_layout) -> CookOutcome
  {
    DLOG_F(1, "TexturePipeline: Cook source content");
    return std::visit(
      [&](auto&& value) -> CookOutcome {
        using ValueT = std::decay_t<decltype(value)>;

        if constexpr (std::is_same_v<ValueT, TexturePipeline::SourceBytes>) {
          return CookFromBytes(std::move(value), desc, policy,
            output_format_is_override, equirect_to_cubemap, cubemap_face_size,
            cubemap_layout);
        } else if constexpr (std::is_same_v<ValueT, TextureSourceSet>) {
          return CookFromSourceSet(
            std::move(value), desc, policy, output_format_is_override);
        } else {
          return CookDecodedImage(
            std::move(value), desc, policy, output_format_is_override, {});
        }
      },
      std::move(source));
  }

} // namespace

TexturePipeline::TexturePipeline(co::ThreadPool& thread_pool, Config config)
  : thread_pool_(thread_pool)
  , config_(config)
  , input_channel_(config.queue_capacity)
  , output_channel_(config.queue_capacity)
{
}

TexturePipeline::~TexturePipeline()
{
  if (started_) {
    DLOG_IF_F(WARNING, HasPending(),
      "TexturePipeline destroyed with {} pending items", PendingCount());
  }

  input_channel_.Close();
  output_channel_.Close();
}

auto TexturePipeline::Start(co::Nursery& nursery) -> void
{
  DCHECK_F(!started_, "TexturePipeline::Start() called more than once");
  started_ = true;

  const auto worker_count = std::max(1U, config_.worker_count);
  for (uint32_t i = 0; i < worker_count; ++i) {
    nursery.Start([this]() -> co::Co<> { co_await Worker(); });
  }
}

auto TexturePipeline::Submit(WorkItem item) -> co::Co<>
{
  ++pending_;
  submitted_.fetch_add(1, std::memory_order_acq_rel);
  co_await input_channel_.Send(std::move(item));
}

auto TexturePipeline::TrySubmit(WorkItem item) -> bool
{
  if (input_channel_.Closed()) {
    return false;
  }

  if (input_channel_.Full()) {
    return false;
  }

  const auto ok = input_channel_.TrySend(std::move(item));
  if (ok) {
    ++pending_;
    submitted_.fetch_add(1, std::memory_order_acq_rel);
  }
  return ok;
}

auto TexturePipeline::Collect() -> co::Co<WorkResult>
{
  auto maybe_result = co_await output_channel_.Receive();
  if (!maybe_result.has_value()) {
    co_return WorkResult {
      .source_id = {},
      .texture_id = {},
      .source_key = nullptr,
      .cooked = {},
      .used_placeholder = false,
      .diagnostics = {},
      .success = false,
    };
  }

  pending_.fetch_sub(1, std::memory_order_acq_rel);
  if (maybe_result->success) {
    completed_.fetch_add(1, std::memory_order_acq_rel);
  } else {
    failed_.fetch_add(1, std::memory_order_acq_rel);
  }
  co_return std::move(*maybe_result);
}

auto TexturePipeline::Close() -> void { input_channel_.Close(); }

auto TexturePipeline::HasPending() const noexcept -> bool
{
  return pending_.load(std::memory_order_acquire) > 0;
}

auto TexturePipeline::PendingCount() const noexcept -> size_t
{
  return pending_.load(std::memory_order_acquire);
}

auto TexturePipeline::GetProgress() const noexcept -> PipelineProgress
{
  const auto submitted = submitted_.load(std::memory_order_acquire);
  const auto completed = completed_.load(std::memory_order_acquire);
  const auto failed = failed_.load(std::memory_order_acquire);
  return PipelineProgress {
    .submitted = submitted,
    .completed = completed,
    .failed = failed,
    .in_flight = submitted - completed - failed,
    .throughput = 0.0F,
  };
}

auto TexturePipeline::Worker() -> co::Co<>
{
  while (true) {
    auto maybe_item = co_await input_channel_.Receive();
    if (!maybe_item.has_value()) {
      break;
    }

    auto item = std::move(*maybe_item);
    if (item.stop_token.stop_requested()) {
      co_await ReportCancelled(std::move(item));
      continue;
    }

    const auto& policy = emit::GetPackingPolicy(item.packing_policy_id);
    const bool unknown_policy = item.packing_policy_id != policy.Id();
    auto local_desc = item.desc;
    local_desc.source_id = item.source_id;
    local_desc.stop_token = item.stop_token;

    auto source_ptr = std::make_shared<SourceContent>(std::move(item.source));
    auto result = co_await thread_pool_.Run(
      [source_ptr, desc = std::move(local_desc), &policy,
        output_format_is_override = item.output_format_is_override,
        equirect_to_cubemap = item.equirect_to_cubemap,
        cubemap_face_size = item.cubemap_face_size,
        cubemap_layout = item.cubemap_layout, stop_token = item.stop_token](
        co::ThreadPool::CancelToken cancelled) -> CookOutcome {
        DLOG_F(1, "TexturePipeline: Cook task begin");
        if (stop_token.stop_requested() || cancelled) {
          return { .cooked = ::oxygen::Err(TextureImportError::kCancelled),
            .decode_duration = {} };
        }
        return CookFromSourceContent(std::move(*source_ptr), desc, policy,
          output_format_is_override, equirect_to_cubemap, cubemap_face_size,
          cubemap_layout);
      });

    WorkResult output {
      .source_id = std::move(item.source_id),
      .texture_id = std::move(item.texture_id),
      .source_key = item.source_key,
      .cooked = std::nullopt,
      .used_placeholder = false,
      .diagnostics = {},
      .success = false,
    };

    if (unknown_policy) {
      output.diagnostics.push_back(MakePackingPolicyDiagnostic(
        item.packing_policy_id, policy.Id(), output.source_id));
    }

    output.decode_duration = result.decode_duration;

    if (result.cooked.has_value()) {
      output.cooked = std::move(result.cooked.value());
      output.success = true;
      co_await output_channel_.Send(std::move(output));
      continue;
    }

    const auto error = result.cooked.error();
    if (error == TextureImportError::kCancelled) {
      co_await output_channel_.Send(std::move(output));
      continue;
    }

    if (item.failure_policy == FailurePolicy::kPlaceholder) {
      output.cooked
        = BuildPlaceholderPayload(output.texture_id, item.packing_policy_id);
      output.used_placeholder = true;
      output.success = true;
      co_await output_channel_.Send(std::move(output));
      continue;
    }

    output.diagnostics.push_back(MakeErrorDiagnostic(error, output.source_id));
    co_await output_channel_.Send(std::move(output));
  }

  co_return;
}

auto TexturePipeline::ReportCancelled(WorkItem item) -> co::Co<>
{
  WorkResult cancelled {
    .source_id = std::move(item.source_id),
    .texture_id = std::move(item.texture_id),
    .source_key = item.source_key,
    .cooked = std::nullopt,
    .used_placeholder = false,
    .diagnostics = {},
    .success = false,
  };
  co_await output_channel_.Send(std::move(cancelled));
}

} // namespace oxygen::content::import
