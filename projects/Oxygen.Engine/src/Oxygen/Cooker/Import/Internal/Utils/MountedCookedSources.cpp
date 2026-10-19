//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cctype>
#include <exception>
#include <string>
#include <system_error>
#include <utility>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Utils/MountedCookedSources.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>

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

auto ResolveMountedAssetReference(
  const std::span<const MountedCookedSource> mounts,
  const ImportRequest& request, const std::string_view virtual_path,
  const std::optional<data::AssetType> expected_type)
  -> std::expected<ResolvedAssetReference, AssetReferenceRefused>
{
  const auto refuse = [](const AssetReferenceRefusal reason)
    -> std::expected<ResolvedAssetReference, AssetReferenceRefused> {
    return std::unexpected(AssetReferenceRefused {
      .reason = reason,
      .found_type = std::nullopt,
    });
  };
  if (!IsCanonicalVirtualPath(virtual_path)) {
    return refuse(AssetReferenceRefusal::kInvalidVirtualPath);
  }

  auto relpath = std::string {};
  const auto is_mounted
    = TryVirtualPathToRelPath(request, virtual_path, relpath);
  auto found_descriptor = false;
  for (auto it = mounts.rbegin(); it != mounts.rend(); ++it) {
    if (it->inspection.has_value()) {
      for (const auto& asset : it->inspection->Assets()) {
        if (asset.virtual_path == virtual_path) {
          return CheckType(asset.key,
            static_cast<data::AssetType>(asset.asset_type), expected_type);
        }
      }
    }
    if (is_mounted) {
      std::error_code ec;
      if (std::filesystem::exists(
            it->root / std::filesystem::path(relpath), ec)) {
        found_descriptor = true;
        break;
      }
    }
  }

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

} // namespace oxygen::content::import::internal
