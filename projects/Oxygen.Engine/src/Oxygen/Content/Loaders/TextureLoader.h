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
  LOG_SCOPE_FUNCTION(INFO);

  using oxygen::data::pak::TextureResourceDesc;

  auto check_result = [](auto&& result, const char* field) {
    if (!result) {
      LOG_F(INFO, "-failed- on {}: {}", field, result.error().message());
      throw std::runtime_error(
        fmt::format("error reading texture resource ({}): {}", field,
          result.error().message()));
    }
  };

  // Read TextureResourceDesc from the stream
  auto result = reader.read<TextureResourceDesc>();
  check_result(result, "TextureResourceDesc");
  const auto& desc = result.value();

  LOG_F(INFO, "data_offset      : {}", desc.data_offset);
  LOG_F(INFO, "data_size        : {}", desc.data_size);
  LOG_F(INFO, "texture_type     : {}", desc.texture_type);
  LOG_F(INFO, "compression_type : {}", desc.compression_type);
  LOG_F(INFO, "width            : {}", desc.width);
  LOG_F(INFO, "height           : {}", desc.height);
  LOG_F(INFO, "depth            : {}", desc.depth);
  LOG_F(INFO, "array_layers     : {}", desc.array_layers);
  LOG_F(INFO, "mip_levels       : {}", desc.mip_levels);
  LOG_F(INFO, "format           : {}", desc.format);
  LOG_F(INFO, "alignment        : {}", desc.alignment);
  LOG_F(INFO, "is_cubemap       : {}", desc.is_cubemap);

  // Move to the aligned position for image data
  auto align_result = reader.align_to(desc.alignment);
  check_result(align_result, "data blob offset");

  // Construct TextureResource using the new struct-based constructor
  return std::make_unique<data::TextureResource>(desc);
}

} // namespace oxygen::content::loaders
