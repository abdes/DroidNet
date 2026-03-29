//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/BuildPaths.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/ModuleArtifact.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

struct BuildStateModuleRecord {
  uint64_t request_key { 0 };
  uint64_t action_key { 0 };
  ShaderRequest request;
  std::filesystem::path module_artifact_relpath;

  auto operator==(const BuildStateModuleRecord&) const -> bool = default;
};

struct BuildStateSnapshot {
  std::filesystem::path workspace_root;
  std::filesystem::path build_root;
  std::vector<BuildStateModuleRecord> modules;

  auto operator==(const BuildStateSnapshot&) const -> bool = default;
};

[[nodiscard]] auto BuildBuildStateSnapshot(
  const std::filesystem::path& workspace_root, const BuildRootLayout& layout,
  std::span<const ModuleArtifact> artifacts) -> BuildStateSnapshot;

auto WriteBuildStateFile(const std::filesystem::path& build_state_path,
  const BuildStateSnapshot& state) -> void;

[[nodiscard]] auto ReadBuildStateFile(
  const std::filesystem::path& build_state_path) -> BuildStateSnapshot;

[[nodiscard]] auto ScanModulesForBuildState(
  const std::filesystem::path& workspace_root, const BuildRootLayout& layout)
  -> BuildStateSnapshot;

[[nodiscard]] auto LoadOrRecoverBuildStateFile(
  const std::filesystem::path& workspace_root, const BuildRootLayout& layout)
  -> BuildStateSnapshot;

} // namespace oxygen::graphics::d3d12::tools::shader_bake
