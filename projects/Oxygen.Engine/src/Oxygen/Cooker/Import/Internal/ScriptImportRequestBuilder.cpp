//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ImportRequestCodes.h>
#include <Oxygen/Cooker/Import/Internal/ScriptingSidecarSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/RequestPreparation.h>
#include <Oxygen/Cooker/Import/ScriptImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/ScriptImportSettings.h>
#include <Oxygen/Core/Meta/Scripting/ScriptCompileMode.h>

namespace oxygen::content::import::internal {

namespace {

  using core::meta::scripting::ScriptCompileMode;

  //! The source path an inline sidecar reports its diagnostics under.
  constexpr std::string_view kInlineSidecarSourcePath
    = "inline://script-sidecar";

  auto ParseScriptCompileMode(const std::string_view mode)
    -> std::optional<ScriptCompileMode>
  {
    if (mode == "debug") {
      return ScriptCompileMode::kDebug;
    }
    if (mode == "optimized") {
      return ScriptCompileMode::kOptimized;
    }
    return std::nullopt;
  }

  auto ParseScriptStorageMode(const std::string_view storage)
    -> std::optional<ScriptStorageMode>
  {
    if (storage == "embedded") {
      return ScriptStorageMode::kEmbedded;
    }
    if (storage == "external") {
      return ScriptStorageMode::kExternal;
    }
    return std::nullopt;
  }

  //! Prepares the inline sidecar as the import will, and returns it in the form
  //! the request carries.
  auto NormalizeInlineSidecarPayload(const std::string_view payload_text,
    std::vector<ImportDiagnostic>& diagnostics) -> std::optional<std::string>
  {
    if (!PrepareScriptingSidecarDocument(
          payload_text, kInlineSidecarSourcePath, diagnostics)
          .has_value()) {
      return std::nullopt;
    }
    auto normalized = NormalizeScriptingSidecarText(payload_text);
    // The preparation parsed the same text.
    CHECK_F(normalized.has_value());
    return normalized;
  }

} // namespace

/*!
 Convert tooling-facing script-asset settings into the shared runtime request
 model.

 This validates parsing and invariant rules, then writes normalized scripting
 semantics into `ImportRequest::options.scripting`.

 @param settings Tooling-facing script import settings.
 @param diagnostics Validation failures are added here.
 @return Normalized `ImportRequest` on success, `std::nullopt` when validation
 fails.
*/
auto BuildScriptAssetRequest(const ScriptAssetImportSettings& settings,
  std::vector<ImportDiagnostic>& diagnostics) -> std::optional<ImportRequest>
{
  if (settings.source_path.empty()) {
    RefuseSetting(diagnostics, request_codes::kSourceMissing,
      "source_path is required", "/source");
    return std::nullopt;
  }

  auto request = ImportRequest {};
  request.source_path = settings.source_path;
  if (!RequireCookedRoot(settings.cooked_root, request, diagnostics)) {
    return std::nullopt;
  }
  ApplyJobName(settings.job_name, request);
  request.asset_virtual_path = settings.virtual_path;

  const auto compile_mode = ParseOption(
    "compile_mode", settings.compile_mode, ParseScriptCompileMode, diagnostics);
  if (!compile_mode.has_value()) {
    return std::nullopt;
  }

  const auto storage_mode = ParseOption("script_storage",
    settings.script_storage, ParseScriptStorageMode, diagnostics);
  if (!storage_mode.has_value()) {
    return std::nullopt;
  }

  request.options.scripting.import_kind = ScriptingImportKind::kScriptAsset;
  request.options.scripting.compile_scripts = settings.compile_scripts;
  request.options.scripting.compile_mode = *compile_mode;
  request.options.scripting.script_storage = *storage_mode;
  request.options.scripting.target_scene_virtual_path.clear();
  request.options.scripting.inline_bindings_json.clear();
  return request;
}

/*!
 Convert tooling-facing sidecar settings into the shared runtime request model.

 The builder validates canonical virtual-path requirements and writes normalized
 sidecar routing semantics into `ImportRequest::options.scripting`.

 @param settings Tooling-facing sidecar import settings.
 @param diagnostics Validation failures are added here.
 @return Normalized `ImportRequest` on success, `std::nullopt` when validation
 fails.
*/
auto BuildScriptingSidecarRequest(
  const ScriptingSidecarImportSettings& settings,
  std::vector<ImportDiagnostic>& diagnostics) -> std::optional<ImportRequest>
{
  const auto has_source = !settings.source_path.empty();
  const auto has_inline = !settings.inline_bindings_json.empty();
  if (!CheckSidecarSource(has_source, has_inline, diagnostics)) {
    return std::nullopt;
  }

  auto normalized_inline_bindings = std::string {};
  if (has_inline) {
    auto normalized = NormalizeInlineSidecarPayload(
      settings.inline_bindings_json, diagnostics);
    if (!normalized.has_value()) {
      return std::nullopt;
    }
    normalized_inline_bindings = std::move(*normalized);
  }

  auto request = ImportRequest {};
  request.source_path
    = has_source ? settings.source_path : std::string(kInlineSidecarSourcePath);
  if (!RequireCookedRoot(settings.cooked_root, request, diagnostics)
    || !CheckTargetScene(settings.target_scene_virtual_path, diagnostics)
    || !ApplyCookedContextRoots(
      settings.cooked_context_roots, request, diagnostics)) {
    return std::nullopt;
  }
  ApplyJobName(settings.job_name, request);

  request.options.scripting.import_kind
    = ScriptingImportKind::kScriptingSidecar;
  request.options.scripting.compile_scripts = false;
  request.options.scripting.compile_mode = ScriptCompileMode::kDebug;
  request.options.scripting.script_storage = ScriptStorageMode::kEmbedded;
  request.options.scripting.target_scene_virtual_path
    = settings.target_scene_virtual_path;
  request.options.scripting.inline_bindings_json
    = std::move(normalized_inline_bindings);
  return request;
}

} // namespace oxygen::content::import::internal
