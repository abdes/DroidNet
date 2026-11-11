//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <span>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Cooker/Pak/PakTextures.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::pak {

auto KeyedTextureDigest(const data::pak::core::TextureResourceDesc& record)
  -> base::Sha256Digest
{
  auto normalized = record;
  normalized.data_offset = 0U;
  return base::ComputeSha256(std::as_bytes(std::span(&normalized, 1)));
}

auto ReadPakTextures(const PakFile& pak) -> PakTextures
{
  auto textures = PakTextures {};
  for (const auto& texture : pak.BrowseTextures()) {
    const auto offset
      = pak.TexturesTable().GetResourceOffset(texture.texture_index);
    if (!offset.has_value()) {
      throw data::CookedFormatError(data::CookedDefect::kOutOfBounds,
        "Browse texture '" + texture.virtual_path
          + "' names no row of the texture table");
    }
    const auto record = serio::CopyAt<data::pak::core::TextureResourceDesc>(
      pak.Bytes(), serio::ByteOffset { *offset });
    if (!record) {
      throw data::CookedFormatError(data::CookedDefect::kOutOfBounds,
        "Texture record of '" + texture.virtual_path
          + "' lies outside the pak");
    }
    textures.insert_or_assign(
      data::TextureKeyFromVirtualPath(texture.virtual_path),
      KeyedTextureDigest(*record));
  }
  return textures;
}

} // namespace oxygen::content::pak
