//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/Internal/ContentSourceRegistry.h>
#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Content/Internal/LooseCookedSource.h>
#include <Oxygen/Content/Internal/SceneCatalogQueryService.h>
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_core.h>

namespace oxygen::content::internal {

namespace {

  //! The scene's name.
  /*!
   A scene whose descriptor cannot be read is still listed, with an empty name
   and a warning, so loading it reports why instead of the scene vanishing.
  */
  auto ReadSceneName(const IContentSource& source, const data::AssetKey& key)
    -> std::string
  {
    auto desc_reader = source.CreateAssetDescriptorReader(key);
    if (!desc_reader) {
      LOG_F(
        WARNING, "scene {} has no readable descriptor", data::to_string(key));
      return {};
    }
    try {
      const auto base_pos = desc_reader->Position();
      loaders::CheckLoaderResult(base_pos, "scene", "position");
      auto header = data::pak::core::AssetHeader {};
      loaders::LoadAssetHeader(*desc_reader, header);
      return loaders::ResolveDescriptorString(
        loaders::ReadDescriptorStrings(*desc_reader, *base_pos, header),
        header.name_offset, "scene name");
    } catch (const std::runtime_error& error) {
      LOG_F(WARNING, "scene {} name is unreadable: {}", data::to_string(key),
        error.what());
      return {};
    }
  }

} // namespace

auto SceneCatalogQueryService::EnumerateMountedScenes(
  const ContentSourceRegistry& source_registry) const
  -> std::vector<IAssetLoader::MountedSceneEntry>
{
  std::vector<IAssetLoader::MountedSceneEntry> scenes;
  const auto view = source_registry.CaptureView();
  if (!view) {
    return scenes;
  }
  scenes.reserve(view->Layers().size());
  for (const auto& layer : view->Layers()) {
    const auto source = source_registry.AcquireSource(layer.id);
    if (!source) {
      continue;
    }
    const auto source_id = layer.id;
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

      if (source->GetAssetType(*scene_key_opt) != data::AssetType::kScene
        || view->ResolveAsset(*scene_key_opt) != source_id) {
        continue;
      }

      IAssetLoader::MountedSceneEntry scene_entry {};
      scene_entry.scene_key = *scene_key_opt;
      scene_entry.source_key = source_key;
      scene_entry.source_id = source_id;
      scene_entry.source_kind = source_kind;
      scene_entry.source_path = source_path;
      scene_entry.display_name = ReadSceneName(*source, *scene_key_opt);

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
