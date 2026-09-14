//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <memory>
#include <stdexcept>

#include <Oxygen/Content/DescriptorDependencies.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Data/SceneAsset.h>

namespace oxygen::content {

auto InspectDescriptorDependencies(serio::AnyReader& reader,
  const data::AssetKey& key, const data::AssetType type)
  -> DescriptorDependencies
{
  auto collector = std::make_shared<internal::DependencyCollector>();
  const LoaderContext context {
    .current_asset_key = key,
    .desc_reader = &reader,
    .work_offline = true,
    .dependency_collector = collector,
    .parse_only = true,
  };
  DescriptorDependencies result;
  std::unique_ptr<data::Asset> asset;
  switch (type) {
  case data::AssetType::kGeometry:
    asset = loaders::LoadGeometryAsset(context);
    break;
  case data::AssetType::kMaterial:
    asset = loaders::LoadMaterialAsset(context);
    break;
  case data::AssetType::kScene: {
    auto scene = loaders::LoadSceneAsset(context);
    if (!scene->GetComponents<data::pak::scripting::ScriptingComponentRecord>()
          .empty()) {
      result.complete = false;
      result.explanation
        = "Scene script dependencies require source sidecar inspection.";
    }
    asset = std::move(scene);
    break;
  }
  default:
    result.complete = false;
    result.explanation
      = "Asset dependency inspection is unavailable for this asset type.";
    return result;
  }
  if (!asset || asset->GetAssetType() != type) {
    throw std::runtime_error(
      "Descriptor type does not match its container entry.");
  }
  result.assets = collector->AssetDependencies();
  std::erase(result.assets, data::AssetKey {});
  std::ranges::sort(result.assets);
  return result;
}

} // namespace oxygen::content
