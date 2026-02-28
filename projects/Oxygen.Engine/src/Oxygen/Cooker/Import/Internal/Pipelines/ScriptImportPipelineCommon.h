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
#include <span>
#include <string>
#include <string_view>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>

namespace oxygen::content::import::detail::script_import {

constexpr auto kScriptDescriptorExtension = std::string_view { ".oscript" };
constexpr auto kScriptsTableFileName = std::string_view { "scripts.table" };
constexpr auto kScriptsDataFileName = std::string_view { "scripts.data" };

constexpr auto kScriptContentHashBytes = size_t { 8 };
constexpr auto kByteShiftBits = size_t { 8 };

inline auto JoinRelativePath(
  const std::string_view base, const std::string_view child) -> std::string
{
  if (base.empty()) {
    return std::string { child };
  }
  if (child.empty()) {
    return std::string { base };
  }
  return std::string { base } + "/" + std::string { child };
}

inline auto EnsureLeadingSlash(const std::string_view path) -> std::string
{
  if (path.starts_with('/')) {
    return std::string { path };
  }
  return std::string { "/" } + std::string { path };
}

inline auto JoinVirtualPath(
  const std::string_view root, const std::string_view leaf) -> std::string
{
  auto rooted = EnsureLeadingSlash(root);
  if (rooted == "/") {
    return EnsureLeadingSlash(leaf);
  }
  if (leaf.empty()) {
    return rooted;
  }
  if (leaf.front() == '/') {
    return rooted + std::string { leaf };
  }
  return rooted + "/" + std::string { leaf };
}

inline auto DeriveScriptName(const std::filesystem::path& source_path)
  -> std::string
{
  const auto stem = source_path.stem().string();
  if (!stem.empty()) {
    return stem;
  }
  return "Script";
}

inline auto BuildScriptDescriptorRelPath(const ImportRequest& request,
  const std::string_view script_name) -> std::string
{
  const auto leaf
    = std::string { script_name } + std::string { kScriptDescriptorExtension };
  const auto scripts_dir
    = JoinRelativePath(request.loose_cooked_layout.descriptors_dir, "Scripts");
  return JoinRelativePath(scripts_dir, leaf);
}

inline auto BuildScriptsTableRelPath(const ImportRequest& request)
  -> std::string
{
  return JoinRelativePath(
    request.loose_cooked_layout.resources_dir, kScriptsTableFileName);
}

inline auto BuildScriptsDataRelPath(const ImportRequest& request) -> std::string
{
  return JoinRelativePath(
    request.loose_cooked_layout.resources_dir, kScriptsDataFileName);
}

inline auto BuildExternalSourcePath(const std::filesystem::path& source_path)
  -> std::string
{
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
