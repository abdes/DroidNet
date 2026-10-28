//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace oxygen::cooker::test {

//! Builds a minimal uncompressed 32-bit BGRA BMP file in memory.
/*!
 The file is a 14-byte file header, a 40-byte BITMAPINFOHEADER (bottom-up rows,
 2835 pixels per meter) and `width * height` pixels.

 Pixel pattern, in storage order (bottom row first, left to right):
 - 1x1: a single pixel, B=10 G=20 R=30 A=255.
 - Any other size: the pixels cycle red, white, blue, green (all opaque). For
   2x2 that is the bottom row red, white and the top row blue, green.

 \param width Image width in pixels; must be positive.
 \param height Image height in pixels; must be positive.
 \return The complete BMP file bytes.
*/
[[nodiscard]] auto MakeBmp(uint32_t width, uint32_t height)
  -> std::vector<std::byte>;

//! Encodes RGBA8 pixels as a lossless still WebP file, with libwebp.
/*!
 \param width Image width in pixels; must be positive.
 \param height Image height in pixels; must be positive.
 \param rgba `width * height` RGBA8 pixels, top row first.
 \return The complete WebP file bytes.
*/
[[nodiscard]] auto MakeWebp(uint32_t width, uint32_t height,
  std::span<const std::byte> rgba) -> std::vector<std::byte>;

//! How `MakeKtx2` encodes its image.
enum class Ktx2Encoding : uint8_t {
  kRaw, //!< Uncompressed `VK_FORMAT_R8G8B8A8_UNORM`.
  kEtc1s, //!< Basis Universal ETC1S, BasisLZ supercompressed.
  kUastc, //!< Basis Universal UASTC.
};

//! A KTX2 texture for `MakeKtx2`: one level, one layer.
struct Ktx2Image final {
  uint32_t width = 0;
  uint32_t height = 0;
  //! `width * height` RGBA8 pixels, top row first, written to every face.
  std::span<const std::byte> rgba;
  Ktx2Encoding encoding = Ktx2Encoding::kUastc;
  //! 1 for a 2D texture, 6 for a cube map.
  uint32_t faces = 1;
  //! Stores the rows bottom row first and says so (`KTXorientation` "ru").
  bool bottom_up = false;
};

//! Encodes a KTX2 file with libktx.
/*!
 \return The complete KTX2 file bytes; the test fails if libktx refuses.
*/
[[nodiscard]] auto MakeKtx2(const Ktx2Image& image) -> std::vector<std::byte>;

} // namespace oxygen::cooker::test
