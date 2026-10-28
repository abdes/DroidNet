//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Cooker/Import/Internal/ImageDecode.h>
#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Cooker/Import/TextureImportError.h>
#include <Oxygen/Core/Types/Format.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#include <ktx.h>
#include <spng.h>
#include <tinyexr.h>
#include <webp/decode.h>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Cooker/Import/Internal/stb/stb_image.h>

namespace oxygen::content::import {

namespace {

  //=== C Library Boundary ===------------------------------------------------//

  //! The bytes as the `unsigned char` buffer the C decoders read; `std::byte`
  //! and `unsigned char` may alias each other.
  [[nodiscard]] auto AsUChars(std::span<const std::byte> bytes) noexcept
    -> const unsigned char*
  {
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    return reinterpret_cast<const unsigned char*>(bytes.data());
  }

  //! The bytes as the `unsigned char` buffer the C decoders write.
  [[nodiscard]] auto AsWritableUChars(std::span<std::byte> bytes) noexcept
    -> unsigned char*
  {
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    return reinterpret_cast<unsigned char*>(bytes.data());
  }

  //! The bytes as the `char` buffer `std::istream::read` writes.
  [[nodiscard]] auto AsChars(std::span<std::byte> bytes) noexcept -> char*
  {
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    return reinterpret_cast<char*>(bytes.data());
  }

  //! Whether the bytes start with the ASCII text.
  [[nodiscard]] auto StartsWithText(std::span<const std::byte> bytes,
    const std::string_view text) noexcept -> bool
  {
    return bytes.size() >= text.size()
      && std::ranges::equal(bytes.first(text.size()), text,
        [](const std::byte byte, const char ch) -> bool {
          return byte == static_cast<std::byte>(ch);
        });
  }

  //! Frees an image stb_image allocated.
  struct StbiFree final {
    auto operator()(void* image) const noexcept -> void
    {
      stbi_image_free(image);
    }
  };

  template <typename T> using StbiImage = std::unique_ptr<T, StbiFree>;

  //! Frees memory tinyexr allocated with `malloc`.
  struct CFree final {
    auto operator()(void* memory) const noexcept -> void
    {
      std::free(memory); // NOLINT(*-no-malloc,*-owning-memory)
    }
  };

  template <typename T> using CBuffer = std::unique_ptr<T, CFree>;

  //! Copies `count` elements of a decoder's output into owned bytes.
  template <typename T>
  [[nodiscard]] auto CopyBytes(const T* data, const std::size_t count)
    -> std::vector<std::byte>
  {
    const auto bytes = std::as_bytes(std::span(data, count));
    return { bytes.begin(), bytes.end() };
  }

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

  [[nodiscard]] auto IsPngSignature(std::span<const std::byte> bytes) noexcept
    -> bool
  {
    constexpr std::array<std::byte, 8> kPngSignature {
      std::byte { 0x89 },
      std::byte { 0x50 },
      std::byte { 0x4E },
      std::byte { 0x47 },
      std::byte { 0x0D },
      std::byte { 0x0A },
      std::byte { 0x1A },
      std::byte { 0x0A },
    };
    return std::ranges::starts_with(bytes, kPngSignature);
  }

  //=== Legacy stb_image RGBA8 Decoder ===------------------------------------//

  [[nodiscard]] auto DecodeWithStb(std::span<const std::byte> bytes)
    -> ImageDecodeResult
  {
    int width = 0;
    int height = 0;
    int channels_in_file = 0;
    const auto decoded = StbiImage<stbi_uc>(
      stbi_load_from_memory(AsUChars(bytes), static_cast<int>(bytes.size()),
        &width, &height, &channels_in_file, STBI_rgb_alpha));
    if (decoded == nullptr) {
      const char* reason = stbi_failure_reason();
      return ImageDecodeResult {
        .image = std::nullopt,
        .error
        = (reason != nullptr) ? std::string(reason) : "stb decode failed",
      };
    }

    const auto decoded_size = static_cast<std::size_t>(width)
      * static_cast<std::size_t>(height) * std::size_t { 4 };
    return ImageDecodeResult {
      .image = DecodedImageRgba8 {
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
        .pixels = CopyBytes(decoded.get(), decoded_size),
      },
      .error = {},
    };
  }

  //=== Image Y-Flip Utility ===----------------------------------------------//

  //! Reverses the order of the rows of an image whose rows are `row_pitch`
  //! bytes apart.
  auto FlipImageY(std::span<std::byte> pixels, const std::size_t row_pitch)
    -> void
  {
    if (row_pitch == 0) {
      return;
    }
    const auto rows = pixels.size() / row_pitch;
    for (std::size_t top = 0; top < rows / 2; ++top) {
      const auto bottom = rows - 1 - top;
      std::ranges::swap_ranges(pixels.subspan(top * row_pitch, row_pitch),
        pixels.subspan(bottom * row_pitch, row_pitch));
    }
  }

  //=== RGBA32F Images ===----------------------------------------------------//

  //! The size of a decoded image, in pixels.
  using ImageExtent = Extent<uint32_t>;

  [[nodiscard]] auto PixelCount(const ImageExtent extent) noexcept
    -> std::size_t
  {
    return static_cast<std::size_t>(extent.width)
      * static_cast<std::size_t>(extent.height);
  }

  constexpr auto kRgba32FBytesPerPixel
    = static_cast<uint32_t>(4U * sizeof(float));

  //! The interleaved RGBA32F pixels of an image, top row first.
  [[nodiscard]] auto Rgba32FPixels(const float* rgba, const ImageExtent extent)
    -> std::span<const float>
  {
    return { rgba, PixelCount(extent) * 4U };
  }

  //! An RGBA32F image of the interleaved pixels, top row first.
  [[nodiscard]] auto Rgba32FImage(std::span<const float> rgba,
    const ImageExtent extent, const DecodeOptions& options) -> ScratchImage
  {
    const auto row_pitch = extent.width * kRgba32FBytesPerPixel;
    const auto bytes = std::as_bytes(rgba);
    auto pixel_data = std::vector<std::byte>(bytes.begin(), bytes.end());
    if (options.flip_y) {
      FlipImageY(pixel_data, row_pitch);
    }
    return ScratchImage::CreateFromData(extent.width, extent.height,
      Format::kRGBA32Float, row_pitch, std::move(pixel_data));
  }

  //=== EXR Decoder ===-------------------------------------------------------//

  auto ReleaseExrError(const char* error) noexcept -> void
  {
    if (error != nullptr) {
      FreeEXRErrorMessage(error);
    }
  }

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

    OXYGEN_MAKE_NON_COPYABLE(EXRHeaderGuard)
    OXYGEN_MAKE_NON_MOVABLE(EXRHeaderGuard)
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

    OXYGEN_MAKE_NON_COPYABLE(EXRImageGuard)
    OXYGEN_MAKE_NON_MOVABLE(EXRImageGuard)
  };

  //! The headers of the parts of a multipart EXR, which tinyexr allocates.
  class ExrPartHeaders final {
  public:
    ExrPartHeaders() = default;
    ~ExrPartHeaders()
    {
      for (auto* header : Headers()) {
        FreeEXRHeader(header);
        std::free(header); // NOLINT(*-no-malloc,*-owning-memory)
      }
      // NOLINTNEXTLINE(*-no-malloc,*-owning-memory)
      std::free(static_cast<void*>(headers_));
    }

    OXYGEN_MAKE_NON_COPYABLE(ExrPartHeaders)
    OXYGEN_MAKE_NON_MOVABLE(ExrPartHeaders)

    [[nodiscard]] auto Parse(
      EXRVersion& version, std::span<const std::byte> bytes) noexcept -> bool
    {
      const char* error = nullptr;
      const auto result = ParseEXRMultipartHeaderFromMemory(
        &headers_, &count_, &version, AsUChars(bytes), bytes.size(), &error);
      ReleaseExrError(error);
      return result == TINYEXR_SUCCESS;
    }

    [[nodiscard]] auto Headers() const noexcept -> std::span<EXRHeader* const>
    {
      return { headers_, static_cast<std::size_t>(count_) };
    }

    [[nodiscard]] auto ForLoading() const noexcept -> const EXRHeader**
    {
      return const_cast<const EXRHeader**>(headers_);
    }

  private:
    EXRHeader** headers_ = nullptr;
    int count_ = 0;
  };

  //! The images of the parts of a multipart EXR, one per header.
  class ExrPartImages final {
  public:
    explicit ExrPartImages(const std::size_t count)
      : images_(count)
    {
      for (auto& image : images_) {
        InitEXRImage(&image);
      }
    }

    ~ExrPartImages()
    {
      for (auto& image : images_) {
        FreeEXRImage(&image);
      }
    }

    OXYGEN_MAKE_NON_COPYABLE(ExrPartImages)
    OXYGEN_MAKE_NON_MOVABLE(ExrPartImages)

    [[nodiscard]] auto Data() noexcept -> EXRImage* { return images_.data(); }

    [[nodiscard]] auto At(const std::size_t index) const -> const EXRImage&
    {
      return images_.at(index);
    }

  private:
    std::vector<EXRImage> images_;
  };

  //! Has tinyexr decode every channel of the header as 32-bit float.
  auto RequestFloatPixels(EXRHeader& header) -> void
  {
    std::ranges::fill(std::span(header.requested_pixel_types,
                        static_cast<std::size_t>(header.num_channels)),
      TINYEXR_PIXELTYPE_FLOAT);
  }

  [[nodiscard]] auto ChannelName(const EXRChannelInfo& channel) noexcept
    -> std::string_view
  {
    const auto name = std::span(channel.name);
    return { name.begin(), std::ranges::find(name, '\0') };
  }

  [[nodiscard]] auto ChannelIndex(const EXRHeader& header,
    const std::string_view name) -> std::optional<std::size_t>
  {
    const auto channels = std::span(
      header.channels, static_cast<std::size_t>(header.num_channels));
    const auto it = std::ranges::find(channels, name, ChannelName);
    if (it == channels.end()) {
      return std::nullopt;
    }
    return static_cast<std::size_t>(
      std::ranges::distance(channels.begin(), it));
  }

  //! A channel plane decoded as float (see RequestFloatPixels); tinyexr hands
  //! every plane out as bytes.
  [[nodiscard]] auto FloatPlane(const unsigned char* plane,
    const std::size_t count) noexcept -> std::span<const float>
  {
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    return { reinterpret_cast<const float*>(plane), count };
  }

  //! Interleaves the R, G, B and, when present, A planes of a decoded EXR
  //! part into RGBA32F; a part without alpha is opaque. A tiled part, whose
  //! pixels are in tiles rather than planes, is refused.
  [[nodiscard]] auto ExrPartToScratchImage(const EXRHeader& header,
    const EXRImage& image, const DecodeOptions& options)
    -> Result<ScratchImage, TextureImportError>
  {
    if (image.images == nullptr || image.width <= 0 || image.height <= 0) {
      return Err(TextureImportError::kUnsupportedFormat);
    }
    // Channel indices come from the header and select the image's planes.
    if (image.num_channels != header.num_channels) {
      return Err(TextureImportError::kCorruptedData);
    }
    const auto red_channel = ChannelIndex(header, "R");
    const auto green_channel = ChannelIndex(header, "G");
    const auto blue_channel = ChannelIndex(header, "B");
    const auto alpha_channel = ChannelIndex(header, "A");
    if (!red_channel || !green_channel || !blue_channel) {
      return Err(TextureImportError::kUnsupportedFormat);
    }

    const auto extent = ImageExtent {
      .width = static_cast<uint32_t>(image.width),
      .height = static_cast<uint32_t>(image.height),
    };
    const auto pixel_count = PixelCount(extent);
    const auto planes
      = std::span(image.images, static_cast<std::size_t>(image.num_channels));
    const auto plane = [&planes, pixel_count](const std::size_t channel) {
      return FloatPlane(planes[channel], pixel_count);
    };
    const auto red = plane(*red_channel);
    const auto green = plane(*green_channel);
    const auto blue = plane(*blue_channel);
    const auto alpha
      = alpha_channel ? plane(*alpha_channel) : std::span<const float> {};

    auto rgba = std::vector<float>(pixel_count * 4U);
    const auto pixels = std::span(rgba);
    for (std::size_t i = 0; i < pixel_count; ++i) {
      const auto pixel = pixels.subspan(i * 4U, 4U);
      pixel[0] = red[i];
      pixel[1] = green[i];
      pixel[2] = blue[i];
      pixel[3] = alpha.empty() ? 1.0F : alpha[i];
    }
    return Ok(Rgba32FImage(rgba, extent, options));
  }

  //! Decodes the first scanline part of a multipart EXR.
  [[nodiscard]] auto DecodeExrMultipart(std::span<const std::byte> bytes,
    EXRVersion& version, const DecodeOptions& options)
    -> Result<ScratchImage, TextureImportError>
  {
    auto headers = ExrPartHeaders {};
    if (!headers.Parse(version, bytes)) {
      return Err(TextureImportError::kCorruptedData);
    }

    const auto parts = headers.Headers();
    const auto part = std::ranges::find_if(
      parts, [](const EXRHeader* header) { return header->tiled == 0; });
    if (part == parts.end()) {
      return Err(TextureImportError::kUnsupportedFormat);
    }
    for (auto* header : parts) {
      RequestFloatPixels(*header);
    }

    auto images = ExrPartImages(parts.size());
    const char* error = nullptr;
    const auto result = LoadEXRMultipartImageFromMemory(images.Data(),
      headers.ForLoading(), static_cast<unsigned int>(parts.size()),
      AsUChars(bytes), bytes.size(), &error);
    ReleaseExrError(error);
    if (result != TINYEXR_SUCCESS) {
      return Err(TextureImportError::kDecodeFailed);
    }

    const auto index
      = static_cast<std::size_t>(std::ranges::distance(parts.begin(), part));
    return ExrPartToScratchImage(**part, images.At(index), options);
  }

  //! Decodes an EXR that the simple tinyexr API refused: a multipart file, or
  //! a single part read with the low-level API.
  [[nodiscard]] auto DecodeExrLowLevel(std::span<const std::byte> bytes,
    const DecodeOptions& options) -> Result<ScratchImage, TextureImportError>
  {
    EXRVersion version {};
    if (ParseEXRVersionFromMemory(&version, AsUChars(bytes), bytes.size())
      != TINYEXR_SUCCESS) {
      return Err(TextureImportError::kCorruptedData);
    }
    if (version.multipart != 0) {
      return DecodeExrMultipart(bytes, version, options);
    }

    const char* error = nullptr;
    EXRHeaderGuard header;
    auto result = ParseEXRHeaderFromMemory(
      &header.header, &version, AsUChars(bytes), bytes.size(), &error);
    ReleaseExrError(error);
    if (result != TINYEXR_SUCCESS) {
      return Err(TextureImportError::kCorruptedData);
    }
    header.initialized = true;
    RequestFloatPixels(header.header);

    EXRImageGuard image;
    error = nullptr;
    result = LoadEXRImageFromMemory(
      &image.image, &header.header, AsUChars(bytes), bytes.size(), &error);
    ReleaseExrError(error);
    if (result != TINYEXR_SUCCESS) {
      return Err(TextureImportError::kDecodeFailed);
    }
    image.initialized = true;

    return ExrPartToScratchImage(header.header, image.image, options);
  }

  [[nodiscard]] auto DecodeExrToScratchImage(std::span<const std::byte> bytes,
    const DecodeOptions& options) -> Result<ScratchImage, TextureImportError>
  {
    // The simple API reads most single-part files to interleaved RGBA.
    float* decoded = nullptr;
    int width = 0;
    int height = 0;
    const char* error = nullptr;
    const auto result = LoadEXRFromMemory(
      &decoded, &width, &height, AsUChars(bytes), bytes.size(), &error);
    ReleaseExrError(error);
    const auto rgba = CBuffer<float>(decoded);
    if (result == TINYEXR_SUCCESS && rgba != nullptr) {
      const auto extent = ImageExtent {
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
      };
      return Ok(
        Rgba32FImage(Rgba32FPixels(rgba.get(), extent), extent, options));
    }
    return DecodeExrLowLevel(bytes, options);
  }

  //=== HDR (Radiance) Decoder ===--------------------------------------------//

  [[nodiscard]] auto DecodeHdrToScratchImage(std::span<const std::byte> bytes,
    const DecodeOptions& options) -> Result<ScratchImage, TextureImportError>
  {
    int width = 0;
    int height = 0;
    int channels_in_file = 0;
    const auto decoded = StbiImage<float>(
      stbi_loadf_from_memory(AsUChars(bytes), static_cast<int>(bytes.size()),
        &width, &height, &channels_in_file, STBI_rgb_alpha));
    if (decoded == nullptr) {
      return Err(TextureImportError::kDecodeFailed);
    }
    const auto extent = ImageExtent {
      .width = static_cast<uint32_t>(width),
      .height = static_cast<uint32_t>(height),
    };
    return Ok(
      Rgba32FImage(Rgba32FPixels(decoded.get(), extent), extent, options));
  }

  //=== PNG Decoder (libspng) ===-------------------------------------------//

  struct SpngContextFree final {
    auto operator()(spng_ctx* context) const noexcept -> void
    {
      spng_ctx_free(context);
    }
  };

  //! The output libspng decodes a PNG to: grayscale keeps its one or two
  //! channels unless RGBA is forced; everything else decodes to RGBA8.
  struct PngOutput final {
    int spng_format = SPNG_FMT_RGBA8;
    Format format = Format::kRGBA8UNorm;
    uint32_t bytes_per_pixel = 4U;
  };

  [[nodiscard]] auto PngOutputFor(
    const spng_ihdr& ihdr, const DecodeOptions& options) noexcept -> PngOutput
  {
    if (options.force_rgba) {
      return {};
    }
    switch (ihdr.color_type) {
    case SPNG_COLOR_TYPE_GRAYSCALE:
      return {
        .spng_format = SPNG_FMT_G8,
        .format = Format::kR8UNorm,
        .bytes_per_pixel = 1U,
      };
    case SPNG_COLOR_TYPE_GRAYSCALE_ALPHA:
      return {
        .spng_format = SPNG_FMT_GA8,
        .format = Format::kRG8UNorm,
        .bytes_per_pixel = 2U,
      };
    default:
      return {};
    }
  }

  [[nodiscard]] auto DecodePngToScratchImage(std::span<const std::byte> bytes,
    const DecodeOptions& options) -> Result<ScratchImage, TextureImportError>
  {
    const auto context
      = std::unique_ptr<spng_ctx, SpngContextFree>(spng_ctx_new(0));
    if (context == nullptr) {
      return Err(TextureImportError::kOutOfMemory);
    }
    if (spng_set_png_buffer(context.get(), bytes.data(), bytes.size()) != 0) {
      return Err(TextureImportError::kDecodeFailed);
    }
    spng_ihdr ihdr {};
    if (spng_get_ihdr(context.get(), &ihdr) != 0) {
      return Err(TextureImportError::kDecodeFailed);
    }

    const auto output = PngOutputFor(ihdr, options);
    std::size_t decoded_size = 0;
    if (spng_decoded_image_size(
          context.get(), output.spng_format, &decoded_size)
        != 0
      || decoded_size == 0) {
      return Err(TextureImportError::kDecodeFailed);
    }
    auto pixel_data = std::vector<std::byte>(decoded_size);
    if (spng_decode_image(
          context.get(), pixel_data.data(), decoded_size, output.spng_format, 0)
      != 0) {
      return Err(TextureImportError::kDecodeFailed);
    }

    const auto row_pitch = ihdr.width * output.bytes_per_pixel;
    if (options.flip_y) {
      FlipImageY(pixel_data, row_pitch);
    }
    return Ok(ScratchImage::CreateFromData(ihdr.width, ihdr.height,
      output.format, row_pitch, std::move(pixel_data)));
  }

  //=== LDR Decoder (stb_image RGBA8) ===-------------------------------------//

  //! The format of a stb_image decode: one and two channels keep their
  //! channels, three are expanded to RGBA8.
  [[nodiscard]] auto LdrFormat(const int channels) noexcept -> Format
  {
    switch (channels) {
    case 1:
      return Format::kR8UNorm;
    case 2:
      return Format::kRG8UNorm;
    default:
      return Format::kRGBA8UNorm;
    }
  }

  //! Expands RGB8 pixels to opaque RGBA8.
  [[nodiscard]] auto ExpandRgbToRgba(std::span<const stbi_uc> rgb)
    -> std::vector<std::byte>
  {
    auto rgba = std::vector<std::byte>(rgb.size() / 3U * 4U);
    for (auto&& [source, target] : std::views::zip(
           rgb | std::views::chunk(3), rgba | std::views::chunk(4))) {
      std::ranges::transform(source, target.begin(),
        [](const stbi_uc channel) { return std::byte { channel }; });
      target[3] = std::byte { 0xFF };
    }
    return rgba;
  }

  [[nodiscard]] auto DecodeLdrToScratchImage(std::span<const std::byte> bytes,
    const DecodeOptions& options) -> Result<ScratchImage, TextureImportError>
  {
    const auto ext_lower = ToLower(options.extension_hint);
    if (IsPngSignature(bytes) || ext_lower == ".png") {
      auto png = DecodePngToScratchImage(bytes, options);
      if (png) {
        return png;
      }
    }

    int width = 0;
    int height = 0;
    int channels_in_file = 0;
    const int desired_channels = options.force_rgba ? STBI_rgb_alpha : 0;
    const auto decoded = StbiImage<stbi_uc>(
      stbi_load_from_memory(AsUChars(bytes), static_cast<int>(bytes.size()),
        &width, &height, &channels_in_file, desired_channels));
    if (decoded == nullptr) {
      return Err(TextureImportError::kDecodeFailed);
    }

    const auto extent = ImageExtent {
      .width = static_cast<uint32_t>(width),
      .height = static_cast<uint32_t>(height),
    };
    const int channels
      = (desired_channels != 0) ? desired_channels : channels_in_file;
    const auto decoded_pixels = std::span(
      decoded.get(), PixelCount(extent) * static_cast<std::size_t>(channels));
    auto pixel_data = channels == 3
      ? ExpandRgbToRgba(decoded_pixels)
      : CopyBytes(decoded_pixels.data(), decoded_pixels.size());

    const auto bytes_per_pixel
      = static_cast<uint32_t>(channels == 3 ? 4 : channels);
    const auto row_pitch = extent.width * bytes_per_pixel;
    if (options.flip_y) {
      FlipImageY(pixel_data, row_pitch);
    }
    return Ok(ScratchImage::CreateFromData(extent.width, extent.height,
      LdrFormat(channels), row_pitch, std::move(pixel_data)));
  }

  //=== WebP Decoder (libwebp) ===--------------------------------------------//

  //! Detects a RIFF container of the WebP form ("RIFF", size, "WEBP").
  [[nodiscard]] auto IsWebpSignature(std::span<const std::byte> bytes) noexcept
    -> bool
  {
    constexpr auto kFormOffset = std::size_t { 8 };
    return bytes.size() >= kFormOffset && StartsWithText(bytes, "RIFF")
      && StartsWithText(bytes.subspan(kFormOffset), "WEBP");
  }

  [[nodiscard]] auto WebpError(const VP8StatusCode status) -> TextureImportError
  {
    switch (status) {
    case VP8_STATUS_OUT_OF_MEMORY:
      return TextureImportError::kOutOfMemory;
    case VP8_STATUS_UNSUPPORTED_FEATURE:
      return TextureImportError::kUnsupportedFormat;
    case VP8_STATUS_BITSTREAM_ERROR:
    case VP8_STATUS_NOT_ENOUGH_DATA:
      return TextureImportError::kCorruptedData;
    default:
      return TextureImportError::kDecodeFailed;
    }
  }

  //! Decodes a still WebP image, lossy or lossless, to RGBA8. An animated
  //! WebP is not a texture and is refused.
  [[nodiscard]] auto DecodeWebpToScratchImage(std::span<const std::byte> bytes,
    const DecodeOptions& options) -> Result<ScratchImage, TextureImportError>
  {
    auto features = WebPBitstreamFeatures {};
    if (const auto status
      = WebPGetFeatures(AsUChars(bytes), bytes.size(), &features);
      status != VP8_STATUS_OK) {
      return Err(WebpError(status));
    }
    if (features.has_animation != 0) {
      return Err(TextureImportError::kUnsupportedFormat);
    }

    const auto width = static_cast<uint32_t>(features.width);
    const auto height = static_cast<uint32_t>(features.height);
    constexpr auto kBytesPerPixel = 4U;
    const auto row_pitch = width * kBytesPerPixel;
    auto pixel_data = std::vector<std::byte>(
      static_cast<std::size_t>(row_pitch) * static_cast<std::size_t>(height));
    if (WebPDecodeRGBAInto(AsUChars(bytes), bytes.size(),
          AsWritableUChars(pixel_data), pixel_data.size(),
          static_cast<int>(row_pitch))
      == nullptr) {
      return Err(TextureImportError::kDecodeFailed);
    }

    if (options.flip_y) {
      FlipImageY(pixel_data, row_pitch);
    }
    return Ok(ScratchImage::CreateFromData(
      width, height, Format::kRGBA8UNorm, row_pitch, std::move(pixel_data)));
  }

  //=== KTX2 Decoder (libktx, Basis Universal) ===----------------------------//

  //! Detects the KTX 2.0 file identifier.
  [[nodiscard]] auto IsKtx2Signature(std::span<const std::byte> bytes) noexcept
    -> bool
  {
    constexpr auto kIdentifier = std::to_array<std::byte>({
      std::byte { 0xAB },
      std::byte { 'K' },
      std::byte { 'T' },
      std::byte { 'X' },
      std::byte { ' ' },
      std::byte { '2' },
      std::byte { '0' },
      std::byte { 0xBB },
      std::byte { '\r' },
      std::byte { '\n' },
      std::byte { 0x1A },
      std::byte { '\n' },
    });
    return std::ranges::starts_with(bytes, kIdentifier);
  }

  struct KtxTextureDeleter final {
    auto operator()(ktxTexture2* texture) const noexcept -> void
    {
      ktxTexture2_Destroy(texture);
    }
  };

  using KtxTexturePtr = std::unique_ptr<ktxTexture2, KtxTextureDeleter>;

  [[nodiscard]] auto KtxError(const KTX_error_code code) -> TextureImportError
  {
    switch (code) {
    case KTX_OUT_OF_MEMORY:
      return TextureImportError::kOutOfMemory;
    case KTX_FILE_DATA_ERROR:
    case KTX_FILE_UNEXPECTED_EOF:
    case KTX_UNKNOWN_FILE_FORMAT:
      return TextureImportError::kCorruptedData;
    case KTX_UNSUPPORTED_TEXTURE_TYPE:
    case KTX_UNSUPPORTED_FEATURE:
      return TextureImportError::kUnsupportedFormat;
    default:
      return TextureImportError::kDecodeFailed;
    }
  }

  //! Decodes a KTX2 texture compressed with Basis Universal (ETC1S or UASTC,
  //! zstd supercompressed or not) by transcoding its base level to RGBA8.
  /*!
   The decoded image is one 2D image, top row first like every other decoder:
   a texture stored bottom-up (`KTXorientation` "u") is flipped. The cook
   regenerates the mip chain, so the file's other levels are not read. A KTX2
   texture in any other encoding (raw or GPU block formats), or with array
   layers, cube faces or depth, is refused.
  */
  [[nodiscard]] auto DecodeKtx2ToScratchImage(std::span<const std::byte> bytes,
    const DecodeOptions& options) -> Result<ScratchImage, TextureImportError>
  {
    ktxTexture2* created = nullptr;
    if (const auto code = ktxTexture2_CreateFromMemory(AsUChars(bytes),
          bytes.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &created);
      code != KTX_SUCCESS) {
      return Err(KtxError(code));
    }
    const auto texture = KtxTexturePtr(created);

    if (texture->isArray || texture->isCubemap || texture->numLayers > 1U
      || texture->numFaces > 1U) {
      return Err(TextureImportError::kArrayLayerCountInvalid);
    }
    if (texture->numDimensions > 2U || texture->baseDepth > 1U) {
      return Err(TextureImportError::kDepthInvalidFor2D);
    }
    if (!ktxTexture2_NeedsTranscoding(texture.get())) {
      return Err(TextureImportError::kUnsupportedFormat);
    }
    if (const auto code
      = ktxTexture2_TranscodeBasis(texture.get(), KTX_TTF_RGBA32, 0);
      code != KTX_SUCCESS) {
      return Err(KtxError(code));
    }

    auto offset = ktx_size_t { 0 };
    if (const auto code
      = ktxTexture2_GetImageOffset(texture.get(), 0U, 0U, 0U, &offset);
      code != KTX_SUCCESS) {
      return Err(KtxError(code));
    }
    const auto width = texture->baseWidth;
    const auto height = texture->baseHeight;
    constexpr auto kBytesPerPixel = 4U;
    const auto row_pitch = width * kBytesPerPixel;
    const auto image_size
      = static_cast<std::size_t>(row_pitch) * static_cast<std::size_t>(height);
    const auto data
      = std::as_bytes(std::span(texture->pData, texture->dataSize));
    if (offset > data.size() || image_size > data.size() - offset) {
      return Err(TextureImportError::kCorruptedData);
    }
    const auto image = data.subspan(offset, image_size);
    auto pixel_data = std::vector<std::byte>(image.begin(), image.end());

    const bool stored_bottom_up = texture->orientation.y == KTX_ORIENT_Y_UP;
    if (options.flip_y != stored_bottom_up) {
      FlipImageY(pixel_data, row_pitch);
    }
    return Ok(ScratchImage::CreateFromData(
      width, height, Format::kRGBA8UNorm, row_pitch, std::move(pixel_data)));
  }

  //=== File Reading Utility ===----------------------------------------------//

  [[nodiscard]] auto ReadFileBytes(const std::filesystem::path& path)
    -> Result<std::vector<std::byte>, TextureImportError>
  {
    std::ifstream file(base::ToNativePath(path), std::ios::binary);
    if (!file) {
      return Err(TextureImportError::kFileNotFound);
    }

    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size <= 0) {
      return Err(TextureImportError::kFileReadFailed);
    }

    auto bytes = std::vector<std::byte>(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(AsChars(bytes), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
      return Err(TextureImportError::kFileReadFailed);
    }

    return Ok(std::move(bytes));
  }

  [[nodiscard]] auto FitsStb(std::span<const std::byte> bytes) noexcept -> bool
  {
    return bytes.size()
      <= static_cast<std::size_t>(std::numeric_limits<int>::max());
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

  if (!FitsStb(bytes)) {
    return ImageDecodeResult {
      .image = std::nullopt,
      .error = "input too large for stb",
    };
  }

  return DecodeWithStb(bytes);
}

auto DecodeImageRgba8FromFile(const std::filesystem::path& path)
  -> ImageDecodeResult
{
  auto bytes = ReadFileBytes(path);
  if (!bytes) {
    return ImageDecodeResult {
      .image = std::nullopt,
      .error = bytes.error() == TextureImportError::kFileNotFound
        ? "failed to open file"
        : "failed to read file",
    };
  }
  return DecodeImageRgba8FromMemory(bytes.value());
}

//=== Format Detection ===----------------------------------------------------//

auto IsExrSignature(std::span<const std::byte> bytes) noexcept -> bool
{
  constexpr std::array magic {
    std::byte { 0x76 },
    std::byte { 0x2F },
    std::byte { 0x31 },
    std::byte { 0x01 },
  };
  return std::ranges::starts_with(bytes, magic);
}

auto IsHdrSignature(std::span<const std::byte> bytes) noexcept -> bool
{
  // Radiance HDR signature: "#?RADIANCE" or "#?RGBE"
  return StartsWithText(bytes, "#?RADIANCE") || StartsWithText(bytes, "#?RGBE");
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
  return !bytes.empty() && FitsStb(bytes)
    && stbi_is_hdr_from_memory(AsUChars(bytes), static_cast<int>(bytes.size()))
    != 0;
}

//=== Unified Decode API ===--------------------------------------------------//

auto DecodeToScratchImage(std::span<const std::byte> bytes,
  const DecodeOptions& options) -> Result<ScratchImage, TextureImportError>
{
  if (bytes.empty()) {
    return Err(TextureImportError::kCorruptedData);
  }

  if (!FitsStb(bytes)) {
    return Err(TextureImportError::kOutOfMemory);
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

  // 4. WebP and KTX2 signatures
  if (IsWebpSignature(bytes)) {
    return DecodeWebpToScratchImage(bytes, options);
  }
  if (IsKtx2Signature(bytes)) {
    return DecodeKtx2ToScratchImage(bytes, options);
  }

  // 5. Fallback to LDR decoder (stb_image)
  return DecodeLdrToScratchImage(bytes, options);
}

auto DecodeToScratchImage(const std::filesystem::path& path,
  const DecodeOptions& options) -> Result<ScratchImage, TextureImportError>
{
  // Read file
  auto bytes_result = ReadFileBytes(path);
  if (!bytes_result) {
    return Err(bytes_result.error());
  }

  // Create options with extension hint
  DecodeOptions opts_with_ext = options;
  if (opts_with_ext.extension_hint.empty()) {
    opts_with_ext.extension_hint = path.extension().string();
  }

  return DecodeToScratchImage(bytes_result.value(), opts_with_ext);
}

} // namespace oxygen::content::import
