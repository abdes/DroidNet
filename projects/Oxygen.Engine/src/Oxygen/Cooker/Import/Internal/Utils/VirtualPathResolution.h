//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>

namespace oxygen::content::import::internal {

inline auto IsCanonicalVirtualPath(const std::string_view virtual_path) -> bool
{
  return oxygen::content::IsCanonicalVirtualPath(virtual_path);
}

//! The error refusing the virtual path an asset's name gives it.
/*!
 A name becomes the leaf of its asset's virtual path. The schemas refuse what a
 pattern can express; this refuses the rest (NFC, unassigned code points, the
 255-byte segment bound) and names taken from a job or a file instead of a
 schema. The diagnostic code is the rule's `vpath.*` code.

 @return The diagnostic, or nothing when the path is accepted.
*/
inline auto CheckAssetVirtualPath(const ImportRequest& request,
  const std::string_view virtual_path, std::string object_path)
  -> std::optional<ImportDiagnostic>
{
  const auto error = oxygen::content::ValidateVirtualPath(virtual_path);
  if (!error.has_value()) {
    return std::nullopt;
  }
  return ImportDiagnostic {
    .severity = ImportSeverity::kError,
    .code = oxygen::content::to_string(*error),
    .message = "The asset name gives a virtual path the engine refuses: "
      + std::string(virtual_path),
    .source_path = request.source_path.string(),
    .object_path = std::move(object_path),
  };
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

//! Where a job's identity lives in the cooked root.
struct AssetPlacement final {
  //! The identity: a single asset, from which its key derives, or the
  //! container an input job names its products under.
  std::string virtual_path;

  //! The identity's path, relative to the cooked root: the descriptor of a
  //! single asset, the folder of an input source's products.
  std::string descriptor_relpath;
};

//! Places a job at the identity its request carries.
/*!
 The cooked layout is the identity: the descriptor of `/Mount/X/Y.ext` lives at
 `X/Y.ext` under the mount's cooked root, and the products of the input source
 `/Mount/X/Player.input` live under `X/Player.input/`. The identity must be a
 virtual path the engine accepts, inside the job's mount and ending in
 `extension`.

 @return The placement, or the diagnostic refusing the identity:
   `import.asset_virtual_path_missing`, the rule's `vpath.*` code, or
   `import.asset_virtual_path_invalid` for a path outside the mount or of
   another type.
*/
inline auto PlaceAsset(
  const ImportRequest& request, const std::string_view extension)
  -> std::expected<AssetPlacement, ImportDiagnostic>
{
  const auto& virtual_path = request.asset_virtual_path;
  const auto refuse
    = [&](std::string code, std::string message) -> ImportDiagnostic {
    return ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = request.source_path.string(),
      .object_path = "virtual_path",
    };
  };
  if (virtual_path.empty()) {
    return std::unexpected(refuse("import.asset_virtual_path_missing",
      "The job names no virtual_path for the asset it cooks"));
  }
  if (const auto error = oxygen::content::ValidateVirtualPath(virtual_path)) {
    return std::unexpected(refuse(oxygen::content::to_string(*error),
      "The engine refuses the asset's virtual path: " + virtual_path));
  }
  auto relpath = std::string {};
  if (!TryVirtualPathToRelPath(request, virtual_path, relpath)
    || !virtual_path.ends_with(extension)) {
    return std::unexpected(refuse("import.asset_virtual_path_invalid",
      "The asset's virtual path must name a '" + std::string(extension)
        + "' asset under the mount '"
        + request.loose_cooked_layout.virtual_mount_root
        + "': " + virtual_path));
  }
  return AssetPlacement {
    .virtual_path = virtual_path,
    .descriptor_relpath = std::move(relpath),
  };
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

  const auto roots = BuildMountedCookedRoots(request);
  for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
    auto root = *it;
    root = root.lexically_normal();
    const auto key = root.generic_string();
    if (seen.insert(key).second) {
      unique_roots.push_back(std::move(root));
    }
  }
  std::ranges::reverse(unique_roots);
  return unique_roots;
}

} // namespace oxygen::content::import::internal
