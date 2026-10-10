//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
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

} // namespace oxygen::cooker::test
