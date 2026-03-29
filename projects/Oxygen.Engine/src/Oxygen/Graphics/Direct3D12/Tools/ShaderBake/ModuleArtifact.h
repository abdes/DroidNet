//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/FileFingerprint.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

inline constexpr uint32_t kOxsmMagic = 0x4F58534DU; // "OXSM"
inline constexpr uint32_t kOxsmVersion = 1;

struct ModuleArtifact {
  uint64_t request_key { 0 };
  uint64_t action_key { 0 };
  uint64_t toolchain_hash { 0 };
  ShaderRequest request;
  uint64_t primary_hash { 0 };
  std::vector<DependencyFingerprint> dependencies;
  std::vector<std::byte> dxil;
  std::vector<std::byte> reflection;

  auto operator==(const ModuleArtifact&) const -> bool = default;
};

auto WriteModuleArtifactFile(const std::filesystem::path& artifact_path,
  const ModuleArtifact& artifact) -> void;

[[nodiscard]] auto ReadModuleArtifactFile(
  const std::filesystem::path& artifact_path) -> ModuleArtifact;

[[nodiscard]] auto TryReadModuleArtifactFile(
  const std::filesystem::path& artifact_path) -> std::optional<ModuleArtifact>;

} // namespace oxygen::graphics::d3d12::tools::shader_bake
