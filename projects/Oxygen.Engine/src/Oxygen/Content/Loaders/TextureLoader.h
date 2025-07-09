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
#include <Oxygen/Base/Stream.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/Reader.h>
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
  auto tex_format = static_cast<Format>(desc.format);
  LOG_F(1, "data offset      : {}", desc.data_offset);
  LOG_F(1, "data size        : {}", desc.data_size);
  LOG_F(2, "texture type     : {}", nostd::to_string(tex_type));
  LOG_F(2, "compression type : {}", desc.compression_type);
  LOG_F(2, "width            : {}", desc.width);
  LOG_F(2, "height           : {}", desc.height);
  LOG_F(2, "depth            : {}", desc.depth);
  LOG_F(2, "array layers     : {}", desc.array_layers);
  LOG_F(2, "mip levels       : {}", desc.mip_levels);
  LOG_F(2, "format           : {}", nostd::to_string(tex_format));
  LOG_F(2, "alignment        : {}", desc.alignment);

  // Note: In offline mode, we skip any GPU resource creation
  if (!context.offline && desc.data_size > 0) {
    // TODO: Read the texture data
    // For now we just do some sanity checks and throw if the data cannot be
    // fully read
    constexpr std::size_t tex_index
      = IndexOf<data::TextureResource, ResourceTypeList>::value;
    DCHECK_NOTNULL_F(std::get<tex_index>(context.data_readers),
      "expecting data reader for TextureResource to be valid");
    auto& data_reader = *std::get<tex_index>(context.data_readers);

    check_result(data_reader.Seek(desc.data_offset), "Texture Data");
    auto align_result = data_reader.AlignTo(desc.alignment);
    check_result(align_result, "Texture Data");
    auto data_result = data_reader.ReadBlob(desc.data_size);
    check_result(data_result, "Texture Data");
  }

  return std::make_unique<data::TextureResource>(desc);
}

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

} // namespace oxygen::content::loaders
