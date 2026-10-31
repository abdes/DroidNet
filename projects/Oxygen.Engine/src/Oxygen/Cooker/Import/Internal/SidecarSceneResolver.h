//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>

namespace oxygen::content {
class VirtualPathResolver;
} // namespace oxygen::content

namespace oxygen::content::import {
struct ImportRequest;
class ImportSession;
} // namespace oxygen::content::import

namespace oxygen::content::import::detail {

//! Diagnostic-code selection for sidecar target-scene resolution.
struct SidecarSceneResolverDiagnostics final {
  std::string_view index_load_failed_code;
  std::string_view inflight_target_scene_ambiguous_code;
  std::string_view target_scene_invalid_code;
  std::string_view target_scene_not_scene_code;
  std::string_view target_scene_read_failed_code;
  std::string_view target_scene_virtual_path_invalid_code;
  std::string_view target_scene_missing_code;
};

//! Resolved target scene binding state used by sidecar pipelines.
struct ResolvedSceneState final {
  data::AssetKey scene_key {};
  std::string scene_virtual_path;
  std::string scene_descriptor_relpath;
  std::vector<std::byte> source_scene_descriptor;
  data::AssetReferences references;
  size_t environment_offset = 0;
  uint32_t node_count = 0;
  std::vector<data::pak::scripting::ScriptingComponentRecord>
    existing_scripting_components;
};

//! A mounted cooked root used for resolution, with its content.
/*!
 The content keeps files of the root open (`cooked::CookedContent`): release
 it before the import rewrites a file of that root.
*/
struct CookedInspectionContext final {
  std::filesystem::path cooked_root;
  cooked::CookedContent content;
};

//! Opens the cooked root `cooked_root` as a context; nullopt, with
//! `index_load_failed_code`, when it cannot be opened.
OXGN_COOK_NDAPI auto LoadCookedInspectionContext(
  const std::filesystem::path& cooked_root, ImportSession& session,
  const ImportRequest& request,
  const SidecarSceneResolverDiagnostics& diagnostics)
  -> std::optional<CookedInspectionContext>;

//! Mount a loaded context, a pak or a loose root, into a resolver.
OXGN_COOK_API auto MountCookedInspectionContext(
  content::VirtualPathResolver& resolver,
  const CookedInspectionContext& context) -> void;

//! Read the descriptor of the asset `key` a loaded context lists; nullopt
//! when it cannot be read.
OXGN_COOK_NDAPI auto ReadCookedDescriptor(
  const CookedInspectionContext& context, const data::AssetKey& key)
  -> std::optional<std::vector<std::byte>>;

//! Resolve the mounted context containing a given scene key (highest
//! precedence).
OXGN_COOK_API auto ResolveSceneInspectionContextByKey(
  std::span<const CookedInspectionContext> cooked_contexts,
  const data::AssetKey& scene_key) -> const CookedInspectionContext*;

//! Resolve target scene state from inflight contexts and mounted cooked roots.
OXGN_COOK_NDAPI auto ResolveTargetSceneState(ImportSession& session,
  const ImportRequest& request, content::VirtualPathResolver& resolver,
  std::span<const CookedInspectionContext> cooked_contexts,
  std::string_view target_scene_virtual_path,
  const SidecarSceneResolverDiagnostics& diagnostics)
  -> std::optional<ResolvedSceneState>;

} // namespace oxygen::content::import::detail
