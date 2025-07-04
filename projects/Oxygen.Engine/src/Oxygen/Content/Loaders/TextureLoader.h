//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Reader.h>
#include <Oxygen/Base/Stream.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>

namespace oxygen::content::loaders {

//! Loader for texture assets.

//! Loads a texture resource from a PAK file stream.
template <oxygen::serio::Stream S>
auto LoadTextureResource(oxygen::serio::Reader<S> reader)
  -> std::unique_ptr<data::TextureResource>
{
  LOG_SCOPE_F(1, "Load Texture Resource");

  using oxygen::data::pak::TextureResourceDesc;

  auto check_result = [](auto&& result, const char* field) {
    if (!result) {
      LOG_F(ERROR, "-failed- on {}: {}", field, result.error().message());
      throw std::runtime_error(
        fmt::format("error reading texture resource ({}): {}", field,
          result.error().message()));
    }
  };

  // Read TextureResourceDesc from the stream
  auto result = reader.read<TextureResourceDesc>();
  check_result(result, "TextureResourceDesc");
  const auto& desc = result.value();

  auto tex_type = static_cast<oxygen::TextureType>(desc.texture_type);
  auto tex_format = static_cast<oxygen::Format>(desc.format);
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
  LOG_F(2, "is cubemap       : {}", desc.is_cubemap ? "yes" : "no");

  // Construct TextureResource using the new struct-based constructor
  return std::make_unique<data::TextureResource>(desc);
}

} // namespace oxygen::content::loaders
