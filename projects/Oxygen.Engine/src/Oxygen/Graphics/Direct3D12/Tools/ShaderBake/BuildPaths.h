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
#include <span>
#include <string>
#include <string_view>

#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

struct ExpandedShaderRequest;

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
//! The key `RequestKeyToHex` wrote: 16 lowercase hex characters, else
//! nothing.
[[nodiscard]] auto RequestKeyFromHex(std::string_view text)
  -> std::optional<uint64_t>;
//! Leaf name of a published request sidecar: `<entry-point>__<key><ext>`.
[[nodiscard]] auto GetRequestPublishedArtifactFileName(
  std::string_view entry_point, uint64_t request_key,
  std::string_view extension) -> std::string;
//! Leaf name of a request's PDB sidecar, also the name its DXIL records.
[[nodiscard]] auto GetRequestPdbFileName(
  std::string_view entry_point, uint64_t request_key) -> std::string;
[[nodiscard]] auto GetModuleArtifactPath(
  const BuildRootLayout& layout, uint64_t request_key) -> std::filesystem::path;
[[nodiscard]] auto GetRequestLogPath(
  const BuildRootLayout& layout, uint64_t request_key) -> std::filesystem::path;
//! Where `request`'s DXIL is published beside `final_archive_path`.
[[nodiscard]] auto GetRequestDxilPath(
  const std::filesystem::path& final_archive_path,
  const ExpandedShaderRequest& request) -> std::filesystem::path;
//! Where `request`'s PDB sidecar is published beside `final_archive_path`.
[[nodiscard]] auto GetRequestPdbPath(
  const std::filesystem::path& final_archive_path,
  const ExpandedShaderRequest& request) -> std::filesystem::path;

auto EnsureBuildRootLayout(const BuildRootLayout& layout) -> void;
auto ResetTempDirectory(const BuildRootLayout& layout) -> void;
auto ClearCache(const BuildRootLayout& layout) -> void;
auto RemoveLegacyDebugExportTree(const BuildRootLayout& layout) -> void;

[[nodiscard]] auto ToUtf8PathString(const std::filesystem::path& path)
  -> std::string;

//! The most bytes ShaderBake reads whole from one file: a shader source, a
//! module artifact, the manifest or the build state.
inline constexpr serio::ByteCount kMaxReadBytes {
  uint64_t { 64 } * 1024 * 1024,
};

//! The whole file `path`, read while its writers may republish it; a
//! failed or oversized read throws, naming the file `what`.
[[nodiscard]] auto ReadFileBytes(const std::filesystem::path& path,
  std::string_view what) -> serio::ByteBuffer;

auto WriteBinaryFileAtomically(const std::filesystem::path& final_path,
  std::span<const std::byte> contents) -> void;

auto WriteTextFileAtomically(
  const std::filesystem::path& final_path, std::string_view contents) -> void;

} // namespace oxygen::graphics::d3d12::tools::shader_bake
