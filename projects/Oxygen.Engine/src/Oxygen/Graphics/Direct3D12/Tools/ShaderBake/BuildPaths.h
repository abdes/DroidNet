//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace oxygen::graphics::d3d12::tools::shader_bake {

struct BuildRootLayout {
  std::filesystem::path root;
  std::filesystem::path state_dir;
  std::filesystem::path build_state_file;
  std::filesystem::path manifest_file;
  std::filesystem::path modules_dir;
  std::filesystem::path logs_dir;
  std::filesystem::path temp_dir;
};

[[nodiscard]] auto GetBuildRootLayout(const std::filesystem::path& build_root)
  -> BuildRootLayout;
[[nodiscard]] auto RequestKeyToHex(uint64_t request_key) -> std::string;
[[nodiscard]] auto GetModuleArtifactPath(
  const BuildRootLayout& layout, uint64_t request_key) -> std::filesystem::path;
[[nodiscard]] auto GetRequestLogPath(
  const BuildRootLayout& layout, uint64_t request_key) -> std::filesystem::path;
[[nodiscard]] auto GetRequestDxilPath(
  const std::filesystem::path& final_archive_path, std::string_view source_path,
  std::string_view entry_point, uint64_t request_key) -> std::filesystem::path;
[[nodiscard]] auto GetRequestPdbPath(
  const std::filesystem::path& final_archive_path, std::string_view source_path,
  std::string_view entry_point, uint64_t request_key) -> std::filesystem::path;

auto EnsureBuildRootLayout(const BuildRootLayout& layout) -> void;
auto ResetTempDirectory(const BuildRootLayout& layout) -> void;
auto ClearCache(const BuildRootLayout& layout) -> void;
auto RemoveLegacyDebugExportTree(const BuildRootLayout& layout) -> void;

[[nodiscard]] auto ToUtf8PathString(const std::filesystem::path& path)
  -> std::string;

auto WriteBinaryFileAtomically(const std::filesystem::path& final_path,
  std::span<const std::byte> contents) -> void;

auto WriteTextFileAtomically(
  const std::filesystem::path& final_path, std::string_view contents) -> void;

} // namespace oxygen::graphics::d3d12::tools::shader_bake
