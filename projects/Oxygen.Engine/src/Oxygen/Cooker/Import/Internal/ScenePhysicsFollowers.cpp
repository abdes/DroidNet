//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ScenePhysicsFollowers.h>
#include <Oxygen/Cooker/Import/PhysicsImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/PhysicsImportSettings.h>
#include <Oxygen/Core/Meta/Physics/Backend.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Serio/ByteReader.h>

namespace oxygen::content::import::internal {

namespace {
  using MetaBackend = core::meta::physics::PhysicsBackend;
  using PakBackend = data::pak::physics::PhysicsBackend;

  //! The import backend of a physics scene cooked for `backend`.
  [[nodiscard]] auto ImportBackendOf(
    const PakBackend backend, const std::string& virtual_path) -> MetaBackend
  {
    switch (backend) {
    case PakBackend::kJolt:
      return MetaBackend::kJolt;
    case PakBackend::kPhysX:
      return MetaBackend::kPhysX;
    case PakBackend::kNone:
      break;
    }
    throw std::runtime_error(
      "the physics sidecar '" + virtual_path + "' names no backend");
  }
} // namespace

auto FindScenePhysicsFollowers(const cooked::CookedContent& root,
  const data::AssetKey& scene_key,
  const data::pak::core::ContentHashDigest& scene_layout)
  -> std::vector<ScenePhysicsFollower>
{
  auto followers = std::vector<ScenePhysicsFollower> {};
  const auto* const scene = root.FindAsset(scene_key);
  if (scene == nullptr) {
    return followers;
  }
  for (const auto& asset : root.Assets()) {
    if (asset.type != data::AssetType::kPhysicsScene) {
      continue;
    }
    const auto bytes = root.Descriptor(asset.key);
    const auto binding
      = serio::CopyAt<data::pak::physics::PhysicsSceneAssetDesc>(bytes);
    if (!binding) {
      throw std::runtime_error("the physics sidecar '" + asset.virtual_path
        + "' has a truncated descriptor");
    }
    const auto targets_scene = binding->target_scene_key == scene_key;
    const auto has_own_source = asset.source_path != scene->source_path;
    const auto cooked_for_scene
      = std::ranges::equal(binding->target_scene_layout_hash, scene_layout);
    if (!targets_scene || !has_own_source || cooked_for_scene) {
      continue;
    }
    followers.push_back({
      .virtual_path = asset.virtual_path,
      .source_mount_path = asset.source_path,
      .backend = ImportBackendOf(binding->backend, asset.virtual_path),
    });
  }
  return followers;
}

auto BuildScenePhysicsFollowerRequest(const ImportRequest& scene_request,
  const ScenePhysicsFollower& follower, const std::filesystem::path& source,
  ImportRequest::InflightSceneContext scene,
  std::vector<ImportDiagnostic>& diagnostics) -> std::optional<ImportRequest>
{
  auto request = BuildPhysicsSidecarRequest(
    PhysicsSidecarImportSettings {
      .source_path = source.string(),
      .inline_bindings_json = {},
      .cooked_root = scene_request.ResolveCookedRoot().string(),
      .job_name = {},
      .report_path = {},
      .verbose = false,
      .target_scene_virtual_path = scene.virtual_path,
      .physics_backend = follower.backend,
    },
    diagnostics);
  if (!request.has_value()) {
    return std::nullopt;
  }
  request->source_mount_path = follower.source_mount_path;
  request->source_mounts = scene_request.source_mounts;
  request->captured_inputs = scene_request.captured_inputs;
  request->source_key = scene_request.source_key;
  request->cooked_context_roots = scene_request.cooked_context_roots;
  request->inflight_scene_contexts.push_back(std::move(scene));
  return request;
}

} // namespace oxygen::content::import::internal
