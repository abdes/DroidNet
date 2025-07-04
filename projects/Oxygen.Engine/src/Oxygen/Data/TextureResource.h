//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Texture resource as described in the PAK file resource table.
/*!
 Represents a texture resource referenced by assets in the PAK file. This is not
 a first-class asset: it is not named or globally identified, but is referenced
 by index in the textures resource table from materials or geometry.

 ### Binary Encoding (PAK v1, 40 bytes)

 ```text
 offset size name             description
 ------ ---- ---------------- ---------------------------------------------
 0x00   8    data_offset      Absolute offset to texture data in PAK file
 0x08   4    data_size        Size of texture data in bytes
 0x0C   1    texture_type     Texture type/dimension (enum)
 0x0D   1    compression_type Compression type (enum)
 0x0E   4    width            Texture width in pixels
 0x12   4    height           Texture height in pixels
 0x16   2    depth            Texture depth (3D/volume), otherwise 1
 0x18   2    array_layers     Array/cubemap layers, otherwise 1
 0x1A   2    mip_levels       Number of mipmap levels
 0x1C   1    format           Texture format enum value
 0x1D   2    alignment        Required alignment (default 256)
 0x1F   1    is_cubemap       1 if cubemap, 0 otherwise (legacy, prefer
 texture_type) 0x20   8    reserved         Reserved for future use
 ```

 @see TextureResourceDesc, MaterialAssetDesc
*/
class TextureResource : public oxygen::Object {
  OXYGEN_TYPED(TextureResource)

public:
  //! Type alias for the descriptor type used by this resource.
  using DescT = pak::TextureResourceDesc;

  explicit TextureResource(pak::TextureResourceDesc desc)
    : desc_(std::move(desc))
  {
  }

  ~TextureResource() override = default;

  OXYGEN_MAKE_NON_COPYABLE(TextureResource)
  OXYGEN_DEFAULT_MOVABLE(TextureResource)

  [[nodiscard]] auto GetDataOffset() const noexcept -> uint64_t
  {
    return desc_.data_offset;
  }

  [[nodiscard]] auto GetDataSize() const noexcept -> uint32_t
  {
    return desc_.data_size;
  }

  OXGN_DATA_NDAPI auto GetTextureType() const noexcept -> TextureType;

  [[nodiscard]] auto GetCompressionType() const noexcept -> uint8_t
  {
    return desc_.compression_type;
  }

  [[nodiscard]] auto GetWidth() const noexcept -> uint32_t
  {
    return desc_.width;
  }

  [[nodiscard]] auto GetHeight() const noexcept -> uint32_t
  {
    return desc_.height;
  }

  [[nodiscard]] auto GetDepth() const noexcept -> uint16_t
  {
    return desc_.depth;
  }

  [[nodiscard]] auto GetArrayLayers() const noexcept -> uint16_t
  {
    return desc_.array_layers;
  }

  [[nodiscard]] auto GetMipCount() const noexcept -> uint16_t
  {
    return desc_.mip_levels;
  }

  OXGN_DATA_NDAPI auto GetFormat() const noexcept -> Format;

  [[nodiscard]] auto GetDataAlignment() const noexcept -> uint16_t
  {
    return desc_.alignment;
  }

  //! @deprecated Prefer GetTextureType() for new code.
  [[nodiscard]] auto IsCubemap() const noexcept -> bool
  {
    return desc_.is_cubemap != 0;
  }

private:
  pak::TextureResourceDesc desc_ {};
};

} // namespace oxygen::data
