//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/SourceOrigin.h>

namespace oxygen::content::internal {

class PhysicsQueryService final {
public:
  struct Callbacks final {
    std::function<std::optional<data::SourceInstanceId>(const data::AssetKey&)>
      resolve_source_id_for_asset;
    std::function<std::shared_ptr<const IContentSource>(data::SourceInstanceId)>
      resolve_source_for_id;
    std::function<std::optional<data::SourceInstanceId>(data::SourceKey)>
      resolve_source_id_for_source_key;
    std::function<ResourceKey(
      data::SourceInstanceId, data::pak::core::ResourceIndexT)>
      make_physics_resource_key;
  };

  [[nodiscard]] auto MakePhysicsResourceKey(data::SourceKey source_key,
    data::pak::core::ResourceIndexT resource_index, const Callbacks& callbacks)
    -> std::optional<ResourceKey>;

  [[nodiscard]] auto MakePhysicsResourceKeyForAsset(
    const data::AssetKey& context_asset_key,
    data::pak::core::ResourceIndexT resource_index, const Callbacks& callbacks)
    -> std::optional<ResourceKey>;

  [[nodiscard]] auto MakePhysicsResourceKeyForAsset(
    const data::AssetKey& context_asset_key,
    const data::AssetKey& resource_asset_key, const Callbacks& callbacks)
    -> std::optional<ResourceKey>;

  [[nodiscard]] auto ReadCollisionShapeAssetDescForAsset(
    const data::AssetKey& context_asset_key,
    const data::AssetKey& shape_asset_key, const Callbacks& callbacks) const
    -> std::optional<data::pak::physics::CollisionShapeAssetDesc>;

  [[nodiscard]] auto ReadPhysicsMaterialAssetDescForAsset(
    const data::AssetKey& context_asset_key,
    const data::AssetKey& material_asset_key, const Callbacks& callbacks) const
    -> std::optional<data::pak::physics::PhysicsMaterialAssetDesc>;

  [[nodiscard]] auto FindPhysicsSidecarAssetKeyForScene(
    const data::AssetKey& scene_key, const Callbacks& callbacks) const
    -> std::optional<data::AssetKey>;
};

} // namespace oxygen::content::internal
