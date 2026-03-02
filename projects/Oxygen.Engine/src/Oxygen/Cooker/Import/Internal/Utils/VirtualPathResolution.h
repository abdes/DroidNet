//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <Oxygen/Cooker/Import/ImportRequest.h>

namespace oxygen::content::import::internal {

inline auto ValidateNoDotSegments(const std::string_view path) -> bool
{
  size_t pos = 0;
  while (pos <= path.size()) {
    const auto next = path.find('/', pos);
    const auto len
      = (next == std::string_view::npos) ? (path.size() - pos) : (next - pos);
    const auto segment = path.substr(pos, len);
    if (segment == "." || segment == "..") {
      return false;
    }
    if (next == std::string_view::npos) {
      break;
    }
    pos = next + 1;
  }
  return true;
}

inline auto IsCanonicalVirtualPath(const std::string_view virtual_path) -> bool
{
  if (virtual_path.empty()) {
    return false;
  }
  if (virtual_path.front() != '/') {
    return false;
  }
  if (virtual_path.find('\\') != std::string_view::npos) {
    return false;
  }
  if (virtual_path.find("//") != std::string_view::npos) {
    return false;
  }
  if (virtual_path.size() > 1 && virtual_path.back() == '/') {
    return false;
  }
  return ValidateNoDotSegments(virtual_path);
}

inline auto NormalizeVirtualMountRoot(std::string mount_root) -> std::string
{
  if (mount_root.empty()) {
    return "/";
  }
  if (mount_root.front() != '/') {
    mount_root.insert(mount_root.begin(), '/');
  }
  while (mount_root.size() > 1 && mount_root.back() == '/') {
    mount_root.pop_back();
  }
  return mount_root;
}

inline auto TryVirtualPathToRelPath(const std::string_view mount_root,
  const std::string_view virtual_path, std::string& relpath) -> bool
{
  if (!virtual_path.starts_with(mount_root)) {
    return false;
  }
  if (virtual_path.size() == mount_root.size()) {
    relpath.clear();
    return false;
  }
  const auto slash_pos = mount_root.size();
  if (virtual_path[slash_pos] != '/') {
    return false;
  }
  relpath = std::string(virtual_path.substr(slash_pos + 1));
  return !relpath.empty();
}

inline auto TryVirtualPathToRelPath(const ImportRequest& request,
  const std::string_view virtual_path, std::string& relpath) -> bool
{
  const auto mount_root
    = NormalizeVirtualMountRoot(request.loose_cooked_layout.virtual_mount_root);
  return TryVirtualPathToRelPath(mount_root, virtual_path, relpath);
}

inline auto BuildMountedCookedRoots(const ImportRequest& request)
  -> std::vector<std::filesystem::path>
{
  auto roots = std::vector<std::filesystem::path> {};
  roots.reserve(1U + request.cooked_context_roots.size());
  if (request.cooked_root.has_value()) {
    roots.push_back(*request.cooked_root);
  } else {
    roots.push_back(request.source_path.parent_path());
  }
  for (const auto& root : request.cooked_context_roots) {
    roots.push_back(root);
  }
  return roots;
}

inline auto BuildUniqueMountedCookedRoots(const ImportRequest& request)
  -> std::vector<std::filesystem::path>
{
  auto unique_roots = std::vector<std::filesystem::path> {};
  auto seen = std::unordered_set<std::string> {};

  for (auto root : BuildMountedCookedRoots(request)) {
    root = root.lexically_normal();
    const auto key = root.generic_string();
    if (seen.insert(key).second) {
      unique_roots.push_back(std::move(root));
    }
  }
  return unique_roots;
}

} // namespace oxygen::content::import::internal
