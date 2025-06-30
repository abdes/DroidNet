//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>

namespace oxygen::data {

//! A texture asset as stored in the PAK file.
/*!
 ### Binary Encoding:
 ```text
 offset size name           description
 ------ ---- -------------- ----------------------------------------------------
 0x00   4    width          Texture width in pixels
 0x04   4    height         Texture height in pixels
 0x08   4    mip_count      Number of mipmap levels
 0x0C   4    array_layers   Number of array layers
 0x10   4    format         Texture format enum value
 0x14   4    image_size     Total image data size in bytes
 0x18   4    alignment      Required alignment (default 256)
 0x1C   1    is_cubemap     1 if cubemap, 0 otherwise
 0x1D   35   reserved       Reserved/padding to 64 bytes
 0x40   ...  image_data     Texture image data (GPU-native format)
 ```

 @note Packed to 64 bytes total. Not aligned.
 @note Image data follows the header, aligned at 256 bytes.

 @see TextureFormat
*/
class TextureAsset : public oxygen::Object {
  OXYGEN_TYPED(TextureAsset)

public:
  TextureAsset() = default;
  TextureAsset(uint32_t width, uint32_t height, uint32_t mip_count,
    uint32_t array_layers, uint32_t format, uint32_t image_size,
    uint32_t alignment, bool is_cubemap, size_t data_offset = 0)
    : width_(width)
    , height_(height)
    , mip_count_(mip_count)
    , array_layers_(array_layers)
    , format_(format)
    , image_size_(image_size)
    , alignment_(alignment)
    , is_cubemap_(is_cubemap)
    , data_offset_(data_offset)
  {
  }
  ~TextureAsset() override = default;

  OXYGEN_MAKE_NON_COPYABLE(TextureAsset)
  OXYGEN_DEFAULT_MOVABLE(TextureAsset)

  [[nodiscard]] auto GetWidth() const noexcept -> uint32_t { return width_; }
  [[nodiscard]] auto GetHeight() const noexcept -> uint32_t { return height_; }
  [[nodiscard]] auto GetMipCount() const noexcept -> uint32_t
  {
    return mip_count_;
  }
  [[nodiscard]] auto GetArrayLayers() const noexcept -> uint32_t
  {
    return array_layers_;
  }
  [[nodiscard]] auto GetFormat() const noexcept -> uint32_t { return format_; }
  [[nodiscard]] auto GetImageSize() const noexcept -> uint32_t
  {
    return image_size_;
  }
  [[nodiscard]] auto GetAlignment() const noexcept -> uint32_t
  {
    return alignment_;
  }
  [[nodiscard]] auto IsCubemap() const noexcept -> bool
  {
    return is_cubemap_ != 0;
  }
  [[nodiscard]] auto GetDataOffset() const noexcept -> size_t
  {
    return data_offset_;
  }

private:
  uint32_t width_;
  uint32_t height_;
  uint32_t mip_count_;
  uint32_t array_layers_;
  uint32_t format_;
  uint32_t image_size_;
  uint32_t alignment_;
  bool is_cubemap_;

  // Offset to the image data in the stream (calculated during loading)
  size_t data_offset_;
};

} // namespace oxygen::data
