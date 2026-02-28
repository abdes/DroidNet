//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <optional>
#include <string_view>

#include <Oxygen/Cooker/Import/Internal/Utils/ImportSettingsUtils.h>
#include <Oxygen/Cooker/Import/ScriptImportRequestBuilder.h>

namespace oxygen::content::import::internal {

namespace {

  using core::meta::scripting::ScriptCompileMode;

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

  auto ValidateNoDotSegments(const std::string_view path) -> bool
  {
    size_t pos = 0;
    while (pos <= path.size()) {
      const auto next = path.find('/', pos);
      const auto len
        = (next == std::string_view::npos) ? (path.size() - pos) : (next - pos);
      const auto segment = path.substr(pos, len);
      if (segment == "." || segment == "..") {
        return false;
      }
      if (next == std::string_view::npos) {
        break;
      }
      pos = next + 1;
    }
    return true;
  }

  auto IsCanonicalVirtualPath(const std::string_view virtual_path) -> bool
  {
    if (virtual_path.empty()) {
      return false;
    }
    if (virtual_path.front() != '/') {
      return false;
    }
    if (virtual_path.find('\\') != std::string_view::npos) {
      return false;
    }
    if (virtual_path.find("//") != std::string_view::npos) {
      return false;
    }
    if (virtual_path.size() > 1 && virtual_path.back() == '/') {
      return false;
    }
    if (!ValidateNoDotSegments(virtual_path)) {
      return false;
    }
    return true;
  }

  auto BuildBaseImportRequest(const std::string& source_path,
    const std::string& cooked_root, const std::string& job_name,
    const bool with_content_hashing, std::ostream& error_stream)
    -> std::optional<ImportRequest>
  {
    ImportRequest request {};
    request.source_path = source_path;

    if (cooked_root.empty()) {
      error_stream << "ERROR: --output or --cooked-root is required\n";
      return std::nullopt;
    }

    std::filesystem::path root(cooked_root);
    if (!root.is_absolute()) {
      error_stream << "ERROR: cooked root must be an absolute path\n";
      return std::nullopt;
    }
    request.cooked_root = root;

    if (!job_name.empty()) {
      request.job_name = job_name;
    } else {
      const auto stem = request.source_path.stem().string();
      if (!stem.empty()) {
        request.job_name = stem;
      }
    }

    request.options.with_content_hashing
      = EffectiveContentHashingEnabled(with_content_hashing);
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
  auto request
    = BuildBaseImportRequest(settings.source_path, settings.cooked_root,
      settings.job_name, settings.with_content_hashing, error_stream);
  if (!request.has_value()) {
    return std::nullopt;
  }

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

  if (settings.compile_scripts
    && *storage_mode == ScriptStorageMode::kExternal) {
    error_stream << "ERROR: compile_scripts=true is invalid with "
                    "script_storage=external\n";
    return std::nullopt;
  }

  request->options.scripting.import_kind = ScriptingImportKind::kScriptAsset;
  request->options.scripting.compile_scripts = settings.compile_scripts;
  request->options.scripting.compile_mode = *compile_mode;
  request->options.scripting.script_storage = *storage_mode;
  request->options.scripting.target_scene_virtual_path.clear();
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
  auto request
    = BuildBaseImportRequest(settings.source_path, settings.cooked_root,
      settings.job_name, settings.with_content_hashing, error_stream);
  if (!request.has_value()) {
    return std::nullopt;
  }

  if (settings.target_scene_virtual_path.empty()) {
    error_stream << "ERROR: target_scene_virtual_path is required\n";
    return std::nullopt;
  }

  if (!IsCanonicalVirtualPath(settings.target_scene_virtual_path)) {
    error_stream << "ERROR: target_scene_virtual_path must be a canonical "
                    "virtual path\n";
    return std::nullopt;
  }

  request->options.scripting.import_kind
    = ScriptingImportKind::kScriptingSidecar;
  request->options.scripting.compile_scripts = false;
  request->options.scripting.compile_mode = ScriptCompileMode::kDebug;
  request->options.scripting.script_storage = ScriptStorageMode::kEmbedded;
  request->options.scripting.target_scene_virtual_path
    = settings.target_scene_virtual_path;
  return request;
}

} // namespace oxygen::content::import::internal
