//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <exception>
#include <expected>
#include <filesystem>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Utils/MountedCookedSources.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedFormatError.h>

namespace oxygen::content::import::internal {

namespace {

  auto CheckType(const data::AssetKey& key, const data::AssetType type,
    const std::optional<data::AssetType> expected_type)
    -> std::expected<ResolvedAssetReference, AssetReferenceRefused>
  {
    if (expected_type.has_value() && type != *expected_type) {
      return std::unexpected(AssetReferenceRefused {
        .reason = AssetReferenceRefusal::kTypeMismatch,
        .found_type = type,
      });
    }
    return ResolvedAssetReference { .key = key, .type = type };
  }

  auto AddIndexLoadFailure(ImportSession& session, const ImportRequest& request,
    const std::string_view diagnostic_prefix, const std::filesystem::path& root,
    const std::string_view what) -> void
  {
    session.AddDiagnostic({
      .severity = ImportSeverity::kError,
      .code = std::string(diagnostic_prefix) + "index_load_failed",
      .message = "Failed loading cooked index: " + std::string(what),
      .source_path = request.source_path.string(),
      .object_path = root.string(),
    });
  }

} // namespace

auto LoadMountedCookedSources(ImportSession& session,
  const ImportRequest& request, const std::string_view diagnostic_prefix)
  -> std::vector<MountedCookedSource>
{
  const auto& files = cooked::RealCookerStorage();
  auto mounts = std::vector<MountedCookedSource> {};
  for (const auto& root : BuildUniqueMountedCookedRoots(request)) {
    auto mount = MountedCookedSource {
      .root = root,
      .content = std::nullopt,
    };
    try {
      mount.content = cooked::CookedContent::Open(files, mount.root);
    } catch (const data::CookedFormatError& error) {
      // A root nothing was cooked into yet holds no content; anything else
      // there that cannot be read is reported.
      if (error.Defect() != data::CookedDefect::kAbsent) {
        AddIndexLoadFailure(
          session, request, diagnostic_prefix, mount.root, error.what());
      }
    } catch (const std::exception& error) {
      AddIndexLoadFailure(
        session, request, diagnostic_prefix, mount.root, error.what());
    }
    mounts.push_back(std::move(mount));
  }
  return mounts;
}

auto LookupReference(const std::span<const MountedCookedSource> mounts,
  const ImportRequest& request, const ResolvedReference& reference,
  const std::optional<data::AssetType> expected_type)
  -> std::expected<ResolvedAssetReference, AssetReferenceRefused>
{
  const auto& virtual_path = reference.resolved;
  auto relpath = std::string {};
  const auto is_mounted
    = TryVirtualPathToRelPath(request, virtual_path, relpath);
  auto looked_up = std::vector<std::filesystem::path> {};
  // Only an index lists a cooked asset: a descriptor file it does not list is
  // no asset, whatever its extension.
  for (const auto& mount : std::views::reverse(mounts)) {
    if (!mount.content.has_value()) {
      continue;
    }
    if (const auto* const asset
      = mount.content->FindAsset(std::string_view { virtual_path });
      asset != nullptr) {
      return CheckType(asset->key, asset->type, expected_type);
    }
    if (is_mounted) {
      looked_up.push_back(mount.root / std::filesystem::path(relpath));
    }
  }

  const auto refuse = [&](const AssetReferenceRefusal reason)
    -> std::expected<ResolvedAssetReference, AssetReferenceRefused> {
    return std::unexpected(AssetReferenceRefused {
      .reason = reason,
      .found_type = std::nullopt,
      .looked_up = std::move(looked_up),
    });
  };
  return refuse(is_mounted ? AssetReferenceRefusal::kMissing
                           : AssetReferenceRefusal::kUnmounted);
}

auto DescribeLookup(const ResolvedReference& reference,
  const AssetReferenceRefused& refused) -> std::string
{
  auto text = "'" + reference.written + "' resolves to " + reference.resolved;
  if (refused.looked_up.empty()) {
    return text;
  }
  text.append("; looked for ");
  for (auto index = std::size_t { 0 }; index < refused.looked_up.size();
    ++index) {
    if (index > 0) {
      text.append(", ");
    }
    text.append(refused.looked_up[index].generic_string());
  }
  return text;
}

} // namespace oxygen::content::import::internal
