//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <fmt/format.h>

#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/BuildPaths.h>
#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/Catalog.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

namespace {

  auto WideToUtf8String(std::wstring_view in) -> std::string
  {
    std::string out;
    oxygen::string_utils::WideToUtf8(in, out);
    return out;
  }

  auto ThrowFilesystemError(std::string_view action,
    const std::filesystem::path& path, const std::error_code& ec) -> void
  {
    throw std::runtime_error(std::string(action) + " `" + ToUtf8PathString(path)
      + "`: " + ec.message());
  }

  auto RemovePathIfPresent(const std::filesystem::path& path) -> void
  {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    if (ec) {
      ThrowFilesystemError("failed to remove", path, ec);
    }
  }

  //! A kind of published artifact: its folder beside the archive and its
  //! file extension.
  struct ArtifactKind {
    std::string_view folder;
    std::string_view extension;
  };

  constexpr auto kDxilArtifact
    = ArtifactKind { .folder = "dxil", .extension = ".dxil" };
  constexpr auto kPdbArtifact
    = ArtifactKind { .folder = "pdb", .extension = ".pdb" };

  auto GetRequestPublishedArtifactPath(
    const std::filesystem::path& final_archive_path, const ArtifactKind& kind,
    const ExpandedShaderRequest& request) -> std::filesystem::path
  {
    auto request_path
      = std::filesystem::path(std::string(request.request.source_path));
    request_path = request_path.lexically_normal();

    return final_archive_path.parent_path() / std::string(kind.folder)
      / request_path
      / GetRequestPublishedArtifactFileName(
        request.request.entry_point, request.request_key, kind.extension);
  }

} // namespace

auto GetBuildRootLayout(const std::filesystem::path& build_root)
  -> BuildRootLayout
{
  const auto state_dir = build_root / "state";
  return BuildRootLayout {
    .root = build_root,
    .state_dir = state_dir,
    .build_state_file = state_dir / "build-state-v1.json",
    .manifest_file = state_dir / "manifest-v1.json",
    .modules_dir = build_root / "modules",
    .logs_dir = build_root / "logs",
    .temp_dir = build_root / "temp",
  };
}

auto RequestKeyToHex(const uint64_t request_key) -> std::string
{
  return fmt::format("{:016x}", request_key);
}

auto GetRequestPublishedArtifactFileName(const std::string_view entry_point,
  const uint64_t request_key, const std::string_view extension) -> std::string
{
  return std::string(entry_point) + "__" + RequestKeyToHex(request_key)
    + std::string(extension);
}

auto GetRequestPdbFileName(
  const std::string_view entry_point, const uint64_t request_key) -> std::string
{
  return GetRequestPublishedArtifactFileName(entry_point, request_key, ".pdb");
}

auto GetModuleArtifactPath(const BuildRootLayout& layout,
  const uint64_t request_key) -> std::filesystem::path
{
  const auto request_key_hex = RequestKeyToHex(request_key);
  return layout.modules_dir / request_key_hex.substr(0, 2)
    / request_key_hex.substr(2, 2) / (request_key_hex + ".oxsm");
}

auto GetRequestLogPath(const BuildRootLayout& layout,
  const uint64_t request_key) -> std::filesystem::path
{
  return layout.logs_dir / (RequestKeyToHex(request_key) + ".log");
}

auto GetRequestDxilPath(const std::filesystem::path& final_archive_path,
  const ExpandedShaderRequest& request) -> std::filesystem::path
{
  return GetRequestPublishedArtifactPath(
    final_archive_path, kDxilArtifact, request);
}

auto GetRequestPdbPath(const std::filesystem::path& final_archive_path,
  const ExpandedShaderRequest& request) -> std::filesystem::path
{
  return GetRequestPublishedArtifactPath(
    final_archive_path, kPdbArtifact, request);
}

auto EnsureBuildRootLayout(const BuildRootLayout& layout) -> void
{
  std::filesystem::create_directories(layout.state_dir);
  std::filesystem::create_directories(layout.modules_dir);
  std::filesystem::create_directories(layout.logs_dir);
  std::filesystem::create_directories(layout.temp_dir);
}

auto ResetTempDirectory(const BuildRootLayout& layout) -> void
{
  RemovePathIfPresent(layout.temp_dir);
  std::filesystem::create_directories(layout.temp_dir);
}

auto ClearCache(const BuildRootLayout& layout) -> void
{
  RemovePathIfPresent(layout.state_dir);
  RemovePathIfPresent(layout.modules_dir);
  RemovePathIfPresent(layout.root / "debug");
  RemovePathIfPresent(layout.logs_dir);
  RemovePathIfPresent(layout.temp_dir);
  std::filesystem::create_directories(layout.root);
}

auto RemoveLegacyDebugExportTree(const BuildRootLayout& layout) -> void
{
  RemovePathIfPresent(layout.root / "debug");
}

auto ToUtf8PathString(const std::filesystem::path& path) -> std::string
{
  return WideToUtf8String(path.wstring());
}

auto WriteBinaryFileAtomically(const std::filesystem::path& final_path,
  std::span<const std::byte> contents) -> void
{
  std::filesystem::create_directories(final_path.parent_path());
  // A rebuild regenerates every artifact, so the OS cache is durable enough.
  const auto published = serio::WriteFileAtomically(
    final_path, contents, serio::Durability::kBuffered);
  if (!published) {
    ThrowFilesystemError(
      "failed to publish file", final_path, published.error());
  }
}

auto WriteTextFileAtomically(
  const std::filesystem::path& final_path, std::string_view contents) -> void
{
  const auto bytes = std::as_bytes(std::span(contents.data(), contents.size()));
  WriteBinaryFileAtomically(final_path, bytes);
}

} // namespace oxygen::graphics::d3d12::tools::shader_bake
