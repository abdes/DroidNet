//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace oxygen::graphics::d3d12::tools::shader_bake {

struct DependencyFingerprint {
  std::string path;
  uint64_t content_hash { 0 };
  uint64_t size_bytes { 0 };
  int64_t write_time_utc { 0 };

  auto operator==(const DependencyFingerprint&) const -> bool = default;
};

[[nodiscard]] auto CanonicalizeWorkspaceRelativePath(
  const std::filesystem::path& file_path,
  const std::filesystem::path& workspace_root) -> std::string;

[[nodiscard]] auto ComputeFileFingerprint(
  const std::filesystem::path& file_path,
  const std::filesystem::path& workspace_root) -> DependencyFingerprint;

} // namespace oxygen::graphics::d3d12::tools::shader_bake
