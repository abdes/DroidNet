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
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Catalog.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

struct ManifestRequestRecord {
  ShaderRequest request;
  uint64_t request_key { 0 };

  auto operator==(const ManifestRequestRecord&) const -> bool = default;
};

struct ManifestSnapshot {
  std::vector<ManifestRequestRecord> requests;

  auto operator==(const ManifestSnapshot&) const -> bool = default;
};

[[nodiscard]] auto BuildManifestSnapshot(
  std::span<const ExpandedShaderRequest> requests) -> ManifestSnapshot;

auto WriteManifestFile(const std::filesystem::path& manifest_path,
  const ManifestSnapshot& snapshot) -> void;

[[nodiscard]] auto ReadManifestFile(const std::filesystem::path& manifest_path)
  -> ManifestSnapshot;

} // namespace oxygen::graphics::d3d12::tools::shader_bake
