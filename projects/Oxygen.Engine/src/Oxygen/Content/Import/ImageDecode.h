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

#include <Oxygen/Content/api_export.h>

namespace oxygen::content::import {

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

} // namespace oxygen::content::import
