//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/Import/Internal/DdsDecode.h>
#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Cooker/Import/TextureImportError.h>
#include <Oxygen/Core/Types/Format.h>

namespace oxygen::content::import {

namespace {

  // https://learn.microsoft.com/windows/win32/direct3ddds/dds-header
  constexpr std::size_t kMagicSize = 4U;
  constexpr std::size_t kHeaderSize = 124U;
  constexpr std::size_t kPixelFormatSize = 32U;
  constexpr std::size_t kDx10HeaderSize = 20U;
  constexpr std::size_t kBaseDataOffset = kMagicSize + kHeaderSize;

  // Offsets from the start of the file.
  constexpr std::size_t kHeaderSizeField = 4U;
  constexpr std::size_t kHeightField = 12U;
  constexpr std::size_t kWidthField = 16U;
  constexpr std::size_t kDepthField = 24U;
  constexpr std::size_t kPixelFormatSizeField = 76U;
  constexpr std::size_t kPixelFormatFlagsField = 80U;
  constexpr std::size_t kFourCcField = 84U;
  constexpr std::size_t kBitCountField = 88U;
  constexpr std::size_t kRedMaskField = 92U;
  constexpr std::size_t kGreenMaskField = 96U;
  constexpr std::size_t kBlueMaskField = 100U;
  constexpr std::size_t kAlphaMaskField = 104U;
  constexpr std::size_t kCaps2Field = 112U;
  constexpr std::size_t kDxgiFormatField = kBaseDataOffset;
  constexpr std::size_t kDimensionField = kBaseDataOffset + 4U;
  constexpr std::size_t kMiscFlagField = kBaseDataOffset + 8U;
  constexpr std::size_t kArraySizeField = kBaseDataOffset + 12U;

  constexpr std::uint32_t kPixelFormatFourCc = 0x4U;
  constexpr std::uint32_t kPixelFormatRgb = 0x40U;
  constexpr std::uint32_t kCaps2Cube = 0x200U;
  constexpr std::uint32_t kCaps2Volume = 0x200000U;
  constexpr std::uint32_t kDimensionTexture2D = 3U;
  constexpr std::uint32_t kMiscTextureCube = 0x4U;
  constexpr std::uint32_t kLargestExtent = 65536U;

  constexpr std::size_t kBytesPerPixel = 4U;
  constexpr std::uint32_t kBlockExtent = 4U;
  constexpr std::size_t kBlockPixels = 16U;

  enum class Encoding : std::uint8_t {
    kBc1,
    kBc2,
    kBc3,
    kBc4,
    kBc5,
    kRgba8,
    kBgra8,
  };

  struct Extent final {
    std::uint32_t width { 0U };
    std::uint32_t height { 0U };
  };

  using Rgba8 = std::array<std::uint8_t, kBytesPerPixel>;
  using Block = std::array<Rgba8, kBlockPixels>;

  [[nodiscard]] constexpr auto FourCc(const char a, const char b, const char c,
    const char d) noexcept -> std::uint32_t
  {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(a))
      | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 8U)
      | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 16U)
      | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d)) << 24U);
  }

  [[nodiscard]] auto ReadU16(const std::span<const std::byte> bytes,
    const std::size_t at) noexcept -> std::uint16_t
  {
    auto value = std::uint16_t {};
    std::memcpy(&value, bytes.subspan(at, sizeof(value)).data(), sizeof(value));
    return value;
  }

  [[nodiscard]] auto ReadU32(const std::span<const std::byte> bytes,
    const std::size_t at) noexcept -> std::uint32_t
  {
    auto value = std::uint32_t {};
    std::memcpy(&value, bytes.subspan(at, sizeof(value)).data(), sizeof(value));
    return value;
  }

  [[nodiscard]] constexpr auto EncodingOfDxgi(
    const std::uint32_t format) noexcept -> std::optional<Encoding>
  {
    // DXGI_FORMAT values: typeless, UNORM and UNORM_SRGB.
    switch (format) {
    case 27U:
    case 28U:
    case 29U:
      return Encoding::kRgba8;
    case 87U:
    case 90U:
    case 91U:
      return Encoding::kBgra8;
    case 70U:
    case 71U:
    case 72U:
      return Encoding::kBc1;
    case 73U:
    case 74U:
    case 75U:
      return Encoding::kBc2;
    case 76U:
    case 77U:
    case 78U:
      return Encoding::kBc3;
    case 79U:
    case 80U:
      return Encoding::kBc4;
    case 82U:
    case 83U:
      return Encoding::kBc5;
    default:
      return std::nullopt;
    }
  }

  [[nodiscard]] constexpr auto EncodingOfFourCc(
    const std::uint32_t code) noexcept -> std::optional<Encoding>
  {
    if (code == FourCc('D', 'X', 'T', '1')) {
      return Encoding::kBc1;
    }
    if (code == FourCc('D', 'X', 'T', '3')) {
      return Encoding::kBc2;
    }
    if (code == FourCc('D', 'X', 'T', '5')) {
      return Encoding::kBc3;
    }
    if (code == FourCc('A', 'T', 'I', '1')
      || code == FourCc('B', 'C', '4', 'U')) {
      return Encoding::kBc4;
    }
    if (code == FourCc('A', 'T', 'I', '2')
      || code == FourCc('B', 'C', '5', 'U')) {
      return Encoding::kBc5;
    }
    return std::nullopt;
  }

  [[nodiscard]] auto EncodingOfMasks(
    const std::span<const std::byte> bytes) noexcept -> std::optional<Encoding>
  {
    constexpr std::uint32_t kLowByte = 0x000000FFU;
    constexpr std::uint32_t kSecondByte = 0x0000FF00U;
    constexpr std::uint32_t kThirdByte = 0x00FF0000U;
    constexpr std::uint32_t kHighByte = 0xFF000000U;
    constexpr std::uint32_t kBitsPerPixel = 32U;
    if (ReadU32(bytes, kBitCountField) != kBitsPerPixel
      || ReadU32(bytes, kGreenMaskField) != kSecondByte
      || ReadU32(bytes, kAlphaMaskField) != kHighByte) {
      return std::nullopt;
    }
    const auto red = ReadU32(bytes, kRedMaskField);
    const auto blue = ReadU32(bytes, kBlueMaskField);
    if (red == kLowByte && blue == kThirdByte) {
      return Encoding::kRgba8;
    }
    if (red == kThirdByte && blue == kLowByte) {
      return Encoding::kBgra8;
    }
    return std::nullopt;
  }

  //! Expands a 5:6:5 color, replicating high bits into the low ones.
  [[nodiscard]] constexpr auto Expand565(const std::uint16_t color) noexcept
    -> Rgba8
  {
    const std::uint32_t bits = color;
    const auto r = (bits >> 11U) & 0x1FU;
    const auto g = (bits >> 5U) & 0x3FU;
    const auto b = bits & 0x1FU;
    return {
      static_cast<std::uint8_t>((r << 3U) | (r >> 2U)),
      static_cast<std::uint8_t>((g << 2U) | (g >> 4U)),
      static_cast<std::uint8_t>((b << 3U) | (b >> 2U)),
      255U,
    };
  }

  //! Weights two endpoints channel by channel, rounding to nearest.
  [[nodiscard]] constexpr auto Mix(const Rgba8& a, const Rgba8& b,
    const std::uint32_t weight_a, const std::uint32_t weight_b) noexcept
    -> Rgba8
  {
    const auto total = weight_a + weight_b;
    auto mixed = Rgba8 { 0U, 0U, 0U, 255U };
    for (std::size_t channel = 0U; channel < 3U; ++channel) {
      mixed.at(channel) = static_cast<std::uint8_t>(
        ((a.at(channel) * weight_a) + (b.at(channel) * weight_b) + (total / 2U))
        / total);
    }
    return mixed;
  }

  //! Decodes a BC1 color block. Only BC1 itself has the mode that orders its
  //! endpoints to mean three colors and transparent black.
  auto DecodeColorBlock(const std::span<const std::byte> block,
    const bool allow_transparent, Block& out) noexcept -> void
  {
    const auto low = ReadU16(block, 0U);
    const auto high = ReadU16(block, 2U);
    const auto a = Expand565(low);
    const auto b = Expand565(high);
    auto palette = std::array<Rgba8, 4U> { a, b, Rgba8 {}, Rgba8 {} };
    if (low > high || !allow_transparent) {
      palette[2] = Mix(a, b, 2U, 1U);
      palette[3] = Mix(a, b, 1U, 2U);
    } else {
      palette[2] = Mix(a, b, 1U, 1U);
      palette[3] = Rgba8 { 0U, 0U, 0U, 0U };
    }
    const auto indices = ReadU32(block, 4U);
    for (std::size_t pixel = 0U; pixel < kBlockPixels; ++pixel) {
      out.at(pixel) = palette.at((indices >> (pixel * 2U)) & 3U);
    }
  }

  //! Decodes an 8-byte single-channel block of BC3 alpha, BC4 or BC5.
  auto DecodeChannelBlock(const std::span<const std::byte> block,
    const std::size_t channel, Block& out) noexcept -> void
  {
    const auto a = std::to_integer<std::uint32_t>(block[0]);
    const auto b = std::to_integer<std::uint32_t>(block[1]);
    auto palette = std::array<std::uint32_t, 8U> { a, b };
    if (a > b) {
      for (std::uint32_t step = 1U; step <= 6U; ++step) {
        palette.at(step + 1U) = (((7U - step) * a) + (step * b) + 3U) / 7U;
      }
    } else {
      for (std::uint32_t step = 1U; step <= 4U; ++step) {
        palette.at(step + 1U) = (((5U - step) * a) + (step * b) + 2U) / 5U;
      }
      palette[6] = 0U;
      palette[7] = 255U;
    }
    auto indices = std::uint64_t {};
    std::memcpy(&indices, block.subspan(2U, 6U).data(), 6U);
    for (std::size_t pixel = 0U; pixel < kBlockPixels; ++pixel) {
      out.at(pixel).at(channel)
        = static_cast<std::uint8_t>(palette.at((indices >> (pixel * 3U)) & 7U));
    }
  }

  //! Decodes BC2's explicit 4-bit alpha.
  auto DecodeExplicitAlpha(
    const std::span<const std::byte> block, Block& out) noexcept -> void
  {
    auto alpha = std::uint64_t {};
    std::memcpy(&alpha, block.subspan(0U, sizeof(alpha)).data(), sizeof(alpha));
    for (std::size_t pixel = 0U; pixel < kBlockPixels; ++pixel) {
      out.at(pixel)[3]
        = static_cast<std::uint8_t>(((alpha >> (pixel * 4U)) & 0xFU) * 17U);
    }
  }

  //! The blue of a normal whose x and y a BC5 block holds.
  [[nodiscard]] auto ImpliedNormalZ(const Rgba8& normal) noexcept
    -> std::uint8_t
  {
    const auto nx = (static_cast<float>(normal[0]) / 127.5F) - 1.0F;
    const auto ny = (static_cast<float>(normal[1]) / 127.5F) - 1.0F;
    const auto nz = std::sqrt(std::max(0.0F, 1.0F - (nx * nx) - (ny * ny)));
    return static_cast<std::uint8_t>(std::lround((nz + 1.0F) * 127.5F));
  }

  [[nodiscard]] constexpr auto BlockSize(const Encoding encoding) noexcept
    -> std::size_t
  {
    return encoding == Encoding::kBc1 || encoding == Encoding::kBc4 ? 8U : 16U;
  }

  auto DecodeBlock(const std::span<const std::byte> block,
    const Encoding encoding, Block& out) noexcept -> void
  {
    constexpr auto kOpaque = Rgba8 { 0U, 0U, 0U, 255U };
    out.fill(kOpaque);
    switch (encoding) {
    case Encoding::kBc1:
      DecodeColorBlock(block, true, out);
      break;
    case Encoding::kBc2:
      DecodeColorBlock(block.subspan(8U), false, out);
      DecodeExplicitAlpha(block, out);
      break;
    case Encoding::kBc3:
      DecodeColorBlock(block.subspan(8U), false, out);
      DecodeChannelBlock(block, 3U, out);
      break;
    case Encoding::kBc4:
      DecodeChannelBlock(block, 0U, out);
      for (auto& pixel : out) {
        pixel[1] = pixel[0];
        pixel[2] = pixel[0];
      }
      break;
    case Encoding::kBc5:
      DecodeChannelBlock(block, 0U, out);
      DecodeChannelBlock(block.subspan(8U), 1U, out);
      for (auto& pixel : out) {
        pixel[2] = ImpliedNormalZ(pixel);
      }
      break;
    case Encoding::kRgba8:
    case Encoding::kBgra8:
      break;
    }
  }

  [[nodiscard]] auto DecodeBlocks(const std::span<const std::byte> data,
    const Extent extent, const Encoding encoding)
    -> std::optional<std::vector<std::byte>>
  {
    const auto [width, height] = extent;
    const auto blocks_wide = (std::size_t { width } + 3U) / kBlockExtent;
    const auto blocks_high = (std::size_t { height } + 3U) / kBlockExtent;
    const auto block_size = BlockSize(encoding);
    if (data.size() / block_size / blocks_wide < blocks_high) {
      return std::nullopt;
    }
    const auto row_pitch = std::size_t { width } * kBytesPerPixel;
    auto pixels = std::vector<std::byte>(row_pitch * height);
    auto decoded = Block {};
    for (std::size_t row = 0U; row < blocks_high; ++row) {
      for (std::size_t column = 0U; column < blocks_wide; ++column) {
        const auto at = ((row * blocks_wide) + column) * block_size;
        DecodeBlock(data.subspan(at, block_size), encoding, decoded);
        const auto rows
          = std::min<std::size_t>(kBlockExtent, height - (row * kBlockExtent));
        const auto columns = std::min<std::size_t>(
          kBlockExtent, width - (column * kBlockExtent));
        for (std::size_t y = 0U; y < rows; ++y) {
          const auto target = (((row * kBlockExtent) + y) * row_pitch)
            + (column * kBlockExtent * kBytesPerPixel);
          std::memcpy(std::span(pixels).subspan(target).data(),
            std::span(decoded).subspan(y * kBlockExtent).data(),
            columns * kBytesPerPixel);
        }
      }
    }
    return pixels;
  }

  [[nodiscard]] auto CopyPixels(const std::span<const std::byte> data,
    const Extent extent, const Encoding encoding)
    -> std::optional<std::vector<std::byte>>
  {
    const auto [width, height] = extent;
    const auto row_pitch = std::size_t { width } * kBytesPerPixel;
    if (data.size() / row_pitch < height) {
      return std::nullopt;
    }
    const auto source = data.subspan(0U, row_pitch * height);
    auto pixels = std::vector<std::byte>(source.begin(), source.end());
    if (encoding == Encoding::kBgra8) {
      for (std::size_t at = 0U; at < pixels.size(); at += kBytesPerPixel) {
        std::swap(pixels.at(at), pixels.at(at + 2U));
      }
    }
    return pixels;
  }

  auto FlipRows(std::vector<std::byte>& pixels, const std::size_t row_pitch)
    -> void
  {
    const auto rows = pixels.size() / row_pitch;
    auto scratch = std::vector<std::byte>(row_pitch);
    const auto view = std::span(pixels);
    for (std::size_t row = 0U; row < rows / 2U; ++row) {
      const auto top = view.subspan(row * row_pitch, row_pitch);
      const auto bottom
        = view.subspan((rows - 1U - row) * row_pitch, row_pitch);
      std::ranges::copy(top, scratch.begin());
      std::ranges::copy(bottom, top.begin());
      std::ranges::copy(scratch, bottom.begin());
    }
  }

} // namespace

auto IsDdsSignature(const std::span<const std::byte> bytes) noexcept -> bool
{
  return bytes.size() >= kMagicSize
    && ReadU32(bytes, 0U) == FourCc('D', 'D', 'S', ' ');
}

auto DecodeDdsToScratchImage(const std::span<const std::byte> bytes,
  const bool flip_y) -> Result<ScratchImage, TextureImportError>
{
  if (!IsDdsSignature(bytes) || bytes.size() < kBaseDataOffset
    || ReadU32(bytes, kHeaderSizeField) != kHeaderSize
    || ReadU32(bytes, kPixelFormatSizeField) != kPixelFormatSize) {
    return Err(TextureImportError::kCorruptedData);
  }
  const auto width = ReadU32(bytes, kWidthField);
  const auto height = ReadU32(bytes, kHeightField);
  if (width == 0U || height == 0U || width > kLargestExtent
    || height > kLargestExtent) {
    return Err(TextureImportError::kCorruptedData);
  }
  const auto caps2 = ReadU32(bytes, kCaps2Field);
  if ((caps2 & (kCaps2Cube | kCaps2Volume)) != 0U
    || ReadU32(bytes, kDepthField) > 1U) {
    return Err(TextureImportError::kUnsupportedFormat);
  }

  const auto flags = ReadU32(bytes, kPixelFormatFlagsField);
  const auto four_cc = ReadU32(bytes, kFourCcField);
  const bool has_four_cc = (flags & kPixelFormatFourCc) != 0U;
  const bool has_dx10_header
    = has_four_cc && four_cc == FourCc('D', 'X', '1', '0');
  auto data_offset = kBaseDataOffset;
  auto encoding = std::optional<Encoding> {};
  if (has_dx10_header) {
    if (bytes.size() < kBaseDataOffset + kDx10HeaderSize) {
      return Err(TextureImportError::kCorruptedData);
    }
    data_offset += kDx10HeaderSize;
    if (ReadU32(bytes, kDimensionField) != kDimensionTexture2D
      || (ReadU32(bytes, kMiscFlagField) & kMiscTextureCube) != 0U
      || ReadU32(bytes, kArraySizeField) > 1U) {
      return Err(TextureImportError::kUnsupportedFormat);
    }
    encoding = EncodingOfDxgi(ReadU32(bytes, kDxgiFormatField));
  } else if (has_four_cc) {
    encoding = EncodingOfFourCc(four_cc);
  } else if ((flags & kPixelFormatRgb) != 0U) {
    encoding = EncodingOfMasks(bytes);
  }
  if (!encoding.has_value()) {
    return Err(TextureImportError::kUnsupportedFormat);
  }

  const auto data = bytes.subspan(data_offset);
  const bool uncompressed
    = *encoding == Encoding::kRgba8 || *encoding == Encoding::kBgra8;
  const auto extent = Extent { .width = width, .height = height };
  auto pixels = uncompressed ? CopyPixels(data, extent, *encoding)
                             : DecodeBlocks(data, extent, *encoding);
  if (!pixels.has_value()) {
    return Err(TextureImportError::kCorruptedData);
  }
  const auto row_pitch = width * static_cast<std::uint32_t>(kBytesPerPixel);
  if (flip_y) {
    FlipRows(*pixels, row_pitch);
  }
  return Ok(ScratchImage::CreateFromData(
    width, height, Format::kRGBA8UNorm, row_pitch, std::move(*pixels)));
}

} // namespace oxygen::content::import
