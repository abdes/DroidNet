//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Core/Meta/Physics/Backend.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_core.h>

namespace oxygen::content::import::internal {

//! A physics sidecar a re-cook of its scene cooks again.
/*!
 A scene and its physics sidecar are one regeneration unit (`physics.md`): a
 scene re-cooked with other nodes takes along each sidecar the cooked root
 lists for it that was cooked from a file of its own.
*/
struct ScenePhysicsFollower final {
  //! The cooked sidecar, such as `/Content/Scenes/Hall.opscene`.
  std::string virtual_path;
  //! The mount path of its source, such as
  //! `/Content/Scenes/Hall.physics.json`.
  std::string source_mount_path;
  //! The backend it was cooked for.
  core::meta::physics::PhysicsBackend backend
    = core::meta::physics::PhysicsBackend::kNone;
};

//! The physics sidecars `root` lists for the scene `scene_key` that were
//! cooked for another node layout than `scene_layout`
//! (`data::ComputeNodeLayoutHash`) and from a file of their own.
/*!
 A sidecar that records its scene's source has no file of its own, as when
 its bindings were given inline; it cannot follow its scene, and a commit
 that keeps it stale is refused (`physics.sidecar.stale`).

 @throw std::runtime_error When a sidecar's descriptor is truncated or names
   no backend.
*/
OXGN_COOK_NDAPI auto FindScenePhysicsFollowers(
  const cooked::CookedContent& root, const data::AssetKey& scene_key,
  const data::pak::core::ContentHashDigest& scene_layout)
  -> std::vector<ScenePhysicsFollower>;

//! The request cooking `follower` again from `source`, for the scene in
//! flight `scene`, into the cooked root and with the mounts of
//! `scene_request`.
OXGN_COOK_NDAPI auto BuildScenePhysicsFollowerRequest(
  const ImportRequest& scene_request, const ScenePhysicsFollower& follower,
  const std::filesystem::path& source,
  ImportRequest::InflightSceneContext scene,
  std::vector<ImportDiagnostic>& diagnostics) -> std::optional<ImportRequest>;

} // namespace oxygen::content::import::internal
