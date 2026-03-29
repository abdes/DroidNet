//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Execution.h>

#include <algorithm>
#include <stdexcept>
#include <system_error>
#include <unordered_set>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/BuildState.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/DirtyAnalysis.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/FinalArchivePack.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

namespace {

  auto JoinDirtyReasons(std::span<const DirtyReason> reasons) -> std::string
  {
    std::string joined;
    for (const auto reason : reasons) {
      if (!joined.empty()) {
        joined += ",";
      }
      joined += DirtyReasonToString(reason);
    }
    return joined;
  }

  auto RemovePathIfPresent(const std::filesystem::path& path) -> bool
  {
    std::error_code ec;
    const auto removed = std::filesystem::remove(path, ec);
    if (ec) {
      throw std::runtime_error(
        fmt::format("failed to remove `{}`: {}", path.string(), ec.message()));
    }
    return removed;
  }

  auto RemovePaths(std::span<const std::filesystem::path> paths,
    std::string_view label) -> size_t
  {
    size_t removed_count = 0;
    for (const auto& path : paths) {
      if (RemovePathIfPresent(path)) {
        ++removed_count;
        LOG_F(INFO, "removed {} {}", label, path.string());
      }
    }
    return removed_count;
  }

  auto ClearRequestDiagnosticsLog(
    const BuildRootLayout& layout, const uint64_t request_key) -> void
  {
    (void)RemovePathIfPresent(GetRequestLogPath(layout, request_key));
  }

  auto WriteRequestDiagnosticsLog(const ExecutionOptions& options,
    const uint64_t request_key, std::string_view diagnostics) -> void
  {
    if (options.mode != ShaderBakeMode::kDev) {
      return;
    }

    const auto log_path = GetRequestLogPath(options.layout, request_key);
    const auto contents = diagnostics.empty()
      ? std::string("request compilation failed\n")
      : std::string(diagnostics);
    WriteTextFileAtomically(log_path, contents);
    LOG_F(ERROR, "Wrote failure diagnostics to {}", log_path.string());
  }

  auto CollectTrackedRequestKeyHexes(
    std::span<const ExpandedShaderRequest> requests)
    -> std::unordered_set<std::string>
  {
    std::unordered_set<std::string> request_keys;
    request_keys.reserve(requests.size());
    for (const auto& request : requests) {
      request_keys.insert(RequestKeyToHex(request.request_key));
    }
    return request_keys;
  }

  auto CollectPrunableFiles(const std::filesystem::path& root,
    std::string_view extension,
    const std::unordered_set<std::string>& live_request_keys)
    -> std::vector<std::filesystem::path>
  {
    std::vector<std::filesystem::path> paths;
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) {
      return paths;
    }
    if (ec) {
      throw std::runtime_error(
        fmt::format("failed to inspect `{}`: {}", root.string(), ec.message()));
    }

    for (std::filesystem::recursive_directory_iterator it(root, ec), end;
      it != end; it.increment(ec)) {
      if (ec) {
        throw std::runtime_error(
          fmt::format("failed to scan `{}`: {}", root.string(), ec.message()));
      }
      if (!it->is_regular_file()) {
        continue;
      }

      const auto& path = it->path();
      if (path.extension() != extension) {
        continue;
      }

      const auto stem = path.stem().string();
      if (!live_request_keys.contains(stem)) {
        paths.push_back(path);
      }
    }
    return paths;
  }

  auto CollectStaleLogPaths(const ExecutionOptions& options)
    -> std::vector<std::filesystem::path>
  {
    const auto live_request_keys
      = CollectTrackedRequestKeyHexes(options.requests);
    auto paths = CollectPrunableFiles(
      options.layout.logs_dir, ".log", live_request_keys);
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
  }

  auto CollectUpdateStaleArtifactPaths(
    const ExecutionOptions& options, const DirtyAnalysisResult& dirty_analysis)
    -> std::vector<std::filesystem::path>
  {
    std::vector<std::filesystem::path> paths
      = dirty_analysis.stale_artifact_paths;
    const auto live_request_keys
      = CollectTrackedRequestKeyHexes(options.requests);
    auto scanned_paths = CollectPrunableFiles(
      options.layout.modules_dir, ".oxsm", live_request_keys);
    paths.insert(paths.end(), scanned_paths.begin(), scanned_paths.end());

    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
  }

  auto PersistBuildState(const ExecutionOptions& options,
    const std::vector<ModuleArtifact>& artifacts,
    const ManifestSnapshot& manifest_snapshot) -> void
  {
    WriteBuildStateFile(options.layout.build_state_file,
      BuildBuildStateSnapshot(
        options.workspace_root, options.layout, artifacts));
    WriteManifestFile(options.layout.manifest_file, manifest_snapshot);
  }

} // namespace

auto ExecuteUpdate(const ExecutionOptions& options)
  -> std::optional<ExecutionResult>
{
  auto dirty_analysis = AnalyzeDirtyRequests(options.workspace_root,
    options.shader_source_root, options.layout, options.final_archive_path,
    options.requests, options.include_dirs);

  size_t dirty_count = 0;
  for (const auto& request_analysis : dirty_analysis.requests) {
    if (request_analysis.IsDirty()) {
      ++dirty_count;
    }
  }
  LOG_F(INFO, "dirty_requests={} clean_requests={} stale_requests={}",
    dirty_count, dirty_analysis.requests.size() - dirty_count,
    dirty_analysis.stale_artifact_paths.size());

  std::vector<ModuleArtifact> artifacts;
  artifacts.reserve(dirty_analysis.requests.size());

  size_t index = 0;
  size_t compiled_count = 0;
  size_t clean_count = 0;
  for (auto& request_analysis : dirty_analysis.requests) {
    ++index;
    const auto request_key = request_analysis.expanded_request.request_key;
    if (request_analysis.IsDirty()) {
      LOG_F(INFO, "[dirty:{}] {}",
        JoinDirtyReasons(request_analysis.dirty_reasons),
        FormatShaderLogKey(request_analysis.expanded_request.request));
      ClearRequestDiagnosticsLog(options.layout, request_key);
      const auto compile_outcome
        = options.compile_request(request_analysis.expanded_request, index,
          dirty_analysis.requests.size());
      if (!compile_outcome.artifact.has_value()) {
        WriteRequestDiagnosticsLog(
          options, request_key, compile_outcome.diagnostics);
        return std::nullopt;
      }

      WriteModuleArtifactFile(
        request_analysis.artifact_path, *compile_outcome.artifact);
      artifacts.push_back(*compile_outcome.artifact);
      ++compiled_count;
    } else {
      LOG_F(INFO, "[clean] {}",
        FormatShaderLogKey(request_analysis.expanded_request.request));
      ClearRequestDiagnosticsLog(options.layout, request_key);
      if (!request_analysis.reusable_artifact.has_value()) {
        throw std::runtime_error(
          "clean request is missing reusable module artifact");
      }
      artifacts.push_back(std::move(*request_analysis.reusable_artifact));
      ++clean_count;
    }
  }

  const auto stale_artifact_paths
    = CollectUpdateStaleArtifactPaths(options, dirty_analysis);
  const auto removed_stale_artifacts
    = RemovePaths(stale_artifact_paths, "stale module");
  const auto stale_log_paths = CollectStaleLogPaths(options);
  (void)RemovePaths(stale_log_paths, "stale log");

  const auto should_repack = (compiled_count > 0)
    || (removed_stale_artifacts > 0) || dirty_analysis.final_archive_missing
    || dirty_analysis.manifest_changed;
  if (!should_repack) {
    LOG_F(INFO, "Final archive is up to date; skipping repack");
  } else {
    LOG_F(INFO,
      "Repack required: compiled_any={} removed_stale={} final_missing={} "
      "manifest_changed={}",
      compiled_count > 0, removed_stale_artifacts > 0,
      dirty_analysis.final_archive_missing, dirty_analysis.manifest_changed);
    PackFinalShaderArchive(options.final_archive_path, options.toolchain_hash,
      options.requests, artifacts);
  }

  PersistBuildState(options, artifacts, dirty_analysis.current_manifest);
  return ExecutionResult {
    .manifest_snapshot = std::move(dirty_analysis.current_manifest),
    .artifacts = std::move(artifacts),
    .compiled_request_count = compiled_count,
    .clean_request_count = clean_count,
    .stale_request_count = stale_artifact_paths.size(),
    .repacked = should_repack,
  };
}

auto ExecuteRebuild(const ExecutionOptions& options)
  -> std::optional<ExecutionResult>
{
  const auto manifest_snapshot = BuildManifestSnapshot(options.requests);

  std::vector<ModuleArtifact> artifacts;
  artifacts.reserve(options.requests.size());

  size_t index = 0;
  for (const auto& request : options.requests) {
    ++index;
    ClearRequestDiagnosticsLog(options.layout, request.request_key);
    const auto compile_outcome
      = options.compile_request(request, index, options.requests.size());
    if (!compile_outcome.artifact.has_value()) {
      WriteRequestDiagnosticsLog(
        options, request.request_key, compile_outcome.diagnostics);
      return std::nullopt;
    }

    WriteModuleArtifactFile(
      GetModuleArtifactPath(options.layout, request.request_key),
      *compile_outcome.artifact);
    artifacts.push_back(*compile_outcome.artifact);
  }

  const auto live_request_keys
    = CollectTrackedRequestKeyHexes(options.requests);
  const auto stale_artifact_paths = CollectPrunableFiles(
    options.layout.modules_dir, ".oxsm", live_request_keys);
  const auto stale_log_paths = CollectStaleLogPaths(options);
  (void)RemovePaths(stale_artifact_paths, "stale module");
  (void)RemovePaths(stale_log_paths, "stale log");

  PackFinalShaderArchive(options.final_archive_path, options.toolchain_hash,
    options.requests, artifacts);
  PersistBuildState(options, artifacts, manifest_snapshot);

  return ExecutionResult {
    .manifest_snapshot = manifest_snapshot,
    .artifacts = std::move(artifacts),
    .compiled_request_count = options.requests.size(),
    .stale_request_count = stale_artifact_paths.size(),
    .repacked = true,
  };
}

} // namespace oxygen::graphics::d3d12::tools::shader_bake
