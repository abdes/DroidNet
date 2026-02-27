//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>
#include <optional>
#include <string>

#include <Oxygen/Content/Internal/ContentSourceRegistry.h>
#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Content/Internal/LooseCookedSource.h>
#include <Oxygen/Content/Internal/PakFileSource.h>
#include <Oxygen/Content/Internal/SceneCatalogQueryService.h>
#include <Oxygen/Data/AssetType.h>

namespace oxygen::content::internal {

namespace {

  auto ReadAssetHeader(const IContentSource& source, const data::AssetKey& key)
    -> std::optional<data::pak::core::AssetHeader>
  {
    auto desc_reader = source.CreateAssetDescriptorReader(key);
    if (!desc_reader) {
      return std::nullopt;
    }
    auto blob = desc_reader->ReadBlob(sizeof(data::pak::core::AssetHeader));
    if (!blob || blob->size() < sizeof(data::pak::core::AssetHeader)) {
      return std::nullopt;
    }
    data::pak::core::AssetHeader header {};
    std::memcpy(&header, blob->data(), sizeof(header));
    return header;
  }

} // namespace

auto SceneCatalogQueryService::EnumerateMountedScenes(
  const ContentSourceRegistry& source_registry) const
  -> std::vector<IAssetLoader::MountedSceneEntry>
{
  std::vector<IAssetLoader::MountedSceneEntry> scenes;
  const auto& sources = source_registry.Sources();
  const auto& source_ids = source_registry.SourceIds();
  scenes.reserve(sources.size() * 4U);

  for (size_t source_index = 0; source_index < sources.size(); ++source_index) {
    const auto& source = sources[source_index];
    if (!source) {
      continue;
    }

    const auto source_id = source_ids[source_index];
    const auto source_key = source->GetSourceKey();
    const auto source_path = source->SourcePath();
    const auto source_kind
      = source->GetTypeId() == LooseCookedSource::ClassTypeId()
      ? IAssetLoader::ContentSourceKind::kLooseCooked
      : IAssetLoader::ContentSourceKind::kPak;

    const auto asset_count = source->GetAssetCount();
    for (size_t i = 0; i < asset_count; ++i) {
      const auto scene_key_opt
        = source->GetAssetKeyByIndex(static_cast<uint32_t>(i));
      if (!scene_key_opt.has_value()) {
        continue;
      }

      if (!source->HasAsset(*scene_key_opt)) {
        continue;
      }

      const auto header_opt = ReadAssetHeader(*source, *scene_key_opt);
      if (!header_opt.has_value()) {
        continue;
      }

      if (static_cast<data::AssetType>(header_opt->asset_type)
        != data::AssetType::kScene) {
        continue;
      }

      IAssetLoader::MountedSceneEntry scene_entry {};
      scene_entry.scene_key = *scene_key_opt;
      scene_entry.source_key = source_key;
      scene_entry.source_id = source_id;
      scene_entry.source_kind = source_kind;
      scene_entry.source_path = source_path;
      scene_entry.display_name = header_opt->name[0] == '\0'
        ? std::string {}
        : std::string(header_opt->name);

      if (const auto vpath = source->ResolveVirtualPath(*scene_key_opt);
        vpath.has_value()) {
        scene_entry.virtual_path = *vpath;
      }

      scenes.push_back(std::move(scene_entry));
    }
  }

  return scenes;
}

} // namespace oxygen::content::internal
