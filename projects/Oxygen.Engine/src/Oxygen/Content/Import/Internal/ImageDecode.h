//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Content/Import/ScratchImage.h>
#include <Oxygen/Content/Import/TextureImportError.h>
#include <Oxygen/Content/api_export.h>

namespace oxygen::content::import {

//=== Legacy RGBA8 Decode API ===---------------------------------------------//

//! RGBA8 image decoded for use by importers.
struct DecodedImageRgba8 final {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<std::byte> pixels;
};

//! Result of an image decode operation.
struct ImageDecodeResult final {
  std::optional<DecodedImageRgba8> image;
  std::string error;

  [[nodiscard]] auto Succeeded() const noexcept -> bool
  {
    return image.has_value();
  }
};

//! Decode an image blob into RGBA8.
OXGN_CNTT_NDAPI auto DecodeImageRgba8FromMemory(
  std::span<const std::byte> bytes) -> ImageDecodeResult;

//! Decode an image file into RGBA8.
OXGN_CNTT_NDAPI auto DecodeImageRgba8FromFile(const std::filesystem::path& path)
  -> ImageDecodeResult;

//=== Unified ScratchImage Decode API ===-------------------------------------//

//! Options for the unified image decoder.
/*!
  Controls decoding behavior including Y-flip, channel expansion, and
  format hints.

  @see DecodeToScratchImage
*/
struct DecodeOptions {
  //! Flip image vertically during decode (common for OpenGL textures).
  bool flip_y = false;

  //! Force RGBA output (expand grayscale/RGB to 4 channels).
  bool force_rgba = true;

  //! Optional file extension hint (e.g., ".exr", ".hdr") for format detection.
  /*!
    When provided, helps the decoder select the appropriate backend.
    Include the leading dot. Case-insensitive.
  */
  std::string extension_hint;
};

//! Check if a byte buffer appears to contain EXR data.
/*!
  Detects the OpenEXR magic number (0x76, 0x2F, 0x31, 0x01).

  @param bytes Input data to check (at least 4 bytes recommended)
  @return True if the data appears to be EXR format
*/
OXGN_CNTT_NDAPI auto IsExrSignature(std::span<const std::byte> bytes) noexcept
  -> bool;

//! Check if a byte buffer appears to contain Radiance HDR data.
/*!
  Detects the Radiance HDR signature ("#?RADIANCE" or "#?RGBE").

  @param bytes Input data to check
  @return True if the data appears to be HDR format
*/
OXGN_CNTT_NDAPI auto IsHdrSignature(std::span<const std::byte> bytes) noexcept
  -> bool;

//! Check if the input data is a high dynamic range format.
/*!
  Returns true for EXR and Radiance HDR formats.

  @param bytes       Input data to check
  @param extension   Optional file extension hint (e.g., ".exr", ".hdr")
  @return True if the data is HDR format
*/
OXGN_CNTT_NDAPI auto IsHdrFormat(std::span<const std::byte> bytes,
  std::string_view extension = {}) noexcept -> bool;

//! Decode an image from memory into a ScratchImage.
/*!
  Unified entry point for all supported image formats:
  - **EXR**: Decoded via tinyexr to RGBA32Float
  - **HDR (Radiance)**: Decoded via stb_image to RGBA32Float
  - **LDR formats**: Decoded via stb_image to RGBA8UNorm

  Format detection order:
  1. EXR signature detection (magic bytes)
  2. HDR signature detection or `.hdr` extension hint
  3. Fallback to stb_image for LDR formats

  @param bytes   Input image data
  @param options Decode options (flip, force RGBA, extension hint)
  @return ScratchImage on success, or TextureImportError on failure
*/
OXGN_CNTT_NDAPI auto DecodeToScratchImage(
  std::span<const std::byte> bytes, const DecodeOptions& options = {})
  -> Result<ScratchImage, TextureImportError>;

//! Decode an image file into a ScratchImage.
/*!
  Reads the file and delegates to the memory-based decoder.
  The file extension is used as a format hint.

  @param path    Path to the image file
  @param options Decode options (flip, force RGBA)
  @return ScratchImage on success, or TextureImportError on failure
*/
OXGN_CNTT_NDAPI auto DecodeToScratchImage(
  const std::filesystem::path& path, const DecodeOptions& options = {})
  -> Result<ScratchImage, TextureImportError>;

} // namespace oxygen::content::import
