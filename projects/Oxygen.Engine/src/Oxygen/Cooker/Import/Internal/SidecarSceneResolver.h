//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content {
class VirtualPathResolver;
} // namespace oxygen::content

namespace oxygen::content::import {
class IAsyncFileReader;
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
  uint32_t node_count = 0;
  std::vector<data::pak::scripting::ScriptingComponentRecord>
    existing_scripting_components;
};

//! Mounted cooked-root inspection context used for resolution.
struct CookedInspectionContext final {
  std::filesystem::path cooked_root;
  lc::Inspection inspection;
};

//! Load one inspection context from cooked root.
OXGN_COOK_API auto LoadCookedInspectionContext(
  const std::filesystem::path& cooked_root, ImportSession& session,
  const ImportRequest& request,
  const SidecarSceneResolverDiagnostics& diagnostics,
  CookedInspectionContext& out_context) -> bool;

//! Resolve the mounted context containing a given scene key (highest
//! precedence).
OXGN_COOK_API auto ResolveSceneInspectionContextByKey(
  std::span<const CookedInspectionContext> cooked_contexts,
  const data::AssetKey& scene_key) -> const CookedInspectionContext*;

//! Resolve target scene state from inflight contexts and mounted cooked roots.
OXGN_COOK_API auto ResolveTargetSceneState(ImportSession& session,
  const ImportRequest& request, content::VirtualPathResolver& resolver,
  std::span<const CookedInspectionContext> cooked_contexts,
  IAsyncFileReader& reader, std::string_view target_scene_virtual_path,
  const SidecarSceneResolverDiagnostics& diagnostics)
  -> co::Co<std::optional<ResolvedSceneState>>;

} // namespace oxygen::content::import::detail
