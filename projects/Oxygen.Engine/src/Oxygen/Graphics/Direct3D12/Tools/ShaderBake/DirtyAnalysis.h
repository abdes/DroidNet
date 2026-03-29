//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/BuildPaths.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Catalog.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Manifest.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/ModuleArtifact.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

enum class DirtyReason : uint8_t {
  kMissingArtifact,
  kInvalidArtifact,
  kStoredRequestMismatch,
  kActionKeyMismatch,
  kMissingPrimarySource,
  kPrimarySourceChanged,
  kMissingDependency,
  kDependencyChanged,
  kNewManifestMembership,
};

struct RequestDirtyAnalysis {
  ExpandedShaderRequest expanded_request;
  uint64_t action_key { 0 };
  std::filesystem::path artifact_path;
  std::optional<ModuleArtifact> reusable_artifact;
  std::vector<DirtyReason> dirty_reasons;

  [[nodiscard]] auto IsDirty() const -> bool { return !dirty_reasons.empty(); }
};

struct DirtyAnalysisResult {
  ManifestSnapshot current_manifest;
  std::vector<RequestDirtyAnalysis> requests;
  std::vector<std::filesystem::path> stale_artifact_paths;
  bool manifest_changed { false };
  bool final_archive_missing { false };
};

[[nodiscard]] auto AnalyzeDirtyRequests(
  const std::filesystem::path& workspace_root,
  const std::filesystem::path& shader_source_root,
  const BuildRootLayout& layout,
  const std::filesystem::path& final_archive_path,
  std::span<const ExpandedShaderRequest> requests,
  std::span<const std::filesystem::path> include_dirs) -> DirtyAnalysisResult;

[[nodiscard]] auto DirtyReasonToString(DirtyReason reason) -> const char*;

} // namespace oxygen::graphics::d3d12::tools::shader_bake
