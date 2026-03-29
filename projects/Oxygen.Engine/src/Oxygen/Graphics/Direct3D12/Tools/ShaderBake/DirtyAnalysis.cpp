//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/DirtyAnalysis.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/ActionKey.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/BuildState.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/CompileProfile.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/FileFingerprint.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

namespace {

  auto BuildRequestKeySet(std::span<const ExpandedShaderRequest> requests)
    -> std::unordered_set<uint64_t>
  {
    std::unordered_set<uint64_t> keys;
    keys.reserve(requests.size());
    for (const auto& request : requests) {
      keys.insert(request.request_key);
    }
    return keys;
  }

  auto LoadPreviousManifest(const std::filesystem::path& manifest_path)
    -> std::optional<ManifestSnapshot>
  {
    try {
      return ReadManifestFile(manifest_path);
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }

  auto BuildPreviousManifestKeySet(
    const std::optional<ManifestSnapshot>& previous_manifest)
    -> std::unordered_set<uint64_t>
  {
    std::unordered_set<uint64_t> keys;
    if (!previous_manifest.has_value()) {
      return keys;
    }

    keys.reserve(previous_manifest->requests.size());
    for (const auto& request : previous_manifest->requests) {
      keys.insert(request.request_key);
    }
    return keys;
  }

  auto BuildStateModuleMap(const BuildStateSnapshot& state)
    -> std::unordered_map<uint64_t, BuildStateModuleRecord>
  {
    std::unordered_map<uint64_t, BuildStateModuleRecord> modules;
    modules.reserve(state.modules.size());
    for (const auto& module : state.modules) {
      modules.emplace(module.request_key, module);
    }
    return modules;
  }

  auto GetFileWriteTimeUtcTicks(const std::filesystem::path& file_path)
    -> std::optional<int64_t>
  {
    std::error_code ec;
    const auto write_time = std::filesystem::last_write_time(file_path, ec);
    if (ec) {
      return std::nullopt;
    }
    return static_cast<int64_t>(write_time.time_since_epoch().count());
  }

  auto MatchesDependencyFingerprint(const std::filesystem::path& workspace_root,
    const DependencyFingerprint& dependency) -> bool
  {
    const auto absolute_path
      = (workspace_root / std::filesystem::path(dependency.path))
          .lexically_normal();

    std::error_code ec;
    if (!std::filesystem::is_regular_file(absolute_path, ec) || ec) {
      return false;
    }

    ec.clear();
    const auto size_bytes = std::filesystem::file_size(absolute_path, ec);
    if (ec) {
      return false;
    }

    const auto write_time_utc = GetFileWriteTimeUtcTicks(absolute_path);
    if (!write_time_utc.has_value()) {
      return false;
    }

    if (size_bytes == dependency.size_bytes
      && *write_time_utc == dependency.write_time_utc) {
      return true;
    }

    const auto current = ComputeFileFingerprint(absolute_path, workspace_root);
    return current.content_hash == dependency.content_hash;
  }

  auto MatchesPrimarySourceHash(const std::filesystem::path& workspace_root,
    const std::filesystem::path& shader_source_root,
    const ShaderRequest& request, const uint64_t stored_primary_hash) -> bool
  {
    const auto source_path
      = (shader_source_root / request.source_path).lexically_normal();

    std::error_code ec;
    if (!std::filesystem::is_regular_file(source_path, ec) || ec) {
      return false;
    }

    return ComputeFileFingerprint(source_path, workspace_root).content_hash
      == stored_primary_hash;
  }

  auto AnalyzeOneRequest(const std::filesystem::path& workspace_root,
    const std::filesystem::path& shader_source_root,
    std::span<const std::filesystem::path> include_dirs,
    const bool has_previous_manifest,
    const std::unordered_set<uint64_t>& previous_manifest_keys,
    const std::unordered_map<uint64_t, BuildStateModuleRecord>&
      previous_modules,
    const BuildRootLayout& layout,
    const std::filesystem::path& final_archive_path,
    const ExpandedShaderRequest& expanded_request) -> RequestDirtyAnalysis
  {
    RequestDirtyAnalysis analysis {
      .expanded_request = expanded_request,
      .action_key
      = ComputeShaderActionKey(expanded_request.request, include_dirs),
      .artifact_path
      = GetModuleArtifactPath(layout, expanded_request.request_key),
    };

    if (has_previous_manifest
      && previous_manifest_keys.find(expanded_request.request_key)
        == previous_manifest_keys.end()) {
      analysis.dirty_reasons.push_back(DirtyReason::kNewManifestMembership);
    }

    const auto previous_it
      = previous_modules.find(expanded_request.request_key);
    const auto artifact = TryReadModuleArtifactFile(analysis.artifact_path);
    if (!artifact.has_value()) {
      analysis.dirty_reasons.push_back(previous_it == previous_modules.end()
          ? DirtyReason::kMissingArtifact
          : DirtyReason::kInvalidArtifact);
      return analysis;
    }

    if (artifact->request != expanded_request.request) {
      analysis.dirty_reasons.push_back(DirtyReason::kStoredRequestMismatch);
    }
    if (artifact->action_key != analysis.action_key) {
      analysis.dirty_reasons.push_back(DirtyReason::kActionKeyMismatch);
    }

    const auto source_path
      = (shader_source_root / expanded_request.request.source_path)
          .lexically_normal();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(source_path, ec) || ec) {
      analysis.dirty_reasons.push_back(DirtyReason::kMissingPrimarySource);
    } else if (!MatchesPrimarySourceHash(workspace_root, shader_source_root,
                 expanded_request.request, artifact->primary_hash)) {
      analysis.dirty_reasons.push_back(DirtyReason::kPrimarySourceChanged);
    }

    for (const auto& dependency : artifact->dependencies) {
      const auto absolute_path
        = (workspace_root / std::filesystem::path(dependency.path))
            .lexically_normal();
      ec.clear();
      if (!std::filesystem::is_regular_file(absolute_path, ec) || ec) {
        analysis.dirty_reasons.push_back(DirtyReason::kMissingDependency);
        break;
      }

      if (!MatchesDependencyFingerprint(workspace_root, dependency)) {
        analysis.dirty_reasons.push_back(DirtyReason::kDependencyChanged);
        break;
      }
    }

    if (IsExternalShaderDebugInfoEnabled()) {
      std::error_code debug_ec;
      const auto pdb_path = GetRequestPdbPath(final_archive_path,
        expanded_request.request.source_path,
        expanded_request.request.entry_point, expanded_request.request_key);
      if (!std::filesystem::is_regular_file(pdb_path, debug_ec) || debug_ec) {
        analysis.dirty_reasons.push_back(DirtyReason::kMissingDebugArtifact);
      }
    }

    if (!analysis.IsDirty()) {
      analysis.reusable_artifact = std::move(*artifact);
    }
    return analysis;
  }

} // namespace

auto AnalyzeDirtyRequests(const std::filesystem::path& workspace_root,
  const std::filesystem::path& shader_source_root,
  const BuildRootLayout& layout,
  const std::filesystem::path& final_archive_path,
  std::span<const ExpandedShaderRequest> requests,
  std::span<const std::filesystem::path> include_dirs) -> DirtyAnalysisResult
{
  const auto current_manifest = BuildManifestSnapshot(requests);
  const auto previous_manifest = LoadPreviousManifest(layout.manifest_file);
  const auto previous_manifest_keys
    = BuildPreviousManifestKeySet(previous_manifest);
  const auto previous_state
    = LoadOrRecoverBuildStateFile(workspace_root, layout);
  const auto previous_modules = BuildStateModuleMap(previous_state);
  const auto current_request_keys = BuildRequestKeySet(requests);

  DirtyAnalysisResult result {
    .current_manifest = current_manifest,
    .manifest_changed = !previous_manifest.has_value()
      || (*previous_manifest != current_manifest),
    .final_archive_missing = !std::filesystem::exists(final_archive_path),
  };
  result.requests.reserve(requests.size());

  for (const auto& request : requests) {
    result.requests.push_back(
      AnalyzeOneRequest(workspace_root, shader_source_root, include_dirs,
        previous_manifest.has_value(), previous_manifest_keys, previous_modules,
        layout, final_archive_path, request));
  }

  for (const auto& previous_module : previous_state.modules) {
    if (current_request_keys.contains(previous_module.request_key)) {
      continue;
    }

    result.stale_artifact_paths.push_back(
      (layout.root / previous_module.module_artifact_relpath)
        .lexically_normal());
  }

  std::sort(
    result.stale_artifact_paths.begin(), result.stale_artifact_paths.end());
  result.stale_artifact_paths.erase(
    std::unique(
      result.stale_artifact_paths.begin(), result.stale_artifact_paths.end()),
    result.stale_artifact_paths.end());
  return result;
}

auto DirtyReasonToString(const DirtyReason reason) -> const char*
{
  switch (reason) {
  case DirtyReason::kMissingArtifact:
    return "missing-artifact";
  case DirtyReason::kInvalidArtifact:
    return "invalid-artifact";
  case DirtyReason::kStoredRequestMismatch:
    return "stored-request-mismatch";
  case DirtyReason::kActionKeyMismatch:
    return "action-key-mismatch";
  case DirtyReason::kMissingPrimarySource:
    return "missing-primary-source";
  case DirtyReason::kPrimarySourceChanged:
    return "primary-source-changed";
  case DirtyReason::kMissingDependency:
    return "missing-dependency";
  case DirtyReason::kDependencyChanged:
    return "dependency-changed";
  case DirtyReason::kMissingDebugArtifact:
    return "missing-debug-artifact";
  case DirtyReason::kNewManifestMembership:
    return "new-manifest-membership";
  default:
    return "unknown";
  }
}

} // namespace oxygen::graphics::d3d12::tools::shader_bake
