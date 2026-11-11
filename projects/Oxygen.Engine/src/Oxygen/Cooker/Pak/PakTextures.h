//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <unordered_map>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_core.h>

namespace oxygen::content {
class PakFile;
} // namespace oxygen::content

namespace oxygen::content::pak {

//! The keyed textures of a pak: each texture its browse index lists, by key,
//! with its `KeyedTextureDigest`.
using PakTextures = std::unordered_map<data::AssetKey, base::Sha256Digest>;

//! What tells two keyed textures apart: the SHA-256 of the texture's
//! descriptor record without its payload offset.
/*!
 The record holds the payload's size, layout and content hash, so two textures
 with equal digests are the same texture wherever their payloads lie.
*/
OXGN_COOK_NDAPI auto KeyedTextureDigest(
  const data::pak::core::TextureResourceDesc& record) -> base::Sha256Digest;

//! The keyed textures of `pak`.
/*!
 @throw data::CookedFormatError `kOutOfBounds` when a texture the browse index
   lists has no record in the texture table.
*/
OXGN_COOK_NDAPI auto ReadPakTextures(const PakFile& pak) -> PakTextures;

} // namespace oxygen::content::pak
