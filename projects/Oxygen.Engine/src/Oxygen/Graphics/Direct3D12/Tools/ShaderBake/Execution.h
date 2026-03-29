//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Bake.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/BuildPaths.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Catalog.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Manifest.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/ModuleArtifact.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/RequestCompilation.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

struct ExecutionOptions {
  ShaderBakeMode mode { ShaderBakeMode::kDev };
  std::filesystem::path workspace_root;
  std::filesystem::path shader_source_root;
  std::filesystem::path final_archive_path;
  BuildRootLayout layout;
  uint64_t toolchain_hash { 0 };
  std::span<const std::filesystem::path> include_dirs;
  std::span<const ExpandedShaderRequest> requests;
  std::function<RequestCompileOutcome(
    const ExpandedShaderRequest&, size_t, size_t)>
    compile_request;
};

struct ExecutionResult {
  ManifestSnapshot manifest_snapshot;
  std::vector<ModuleArtifact> artifacts;
  size_t compiled_request_count { 0 };
  size_t clean_request_count { 0 };
  size_t stale_request_count { 0 };
  bool repacked { false };
};

[[nodiscard]] auto ExecuteUpdate(const ExecutionOptions& options)
  -> std::optional<ExecutionResult>;

[[nodiscard]] auto ExecuteRebuild(const ExecutionOptions& options)
  -> std::optional<ExecutionResult>;

} // namespace oxygen::graphics::d3d12::tools::shader_bake
