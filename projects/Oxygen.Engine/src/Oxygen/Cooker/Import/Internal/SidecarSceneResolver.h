//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Import/Internal/Utils/MountedCookedSources.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>

namespace oxygen::content::import {
struct ImportRequest;
class ImportSession;
} // namespace oxygen::content::import

namespace oxygen::content::import::detail {

//! Diagnostic-code selection for sidecar target-scene resolution.
struct SidecarSceneResolverDiagnostics final {
  std::string_view inflight_target_scene_ambiguous_code;
  std::string_view target_scene_invalid_code;
  std::string_view target_scene_not_scene_code;
  std::string_view target_scene_read_failed_code;
  std::string_view target_scene_virtual_path_invalid_code;
  std::string_view target_scene_missing_code;
};

//! Resolved target scene binding state used by sidecar pipelines.
struct ResolvedSceneState final {
  data::AssetKey scene_key;
  std::string scene_virtual_path;
  std::string scene_descriptor_relpath;
  std::vector<std::byte> source_scene_descriptor;
  data::AssetReferences references;
  size_t environment_offset = 0;
  uint32_t node_count = 0;
  //! The layout hash of its nodes (`data::ComputeNodeLayoutHash`).
  data::pak::core::ContentHashDigest node_layout_hash {};
  std::vector<data::pak::scripting::ScriptingComponentRecord>
    existing_scripting_components;
};

//! Read the descriptor of the asset `key` a mounted cooked root lists;
//! nullopt when the root holds no content or the descriptor cannot be read.
OXGN_COOK_NDAPI auto ReadCookedDescriptor(
  const internal::MountedCookedSource& mount, const data::AssetKey& key)
  -> std::optional<std::vector<std::byte>>;

//! Resolve the target scene of a sidecar, its identity: a scene the same
//! batch imports, else the scene a mounted cooked root lists.
/*!
 The identity is a canonical virtual path, refused with
 `target_scene_virtual_path_invalid_code` otherwise. It is found the way any
 reference is (`internal::LookupReference`), so a sidecar and the jobs it
 completes read the same cooked roots.
*/
OXGN_COOK_NDAPI auto ResolveTargetSceneState(ImportSession& session,
  const ImportRequest& request,
  std::span<const internal::MountedCookedSource> mounts,
  std::string_view target_scene_virtual_path,
  const SidecarSceneResolverDiagnostics& diagnostics)
  -> std::optional<ResolvedSceneState>;

} // namespace oxygen::content::import::detail
