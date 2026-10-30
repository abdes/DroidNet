//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cctype>
#include <cstddef>
#include <exception>
#include <expected>
#include <filesystem>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Utils/MountedCookedSources.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Data/AssetType.h>

namespace oxygen::content::import::internal {

namespace {

  auto ToLowerAscii(std::string value) -> std::string
  {
    for (auto& ch : value) {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
  }

  auto InferAssetTypeFromRelPath(const std::string_view relpath)
    -> std::optional<data::AssetType>
  {
    const auto ext
      = ToLowerAscii(std::filesystem::path(relpath).extension().string());
    if (ext == data::kGeometryDescriptorExtension) {
      return data::AssetType::kGeometry;
    }
    if (ext == data::kMaterialDescriptorExtension) {
      return data::AssetType::kMaterial;
    }
    if (ext == data::kScriptDescriptorExtension) {
      return data::AssetType::kScript;
    }
    if (ext == data::kInputActionDescriptorExtension) {
      return data::AssetType::kInputAction;
    }
    if (ext == data::kInputMappingContextDescriptorExtension) {
      return data::AssetType::kInputMappingContext;
    }
    if (ext == data::kPhysicsSceneDescriptorExtension) {
      return data::AssetType::kPhysicsScene;
    }
    if (ext == data::kSceneDescriptorExtension) {
      return data::AssetType::kScene;
    }
    return std::nullopt;
  }

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

} // namespace

auto LoadMountedCookedSources(ImportSession& session,
  const ImportRequest& request, const std::string_view diagnostic_prefix)
  -> std::vector<MountedCookedSource>
{
  auto mounts = std::vector<MountedCookedSource> {};
  for (const auto& root : BuildUniqueMountedCookedRoots(request)) {
    auto mount = MountedCookedSource {
      .root = root,
      .inspection = std::nullopt,
    };
    if (lc::Inspection::IsCookedSource(mount.root)) {
      try {
        auto inspection = lc::Inspection {};
        inspection.Load(mount.root);
        mount.inspection = std::move(inspection);
      } catch (const std::exception& ex) {
        session.AddDiagnostic({
          .severity = ImportSeverity::kError,
          .code = std::string(diagnostic_prefix) + "index_load_failed",
          .message = "Failed loading cooked index: " + std::string(ex.what()),
          .source_path = request.source_path.string(),
          .object_path = mount.root.string(),
        });
      }
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
  auto found_descriptor = false;
  for (const auto& mount : std::views::reverse(mounts)) {
    if (mount.inspection.has_value()) {
      for (const auto& asset : mount.inspection->Assets()) {
        if (asset.virtual_path == virtual_path) {
          return CheckType(asset.key,
            static_cast<data::AssetType>(asset.asset_type), expected_type);
        }
      }
    }
    if (is_mounted) {
      const auto descriptor = mount.root / std::filesystem::path(relpath);
      std::error_code ec;
      if (std::filesystem::exists(descriptor, ec)) {
        found_descriptor = true;
        break;
      }
      looked_up.push_back(descriptor);
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
  if (!is_mounted) {
    return refuse(AssetReferenceRefusal::kUnmounted);
  }
  if (!found_descriptor) {
    return refuse(AssetReferenceRefusal::kMissing);
  }
  const auto inferred_type = InferAssetTypeFromRelPath(relpath);
  if (!inferred_type.has_value()) {
    return refuse(AssetReferenceRefusal::kTypeUnknown);
  }
  return CheckType(data::AssetKey::FromVirtualPath(virtual_path),
    *inferred_type, expected_type);
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
