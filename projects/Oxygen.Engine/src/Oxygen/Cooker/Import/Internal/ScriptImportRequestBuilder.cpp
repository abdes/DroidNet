//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ScriptingSidecarSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/DescriptorDocument.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
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
    std::string& normalized_payload, std::ostream& error_stream) -> bool
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto prepared = PrepareScriptingSidecarDocument(
      payload_text, kInlineSidecarSourcePath, diagnostics);
    WriteDiagnostics(diagnostics, error_stream);
    if (!prepared.has_value()) {
      return false;
    }
    auto normalized = NormalizeScriptingSidecarText(payload_text);
    // The preparation parsed the same text.
    CHECK_F(normalized.has_value());
    normalized_payload = std::move(*normalized);
    return true;
  }

  //! Settings every script request shares, named so callers cannot swap them.
  struct BaseRequestSettings final {
    std::string_view source_path;
    std::string_view cooked_root;
    std::string_view job_name;
  };

  auto BuildBaseImportRequest(const BaseRequestSettings& settings,
    std::ostream& error_stream) -> std::optional<ImportRequest>
  {
    ImportRequest request {};
    request.source_path = settings.source_path;

    if (settings.cooked_root.empty()) {
      error_stream << "ERROR: --output or --cooked-root is required\n";
      return std::nullopt;
    }

    std::filesystem::path root(settings.cooked_root);
    if (!root.is_absolute()) {
      error_stream << "ERROR: cooked root must be an absolute path\n";
      return std::nullopt;
    }
    request.cooked_root = root;

    if (!settings.job_name.empty()) {
      request.job_name = std::string(settings.job_name);
    } else {
      const auto stem = request.source_path.stem().string();
      if (!stem.empty()) {
        request.job_name = stem;
      }
    }

    return request;
  }

} // namespace

/*!
 Convert tooling-facing script-asset settings into the shared runtime request
 model.

 This validates parsing and invariant rules, then writes normalized scripting
 semantics into `ImportRequest::options.scripting`.

 @param settings Tooling-facing script import settings.
 @param error_stream Validation failures are appended here.
 @return Normalized `ImportRequest` on success, `std::nullopt` when validation
 fails.
*/
auto BuildScriptAssetRequest(const ScriptAssetImportSettings& settings,
  std::ostream& error_stream) -> std::optional<ImportRequest>
{
  if (settings.source_path.empty()) {
    error_stream << "ERROR: source_path is required\n";
    return std::nullopt;
  }

  auto request = BuildBaseImportRequest(
    {
      .source_path = settings.source_path,
      .cooked_root = settings.cooked_root,
      .job_name = settings.job_name,
    },
    error_stream);
  if (!request.has_value()) {
    return std::nullopt;
  }
  request->asset_virtual_path = settings.virtual_path;

  const auto compile_mode = ParseScriptCompileMode(settings.compile_mode);
  if (!compile_mode.has_value()) {
    error_stream << "ERROR: invalid script compile mode; expected "
                    "'debug' or 'optimized'\n";
    return std::nullopt;
  }

  const auto storage_mode = ParseScriptStorageMode(settings.script_storage);
  if (!storage_mode.has_value()) {
    error_stream << "ERROR: invalid script storage mode; expected "
                    "'embedded' or 'external'\n";
    return std::nullopt;
  }

  request->options.scripting.import_kind = ScriptingImportKind::kScriptAsset;
  request->options.scripting.compile_scripts = settings.compile_scripts;
  request->options.scripting.compile_mode = *compile_mode;
  request->options.scripting.script_storage = *storage_mode;
  request->options.scripting.target_scene_virtual_path.clear();
  request->options.scripting.inline_bindings_json.clear();
  return request;
}

/*!
 Convert tooling-facing sidecar settings into the shared runtime request model.

 The builder validates canonical virtual-path requirements and writes normalized
 sidecar routing semantics into `ImportRequest::options.scripting`.

 @param settings Tooling-facing sidecar import settings.
 @param error_stream Validation failures are appended here.
 @return Normalized `ImportRequest` on success, `std::nullopt` when validation
 fails.
*/
auto BuildScriptingSidecarRequest(
  const ScriptingSidecarImportSettings& settings, std::ostream& error_stream)
  -> std::optional<ImportRequest>
{
  const auto has_source = !settings.source_path.empty();
  const auto has_inline = !settings.inline_bindings_json.empty();
  if (has_source == has_inline) {
    error_stream << "ERROR: exactly one of source_path or inline_bindings_json "
                    "must be provided\n";
    return std::nullopt;
  }

  auto normalized_inline_bindings = std::string {};
  if (has_inline
    && !NormalizeInlineSidecarPayload(settings.inline_bindings_json,
      normalized_inline_bindings, error_stream)) {
    return std::nullopt;
  }

  const auto source_path_for_request
    = has_source ? settings.source_path : std::string(kInlineSidecarSourcePath);

  auto request = BuildBaseImportRequest(
    {
      .source_path = source_path_for_request,
      .cooked_root = settings.cooked_root,
      .job_name = settings.job_name,
    },
    error_stream);
  if (!request.has_value()) {
    return std::nullopt;
  }

  if (settings.target_scene_virtual_path.empty()) {
    error_stream << "ERROR: target_scene_virtual_path is required\n";
    return std::nullopt;
  }

  if (!::oxygen::content::import::internal::IsCanonicalVirtualPath(
        settings.target_scene_virtual_path)) {
    error_stream << "ERROR: target_scene_virtual_path must be a canonical "
                    "virtual path\n";
    return std::nullopt;
  }

  for (const auto& root : settings.cooked_context_roots) {
    const auto path = std::filesystem::path(root);
    if (!path.is_absolute()) {
      error_stream << "ERROR: cooked context roots must be absolute paths\n";
      return std::nullopt;
    }
    request->cooked_context_roots.push_back(path.lexically_normal());
  }

  request->options.scripting.import_kind
    = ScriptingImportKind::kScriptingSidecar;
  request->options.scripting.compile_scripts = false;
  request->options.scripting.compile_mode = ScriptCompileMode::kDebug;
  request->options.scripting.script_storage = ScriptStorageMode::kEmbedded;
  request->options.scripting.target_scene_virtual_path
    = settings.target_scene_virtual_path;
  request->options.scripting.inline_bindings_json
    = std::move(normalized_inline_bindings);
  return request;
}

} // namespace oxygen::content::import::internal
