//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Stream.h>
// ReSharper disable once CppUnusedIncludeDirective
#include <Oxygen/Content/Loaders/Helpers.h>

namespace oxygen::content::loaders {

//! Loader for texture assets.

//! Loads a texture resource from a PAK file stream.
inline auto LoadTextureResource(LoaderContext context)
  -> std::unique_ptr<data::TextureResource>
{
  LOG_SCOPE_F(1, "Load Texture Resource");
  LOG_F(2, "offline mode     : {}", context.offline ? "yes" : "no");

  DCHECK_NOTNULL_F(context.desc_reader, "expecting desc_reader not to be null");
  auto& reader = *context.desc_reader;

  using data::pak::TextureResourceDesc;

  auto check_result = [](auto&& result, const char* field) {
    if (!result) {
      LOG_F(ERROR, "-failed- on {}: {}", field, result.error().message());
      throw std::runtime_error(
        fmt::format("error reading texture resource ({}): {}", field,
          result.error().message()));
    }
  };

  // Read TextureResourceDesc from the stream
  auto pack = reader.ScopedAlignment(1);
  auto result = reader.Read<TextureResourceDesc>();
  check_result(result, "TextureResourceDesc");
  const auto& desc = result.value();

  auto tex_type = static_cast<TextureType>(desc.texture_type);
  auto tex_format = static_cast<oxygen::Format>(desc.format);
  LOG_F(1, "data offset      : {}", desc.data_offset);
  LOG_F(1, "data size        : {}", desc.size_bytes);
  LOG_F(2, "texture type     : {}", nostd::to_string(tex_type));
  LOG_F(2, "compression type : {}", desc.compression_type);
  LOG_F(2, "width            : {}", desc.width);
  LOG_F(2, "height           : {}", desc.height);
  LOG_F(2, "depth            : {}", desc.depth);
  LOG_F(2, "array layers     : {}", desc.array_layers);
  LOG_F(2, "mip levels       : {}", desc.mip_levels);
  LOG_F(2, "format           : {}", nostd::to_string(tex_format));
  LOG_F(2, "alignment        : {}", desc.alignment);

  // Validate texture dimensions
  if (desc.width == 0) {
    LOG_F(ERROR, "-failed- texture width cannot be zero");
    throw std::runtime_error(
      "error reading texture resource: width cannot be zero");
  }
  if (desc.height == 0) {
    LOG_F(ERROR, "-failed- texture height cannot be zero");
    throw std::runtime_error(
      "error reading texture resource: height cannot be zero");
  }

  // Validate texture type specific dimension requirements
  switch (tex_type) {
  case TextureType::kTexture1D:
  case TextureType::kTexture1DArray:
    if (desc.height != 1) {
      LOG_F(ERROR, "-failed- 1D texture height must be 1, got {}", desc.height);
      throw std::runtime_error(fmt::format(
        "error reading texture resource: 1D texture height must be 1, got {}",
        desc.height));
    }
    if (desc.depth != 1) {
      LOG_F(ERROR, "-failed- 1D texture depth must be 1, got {}", desc.depth);
      throw std::runtime_error(fmt::format(
        "error reading texture resource: 1D texture depth must be 1, got {}",
        desc.depth));
    }
    break;

  case TextureType::kTexture2D:
  case TextureType::kTexture2DArray:
  case TextureType::kTextureCube:
  case TextureType::kTextureCubeArray:
  case TextureType::kTexture2DMultiSample:
  case TextureType::kTexture2DMultiSampleArray:
    if (desc.depth != 1) {
      LOG_F(ERROR, "-failed- 2D texture depth must be 1, got {}", desc.depth);
      throw std::runtime_error(fmt::format(
        "error reading texture resource: 2D texture depth must be 1, got {}",
        desc.depth));
    }
    break;

  case TextureType::kTexture3D:
    if (desc.depth == 0) {
      LOG_F(ERROR, "-failed- 3D texture depth cannot be zero");
      throw std::runtime_error(
        "error reading texture resource: 3D texture depth cannot be zero");
    }
    break;

  case TextureType::kUnknown:
    // Allow unknown types to pass through for backward compatibility
    break;

  default:
    LOG_F(ERROR, "-failed- unsupported texture type: {}",
      static_cast<int>(desc.texture_type));
    throw std::runtime_error(fmt::format(
      "error reading texture resource: unsupported texture type: {}",
      static_cast<int>(desc.texture_type)));
  }

  std::vector<uint8_t> data_buffer(desc.size_bytes);
  if (desc.size_bytes > 0) {
    constexpr std::size_t tex_index
      = IndexOf<data::TextureResource, ResourceTypeList>::value;
    DCHECK_NOTNULL_F(std::get<tex_index>(context.data_readers),
      "expecting data reader for TextureResource to be valid");
    auto& data_reader = *std::get<tex_index>(context.data_readers);

    // Create a span of std::byte over the same memory
    std::span<std::byte> byte_view(
      reinterpret_cast<std::byte*>(data_buffer.data()), data_buffer.size());
    check_result(data_reader.Seek(desc.data_offset), "Texture Data");
    auto data_result = data_reader.ReadBlobInto(byte_view);
    check_result(data_result, "Texture Data");
  }

  return std::make_unique<data::TextureResource>(desc, std::move(data_buffer));
}

static_assert(oxygen::content::LoadFunction<decltype(LoadTextureResource)>);

//! Unload function for TextureResource.
inline auto UnloadTextureResource(
  const std::shared_ptr<data::TextureResource>& /*resource*/,
  AssetLoader& /*loader*/, const bool offline) noexcept -> void
{
  if (offline) {
    return;
  }
  // TODO: cleanup GPU resources for the texture.
  (void)0; // Placeholder for future GPU resource cleanup
}

static_assert(oxygen::content::UnloadFunction<decltype(UnloadTextureResource),
  data::TextureResource>);

} // namespace oxygen::content::loaders
