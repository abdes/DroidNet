//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Content/test/Fixtures/LooseCookedTestWriter.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakBuilder.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/TextureResourceDescriptor.h>

namespace oxygen::cooker::test {

//! Writes a loose cooked root holding one texture recorded under
//! `virtual_path`, as a cooked library holds it. The descriptor's file is the
//! path without its mount segment.
inline auto WriteTextureLibrary(const std::filesystem::path& root,
  const std::string_view virtual_path,
  data::pak::core::TextureResourceDesc descriptor) -> void
{
  using data::loose_cooked::FileKind;
  descriptor.data_offset = 0U;
  const auto records
    = std::array { data::pak::core::TextureResourceDesc {}, descriptor };
  const auto payload = std::vector<std::byte>(descriptor.size_bytes);
  const auto sidecar = data::EncodeTextureResourceDescriptor({
    .index = data::pak::core::ResourceIndexT { 1U },
    .descriptor = descriptor,
  });
  auto writer = content::testing::LooseCookedTestWriter(root);
  writer.WriteFile(FileKind::kTexturesTable, "Resources/textures.table",
    std::as_bytes(std::span { records }));
  writer.WriteFile(FileKind::kTexturesData, "Resources/textures.data", payload);
  writer.WriteFile(FileKind::kTextureDescriptor,
    std::string(virtual_path.substr(virtual_path.find('/', 1U) + 1U)), sidecar,
    std::string(virtual_path));
  static_cast<void>(writer.Finish());
}

//! Packs a loose cooked root into `pak_path`, with a browse index so its
//! textures are found by virtual path.
inline auto PackLibrary(const std::filesystem::path& loose_root,
  const std::filesystem::path& pak_path) -> std::filesystem::path
{
  const auto built
    = content::pak::PakBuilder {}.Build(content::pak::PakBuildRequest {
      .mode = content::pak::BuildMode::kFull,
      .sources = { data::CookedSource {
        .kind = data::CookedSourceKind::kLooseCooked,
        .path = loose_root,
      } },
      .output_pak_path = pak_path,
      .content_version = 1U,
      .source_key = data::SourceKey { Uuid::Generate() },
      .options = { .embed_browse_index = true },
    });
  if (!built) {
    throw std::runtime_error("The test library pak could not be built");
  }
  return pak_path;
}

} // namespace oxygen::cooker::test
