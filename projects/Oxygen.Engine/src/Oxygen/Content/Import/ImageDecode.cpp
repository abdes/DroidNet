//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/ImageDecode.h>

#include <fstream>
#include <limits>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#include <Oxygen/Content/Import/stb/stb_image.h>

namespace oxygen::content::import {

namespace {

  [[nodiscard]] auto DecodeWithStb_(
    const unsigned char* bytes, const int size_bytes) -> ImageDecodeResult
  {
    if (bytes == nullptr || size_bytes <= 0) {
      return ImageDecodeResult {
        .image = std::nullopt,
        .error = "empty input",
      };
    }

    int width = 0;
    int height = 0;
    int channels_in_file = 0;

    stbi_uc* decoded = stbi_load_from_memory(
      bytes, size_bytes, &width, &height, &channels_in_file, STBI_rgb_alpha);

    if (decoded == nullptr) {
      const char* reason = stbi_failure_reason();
      return ImageDecodeResult {
        .image = std::nullopt,
        .error
        = (reason != nullptr) ? std::string(reason) : "stb decode failed",
      };
    }

    const auto decoded_size = static_cast<size_t>(width)
      * static_cast<size_t>(height) * static_cast<size_t>(4);

    std::vector<std::byte> pixels;
    pixels.resize(decoded_size);

    const auto* src = reinterpret_cast<const std::byte*>(decoded);
    std::copy_n(src, decoded_size, pixels.data());

    stbi_image_free(decoded);

    return ImageDecodeResult {
    .image = DecodedImageRgba8 {
      .width = static_cast<uint32_t>(width),
      .height = static_cast<uint32_t>(height),
      .pixels = std::move(pixels),
    },
    .error = {},
  };
  }

} // namespace

auto DecodeImageRgba8FromMemory(std::span<const std::byte> bytes)
  -> ImageDecodeResult
{
  if (bytes.empty()) {
    return ImageDecodeResult {
      .image = std::nullopt,
      .error = "empty input",
    };
  }

  if (bytes.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
    return ImageDecodeResult {
      .image = std::nullopt,
      .error = "input too large for stb",
    };
  }

  return DecodeWithStb_(reinterpret_cast<const unsigned char*>(bytes.data()),
    static_cast<int>(bytes.size()));
}

auto DecodeImageRgba8FromFile(const std::filesystem::path& path)
  -> ImageDecodeResult
{
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return ImageDecodeResult {
      .image = std::nullopt,
      .error = "failed to open file",
    };
  }

  file.seekg(0, std::ios::end);
  const auto size = file.tellg();
  if (size <= 0) {
    return ImageDecodeResult {
      .image = std::nullopt,
      .error = "empty file",
    };
  }

  if (size > static_cast<std::streamoff>((std::numeric_limits<int>::max)())) {
    return ImageDecodeResult {
      .image = std::nullopt,
      .error = "file too large for stb",
    };
  }

  std::vector<std::byte> bytes;
  bytes.resize(static_cast<size_t>(size));

  file.seekg(0, std::ios::beg);
  file.read(reinterpret_cast<char*>(bytes.data()),
    static_cast<std::streamsize>(bytes.size()));

  if (!file) {
    return ImageDecodeResult {
      .image = std::nullopt,
      .error = "failed to read file",
    };
  }

  return DecodeImageRgba8FromMemory(bytes);
}

} // namespace oxygen::content::import
