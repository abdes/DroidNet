//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Cooker/Import/TextureSourceAssembly.h>
#include <Oxygen/Core/Types/Format.h>

namespace oxygen::content::import::detail {

using CubeFacePaths = std::array<std::filesystem::path, kCubeFaceCount>;

//! Shared suffix-selection state for synchronous and observed asynchronous
//! reads.
class CubeFaceSearch final {
public:
  explicit CubeFaceSearch(const std::filesystem::path& source);
  [[nodiscard]] auto Candidate() const noexcept -> const std::filesystem::path&
  {
    return candidate_;
  }
  auto Observe(bool present) -> void;
  [[nodiscard]] auto TakeResult() && -> std::optional<CubeFacePaths>;

private:
  auto UpdateCandidate() -> void;
  std::filesystem::path parent_;
  std::string stem_;
  std::string extension_;
  CubeFacePaths paths_ {};
  std::filesystem::path candidate_;
  size_t suffix_set_ = 0;
  size_t face_ = 0;
};

//! Discover six faces in CubeFace order using the native suffix conventions.
//! The source reader observes every candidate probe, including rejected sets.
[[nodiscard]] auto DiscoverCubeFacePaths(const std::filesystem::path& path,
  IAsyncFileReader& reader) -> co::Co<Result<CubeFacePaths, FileErrorInfo>>;

//! Get bytes per pixel for a given format.
[[nodiscard]] auto GetBytesPerPixel(Format format) -> std::size_t;

//! Convert a single cube face from an equirectangular source.
auto ConvertEquirectangularFace(const ScratchImageMeta& src_meta,
  std::span<const std::byte> src_pixels, CubeFace face, uint32_t face_size,
  bool use_bicubic, ScratchImage& cube) -> void;

//! Extract a single cube face from a layout image.
auto ExtractCubeFaceFromLayout(const ImageView& src_view,
  CubeMapImageLayout layout, uint32_t face_size, std::size_t bytes_per_pixel,
  CubeFace face, ScratchImage& cube) -> void;

} // namespace oxygen::content::import::detail
