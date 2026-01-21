//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <type_traits>

#include <Oxygen/Content/api_export.h>

namespace oxygen::content::import {

//! Error taxonomy for texture import and cooking operations.
/*!
  Errors are categorized into four groups:
  - **Decode errors**: Issues during source image decoding
  - **Validation errors**: Invalid parameters or inconsistent configuration
  - **Cook errors**: Issues during mip generation, compression, or format
  conversion
  - **I/O errors**: File system and data stream errors

  Use the category helper functions (`IsDecodeError()`, `IsValidationError()`,
  `IsCookError()`, `IsIoError()`) to classify errors for error handling logic.
*/
enum class TextureImportError : uint8_t {
  // clang-format off

  //=== Success ===-----------------------------------------------------------//
  kSuccess = 0,  //!< Operation completed successfully

  //=== Decode Errors (1-19) ===----------------------------------------------//
  kUnsupportedFormat = 1,  //!< Source format not supported by any decoder
  kCorruptedData     = 2,  //!< Source data is corrupted or malformed
  kDecodeFailed      = 3,  //!< Decoder returned an unspecified error
  kOutOfMemory       = 4,  //!< Insufficient memory during decoding

  //=== Validation Errors (20-39) ===-----------------------------------------//
  kInvalidDimensions       = 20,  //!< Width or height is zero or exceeds limits
  kDimensionMismatch       = 21,  //!< Multi-source textures have inconsistent dimensions
  kArrayLayerCountInvalid  = 22,  //!< Array layer count invalid for texture type
  kDepthInvalidFor2D       = 23,  //!< Depth specified for non-3D texture
  kInvalidMipPolicy        = 24,  //!< Mip policy configuration is invalid
  kInvalidOutputFormat     = 25,  //!< Output format is not supported
  kIntentFormatMismatch    = 26,  //!< Content intent incompatible with output format

  //=== Cook Errors (40-59) ===-----------------------------------------------//
  kMipGenerationFailed   = 40,  //!< Mip chain generation failed
  kCompressionFailed     = 41,  //!< BC7 compression failed
  kOutputFormatInvalid   = 42,  //!< Cannot convert to requested output format
  kHdrRequiresFloatFormat = 43,  //!< HDR content requires float output (bake_hdr_to_ldr not set)

  //=== I/O Errors (60-79) ===------------------------------------------------//
  kFileNotFound  = 60,  //!< Source file does not exist
  kFileReadFailed = 61,  //!< Failed to read source file
  kWriteFailed   = 62,  //!< Failed to write output data

  //=== Cancellation (80-89) ===---------------------------------------------//
  kCancelled     = 80,  //!< Operation was canceled by the caller

  // clang-format on
};

static_assert(
  sizeof(std::underlying_type_t<TextureImportError>) == sizeof(uint8_t),
  "TextureImportError enum must be 8 bits for compact storage");

//! String representation of enum values in `TextureImportError`.
OXGN_CNTT_NDAPI auto to_string(TextureImportError value) -> const char*;

//=== Error Category Helpers ===----------------------------------------------//

//! Returns true if the error is a decode-category error.
[[nodiscard]] constexpr auto IsDecodeError(TextureImportError error) noexcept
  -> bool
{
  const auto code = static_cast<uint8_t>(error);
  return code >= 1 && code <= 19;
}

//! Returns true if the error is a validation-category error.
[[nodiscard]] constexpr auto IsValidationError(
  TextureImportError error) noexcept -> bool
{
  const auto code = static_cast<uint8_t>(error);
  return code >= 20 && code <= 39;
}

//! Returns true if the error is a cook-category error.
[[nodiscard]] constexpr auto IsCookError(TextureImportError error) noexcept
  -> bool
{
  const auto code = static_cast<uint8_t>(error);
  return code >= 40 && code <= 59;
}

//! Returns true if the error is an I/O-category error.
[[nodiscard]] constexpr auto IsIoError(TextureImportError error) noexcept
  -> bool
{
  const auto code = static_cast<uint8_t>(error);
  return code >= 60 && code <= 79;
}

} // namespace oxygen::content::import
