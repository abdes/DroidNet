//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <span>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Cooker/Import/TextureImportError.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import {

//! Whether `bytes` begin as a DirectDraw Surface file.
OXGN_COOK_NDAPI auto IsDdsSignature(std::span<const std::byte> bytes) noexcept
  -> bool;

//! Decodes the first level of a 2D DDS file to RGBA8UNorm.
/*!
 The cook generates its own mips and encodes its own format, as it does for
 every other source, so a file's lower levels are not read.

 Reads BC1, BC2, BC3, BC4 and BC5, under their legacy four-character codes or a
 DX10 header, and uncompressed 32-bit RGBA and BGRA. BC4 expands to grey. BC5
 holds a normal map's x and y, so its blue is the z they imply.

 @return `kUnsupportedFormat` for other encodings, cube maps, arrays and
         volumes; `kCorruptedData` when the header is invalid or the data is
         shorter than it states.
*/
OXGN_COOK_NDAPI auto DecodeDdsToScratchImage(std::span<const std::byte> bytes,
  bool flip_y) -> Result<ScratchImage, TextureImportError>;

} // namespace oxygen::content::import
