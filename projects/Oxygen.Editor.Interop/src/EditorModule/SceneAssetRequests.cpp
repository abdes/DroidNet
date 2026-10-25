//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "pch.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <EditorModule/EditorModule.h>
#include <EditorModule/SceneAssetRequests.h>
#include <EditorModule/ThreadSafeQueue.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Types/RenderablePolicies.h>

namespace oxygen::interop::module {
namespace {

  auto EnvironmentTextureLabel(const std::size_t slot) -> const char*
  {
    switch (static_cast<EnvironmentTextureSlot>(slot)) {
    case EnvironmentTextureSlot::kMeteringMask:
      return "Exposure mask";
    case EnvironmentTextureSlot::kSkySphereCubemap:
      return "Sky sphere cubemap";
    case EnvironmentTextureSlot::kSkyLightCubemap:
      return "Sky light cubemap";
    }
    return "Environment texture";
  }

  //! The material an override set names, if any. The command boundary admits
  //! only material assets, at most one per part.
  [[nodiscard]] auto MaterialUri(const std::vector<OverrideEntry>& entries)
    -> std::optional<std::string>
  {
    for (const auto& entry : entries) {
      if (const auto* asset = std::get_if<OverrideAsset>(&entry.value)) {
        DCHECK_F(entry.id == data::schema::OverrideId::kGeometrySurfaceMaterial);
        return asset->virtual_path;
      }
    }
    return std::nullopt;
  }

  //! Sets each property of the part that `entries` names and clears every other
  //! property of the part's kind, so the renderable holds exactly this state.
  //! `material` is the loaded asset of the material entry, if any.
  [[nodiscard]] auto ApplyOverrides(scene::SceneNode::Renderable& renderable,
    const data::SurfaceId part, const std::vector<OverrideEntry>& entries,
    const std::shared_ptr<const data::MaterialAsset>& material) -> bool
  {
    using data::schema::OverrideId;
    const auto kind = part.IsNil() ? data::schema::PartKind::kNone
                                   : data::schema::PartKind::kSurface;
    auto applied = true;
    for (const auto& property : data::schema::kOverrideProperties) {
      if (!property.asset_sourced || property.part_kind != kind) {
        continue;
      }
      const auto entry = std::ranges::find(entries, property.id, &OverrideEntry::id);
      const auto* value = entry == entries.end() ? nullptr : &entry->value;
      switch (property.id) {
      case OverrideId::kGeometrySurfaceMaterial:
        DCHECK_F(value == nullptr || material != nullptr);
        applied = (value != nullptr ? renderable.SetMaterialOverride(part, material)
                                    : renderable.ClearMaterialOverride(part))
          && applied;
        break;
      case OverrideId::kGeometrySurfaceVisibility:
        applied = (value != nullptr
                      ? renderable.SetVisibilityOverride(part, std::get<bool>(*value))
                      : renderable.ClearVisibilityOverride(part))
          && applied;
        break;
      case OverrideId::kGeometrySurfaceCastsShadows:
        applied = (value != nullptr
                      ? renderable.SetCastsShadowsOverride(part, std::get<bool>(*value))
                      : renderable.ClearCastsShadowsOverride(part))
          && applied;
        break;
      case OverrideId::kGeometryLodPolicy:
        if (value != nullptr) {
          renderable.SetLodPolicyOverride(scene::MakeLodPolicy(
            std::get<data::pak::geometry::LodPolicyRecord>(*value)));
        } else {
          renderable.ClearLodPolicyOverride();
        }
        break;
      default:
        ABORT_F("Every asset-sourced property has its case above");
      }
    }
    return applied;
  }

  //! Whether two sources name the same cooked texture.
  [[nodiscard]] auto IsSameTexture(const EnvironmentTextureSource& left,
    const EnvironmentTextureSource& right) -> bool
  {
    return left.virtual_path && left.virtual_path == right.virtual_path;
  }

} // namespace

struct SceneAssetRequests::State {
  struct EnvironmentCompletion {
    uint64_t generation {};
    std::size_t slot {};
    content::ResourceKey key {};
    Texture texture;
    std::string error;
  };
  struct EnvironmentRequest {
    uint64_t generation {};
    EnvironmentTextureSources sources;
    EnvironmentApply apply;
    FailureCallback on_failure;
    SuccessCallback on_success;
    std::array<EnvironmentTextureStatus, kEnvironmentTextureSlotCount> status;
    //! The sources whose textures are the accepted ones in `status`.
    EnvironmentTextureSources accepted_sources;
    EnvironmentTextureKeys loaded {};
    //! The resources behind `loaded`, and behind the accepted keys. The scene
    //! names textures by key only, and the asset loader evicts a texture,
    //! including its GPU copy, once no one holds it; holding them here keeps
    //! the applied textures resident.
    std::array<Texture, kEnvironmentTextureSlotCount> loaded_textures;
    std::array<Texture, kEnvironmentTextureSlotCount> accepted_textures;
    bool settled { true };

    [[nodiscard]] auto HasSources() const -> bool
    {
      return std::ranges::any_of(
        sources, [](const auto& source) { return source.virtual_path.has_value(); });
    }
    [[nodiscard]] auto IsPending() const -> bool
    {
      return std::ranges::any_of(
        status, [](const auto& slot) { return slot.pending; });
    }
  };
  struct Completion {
    scene::NodeHandle node {};
    uint64_t generation = 0;
    data::SurfaceId surface;
    bool geometry = false;
    Geometry geometry_asset;
    Material material_asset;
    std::string error;
  };
  //! The requested override state of one part: a surface, or with a nil id
  //! the geometry component itself.
  struct PartAssignment {
    uint64_t generation = 0;
    uint64_t geometry_generation = 0;
    OverrideTarget target;
    OverrideIntent intent = OverrideIntent::kObservedEdit;
    std::vector<OverrideEntry> entries;
    //! The material entry's path, loaded before the part applies.
    std::optional<std::string> material_uri;
    Material material;
    std::optional<data::AssetKey> accepted_geometry;
    bool ready = false;
    bool apply = false;
    bool rejected_surface = false;
    FailureCallback on_failure;
    SuccessCallback on_success;
  };
  struct Target {
    uint64_t geometry_generation = 0;
    std::string geometry_uri;
    bool geometry_pending = false;
    bool geometry_failed = false;
    FailureCallback geometry_failure;
    SuccessCallback geometry_success;
    std::unordered_map<data::SurfaceId, PartAssignment> parts;
  };

  GeometryLoader geometry_loader;
  MaterialLoader material_loader;
  Diagnostic diagnostic;
  AssetAvailability available;
  TextureLoader texture_loader;
  EnvironmentRequest environment;
  std::shared_ptr<ThreadSafeQueue<EnvironmentCompletion>> environment_inbox
    = std::make_shared<ThreadSafeQueue<EnvironmentCompletion>>();

  void LoadEnvironmentTexture(std::size_t slot);
  void SettleEnvironment(scene::Scene& scene, bool& changed);
  uint64_t generation = 0;
  std::unordered_map<scene::NodeHandle, Target> targets;
  std::unordered_set<uint64_t> refresh_requests;
  std::string refresh_error;
  bool loads_paused = false;
  std::shared_ptr<ThreadSafeQueue<Completion>> inbox
    = std::make_shared<ThreadSafeQueue<Completion>>();

  auto IsAvailable(const std::string& uri, bool material) -> bool
  {
    try {
      return available(uri, material);
    } catch (const std::exception&) {
      return false;
    }
  }

  auto CanRefresh(const std::string& uri, bool material) -> bool
  {
    try {
      return available(uri, material);
    } catch (const std::exception& error) {
      refresh_error = fmt::format(
        "Asset '{}' could not be resolved: {}", uri, error.what());
      diagnostic(refresh_error);
      return false;
    }
  }

  void Report(scene::NodeHandle node, const std::string& uri,
    const std::string& error, bool geometry, uint64_t generation,
    const FailureCallback& on_failure, data::SurfaceId surface = {})
  {
    if (refresh_requests.contains(generation)) {
      refresh_error = fmt::format("{} '{}' could not be refreshed: {}",
        geometry ? "Geometry" : "Material", uri, error);
    }
    diagnostic(fmt::format("{} request '{}' on node {} surface {}: {}",
      geometry ? "Geometry" : "Material", uri, nostd::to_string(node), surface,
      error));
    if (on_failure) {
      on_failure(generation, error);
    }
  }
};

SceneAssetRequests::SceneAssetRequests(
  content::IAssetLoader& loader, content::VirtualPathResolver& resolver)
  : SceneAssetRequests(
      [&loader, &resolver](
        const std::string& uri, GeometryCompletion complete) {
        const auto key = resolver.ResolveAssetKey(uri);
        if (!key) {
          complete({}, "asset path could not be resolved");
        } else if (auto cached = loader.GetGeometryAsset(*key)) {
          complete(std::move(cached), {});
        } else {
          loader.StartLoadGeometryAsset(
            *key, [complete = std::move(complete)](auto loaded) {
              const auto error = loaded ? "" : "asset load failed";
              complete(std::move(loaded), error);
            });
        }
      },
      [&loader, &resolver](
        const std::string& uri, MaterialCompletion complete) {
        const auto key = resolver.ResolveAssetKey(uri);
        if (!key) {
          complete({}, "asset path could not be resolved");
        } else if (auto cached = loader.GetMaterialAsset(*key)) {
          complete(std::move(cached), {});
        } else {
          loader.StartLoadMaterialAsset(
            *key, [complete = std::move(complete)](auto loaded) {
              const auto error = loaded ? "" : "asset load failed";
              complete(std::move(loaded), error);
            });
        }
      },
      // The editor reports each failed request; this is the engine's trace.
      [](const std::string& message) { LOG_F(1, "{}", message); },
      [&resolver](const std::string& uri, bool /*material*/) {
        return resolver.ResolveAssetKey(uri).has_value();
      },
      [&loader](const std::string& virtual_path, TextureCompletion complete) {
        const auto key = loader.ResolveTextureResourceKey(virtual_path);
        if (!key) {
          complete({}, {}, "texture is not in the mounted content");
          return;
        }
        loader.StartLoadTexture(
          *key, [key = *key, complete = std::move(complete)](auto texture) {
            const auto error
              = texture ? "" : "texture resource could not be loaded";
            complete(key, std::move(texture), error);
          });
      })
{
}

SceneAssetRequests::SceneAssetRequests(GeometryLoader geometry_loader,
  MaterialLoader material_loader, Diagnostic diagnostic,
  AssetAvailability available, TextureLoader texture_loader)
  : state_(std::make_unique<State>())
{
  state_->geometry_loader = std::move(geometry_loader);
  state_->material_loader = std::move(material_loader);
  state_->diagnostic = std::move(diagnostic);
  state_->available = std::move(available);
  state_->texture_loader = std::move(texture_loader);
}

SceneAssetRequests::~SceneAssetRequests() = default;

auto SceneAssetRequests::BeginGeometry(scene::NodeHandle node,
  const std::string& uri, FailureCallback on_failure,
  SuccessCallback on_success) -> GeometryCompletion
{
  auto& target = state_->targets[node];
  if (target.geometry_uri != uri) {
    target.parts.clear();
  }
  const auto generation = ++state_->generation;
  target.geometry_generation = generation;
  target.geometry_uri = uri;
  target.geometry_pending = true;
  target.geometry_failed = false;
  target.geometry_failure = std::move(on_failure);
  target.geometry_success = std::move(on_success);
  return [inbox = std::weak_ptr(state_->inbox), node, generation](
           Geometry asset, std::string error) {
    if (auto queue = inbox.lock()) {
      queue->Enqueue(State::Completion { .node = node,
        .generation = generation,
        .surface = {},
        .geometry = true,
        .geometry_asset = std::move(asset),
        .material_asset = {},
        .error = std::move(error) });
    }
  };
}

void SceneAssetRequests::LoadGeometry(
  const std::string& uri, GeometryCompletion complete)
{
  if (state_->loads_paused) {
    return;
  }
  try {
    state_->geometry_loader(uri, complete);
  } catch (const std::exception& ex) {
    complete({}, ex.what());
  } catch (...) {
    complete({}, "unknown exception starting asset load");
  }
}

void SceneAssetRequests::SetOverrides(scene::NodeHandle node,
  OverrideTarget target, std::vector<OverrideEntry> entries,
  const OverrideIntent intent, FailureCallback on_failure,
  SuccessCallback on_success)
{
  QueueOverrides(node, std::move(target), std::move(entries), intent,
    std::move(on_failure), std::move(on_success), std::nullopt);
}

void SceneAssetRequests::QueueOverrides(scene::NodeHandle node,
  OverrideTarget target, std::vector<OverrideEntry> entries,
  const OverrideIntent intent, FailureCallback on_failure,
  SuccessCallback on_success, std::optional<data::AssetKey> accepted_geometry)
{
  const auto generation = ++state_->generation;
  auto& node_target = state_->targets[node];
  const auto part = target.surface_id;
  auto material_uri = MaterialUri(entries);
  if (node_target.geometry_uri != target.geometry_uri) {
    state_->Report(node, material_uri.value_or("geometry default"),
      "override target belongs to a different geometry", false, generation,
      on_failure, part);
    return;
  }
  node_target.parts.insert_or_assign(part,
    State::PartAssignment {
      .generation = generation,
      .geometry_generation = node_target.geometry_generation,
      .target = std::move(target),
      .intent = intent,
      .entries = std::move(entries),
      .material_uri = material_uri,
      .material = {},
      .accepted_geometry = accepted_geometry,
      .ready = false,
      .apply = false,
      .rejected_surface = false,
      .on_failure = std::move(on_failure),
      .on_success = std::move(on_success),
    });
  auto complete = [inbox = std::weak_ptr(state_->inbox), node, part,
                    generation](Material asset, std::string error) {
    if (auto queue = inbox.lock()) {
      queue->Enqueue(State::Completion {
        .node = node,
        .generation = generation,
        .surface = part,
        .geometry = false,
        .geometry_asset = {},
        .material_asset = std::move(asset),
        .error = std::move(error),
      });
    }
  };
  // A surface part carries the layout revision it was resolved against; the
  // component's own part carries none.
  const auto& requested = node_target.parts.at(part).target;
  if (requested.geometry_uri.empty()
    || part.IsNil() != base::IsAllZero(requested.layout_revision)
    || (material_uri && material_uri->empty())
    || (intent != OverrideIntent::kObservedEdit
      && intent != OverrideIntent::kRetainedOverride)) {
    complete({},
      "overrides require geometry identity, and a surface's overrides its "
      "layout revision");
    return;
  }
  if (state_->loads_paused) {
    return;
  }
  if (!material_uri) {
    complete({}, {});
    return;
  }
  try {
    state_->material_loader(*material_uri, complete);
  } catch (const std::exception& error) {
    complete({}, error.what());
  } catch (...) {
    complete({}, "unknown exception starting asset load");
  }
}

void SceneAssetRequests::Detach(scene::NodeHandle node)
{
  // Generations are session-wide, so erasing and reattaching cannot reuse one.
  state_->targets.erase(node);
}

void SceneAssetRequests::State::LoadEnvironmentTexture(const std::size_t slot)
{
  auto& source = environment.sources.at(slot);
  auto complete = [inbox = std::weak_ptr(environment_inbox),
                    generation = environment.generation, slot](
                    content::ResourceKey key, Texture texture,
                    std::string error) {
    if (const auto queue = inbox.lock()) {
      queue->Enqueue(EnvironmentCompletion { .generation = generation,
        .slot = slot,
        .key = key,
        .texture = std::move(texture),
        .error = std::move(error) });
    }
  };
  try {
    texture_loader(*source.virtual_path, complete);
  } catch (const std::exception& error) {
    complete({}, {}, error.what());
  } catch (...) {
    complete({}, {},
      fmt::format("unknown failure loading the {}",
        EnvironmentTextureLabel(slot)));
  }
}

/*!
 Applies the current environment revision once no slot is pending: every key
 when all slots loaded, otherwise nothing, retaining the previous revision.
*/
void SceneAssetRequests::State::SettleEnvironment(
  scene::Scene& scene, bool& changed)
{
  auto& request = environment;
  if (request.settled || request.IsPending()) {
    return;
  }
  request.settled = true;
  auto errors = std::vector<std::string> {};
  for (std::size_t slot = 0; slot < kEnvironmentTextureSlotCount; ++slot) {
    if (!request.status.at(slot).error.empty()) {
      errors.push_back(fmt::format("{} '{}': {}", EnvironmentTextureLabel(slot),
        *request.sources.at(slot).virtual_path,
        request.status.at(slot).error));
    }
  }
  if (errors.empty()) {
    try {
      request.apply(scene, request.loaded);
    } catch (const std::exception& error) {
      errors.emplace_back(error.what());
    }
  }
  if (errors.empty()) {
    changed = true;
    for (std::size_t slot = 0; slot < kEnvironmentTextureSlotCount; ++slot) {
      request.status.at(slot).accepted = request.loaded.at(slot);
    }
    request.accepted_textures = request.loaded_textures;
    request.accepted_sources = request.sources;
    if (request.on_success) {
      request.on_success(request.generation);
    }
    return;
  }
  auto error = std::string {};
  for (const auto& part : errors) {
    error += error.empty() ? part : "; " + part;
  }
  const auto message = fmt::format(
    "Environment textures rejected: {}; previous settings retained", error);
  if (refresh_requests.contains(request.generation)) {
    refresh_error = message;
  }
  diagnostic(message);
  if (request.on_failure) {
    request.on_failure(request.generation, error);
  }
}

void SceneAssetRequests::SetEnvironmentTextures(scene::Scene& scene,
  EnvironmentTextureSources sources, EnvironmentApply apply,
  FailureCallback on_failure, SuccessCallback on_success)
{
  auto& request = state_->environment;
  request.generation = ++state_->generation;
  request.sources = std::move(sources);
  request.apply = std::move(apply);
  request.on_failure = std::move(on_failure);
  request.on_success = std::move(on_success);
  request.loaded = {};
  request.loaded_textures = {};
  request.settled = false;
  // A slot keeps its accepted texture while its source is unchanged, so an
  // edit to other environment settings applies without reloading textures.
  for (std::size_t slot = 0; slot < kEnvironmentTextureSlotCount; ++slot) {
    auto& status = request.status.at(slot);
    const auto& source = request.sources.at(slot);
    const auto reuse = status.accepted.get() != 0U
      && IsSameTexture(source, request.accepted_sources.at(slot));
    status.pending = source.virtual_path.has_value() && !reuse;
    status.error.clear();
    if (reuse) {
      request.loaded.at(slot) = status.accepted;
      request.loaded_textures.at(slot) = request.accepted_textures.at(slot);
    }
  }
  if (!request.IsPending()) {
    auto changed = false;
    state_->SettleEnvironment(scene, changed);
    return;
  }
  if (state_->loads_paused) {
    return;
  }
  for (std::size_t slot = 0; slot < kEnvironmentTextureSlotCount; ++slot) {
    if (request.status.at(slot).pending) {
      state_->LoadEnvironmentTexture(slot);
    }
  }
}

auto SceneAssetRequests::InspectEnvironmentTexture(
  const EnvironmentTextureSlot slot) const -> EnvironmentTextureStatus
{
  return state_->environment.status.at(static_cast<std::size_t>(slot));
}


void SceneAssetRequests::Refresh(scene::Scene& scene)
{
  const auto was_paused = state_->loads_paused;
  state_->loads_paused = false;
  state_->refresh_requests.clear();
  state_->refresh_error.clear();
  if (state_->environment.apply && state_->environment.HasSources()) {
    // A refresh reloads every texture, since recooking may have replaced it.
    state_->environment.accepted_sources = {};
    const auto previous = state_->environment;
    SetEnvironmentTextures(scene, previous.sources, previous.apply,
      previous.on_failure, previous.on_success);
    state_->refresh_requests.insert(state_->environment.generation);
  }
  for (auto& [handle, target] : state_->targets) {
    const auto node = scene.GetNode(handle);
    if (!node || !node->IsAlive()) {
      continue;
    }
    if (!target.geometry_uri.empty()
      && state_->CanRefresh(target.geometry_uri, false)) {
      auto complete = BeginGeometry(handle, target.geometry_uri,
        target.geometry_failure, target.geometry_success);
      state_->refresh_requests.insert(state_->generation);
      LoadGeometry(target.geometry_uri, std::move(complete));
    }
    // Copy the authored requests before QueueOverrides replaces their state.
    const auto parts = target.parts;
    for (const auto& [id, part] : parts) {
      if (!part.rejected_surface
        && (!part.material_uri || state_->CanRefresh(*part.material_uri, true))) {
        QueueOverrides(handle, part.target, part.entries, part.intent,
          part.on_failure, part.on_success, part.accepted_geometry);
        state_->refresh_requests.insert(state_->generation);
      }
    }
  }
  state_->loads_paused = was_paused;
}

void SceneAssetRequests::SuspendLoads() { state_->loads_paused = true; }

void SceneAssetRequests::ResumeLoads(scene::Scene& scene)
{
  state_->loads_paused = false;
  // A refresh reloads only what the mounted content resolves; a request held
  // back while paused that it cannot resolve is started here so it reports
  // its own failure instead of staying pending.
  for (auto& [handle, target] : state_->targets) {
    const auto node = scene.GetNode(handle);
    if (!node || !node->IsAlive()) {
      continue;
    }
    if (target.geometry_pending
      && !state_->IsAvailable(target.geometry_uri, false)) {
      auto complete = BeginGeometry(handle, target.geometry_uri,
        target.geometry_failure, target.geometry_success);
      LoadGeometry(target.geometry_uri, std::move(complete));
    }
    const auto parts = target.parts;
    for (const auto& [id, part] : parts) {
      if (!part.ready && !part.rejected_surface && part.material_uri
        && !state_->IsAvailable(*part.material_uri, true)) {
        QueueOverrides(handle, part.target, part.entries, part.intent,
          part.on_failure, part.on_success, part.accepted_geometry);
      }
    }
  }
  Refresh(scene);
}

auto SceneAssetRequests::IsRefreshPending() const -> bool
{
  if (state_->environment.IsPending()
    && state_->refresh_requests.contains(state_->environment.generation)) {
    return true;
  }
  for (const auto& [handle, target] : state_->targets) {
    if (target.geometry_pending
      && state_->refresh_requests.contains(target.geometry_generation)) {
      return true;
    }
    for (const auto& [id, part] : target.parts) {
      if (!part.ready && state_->refresh_requests.contains(part.generation)) {
        return true;
      }
    }
  }
  return false;
}

auto SceneAssetRequests::RefreshError() const -> std::string
{
  return state_->refresh_error;
}

auto SceneAssetRequests::Drain(scene::Scene& scene) -> bool
{
  bool changed = false;
  state_->environment_inbox->Drain(
    [this](State::EnvironmentCompletion& result) {
      auto& request = state_->environment;
      auto& status = request.status.at(result.slot);
      if (request.generation != result.generation || !status.pending) {
        return;
      }
      status.pending = false;
      if (result.error.empty() && result.texture && result.key.get() != 0U) {
        request.loaded.at(result.slot) = result.key;
        request.loaded_textures.at(result.slot) = std::move(result.texture);
        return;
      }
      status.error = result.error.empty() ? "texture load returned no resource"
                                          : result.error;
    });
  state_->SettleEnvironment(scene, changed);
  std::erase_if(state_->targets, [&scene](const auto& entry) {
    const auto node = scene.GetNode(entry.first);
    return !node || !node->IsAlive();
  });
  state_->inbox->Drain([this, &scene, &changed](State::Completion& result) {
    const auto found = state_->targets.find(result.node);
    if (found == state_->targets.end()) {
      return;
    }
    auto& target = found->second;
    const auto node = scene.GetNode(result.node);
    if (result.geometry) {
      if (target.geometry_generation != result.generation
        || !target.geometry_pending) {
        return;
      }
      target.geometry_pending = false;
      if (!result.geometry_asset || !result.error.empty()) {
        target.geometry_failed = true;
        state_->Report(result.node, target.geometry_uri,
          result.error.empty() ? "asset load failed" : result.error, true,
          result.generation, target.geometry_failure);
        return;
      }
      node->GetRenderable().SetGeometry(std::move(result.geometry_asset));
      changed = true;
      if (target.geometry_success) {
        target.geometry_success(result.generation);
      }
      const auto geometry = node->GetRenderable().GetGeometry();
      // A surface's overrides need repair when the geometry no longer has the
      // surface they were resolved against; the component's own overrides
      // survive any replacement.
      for (auto& [id, part] : target.parts) {
        if (!id.IsNil() && part.accepted_geometry
          && (*part.accepted_geometry != geometry->GetAssetKey()
            || geometry->FindSurface(id) == nullptr)) {
          part.ready = true;
          part.apply = false;
          part.rejected_surface = true;
          part.material.reset();
          state_->Report(result.node, part.material_uri.value_or("geometry default"),
            "surface overrides require repair after geometry replacement", false,
            part.generation, part.on_failure, id);
        }
      }
    } else {
      const auto part_found = target.parts.find(result.surface);
      if (part_found == target.parts.end()
        || part_found->second.generation != result.generation
        || part_found->second.ready) {
        return;
      }
      auto& part = part_found->second;
      if (!result.error.empty()
        || (!result.material_asset && part.material_uri.has_value())) {
        part.ready = true;
        state_->Report(result.node, part.material_uri.value_or("geometry default"),
          result.error.empty() ? "asset load failed" : result.error, false,
          result.generation, part.on_failure, result.surface);
        return;
      }
      part.material = std::move(result.material_asset);
      part.ready = true;
      part.apply = true;
    }
  });

  for (auto& [handle, target] : state_->targets) {
    if (std::ranges::none_of(
          target.parts, [](const auto& entry) { return entry.second.apply; })) {
      continue;
    }
    const auto node = scene.GetNode(handle);
    auto renderable = node->GetRenderable();
    const auto geometry = renderable.GetGeometry();
    for (auto& [id, part] : target.parts) {
      if (!part.apply || target.geometry_pending || target.geometry_failed
        || !geometry) {
        continue;
      }
      part.apply = false;
      const auto label = part.material_uri.value_or("geometry default");
      const bool is_surface = !id.IsNil();
      const bool same_target = target.geometry_uri == part.target.geometry_uri
        && part.geometry_generation == target.geometry_generation;
      const bool revision_valid = !is_surface
        || (part.accepted_geometry
            ? *part.accepted_geometry == geometry->GetAssetKey()
            : part.intent == OverrideIntent::kRetainedOverride
              || part.target.layout_revision
                == geometry->Surfaces().layout_revision);
      const bool surface_exists
        = !is_surface || geometry->FindSurface(id) != nullptr;
      if (!same_target || !revision_valid || (part.material && !surface_exists)) {
        state_->Report(handle, label,
          "override target is stale or unavailable", false, part.generation,
          part.on_failure, id);
        part.material.reset();
        part.rejected_surface = true;
        continue;
      }
      if (surface_exists
        && !ApplyOverrides(renderable, id, part.entries, part.material)) {
        state_->Report(handle, label, "overrides could not be applied", false,
          part.generation, part.on_failure, id);
        part.material.reset();
        part.rejected_surface = true;
        continue;
      }
      changed = changed || surface_exists;
      part.accepted_geometry = geometry->GetAssetKey();
      if (is_surface) {
        part.target.layout_revision = geometry->Surfaces().layout_revision;
      }
      part.material.reset();
      part.rejected_surface = !surface_exists;
      if (part.on_success) {
        part.on_success(part.generation);
      }
    }
  }
  return changed;
}

} // namespace oxygen::interop::module
