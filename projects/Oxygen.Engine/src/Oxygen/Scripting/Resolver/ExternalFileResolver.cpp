//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <utility>

#include <Oxygen/Scripting/Resolver/ExternalFileResolver.h>

namespace oxygen::scripting {

namespace {

  auto ComputeExternalSourceFingerprint(const std::filesystem::path& full_path,
    const std::vector<uint8_t>& bytes) -> uint64_t
  {
    constexpr uint64_t kFnvOffsetBasis = 1469598103934665603ULL;
    constexpr uint64_t kFnvPrime = 1099511628211ULL;

    auto hash = kFnvOffsetBasis;
    auto mix_u64 = [&hash](const uint64_t value) {
      auto v = value;
      for (int i = 0; i < 8; ++i) {
        hash ^= (v & 0xFFULL);
        hash *= kFnvPrime;
        v >>= 8;
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

} // namespace

ExternalFileResolver::ExternalFileResolver(PathFinder path_finder)
  : path_finder_(std::move(path_finder))
{
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

  auto relative_path = NormalizeExternalPath(*external_path);
  if (relative_path.is_absolute()) {
    return IScriptSourceResolver::ResolveResult {
      .ok = false,
      .blob = {},
      .error_message = "external source path must be relative",
    };
  }
  if (HasParentTraversal(relative_path)) {
    return IScriptSourceResolver::ResolveResult {
      .ok = false,
      .blob = {},
      .error_message = "external source path must not contain parent traversal",
    };
  }

  std::optional<std::filesystem::path> resolved_path;
  for (const auto& root : path_finder_.ScriptSourceRoots()) {
    const auto full_path = (root / relative_path).lexically_normal();
    if (std::filesystem::exists(full_path)) {
      resolved_path = full_path;
      break;
    }
  }

  if (!resolved_path) {
    return IScriptSourceResolver::ResolveResult {
      .ok = false,
      .blob = {},
      .error_message = {}, // Silent failure allows fallback
    };
  }

  const auto& full_path = *resolved_path;
  std::ifstream input(full_path, std::ios::in | std::ios::binary);
  if (!input.is_open()) {
    return IScriptSourceResolver::ResolveResult {
      .ok = false,
      .blob = {},
      .error_message
      = "unable to open external source file: " + full_path.string(),
    };
  }

  std::vector<uint8_t> bytes(
    (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (input.bad()) {
    return IScriptSourceResolver::ResolveResult {
      .ok = false,
      .blob = {},
      .error_message
      = "failed while reading external source file: " + full_path.string(),
    };
  }

  const auto content_hash = ComputeExternalSourceFingerprint(full_path, bytes);

  auto blob = ScriptSourceBlob::FromOwned(std::move(bytes),
    data::pak::ScriptLanguage::kLuau, data::pak::ScriptCompression::kNone,
    content_hash, ScriptBlobOrigin::kExternalFile,
    ScriptBlobCanonicalName { full_path.generic_string() });
  return IScriptSourceResolver::ResolveResult {
    .ok = true,
    .blob = std::optional<ResolvedScriptBlob> { std::move(blob) },
    .error_message = {},
  };
}

} // namespace oxygen::scripting
