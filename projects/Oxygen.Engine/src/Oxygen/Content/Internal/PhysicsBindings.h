//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/SourceOrigin.h>

namespace oxygen::content::internal {
class IContentSource;

struct BoundPhysicsShape final {
  data::AssetKey key {};
  data::pak::physics::CollisionShapeAssetDesc descriptor {};
};

struct BoundPhysicsMaterial final {
  data::AssetKey key {};
  data::pak::physics::PhysicsMaterialAssetDesc descriptor {};
};

struct BoundPhysicsResource final {
  data::AssetKey asset_key {};
  ResourceKey key {};
  data::SourceInstanceId source {};
  std::shared_ptr<const IContentSource> owner {};
};

//! Fixed descriptors and deferred payload locations captured before
//! publication.
struct PhysicsBindings final {
  std::vector<BoundPhysicsShape> shapes {};
  std::vector<BoundPhysicsMaterial> materials {};
  std::vector<BoundPhysicsResource> resources {};
};
} // namespace oxygen::content::internal
