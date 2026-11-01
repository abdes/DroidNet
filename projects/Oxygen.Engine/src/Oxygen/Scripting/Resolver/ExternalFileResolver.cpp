//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>
#include <Oxygen/Scripting/IScriptSourceResolver.h>
#include <Oxygen/Scripting/Resolver/ExternalFileResolver.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::scripting {

namespace {

  auto ComputeExternalSourceFingerprint(const std::filesystem::path& full_path,
    const std::span<const uint8_t> bytes) -> uint64_t
  {
    constexpr uint64_t kFnvOffsetBasis = 1469598103934665603ULL;
    constexpr uint64_t kFnvPrime = 1099511628211ULL;

    auto hash = kFnvOffsetBasis;
    constexpr uint64_t kByteMask = 0xFFU;
    constexpr int kBytesPerWord = 8;
    constexpr uint64_t kBitsPerByte = 8U;
    auto mix_u64 = [&hash](const uint64_t value) -> void {
      auto v = value;
      for (int i = 0; i < kBytesPerWord; ++i) {
        hash ^= (v & kByteMask);
        hash *= kFnvPrime;
        v >>= kBitsPerByte;
      }
    };

    std::error_code ec;
    const auto write_time = std::filesystem::last_write_time(full_path, ec);
    if (!ec) {
      const auto ticks
        = static_cast<uint64_t>(write_time.time_since_epoch().count());
      mix_u64(ticks);
    }

    ec.clear();
    const auto size = std::filesystem::file_size(full_path, ec);
    if (!ec) {
      mix_u64(size);
    }

    mix_u64(static_cast<uint64_t>(bytes.size()));
    for (const auto byte : bytes) {
      hash ^= static_cast<uint64_t>(byte);
      hash *= kFnvPrime;
    }

    return hash;
  }

  auto NormalizeExternalPath(std::string_view path_text)
    -> std::filesystem::path
  {
    std::filesystem::path path { path_text };
    if (path.extension().empty()) {
      path.replace_extension(".luau");
    }
    return path.lexically_normal();
  }

  auto HasParentTraversal(const std::filesystem::path& path) -> bool
  {
    return std::ranges::any_of(
      path, [](const auto& segment) -> auto { return segment == ".."; });
  }

  //! A mount path split into its mount name and its path in the mount.
  struct MountPathParts {
    std::string_view mount;
    std::filesystem::path in_mount;
  };

  //! Splits `/<Mount>/<path in mount>`; nothing when the text is not a mount
  //! path or its path leaves the mount.
  auto SplitMountPath(const std::string_view text)
    -> std::optional<MountPathParts>
  {
    if (text.size() < 2 || text.front() != '/') {
      return std::nullopt;
    }
    const auto slash = text.find('/', 1);
    if (slash == std::string_view::npos || slash == 1
      || slash + 1 == text.size()) {
      return std::nullopt;
    }
    auto in_mount = NormalizeExternalPath(text.substr(slash + 1));
    if (in_mount.empty() || in_mount.has_root_path()
      || HasParentTraversal(in_mount)) {
      return std::nullopt;
    }
    return MountPathParts {
      .mount = text.substr(1, slash - 1),
      .in_mount = std::move(in_mount),
    };
  }

  //! The existing file a mount path names, or nothing when its mount is not a
  //! source mount or the file does not exist.
  auto FindMountedFile(const PathFinder& path_finder,
    const MountPathParts& parts) -> std::optional<std::filesystem::path>
  {
    const auto mounts = path_finder.SourceMounts();
    const auto mount = mounts.find(parts.mount);
    if (mount == mounts.end()) {
      return std::nullopt;
    }
    auto full_path = (mount->second / parts.in_mount).lexically_normal();
    if (!std::filesystem::exists(full_path)) {
      return std::nullopt;
    }
    return full_path;
  }

} // namespace

ExternalFileResolver::ExternalFileResolver(PathFinder path_finder)
  : path_finder_(std::move(path_finder))
{
}

auto ExternalFileResolver::ResolvePath(const data::ScriptAsset& asset) const
  -> std::optional<std::filesystem::path>
{
  const auto external_path = asset.TryGetExternalSourcePath();
  if (!external_path.has_value()) {
    return std::nullopt;
  }
  const auto parts = SplitMountPath(*external_path);
  if (!parts.has_value()) {
    return std::nullopt;
  }
  return FindMountedFile(path_finder_, *parts);
}

auto ExternalFileResolver::Resolve(
  const IScriptSourceResolver::ResolveRequest& request) const
  -> IScriptSourceResolver::ResolveResult
{
  const auto external_path = request.asset.get().TryGetExternalSourcePath();
  if (!external_path.has_value()) {
    return IScriptSourceResolver::ResolveResult {
      .ok = false,
      .blob = {},
      .error_message = "external source path is not set",
    };
  }

  const auto parts = SplitMountPath(*external_path);
  if (!parts.has_value()) {
    return IScriptSourceResolver::ResolveResult {
      .ok = false,
      .blob = {},
      .error_message = "external source path must be a mount path, "
                       "/<Mount>/<path in mount>, that stays in its mount",
    };
  }

  // A mount this run does not bind, or a missing file, falls back silently.
  const auto resolved_path = FindMountedFile(path_finder_, *parts);
  if (!resolved_path) {
    return IScriptSourceResolver::ResolveResult {
      .ok = false,
      .blob = {},
      .error_message = {}, // Silent failure allows fallback
    };
  }

  const auto& full_path = *resolved_path;
  // Editors may hold the script open for writing while it is reloaded.
  auto file = serio::NativeFile::Open(full_path,
    {
      .sharing = serio::Sharing::kReadWriteDelete,
      .hint = serio::AccessHint::kSequential,
    });
  if (!file) {
    return IScriptSourceResolver::ResolveResult {
      .ok = false,
      .blob = {},
      .error_message
      = "unable to open external source file: " + full_path.string(),
    };
  }

  auto bytes = serio::ReadAll(*file);
  if (!bytes) {
    return IScriptSourceResolver::ResolveResult {
      .ok = false,
      .blob = {},
      .error_message
      = "failed while reading external source file: " + full_path.string(),
    };
  }

  const auto content_hash
    = ComputeExternalSourceFingerprint(full_path, bytes->Unsigned());

  auto blob = ScriptSourceBlob::FromOwned(std::move(bytes).value(),
    data::pak::scripting::ScriptLanguage::kLuau,
    data::pak::scripting::ScriptCompression::kNone, content_hash,
    ScriptBlobOrigin::kExternalFile,
    ScriptBlobCanonicalName { full_path.generic_string() });
  return IScriptSourceResolver::ResolveResult {
    .ok = true,
    .blob = std::optional<ResolvedScriptBlob> { std::move(blob) },
    .error_message = {},
  };
}

} // namespace oxygen::scripting
