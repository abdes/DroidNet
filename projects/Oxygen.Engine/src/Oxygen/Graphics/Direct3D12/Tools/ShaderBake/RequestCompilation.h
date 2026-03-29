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
#include <string>

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Catalog.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/ModuleArtifact.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

struct RequestCompilerConfig {
  std::filesystem::path workspace_root;
  std::filesystem::path shader_source_root;
  uint64_t toolchain_hash { 0 };
  std::span<const std::filesystem::path> include_dirs;
};

struct RequestCompileOutcome {
  std::optional<ModuleArtifact> artifact;
  std::vector<std::byte> pdb;
  std::string diagnostics;
};

[[nodiscard]] auto CompileExpandedShaderRequest(
  const RequestCompilerConfig& config,
  const ExpandedShaderRequest& expanded_request, size_t index,
  size_t total_count) -> RequestCompileOutcome;

} // namespace oxygen::graphics::d3d12::tools::shader_bake
