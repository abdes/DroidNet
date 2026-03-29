//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/BuildPaths.h>

#include <fstream>
#include <system_error>

#include <fmt/format.h>

#include <Oxygen/Base/StringUtils.h>

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

  auto AtomicWriteTempPath(const std::filesystem::path& final_path)
    -> std::filesystem::path
  {
    auto temp_path = final_path;
    temp_path += ".tmp";
    return temp_path;
  }

  auto FinalizeAtomicWrite(const std::filesystem::path& temp_path,
    const std::filesystem::path& final_path) -> void
  {
    std::error_code ec;
    if (std::filesystem::exists(final_path, ec)) {
      ec.clear();
      std::filesystem::remove(final_path, ec);
      if (ec) {
        ThrowFilesystemError("failed to replace existing file", final_path, ec);
      }
    } else if (ec) {
      ThrowFilesystemError("failed to probe file", final_path, ec);
    }

    ec.clear();
    std::filesystem::rename(temp_path, final_path, ec);
    if (ec) {
      ThrowFilesystemError("failed to publish file", final_path, ec);
    }
  }

  auto GetRequestPublishedArtifactPath(
    const std::filesystem::path& final_archive_path, std::string_view category,
    std::string_view source_path, std::string_view entry_point,
    const uint64_t request_key, std::string_view extension)
    -> std::filesystem::path
  {
    auto request_path = std::filesystem::path(std::string(source_path));
    request_path = request_path.lexically_normal();

    auto leaf_name = std::string(entry_point) + "__"
      + RequestKeyToHex(request_key) + std::string(extension);
    return final_archive_path.parent_path() / std::string(category)
      / request_path / std::move(leaf_name);
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
  const std::string_view source_path, const std::string_view entry_point,
  const uint64_t request_key) -> std::filesystem::path
{
  return GetRequestPublishedArtifactPath(
    final_archive_path, "dxil", source_path, entry_point, request_key, ".dxil");
}

auto GetRequestPdbPath(const std::filesystem::path& final_archive_path,
  const std::string_view source_path, const std::string_view entry_point,
  const uint64_t request_key) -> std::filesystem::path
{
  return GetRequestPublishedArtifactPath(
    final_archive_path, "pdb", source_path, entry_point, request_key, ".pdb");
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
  const auto temp_path = AtomicWriteTempPath(final_path);
  std::filesystem::create_directories(final_path.parent_path());

  std::error_code ec;
  if (std::filesystem::exists(temp_path, ec)) {
    ec.clear();
    std::filesystem::remove(temp_path, ec);
    if (ec) {
      ThrowFilesystemError("failed to clear temp file", temp_path, ec);
    }
  } else if (ec) {
    ThrowFilesystemError("failed to probe temp file", temp_path, ec);
  }

  {
    std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      throw std::runtime_error(
        "failed to open temp file `" + ToUtf8PathString(temp_path) + "`");
    }

    if (!contents.empty()) {
      out.write(reinterpret_cast<const char*>(contents.data()),
        static_cast<std::streamsize>(contents.size()));
      if (!out) {
        throw std::runtime_error(
          "failed to write temp file `" + ToUtf8PathString(temp_path) + "`");
      }
    }
  }

  FinalizeAtomicWrite(temp_path, final_path);
}

auto WriteTextFileAtomically(
  const std::filesystem::path& final_path, std::string_view contents) -> void
{
  const auto bytes = std::as_bytes(std::span(contents.data(), contents.size()));
  WriteBinaryFileAtomically(final_path, bytes);
}

} // namespace oxygen::graphics::d3d12::tools::shader_bake
