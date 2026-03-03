//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>

namespace oxygen::content::import::detail::script_import {

constexpr auto kScriptContentHashBytes = size_t { 8 };
constexpr auto kByteShiftBits = size_t { 8 };

inline auto DeriveScriptName(const std::filesystem::path& source_path)
  -> std::string
{
  auto stem = source_path.stem().string();
  if (!stem.empty()) {
    return stem;
  }
  return "Script";
}

inline auto BuildScriptDescriptorRelPath(const ImportRequest& request,
  const std::string_view script_name) -> std::string
{
  return request.loose_cooked_layout.ScriptDescriptorRelPath(script_name);
}

inline auto BuildScriptsTableRelPath(const ImportRequest& request)
  -> std::string
{
  return request.loose_cooked_layout.ScriptsTableRelPath();
}

inline auto BuildScriptsDataRelPath(const ImportRequest& request) -> std::string
{
  return request.loose_cooked_layout.ScriptsDataRelPath();
}

inline auto HasParentTraversal(const std::filesystem::path& path) -> bool
{
  return std::ranges::any_of(
    path, [](const auto& segment) { return segment == ".."; });
}

inline auto TryMakeExternalPathRelativeToRoot(
  const std::filesystem::path& source_path,
  const std::filesystem::path& source_root) -> std::optional<std::string>
{
  const auto relative = source_path.lexically_relative(source_root);
  if (relative.empty() || relative.is_absolute()
    || HasParentTraversal(relative)) {
    return std::nullopt;
  }

  const auto relative_path = relative.generic_string();
  if (relative_path.empty() || relative_path == ".") {
    return std::nullopt;
  }
  return relative_path;
}

inline auto BuildExternalSourcePath(const ImportRequest& request) -> std::string
{
  const auto source_path = request.source_path.lexically_normal();

  // External script descriptors are expected to be relative to the runtime
  // script roots (typically the cooked root parent, e.g. Examples/Content).
  if (request.cooked_root.has_value()) {
    const auto source_root = request.cooked_root->parent_path();
    if (!source_root.empty()) {
      if (source_path.is_absolute()) {
        if (const auto relative
          = TryMakeExternalPathRelativeToRoot(source_path, source_root)) {
          return *relative;
        }
      } else {
        std::error_code ec;
        const auto absolute_source
          = std::filesystem::absolute(source_path, ec).lexically_normal();
        if (!ec) {
          if (const auto relative
            = TryMakeExternalPathRelativeToRoot(absolute_source, source_root)) {
            return *relative;
          }
        }
      }
    }
  }

  if (!source_path.is_absolute() && !HasParentTraversal(source_path)) {
    const auto relative_path = source_path.generic_string();
    if (!relative_path.empty() && relative_path != ".") {
      return relative_path;
    }
  }

  auto filename = source_path.filename().generic_string();
  if (!filename.empty()) {
    return filename;
  }
  return "script.luau";
}

template <size_t N>
inline auto CopyNullTerminated(
  const std::string_view src, const std::span<char, N> dst) -> bool
{
  if (src.size() >= dst.size()) {
    return false;
  }
  std::ranges::fill(dst, '\0');
  std::ranges::copy(src, dst.begin());
  return true;
}

inline auto ComputeContentHash64(const std::span<const std::byte> bytes)
  -> uint64_t
{
  const auto digest = base::ComputeSha256(bytes);
  auto hash = uint64_t { 0 };
  for (size_t i = 0; i < kScriptContentHashBytes; ++i) {
    hash |= static_cast<uint64_t>(digest.at(i)) << (i * kByteShiftBits);
  }
  return hash;
}

inline auto AddDiagnostic(ImportSession& session, const ImportRequest& request,
  const ImportSeverity severity, std::string code, std::string message) -> void
{
  session.AddDiagnostic({
    .severity = severity,
    .code = std::move(code),
    .message = std::move(message),
    .source_path = request.source_path.string(),
    .object_path = {},
  });
}

inline auto AddDiagnosticAtPath(ImportSession& session,
  const ImportRequest& request, const ImportSeverity severity, std::string code,
  std::string message, std::string object_path) -> void
{
  session.AddDiagnostic({
    .severity = severity,
    .code = std::move(code),
    .message = std::move(message),
    .source_path = request.source_path.string(),
    .object_path = std::move(object_path),
  });
}

} // namespace oxygen::content::import::detail::script_import
