//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>

#include <glm/gtc/packing.hpp>

namespace oxygen::content::import::util {

//! Pack a 32-bit float to a 16-bit half float using GLM.
/*!
  Uses IEEE 754 binary16 format via `glm::packHalf1x16`.

  @param value 32-bit float value
  @return 16-bit half float bit pattern
*/
[[nodiscard]] inline auto PackFloat32ToHalf(float value) noexcept -> uint16_t
{
  return glm::packHalf1x16(value);
}

//! Unpack a 16-bit half float to a 32-bit float using GLM.
/*!
  Uses IEEE 754 binary16 format via `glm::unpackHalf1x16`.

  @param bits 16-bit half float bit pattern
  @return 32-bit float value
*/
[[nodiscard]] inline auto UnpackHalfToFloat32(uint16_t bits) noexcept -> float
{
  return glm::unpackHalf1x16(bits);
}

//! Convert RGBA32Float pixels to RGBA16Float in-place.
/*!
  Converts an array of 32-bit float RGBA pixels to 16-bit half float format.
  Output is written to a separate destination buffer.

  @param src Source RGBA32Float pixels (4 floats per pixel)
  @param dst Destination RGBA16Float pixels (4 uint16_t per pixel)
  @param pixel_count Number of pixels to convert

  @pre src.size() >= pixel_count * 4 * sizeof(float)
  @pre dst.size() >= pixel_count * 4 * sizeof(uint16_t)
*/
inline void ConvertRgba32FloatToRgba16Float(
  std::span<const float> src, std::span<uint16_t> dst, size_t pixel_count)
{
  for (size_t i = 0; i < pixel_count * 4; ++i) {
    dst[i] = PackFloat32ToHalf(src[i]);
  }
}

//! Convert RGBA16Float pixels to RGBA32Float.
/*!
  Converts an array of 16-bit half float RGBA pixels to 32-bit float format.
  Output is written to a separate destination buffer.

  @param src Source RGBA16Float pixels (4 uint16_t per pixel)
  @param dst Destination RGBA32Float pixels (4 floats per pixel)
  @param pixel_count Number of pixels to convert

  @pre src.size() >= pixel_count * 4 * sizeof(uint16_t)
  @pre dst.size() >= pixel_count * 4 * sizeof(float)
*/
inline void ConvertRgba16FloatToRgba32Float(
  std::span<const uint16_t> src, std::span<float> dst, size_t pixel_count)
{
  for (size_t i = 0; i < pixel_count * 4; ++i) {
    dst[i] = UnpackHalfToFloat32(src[i]);
  }
}

} // namespace oxygen::content::import::util
