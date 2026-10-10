//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <Oxygen/Cooker/Test/Support/TestImages.h>

namespace oxygen::cooker::test {

namespace {

  struct Bgra final {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t alpha;
  };

  constexpr auto kFileHeaderSize = uint32_t { 14U };
  constexpr auto kDibHeaderSize = uint32_t { 40U };
  constexpr auto kPixelOffset = kFileHeaderSize + kDibHeaderSize;
  constexpr auto kBytesPerPixel = uint32_t { 4U };
  constexpr auto kPixelsPerMeter = int32_t { 2835 };

  constexpr auto kSinglePixel
    = Bgra { .blue = 10U, .green = 20U, .red = 30U, .alpha = 255U };
  constexpr auto kCycle = std::array {
    Bgra { .blue = 0U, .green = 0U, .red = 255U, .alpha = 255U }, // red
    Bgra { .blue = 255U, .green = 255U, .red = 255U, .alpha = 255U }, // white
    Bgra { .blue = 255U, .green = 0U, .red = 0U, .alpha = 255U }, // blue
    Bgra { .blue = 0U, .green = 255U, .red = 0U, .alpha = 255U }, // green
  };

} // namespace

auto MakeBmp(const uint32_t width, const uint32_t height)
  -> std::vector<std::byte>
{
  const auto pixel_count = width * height;
  const auto image_size = pixel_count * kBytesPerPixel;

  auto bytes = std::vector<std::byte> {};
  bytes.reserve(kPixelOffset + image_size);

  const auto push_u8 = [&bytes](const uint8_t value) -> void {
    bytes.push_back(std::byte { value });
  };
  const auto push_u16 = [&push_u8](const uint16_t value) -> void {
    push_u8(static_cast<uint8_t>(value & 0xFFU));
    push_u8(static_cast<uint8_t>((value >> 8U) & 0xFFU));
  };
  const auto push_u32 = [&push_u8](const uint32_t value) -> void {
    push_u8(static_cast<uint8_t>(value & 0xFFU));
    push_u8(static_cast<uint8_t>((value >> 8U) & 0xFFU));
    push_u8(static_cast<uint8_t>((value >> 16U) & 0xFFU));
    push_u8(static_cast<uint8_t>((value >> 24U) & 0xFFU));
  };
  const auto push_i32 = [&push_u32](const int32_t value) -> void {
    push_u32(static_cast<uint32_t>(value));
  };

  // File header.
  push_u8('B');
  push_u8('M');
  push_u32(kPixelOffset + image_size);
  push_u16(0U);
  push_u16(0U);
  push_u32(kPixelOffset);

  // BITMAPINFOHEADER.
  push_u32(kDibHeaderSize);
  push_i32(static_cast<int32_t>(width));
  push_i32(static_cast<int32_t>(height));
  push_u16(1U); // planes
  push_u16(32U); // bits per pixel
  push_u32(0U); // compression: none
  push_u32(image_size);
  push_i32(kPixelsPerMeter);
  push_i32(kPixelsPerMeter);
  push_u32(0U); // palette colors
  push_u32(0U); // important colors

  for (uint32_t index = 0U; index < pixel_count; ++index) {
    const auto pixel = (width == 1U && height == 1U)
      ? kSinglePixel
      : kCycle.at(index % kCycle.size());
    push_u8(pixel.blue);
    push_u8(pixel.green);
    push_u8(pixel.red);
    push_u8(pixel.alpha);
  }

  return bytes;
}

} // namespace oxygen::cooker::test
