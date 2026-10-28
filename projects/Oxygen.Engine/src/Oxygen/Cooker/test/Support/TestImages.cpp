//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <ktx.h>
#include <webp/encode.h>
#include <webp/types.h>

#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/TempDir.h>
#include <Oxygen/Cooker/test/Support/TestImages.h>
#include <Oxygen/Testing/GTest.h>

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
  // VK_FORMAT_R8G8B8A8_UNORM; libktx does not install the Vulkan format enum.
  constexpr auto kVkFormatRgba8Unorm = ktx_uint32_t { 37U };
  constexpr auto kRgbaBytes = uint32_t { 4U };

  struct KtxTextureDeleter final {
    auto operator()(ktxTexture2* texture) const noexcept -> void
    {
      ktxTexture2_Destroy(texture);
    }
  };

  struct WebpDeleter final {
    auto operator()(uint8_t* bytes) const noexcept -> void { WebPFree(bytes); }
  };

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
    const auto bits = static_cast<uint32_t>(value);
    push_u8(static_cast<uint8_t>(bits & 0xFFU));
    push_u8(static_cast<uint8_t>((bits >> 8U) & 0xFFU));
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

auto MakeWebp(const uint32_t width, const uint32_t height,
  const std::span<const std::byte> rgba) -> std::vector<std::byte>
{
  uint8_t* encoded = nullptr;
  const auto size = WebPEncodeLosslessRGBA(
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    reinterpret_cast<const uint8_t*>(rgba.data()), static_cast<int>(width),
    static_cast<int>(height), static_cast<int>(width * kRgbaBytes), &encoded);
  const auto owner = std::unique_ptr<uint8_t, WebpDeleter>(encoded);
  EXPECT_NE(size, 0U) << "libwebp refused to encode the image";
  const auto bytes = std::as_bytes(std::span(owner.get(), size));
  return { bytes.begin(), bytes.end() };
}

auto MakeKtx2(const Ktx2Image& image) -> std::vector<std::byte>
{
  auto info = ktxTextureCreateInfo {};
  info.vkFormat = kVkFormatRgba8Unorm;
  info.baseWidth = image.width;
  info.baseHeight = image.height;
  info.baseDepth = 1U;
  info.numDimensions = 2U;
  info.numLevels = 1U;
  info.numLayers = 1U;
  info.numFaces = image.faces;
  info.isArray = KTX_FALSE;
  info.generateMipmaps = KTX_FALSE;
  ktxTexture2* created = nullptr;
  if (ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &created)
    != KTX_SUCCESS) {
    ADD_FAILURE() << "libktx refused to create the texture";
    return {};
  }
  const auto texture = std::unique_ptr<ktxTexture2, KtxTextureDeleter>(created);

  auto pixels = std::vector<std::byte>(image.rgba.begin(), image.rgba.end());
  if (image.bottom_up) {
    const auto row_bytes = std::size_t { image.width } * kRgbaBytes;
    auto rows = std::vector<std::byte> {};
    rows.reserve(pixels.size());
    for (const auto row : std::views::reverse(
           std::views::iota(std::size_t { 0 }, std::size_t { image.height }))) {
      const auto source = std::span(pixels).subspan(row * row_bytes, row_bytes);
      rows.insert(rows.end(), source.begin(), source.end());
    }
    pixels = std::move(rows);
    constexpr auto kBottomUp = std::to_array("ru");
    if (ktxHashList_AddKVPair(&texture->kvDataHead, KTX_ORIENTATION_KEY,
          static_cast<unsigned int>(kBottomUp.size()), kBottomUp.data())
      != KTX_SUCCESS) {
      ADD_FAILURE() << "libktx refused the orientation";
      return {};
    }
  }
  const auto storage
    = std::as_writable_bytes(std::span(texture->pData, texture->dataSize));
  for (const auto face : std::views::iota(0U, image.faces)) {
    auto offset = ktx_size_t { 0 };
    if (ktxTexture2_GetImageOffset(texture.get(), 0U, 0U, face, &offset)
        != KTX_SUCCESS
      || offset + pixels.size() > storage.size()) {
      ADD_FAILURE() << "libktx has no storage for face " << face;
      return {};
    }
    std::ranges::copy(pixels, storage.subspan(offset).begin());
  }

  if (image.encoding != Ktx2Encoding::kRaw) {
    auto params = ktxBasisParams {};
    params.structSize = sizeof(params);
    params.uastc
      = image.encoding == Ktx2Encoding::kUastc ? KTX_TRUE : KTX_FALSE;
    params.threadCount = 1U;
    if (ktxTexture2_CompressBasisEx(texture.get(), &params) != KTX_SUCCESS) {
      ADD_FAILURE() << "libktx refused to compress the texture";
      return {};
    }
  }

  // libktx writes to memory with malloc; a file keeps ownership with libktx.
  const auto directory = ScopedTempDir {};
  const auto path = directory.Path() / "texture.ktx2";
  if (ktxTexture2_WriteToNamedFile(texture.get(), path.string().c_str())
    != KTX_SUCCESS) {
    ADD_FAILURE() << "libktx refused to write the texture";
    return {};
  }
  return ReadBytes(path);
}

} // namespace oxygen::cooker::test
