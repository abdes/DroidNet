//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/ImageDecode.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#include <Oxygen/Content/Import/stb/stb_image.h>

#include <tinyexr.h>

namespace oxygen::content::import {

namespace {

  //=== String Utilities ===--------------------------------------------------//

  [[nodiscard]] auto ToLower(std::string_view str) -> std::string
  {
    std::string result;
    result.reserve(str.size());
    for (const char ch : str) {
      result.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
  }

  //=== Legacy stb_image RGBA8 Decoder ===------------------------------------//

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

  //=== Image Y-Flip Utility ===----------------------------------------------//

  void FlipImageY(std::span<std::byte> pixels, uint32_t width, uint32_t height,
    uint32_t bytes_per_pixel)
  {
    const auto row_bytes = static_cast<size_t>(width) * bytes_per_pixel;
    std::vector<std::byte> row_buffer(row_bytes);

    for (uint32_t y = 0; y < height / 2; ++y) {
      const auto top_offset = static_cast<size_t>(y) * row_bytes;
      const auto bottom_offset
        = static_cast<size_t>(height - 1 - y) * row_bytes;

      std::memcpy(row_buffer.data(), pixels.data() + top_offset, row_bytes);
      std::memcpy(
        pixels.data() + top_offset, pixels.data() + bottom_offset, row_bytes);
      std::memcpy(pixels.data() + bottom_offset, row_buffer.data(), row_bytes);
    }
  }

  //=== EXR Decoder ===-------------------------------------------------------//

  //! RAII wrapper for EXRHeader.
  struct EXRHeaderGuard {
    EXRHeader header {};
    bool initialized { false };

    EXRHeaderGuard() { InitEXRHeader(&header); }
    ~EXRHeaderGuard()
    {
      if (initialized) {
        FreeEXRHeader(&header);
      }
    }

    EXRHeaderGuard(const EXRHeaderGuard&) = delete;
    auto operator=(const EXRHeaderGuard&) -> EXRHeaderGuard& = delete;
    EXRHeaderGuard(EXRHeaderGuard&&) = delete;
    auto operator=(EXRHeaderGuard&&) -> EXRHeaderGuard& = delete;
  };

  //! RAII wrapper for EXRImage.
  struct EXRImageGuard {
    EXRImage image {};
    bool initialized { false };

    EXRImageGuard() { InitEXRImage(&image); }
    ~EXRImageGuard()
    {
      if (initialized) {
        FreeEXRImage(&image);
      }
    }

    EXRImageGuard(const EXRImageGuard&) = delete;
    auto operator=(const EXRImageGuard&) -> EXRImageGuard& = delete;
    EXRImageGuard(EXRImageGuard&&) = delete;
    auto operator=(EXRImageGuard&&) -> EXRImageGuard& = delete;
  };

  //! Try to decode EXR using the simple API first (single-part).
  [[nodiscard]] auto TryDecodeExrSimple(std::span<const std::byte> bytes,
    float** out_rgba, int* width, int* height) -> int
  {
    const char* err = nullptr;
    const int result = LoadEXRFromMemory(out_rgba, width, height,
      reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), &err);

    if (err != nullptr) {
      FreeEXRErrorMessage(err);
    }
    return result;
  }

  //! Decode multipart EXR using the low-level API.
  /*!
   For multipart EXR files, we load the first part that contains image data.
   This handles files where the simple LoadEXRFromMemory API fails.
  */
  [[nodiscard]] auto DecodeExrMultipart(
    std::span<const std::byte> bytes, const DecodeOptions& options)
    -> oxygen::Result<ScratchImage, TextureImportError>
  {
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    const auto size = bytes.size();
    const char* err = nullptr;

    // Parse EXR version to check for multipart
    EXRVersion version {};
    int ret = ParseEXRVersionFromMemory(&version, data, size);
    if (ret != TINYEXR_SUCCESS) {
      return oxygen::Err(TextureImportError::kCorruptedData);
    }

    // For multipart files, we need to use the multipart API
    if (version.multipart != 0) {
      // Parse all headers
      EXRHeader** headers = nullptr;
      int num_headers = 0;

      ret = ParseEXRMultipartHeaderFromMemory(
        &headers, &num_headers, &version, data, size, &err);
      if (ret != TINYEXR_SUCCESS) {
        if (err != nullptr) {
          FreeEXRErrorMessage(err);
        }
        return oxygen::Err(TextureImportError::kCorruptedData);
      }

      // Find first valid image part (skip deep/tiled for now)
      int part_idx = -1;
      for (int i = 0; i < num_headers; ++i) {
        if (headers[i]->tiled == 0) {
          part_idx = i;
          break;
        }
      }

      if (part_idx < 0) {
        // No suitable part found
        for (int i = 0; i < num_headers; ++i) {
          FreeEXRHeader(headers[i]);
          free(headers[i]);
        }
        free(headers);
        return oxygen::Err(TextureImportError::kUnsupportedFormat);
      }

      // Request float output for all channels in all headers
      for (int h = 0; h < num_headers; ++h) {
        for (int c = 0; c < headers[h]->num_channels; ++c) {
          headers[h]->requested_pixel_types[c] = TINYEXR_PIXELTYPE_FLOAT;
        }
      }

      // Load all images
      std::vector<EXRImage> images(static_cast<size_t>(num_headers));
      for (int i = 0; i < num_headers; ++i) {
        InitEXRImage(&images[static_cast<size_t>(i)]);
      }

      ret = LoadEXRMultipartImageFromMemory(images.data(),
        const_cast<const EXRHeader**>(headers),
        static_cast<unsigned int>(num_headers), data, size, &err);

      if (ret != TINYEXR_SUCCESS) {
        if (err != nullptr) {
          FreeEXRErrorMessage(err);
        }
        for (int i = 0; i < num_headers; ++i) {
          FreeEXRImage(&images[static_cast<size_t>(i)]);
          FreeEXRHeader(headers[i]);
          free(headers[i]);
        }
        free(headers);
        return oxygen::Err(TextureImportError::kDecodeFailed);
      }

      // Get the selected part
      const EXRHeader& hdr = *headers[part_idx];
      const EXRImage& img = images[static_cast<size_t>(part_idx)];
      const int width = img.width;
      const int height = img.height;

      // Find RGBA channels
      int idx_r = -1, idx_g = -1, idx_b = -1, idx_a = -1;
      for (int c = 0; c < hdr.num_channels; ++c) {
        const char* name = hdr.channels[c].name;
        if (name[0] == 'R' && name[1] == '\0') {
          idx_r = c;
        } else if (name[0] == 'G' && name[1] == '\0') {
          idx_g = c;
        } else if (name[0] == 'B' && name[1] == '\0') {
          idx_b = c;
        } else if (name[0] == 'A' && name[1] == '\0') {
          idx_a = c;
        }
      }

      // Must have at least RGB
      if (idx_r < 0 || idx_g < 0 || idx_b < 0) {
        for (int i = 0; i < num_headers; ++i) {
          FreeEXRImage(&images[static_cast<size_t>(i)]);
          FreeEXRHeader(headers[i]);
          free(headers[i]);
        }
        free(headers);
        return oxygen::Err(TextureImportError::kUnsupportedFormat);
      }

      // Assemble RGBA float data
      const auto pixel_count
        = static_cast<size_t>(width) * static_cast<size_t>(height);
      const auto byte_size = pixel_count * 4 * sizeof(float);
      std::vector<std::byte> pixel_data(byte_size);
      auto* out = reinterpret_cast<float*>(pixel_data.data());

      const auto* r_data = reinterpret_cast<const float*>(
        img.images[static_cast<size_t>(idx_r)]);
      const auto* g_data = reinterpret_cast<const float*>(
        img.images[static_cast<size_t>(idx_g)]);
      const auto* b_data = reinterpret_cast<const float*>(
        img.images[static_cast<size_t>(idx_b)]);
      const float* a_data = (idx_a >= 0)
        ? reinterpret_cast<const float*>(img.images[static_cast<size_t>(idx_a)])
        : nullptr;

      for (size_t i = 0; i < pixel_count; ++i) {
        out[i * 4 + 0] = r_data[i];
        out[i * 4 + 1] = g_data[i];
        out[i * 4 + 2] = b_data[i];
        out[i * 4 + 3] = a_data != nullptr ? a_data[i] : 1.0F;
      }

      // Cleanup
      for (int i = 0; i < num_headers; ++i) {
        FreeEXRImage(&images[static_cast<size_t>(i)]);
        FreeEXRHeader(headers[i]);
        free(headers[i]);
      }
      free(headers);

      // Apply Y-flip if requested
      if (options.flip_y) {
        FlipImageY(pixel_data, static_cast<uint32_t>(width),
          static_cast<uint32_t>(height), 16);
      }

      return oxygen::Ok(
        ScratchImage::CreateFromData(static_cast<uint32_t>(width),
          static_cast<uint32_t>(height), Format::kRGBA32Float,
          static_cast<uint32_t>(width) * 16, std::move(pixel_data)));
    }

    // Single-part: use simpler header/image loading
    EXRHeaderGuard hdr_guard;
    ret
      = ParseEXRHeaderFromMemory(&hdr_guard.header, &version, data, size, &err);
    if (ret != TINYEXR_SUCCESS) {
      if (err != nullptr) {
        FreeEXRErrorMessage(err);
      }
      return oxygen::Err(TextureImportError::kCorruptedData);
    }
    hdr_guard.initialized = true;

    // Request float output for all channels
    for (int i = 0; i < hdr_guard.header.num_channels; ++i) {
      hdr_guard.header.requested_pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT;
    }

    EXRImageGuard img_guard;
    ret = LoadEXRImageFromMemory(
      &img_guard.image, &hdr_guard.header, data, size, &err);
    if (ret != TINYEXR_SUCCESS) {
      if (err != nullptr) {
        FreeEXRErrorMessage(err);
      }
      return oxygen::Err(TextureImportError::kDecodeFailed);
    }
    img_guard.initialized = true;

    const int width = img_guard.image.width;
    const int height = img_guard.image.height;

    // Find RGBA channels
    int idx_r = -1, idx_g = -1, idx_b = -1, idx_a = -1;
    for (int c = 0; c < hdr_guard.header.num_channels; ++c) {
      const char* name = hdr_guard.header.channels[c].name;
      if (name[0] == 'R' && name[1] == '\0') {
        idx_r = c;
      } else if (name[0] == 'G' && name[1] == '\0') {
        idx_g = c;
      } else if (name[0] == 'B' && name[1] == '\0') {
        idx_b = c;
      } else if (name[0] == 'A' && name[1] == '\0') {
        idx_a = c;
      }
    }

    if (idx_r < 0 || idx_g < 0 || idx_b < 0) {
      return oxygen::Err(TextureImportError::kUnsupportedFormat);
    }

    // Assemble RGBA float data
    const auto pixel_count
      = static_cast<size_t>(width) * static_cast<size_t>(height);
    const auto byte_size = pixel_count * 4 * sizeof(float);
    std::vector<std::byte> pixel_data(byte_size);
    auto* out = reinterpret_cast<float*>(pixel_data.data());

    const auto* r_data = reinterpret_cast<const float*>(
      img_guard.image.images[static_cast<size_t>(idx_r)]);
    const auto* g_data = reinterpret_cast<const float*>(
      img_guard.image.images[static_cast<size_t>(idx_g)]);
    const auto* b_data = reinterpret_cast<const float*>(
      img_guard.image.images[static_cast<size_t>(idx_b)]);
    const float* a_data = (idx_a >= 0)
      ? reinterpret_cast<const float*>(
          img_guard.image.images[static_cast<size_t>(idx_a)])
      : nullptr;

    for (size_t i = 0; i < pixel_count; ++i) {
      out[i * 4 + 0] = r_data[i];
      out[i * 4 + 1] = g_data[i];
      out[i * 4 + 2] = b_data[i];
      out[i * 4 + 3] = a_data != nullptr ? a_data[i] : 1.0F;
    }

    // Apply Y-flip if requested
    if (options.flip_y) {
      FlipImageY(pixel_data, static_cast<uint32_t>(width),
        static_cast<uint32_t>(height), 16);
    }

    return oxygen::Ok(ScratchImage::CreateFromData(static_cast<uint32_t>(width),
      static_cast<uint32_t>(height), Format::kRGBA32Float,
      static_cast<uint32_t>(width) * 16, std::move(pixel_data)));
  }

  [[nodiscard]] auto DecodeExrToScratchImage(
    std::span<const std::byte> bytes, const DecodeOptions& options)
    -> oxygen::Result<ScratchImage, TextureImportError>
  {
    // Try simple API first (faster for single-part files)
    float* out_rgba = nullptr;
    int width = 0;
    int height = 0;

    const int result = TryDecodeExrSimple(bytes, &out_rgba, &width, &height);

    if (result == TINYEXR_SUCCESS && out_rgba != nullptr) {
      // Simple API succeeded
      const auto pixel_count
        = static_cast<size_t>(width) * static_cast<size_t>(height);
      const auto byte_size = pixel_count * 4 * sizeof(float);

      std::vector<std::byte> pixel_data(byte_size);
      std::memcpy(pixel_data.data(), out_rgba, byte_size);

      free(out_rgba);

      if (options.flip_y) {
        FlipImageY(pixel_data, static_cast<uint32_t>(width),
          static_cast<uint32_t>(height), 16);
      }

      return oxygen::Ok(
        ScratchImage::CreateFromData(static_cast<uint32_t>(width),
          static_cast<uint32_t>(height), Format::kRGBA32Float,
          static_cast<uint32_t>(width) * 16, std::move(pixel_data)));
    }

    // Simple API failed - try multipart/low-level API
    return DecodeExrMultipart(bytes, options);
  }

  //=== HDR (Radiance) Decoder ===--------------------------------------------//

  [[nodiscard]] auto DecodeHdrToScratchImage(
    std::span<const std::byte> bytes, const DecodeOptions& options)
    -> oxygen::Result<ScratchImage, TextureImportError>
  {
    int width = 0;
    int height = 0;
    int channels_in_file = 0;

    // stbi_loadf handles Radiance HDR (.hdr) format
    float* decoded = stbi_loadf_from_memory(
      reinterpret_cast<const unsigned char*>(bytes.data()),
      static_cast<int>(bytes.size()), &width, &height, &channels_in_file,
      STBI_rgb_alpha); // Force RGBA

    if (decoded == nullptr) {
      return oxygen::Err(TextureImportError::kDecodeFailed);
    }

    const auto pixel_count
      = static_cast<size_t>(width) * static_cast<size_t>(height);
    const auto byte_size = pixel_count * 4 * sizeof(float);

    std::vector<std::byte> pixel_data(byte_size);
    std::memcpy(pixel_data.data(), decoded, byte_size);

    stbi_image_free(decoded);

    // Apply Y-flip if requested
    if (options.flip_y) {
      FlipImageY(pixel_data, static_cast<uint32_t>(width),
        static_cast<uint32_t>(height), 16);
    }

    return oxygen::Ok(ScratchImage::CreateFromData(static_cast<uint32_t>(width),
      static_cast<uint32_t>(height), Format::kRGBA32Float,
      static_cast<uint32_t>(width) * 16, std::move(pixel_data)));
  }

  //=== LDR Decoder (stb_image RGBA8) ===-------------------------------------//

  [[nodiscard]] auto DecodeLdrToScratchImage(
    std::span<const std::byte> bytes, const DecodeOptions& options)
    -> oxygen::Result<ScratchImage, TextureImportError>
  {
    int width = 0;
    int height = 0;
    int channels_in_file = 0;

    const int desired_channels = options.force_rgba ? STBI_rgb_alpha : 0;

    stbi_uc* decoded = stbi_load_from_memory(
      reinterpret_cast<const unsigned char*>(bytes.data()),
      static_cast<int>(bytes.size()), &width, &height, &channels_in_file,
      desired_channels);

    if (decoded == nullptr) {
      return oxygen::Err(TextureImportError::kDecodeFailed);
    }

    const int actual_channels
      = (desired_channels != 0) ? desired_channels : channels_in_file;
    const auto pixel_count
      = static_cast<size_t>(width) * static_cast<size_t>(height);
    const auto byte_size = pixel_count * static_cast<size_t>(actual_channels);

    std::vector<std::byte> pixel_data;
    if (actual_channels == 3) {
      const auto rgba_bytes = pixel_count * 4U;
      pixel_data.resize(rgba_bytes);
      const auto* src = decoded;
      auto* dst = reinterpret_cast<uint8_t*>(pixel_data.data());
      for (size_t i = 0; i < pixel_count; ++i) {
        dst[i * 4 + 0] = src[i * 3 + 0];
        dst[i * 4 + 1] = src[i * 3 + 1];
        dst[i * 4 + 2] = src[i * 3 + 2];
        dst[i * 4 + 3] = 255U;
      }
    } else {
      pixel_data.resize(byte_size);
      std::memcpy(pixel_data.data(), decoded, byte_size);
    }

    stbi_image_free(decoded);

    // Apply Y-flip if requested
    if (options.flip_y) {
      const uint32_t flip_channels
        = (actual_channels == 3) ? 4U : static_cast<uint32_t>(actual_channels);
      FlipImageY(pixel_data, static_cast<uint32_t>(width),
        static_cast<uint32_t>(height), flip_channels);
    }

    // Determine format based on channel count
    Format format;
    switch (actual_channels) {
    case 1:
      format = Format::kR8UNorm;
      break;
    case 2:
      format = Format::kRG8UNorm;
      break;
    case 4:
      format = Format::kRGBA8UNorm;
      break;
    default:
      // 3-channel (RGB) - expand to RGBA8
      format = Format::kRGBA8UNorm;
      break;
    }

    return oxygen::Ok(ScratchImage::CreateFromData(static_cast<uint32_t>(width),
      static_cast<uint32_t>(height), format,
      static_cast<uint32_t>(width)
        * static_cast<uint32_t>((actual_channels == 3) ? 4 : actual_channels),
      std::move(pixel_data)));
  }

  //=== File Reading Utility ===----------------------------------------------//

  [[nodiscard]] auto ReadFileBytes(const std::filesystem::path& path)
    -> oxygen::Result<std::vector<std::byte>, TextureImportError>
  {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
      return oxygen::Err(TextureImportError::kFileNotFound);
    }

    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size <= 0) {
      return oxygen::Err(TextureImportError::kFileReadFailed);
    }

    std::vector<std::byte> bytes;
    bytes.resize(static_cast<size_t>(size));

    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));

    if (!file) {
      return oxygen::Err(TextureImportError::kFileReadFailed);
    }

    return oxygen::Ok(std::move(bytes));
  }

} // namespace

//=== Legacy API Implementation ===-------------------------------------------//

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

//=== Format Detection ===----------------------------------------------------//

auto IsExrSignature(std::span<const std::byte> bytes) noexcept -> bool
{
  // OpenEXR magic number: 0x76 0x2F 0x31 0x01
  if (bytes.size() < 4) {
    return false;
  }

  return bytes[0] == std::byte { 0x76 } && bytes[1] == std::byte { 0x2F }
  && bytes[2] == std::byte { 0x31 } && bytes[3] == std::byte { 0x01 };
}

auto IsHdrSignature(std::span<const std::byte> bytes) noexcept -> bool
{
  // Radiance HDR signature: "#?RADIANCE" or "#?RGBE"
  if (bytes.size() < 2) {
    return false;
  }

  // Check for "#?" prefix
  if (bytes[0] != std::byte { '#' } || bytes[1] != std::byte { '?' }) {
    return false;
  }

  // Check for "RADIANCE" or "RGBE" after "#?"
  if (bytes.size() >= 10) {
    const char* data = reinterpret_cast<const char*>(bytes.data());
    if (std::strncmp(data + 2, "RADIANCE", 8) == 0) {
      return true;
    }
  }

  if (bytes.size() >= 6) {
    const char* data = reinterpret_cast<const char*>(bytes.data());
    if (std::strncmp(data + 2, "RGBE", 4) == 0) {
      return true;
    }
  }

  return false;
}

auto IsHdrFormat(
  std::span<const std::byte> bytes, std::string_view extension) noexcept -> bool
{
  // Check signatures first
  if (IsExrSignature(bytes) || IsHdrSignature(bytes)) {
    return true;
  }

  // Check extension hint
  if (!extension.empty()) {
    const auto ext_lower = ToLower(extension);
    if (ext_lower == ".exr" || ext_lower == ".hdr") {
      return true;
    }
  }

  // Also check stb_image's HDR detection
  if (!bytes.empty()
    && bytes.size() <= static_cast<size_t>((std::numeric_limits<int>::max)())) {
    if (stbi_is_hdr_from_memory(
          reinterpret_cast<const unsigned char*>(bytes.data()),
          static_cast<int>(bytes.size()))
      != 0) {
      return true;
    }
  }

  return false;
}

//=== Unified Decode API ===--------------------------------------------------//

auto DecodeToScratchImage(
  std::span<const std::byte> bytes, const DecodeOptions& options)
  -> oxygen::Result<ScratchImage, TextureImportError>
{
  if (bytes.empty()) {
    return oxygen::Err(TextureImportError::kCorruptedData);
  }

  if (bytes.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
    return oxygen::Err(TextureImportError::kOutOfMemory);
  }

  // Format detection priority:
  // 1. EXR signature (magic bytes)
  if (IsExrSignature(bytes)) {
    return DecodeExrToScratchImage(bytes, options);
  }

  // 2. HDR signature or extension hint
  const auto ext_lower = ToLower(options.extension_hint);
  if (IsHdrSignature(bytes) || ext_lower == ".hdr") {
    return DecodeHdrToScratchImage(bytes, options);
  }

  // 3. Check extension hint for EXR
  if (ext_lower == ".exr") {
    return DecodeExrToScratchImage(bytes, options);
  }

  // 4. Fallback to LDR decoder (stb_image)
  return DecodeLdrToScratchImage(bytes, options);
}

auto DecodeToScratchImage(
  const std::filesystem::path& path, const DecodeOptions& options)
  -> oxygen::Result<ScratchImage, TextureImportError>
{
  // Read file
  auto bytes_result = ReadFileBytes(path);
  if (!bytes_result) {
    return oxygen::Err(bytes_result.error());
  }

  // Create options with extension hint
  DecodeOptions opts_with_ext = options;
  if (opts_with_ext.extension_hint.empty()) {
    opts_with_ext.extension_hint = path.extension().string();
  }

  return DecodeToScratchImage(bytes_result.value(), opts_with_ext);
}

} // namespace oxygen::content::import
