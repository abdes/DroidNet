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
#include <Oxygen/Data/TextureAsset.h>

namespace oxygen::content::loaders {

//! Loader for shader assets.
template <oxygen::serio::Stream S>
auto LoadTextureAsset(oxygen::serio::Reader<S> reader)
  -> std::unique_ptr<data::TextureAsset>
{
  LOG_SCOPE_FUNCTION(INFO);

#pragma pack(push, 1)
  struct TextureAssetHeader {
    uint32_t width;
    uint32_t height;
    uint32_t mip_count;
    uint32_t array_layers;
    uint32_t format;
    uint32_t image_size;
    uint32_t alignment;
    uint8_t is_cubemap;
    uint8_t reserved[35];
    // Followed by: image data (GPU-native format, e.g., BCn, ASTC),
    // aligned at 256 boundary for memory-mapped access
  };
#pragma pack(pop)
  static_assert(sizeof(TextureAssetHeader) == 64);

  auto check_result = [](auto&& result, const char* field) {
    if (!result) {
      LOG_F(INFO, "-failed- on {}: {}", field, result.error().message());
      throw std::runtime_error(
        fmt::format("error reading shader asset ({}): {}", field,
          result.error().message()));
    }
  };

  // Read shader_type
  auto result = reader.read<TextureAssetHeader>();
  check_result(result, "texture header");
  const auto& tex_header = result.value();
  LOG_F(INFO, "width        : {}", tex_header.width);
  LOG_F(INFO, "height       : {}", tex_header.height);
  LOG_F(INFO, "mip_count    : {}", tex_header.mip_count);
  LOG_F(INFO, "array_layers : {}", tex_header.array_layers);
  LOG_F(INFO, "format       : {}", tex_header.format);
  LOG_F(INFO, "image_size   : {}", tex_header.image_size);
  LOG_F(INFO, "alignment    : {}", tex_header.alignment);
  LOG_F(INFO, "is_cubemap   : {}", tex_header.is_cubemap);

  // Move to the aligned position for image data
  auto align_result = reader.align_to(tex_header.alignment);
  check_result(align_result, "data blob offset");
  const auto data_offset = reader.position();

  // Construct ShaderAsset using the new constructor
  return std::make_unique<data::TextureAsset>(tex_header.width,
    tex_header.height, tex_header.mip_count, tex_header.array_layers,
    tex_header.format, tex_header.image_size, tex_header.alignment,
    tex_header.is_cubemap != 0, *data_offset);
}

} // namespace oxygen::content::loaders
