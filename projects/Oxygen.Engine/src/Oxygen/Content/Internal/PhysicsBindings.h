//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <Oxygen/Content/CollisionShapeData.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/SourceOrigin.h>

namespace oxygen::content::internal {
class IContentSource;

struct BoundPhysicsShape final {
  data::AssetKey key;
  CollisionShapeData shape {};
};

struct BoundPhysicsMaterial final {
  data::AssetKey key;
  data::pak::physics::PhysicsMaterialAssetDesc descriptor {};
};

struct BoundPhysicsResource final {
  data::AssetKey asset_key;
  ResourceKey key {};
  data::SourceInstanceId source;
  std::shared_ptr<const IContentSource> owner;
};

//! Fixed descriptors and deferred payload locations captured before
//! publication.
/*!
 A physics scene asset binds its shapes, materials and payloads. A scene binds
 the one physics sidecar that targets it in its load's mounts, if any.
*/
struct PhysicsBindings final {
  std::vector<BoundPhysicsShape> shapes;
  std::vector<BoundPhysicsMaterial> materials;
  std::vector<BoundPhysicsResource> resources;
  std::optional<data::AssetKey> sidecar;
};
} // namespace oxygen::content::internal
