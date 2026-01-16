//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <Oxygen/Content/Import/ScratchImage.h>
#include <Oxygen/Content/Import/TextureSourceAssembly.h>
#include <Oxygen/Core/Types/Format.h>

namespace oxygen::content::import::detail {

//! Get bytes per pixel for a given format.
[[nodiscard]] auto GetBytesPerPixel(Format format) -> std::size_t;

//! Convert a single cube face from an equirectangular source.
auto ConvertEquirectangularFace(const ScratchImage& equirect,
  const ScratchImageMeta& src_meta, std::span<const std::byte> src_pixels,
  CubeFace face, uint32_t face_size, bool use_bicubic, ScratchImage& cube)
  -> void;

//! Extract a single cube face from a layout image.
auto ExtractCubeFaceFromLayout(const ImageView& src_view,
  CubeMapImageLayout layout, uint32_t face_size, std::size_t bytes_per_pixel,
  CubeFace face, ScratchImage& cube) -> void;

} // namespace oxygen::content::import::detail
