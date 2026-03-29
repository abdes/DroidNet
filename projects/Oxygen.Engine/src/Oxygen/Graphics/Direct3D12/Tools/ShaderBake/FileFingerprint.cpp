//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/FileFingerprint.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/BuildPaths.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

namespace {

  auto ReadBinaryFile(const std::filesystem::path& file_path)
    -> std::vector<char>
  {
    std::ifstream in(file_path, std::ios::binary);
    if (!in.is_open()) {
      throw std::runtime_error(
        "failed to open file `" + ToUtf8PathString(file_path) + "`");
    }

    return std::vector<char> {
      std::istreambuf_iterator<char>(in),
      std::istreambuf_iterator<char>(),
    };
  }

  auto GetWriteTimeUtcTicks(const std::filesystem::path& file_path) -> int64_t
  {
    std::error_code ec;
    const auto write_time = std::filesystem::last_write_time(file_path, ec);
    if (ec) {
      throw std::runtime_error("failed to read write time for `"
        + ToUtf8PathString(file_path) + "`: " + ec.message());
    }

    return static_cast<int64_t>(write_time.time_since_epoch().count());
  }

} // namespace

auto CanonicalizeWorkspaceRelativePath(const std::filesystem::path& file_path,
  const std::filesystem::path& workspace_root) -> std::string
{
  const auto normalized_file = file_path.lexically_normal();
  const auto normalized_root = workspace_root.lexically_normal();
  if (!normalized_file.is_absolute()) {
    throw std::runtime_error(
      "dependency path must be absolute before workspace canonicalization");
  }
  if (!normalized_root.is_absolute()) {
    throw std::runtime_error(
      "workspace root must be absolute before dependency canonicalization");
  }

  const auto relative = normalized_file.lexically_relative(normalized_root);
  if (relative.empty()) {
    throw std::runtime_error(
      "dependency path cannot be relativized against workspace root");
  }
  for (const auto& part : relative) {
    if (part == "..") {
      throw std::runtime_error("dependency path escapes workspace root");
    }
  }

  const auto generic = relative.generic_string();
  if (generic.empty()) {
    throw std::runtime_error("dependency path cannot normalize to empty");
  }
  return generic;
}

auto ComputeFileFingerprint(const std::filesystem::path& file_path,
  const std::filesystem::path& workspace_root) -> DependencyFingerprint
{
  const auto normalized_file = file_path.lexically_normal();

  std::error_code ec;
  const auto file_size = std::filesystem::file_size(normalized_file, ec);
  if (ec) {
    throw std::runtime_error("failed to read file size for `"
      + ToUtf8PathString(normalized_file) + "`: " + ec.message());
  }

  const auto bytes = ReadBinaryFile(normalized_file);
  return DependencyFingerprint {
    .path = CanonicalizeWorkspaceRelativePath(normalized_file, workspace_root),
    .content_hash = oxygen::ComputeFNV1a64(bytes.data(), bytes.size()),
    .size_bytes = file_size,
    .write_time_utc = GetWriteTimeUtcTicks(normalized_file),
  };
}

} // namespace oxygen::graphics::d3d12::tools::shader_bake
