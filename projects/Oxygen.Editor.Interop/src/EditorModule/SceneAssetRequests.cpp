//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged
#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "pch.h"
#include <EditorModule/EditorModule.h>
#include <EditorModule/SceneAssetRequests.h>
#include <EditorModule/ThreadSafeQueue.h>

#include <Oxygen/Data/BuiltinGeometry.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/TextureResource.h>

namespace oxygen::interop::module {
namespace {

  auto VirtualPath(std::string_view uri, bool material) -> std::string
  {
    if (uri.starts_with("asset:")) {
      uri.remove_prefix(6);
    }
    while (!uri.empty() && uri.front() == '/') {
      uri.remove_prefix(1);
    }
    auto path = "/" + std::string(uri);
    if (material && path.ends_with(".omat.json")) {
      path.resize(path.size() - 5);
    }
    return path;
  }

  auto GeometryIdentity(std::string_view uri) -> std::string
  {
    if (const auto builtin = data::ResolveBuiltinGeometryIdentity(uri)) {
      return builtin->asset_uri;
    }
    return VirtualPath(uri, false);
  }

} // namespace

struct SceneAssetRequests::State {
  struct MaskCompletion {
    uint64_t generation {};
    content::ResourceKey key {};
    Texture texture;
    std::string error;
  };
  struct MaskRequest {
    uint64_t generation {};
    std::optional<content::TextureResourceLocator> locator;
    std::optional<std::wstring> project_mount;
    TextureApply apply;
    FailureCallback on_failure;
    SuccessCallback on_success;
    ExposureMaskStatus status;
  };
  struct Completion {
    scene::NodeHandle node {};
    uint64_t generation = 0;
    data::MaterialSlotId slot;
    bool geometry = false;
    Geometry geometry_asset;
    Material material_asset;
    std::string error;
  };
  struct Slot {
    uint64_t generation = 0;
    uint64_t geometry_generation = 0;
    MaterialSlotTarget target;
    MaterialSlotAssignmentIntent intent
      = MaterialSlotAssignmentIntent::kObservedEdit;
    std::optional<std::string> uri;
    Material material;
    std::optional<data::AssetKey> accepted_geometry;
    bool ready = false;
    bool apply = false;
    bool rejected_slot = false;
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
    std::unordered_map<data::MaterialSlotId, Slot> slots;
  };

  GeometryLoader geometry_loader;
  MaterialLoader material_loader;
  Diagnostic diagnostic;
  AssetAvailability available;
  TextureLoader texture_loader;
  std::shared_ptr<const std::vector<CookedRootBinding>> roots;
  MaskRequest mask;
  std::shared_ptr<ThreadSafeQueue<MaskCompletion>> mask_inbox
    = std::make_shared<ThreadSafeQueue<MaskCompletion>>();
  uint64_t generation = 0;
  std::unordered_map<scene::NodeHandle, Target> targets;
  std::unordered_set<uint64_t> refresh_requests;
  std::string refresh_error;
  bool loads_paused = false;
  std::shared_ptr<ThreadSafeQueue<Completion>> inbox
    = std::make_shared<ThreadSafeQueue<Completion>>();

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
    const FailureCallback& on_failure, data::MaterialSlotId slot = {})
  {
    if (refresh_requests.contains(generation)) {
      refresh_error = fmt::format("{} '{}' could not be refreshed: {}",
        geometry ? "Geometry" : "Material", uri, error);
    }
    diagnostic(fmt::format("{} request '{}' on node {} slot {}: {}",
      geometry ? "Geometry" : "Material", uri, nostd::to_string(node), slot,
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
        const auto key = resolver.ResolveAssetKey(VirtualPath(uri, false));
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
        const auto path = VirtualPath(uri, true);
        if (path == "/Engine/Generated/Materials/Default") {
          complete(data::MaterialAsset::CreateDefault(), {});
          return;
        }
        const auto key = resolver.ResolveAssetKey(path);
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
      [](const std::string& message) { LOG_F(ERROR, "{}", message); },
      [&resolver](const std::string& uri, bool material) {
        const auto path = VirtualPath(uri, material);
        return (material && path == "/Engine/Generated/Materials/Default")
          || resolver.ResolveAssetKey(path).has_value();
      },
      [&loader](const content::TextureResourceLocator& locator,
        TextureCompletion complete) {
        const auto key = loader.ResolveTextureResourceKey(locator);
        if (!key) {
          complete({}, {},
            "texture descriptor is unavailable in its mounted cooked source");
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
  if (GeometryIdentity(target.geometry_uri) != GeometryIdentity(uri)) {
    target.slots.clear();
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
        .slot = {},
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

void SceneAssetRequests::SetMaterial(scene::NodeHandle node,
  MaterialSlotTarget target, const std::optional<std::string>& uri,
  const MaterialSlotAssignmentIntent intent, FailureCallback on_failure,
  SuccessCallback on_success)
{
  QueueMaterial(node, std::move(target), uri, intent, std::move(on_failure),
    std::move(on_success), std::nullopt);
}

void SceneAssetRequests::QueueMaterial(scene::NodeHandle node,
  MaterialSlotTarget target, const std::optional<std::string>& uri,
  const MaterialSlotAssignmentIntent intent, FailureCallback on_failure,
  SuccessCallback on_success, std::optional<data::AssetKey> accepted_geometry)
{
  const auto generation = ++state_->generation;
  auto& node_target = state_->targets[node];
  const auto slot_id = target.slot_id;
  if (GeometryIdentity(node_target.geometry_uri)
    != GeometryIdentity(target.geometry_uri)) {
    state_->Report(node, uri.value_or("geometry default"),
      "material slot target belongs to a different geometry", false, generation,
      on_failure, slot_id);
    return;
  }
  node_target.slots.insert_or_assign(slot_id,
    State::Slot {
      .generation = generation,
      .geometry_generation = node_target.geometry_generation,
      .target = std::move(target),
      .intent = intent,
      .uri = uri,
      .material = {},
      .accepted_geometry = accepted_geometry,
      .ready = false,
      .apply = false,
      .rejected_slot = false,
      .on_failure = std::move(on_failure),
      .on_success = std::move(on_success),
    });
  auto complete = [inbox = std::weak_ptr(state_->inbox), node, slot_id,
                    generation](Material asset, std::string error) {
    if (auto queue = inbox.lock()) {
      queue->Enqueue(State::Completion {
        .node = node,
        .generation = generation,
        .slot = slot_id,
        .geometry = false,
        .geometry_asset = {},
        .material_asset = std::move(asset),
        .error = std::move(error),
      });
    }
  };
  const auto& requested = node_target.slots.at(slot_id).target;
  if (slot_id.IsNil() || requested.geometry_uri.empty()
    || base::IsAllZero(requested.layout_revision) || (uri && uri->empty())
    || (intent != MaterialSlotAssignmentIntent::kObservedEdit
      && intent != MaterialSlotAssignmentIntent::kRetainedAssignment)) {
    complete(
      {}, "material assignment requires geometry, slot and layout identities");
    return;
  }
  if (state_->loads_paused) {
    return;
  }
  if (!uri) {
    complete({}, {});
    return;
  }
  try {
    state_->material_loader(*uri, complete);
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

void SceneAssetRequests::SetExposureMask(scene::Scene& scene,
  std::optional<content::TextureResourceLocator> locator, TextureApply apply,
  FailureCallback on_failure, SuccessCallback on_success,
  std::optional<std::wstring> project_mount)
{
  auto& request = state_->mask;
  request.generation = ++state_->generation;
  request.locator = std::move(locator);
  request.project_mount = std::move(project_mount);
  request.apply = std::move(apply);
  request.on_failure = std::move(on_failure);
  request.on_success = std::move(on_success);
  request.status.pending = request.locator.has_value();
  request.status.error.clear();
  if (!request.locator) {
    request.apply(scene, {});
    request.status.accepted = {};
    if (request.on_success) {
      request.on_success(request.generation);
    }
    return;
  }
  if (state_->loads_paused) {
    return;
  }
  auto complete = [inbox = std::weak_ptr(state_->mask_inbox),
                    generation = request.generation](content::ResourceKey key,
                    Texture texture, std::string error) {
    if (const auto queue = inbox.lock()) {
      queue->Enqueue(State::MaskCompletion { .generation = generation,
        .key = key,
        .texture = std::move(texture),
        .error = std::move(error) });
    }
  };
  try {
    if (request.project_mount) {
      const CookedRootBinding* binding = nullptr;
      if (state_->roots) {
        const auto found
          = std::ranges::find_if(*state_->roots, [&](const auto& root) {
              return root.project_mount == request.project_mount;
            });
        if (found != state_->roots->end()) {
          binding = std::addressof(*found);
        }
      }
      if (!binding) {
        throw std::runtime_error(
          "The exposure mask's project content mount is unavailable.");
      }
      request.locator->cooked_root = binding->path;
    }
    state_->texture_loader(*request.locator, complete);
  } catch (const std::exception& error) {
    complete({}, {}, error.what());
  } catch (...) {
    complete({}, {}, "unknown failure loading exposure metering mask");
  }
}

auto SceneAssetRequests::InspectExposureMask() const -> ExposureMaskStatus
{
  return state_->mask.status;
}

void SceneAssetRequests::SetCookedRoots(
  std::shared_ptr<const std::vector<CookedRootBinding>> roots) noexcept
{
  state_->roots = std::move(roots);
}

void SceneAssetRequests::Refresh(scene::Scene& scene)
{
  const auto was_paused = state_->loads_paused;
  state_->loads_paused = false;
  state_->refresh_requests.clear();
  state_->refresh_error.clear();
  if (state_->mask.apply && state_->mask.locator) {
    const auto previous = state_->mask;
    SetExposureMask(scene, previous.locator, previous.apply,
      previous.on_failure, previous.on_success, previous.project_mount);
    state_->refresh_requests.insert(state_->mask.generation);
  }
  for (auto& [handle, target] : state_->targets) {
    const auto node = scene.GetNode(handle);
    if (!node || !node->IsAlive()) {
      continue;
    }
    if (!target.geometry_uri.empty()
      && !data::IsBuiltinGeometryUri(target.geometry_uri)
      && state_->CanRefresh(target.geometry_uri, false)) {
      auto complete = BeginGeometry(handle, target.geometry_uri,
        target.geometry_failure, target.geometry_success);
      state_->refresh_requests.insert(state_->generation);
      LoadGeometry(target.geometry_uri, std::move(complete));
    }
    // Copy the authored requests before SetMaterial replaces their slot state.
    const auto slots = target.slots;
    for (const auto& [index, slot] : slots) {
      if (!slot.rejected_slot
        && (!slot.uri || state_->CanRefresh(*slot.uri, true))) {
        QueueMaterial(handle, slot.target, slot.uri, slot.intent,
          slot.on_failure, slot.on_success, slot.accepted_geometry);
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
  Refresh(scene);
}

auto SceneAssetRequests::IsRefreshPending() const -> bool
{
  if (state_->mask.status.pending
    && state_->refresh_requests.contains(state_->mask.generation)) {
    return true;
  }
  for (const auto& [handle, target] : state_->targets) {
    if (target.geometry_pending
      && state_->refresh_requests.contains(target.geometry_generation)) {
      return true;
    }
    for (const auto& [index, slot] : target.slots) {
      if (!slot.ready && state_->refresh_requests.contains(slot.generation)) {
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
  state_->mask_inbox->Drain([this, &scene, &changed](
                              State::MaskCompletion& result) {
    auto& request = state_->mask;
    if (request.generation != result.generation || !request.status.pending) {
      return;
    }
    request.status.pending = false;
    if (result.error.empty() && result.texture && result.key.get() != 0U) {
      try {
        request.apply(scene, result.key);
      } catch (const std::exception& error) {
        result.error = error.what();
      }
      if (result.error.empty()) {
        changed = true;
        request.status.accepted = result.key;
        if (request.on_success) {
          request.on_success(result.generation);
        }
        return;
      }
    }
    request.status.error = result.error.empty()
      ? "texture load returned no resource"
      : result.error;
    const auto message = fmt::format(
      "Exposure mask '{}' rejected: {}; previous settings retained",
      request.locator->descriptor_relative_path.generic_string(),
      request.status.error);
    if (state_->refresh_requests.contains(result.generation)) {
      state_->refresh_error = message;
    }
    state_->diagnostic(message);
    if (request.on_failure) {
      request.on_failure(result.generation, request.status.error);
    }
  });
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
      for (auto& [id, slot] : target.slots) {
        if (slot.accepted_geometry
          && (*slot.accepted_geometry != geometry->GetAssetKey()
            || geometry->FindMaterialSlot(id) == nullptr)) {
          slot.ready = true;
          slot.apply = false;
          slot.rejected_slot = true;
          slot.material.reset();
          state_->Report(result.node, slot.uri.value_or("geometry default"),
            "material slot requires repair after geometry replacement", false,
            slot.generation, slot.on_failure, id);
        }
      }
    } else {
      const auto slot_found = target.slots.find(result.slot);
      if (slot_found == target.slots.end()
        || slot_found->second.generation != result.generation
        || slot_found->second.ready) {
        return;
      }
      auto& slot = slot_found->second;
      if (!result.error.empty()
        || (!result.material_asset && slot.uri.has_value())) {
        slot.ready = true;
        state_->Report(result.node, slot.uri.value_or("geometry default"),
          result.error.empty() ? "asset load failed" : result.error, false,
          result.generation, slot.on_failure, result.slot);
        return;
      }
      slot.material = std::move(result.material_asset);
      slot.ready = true;
      slot.apply = true;
    }
  });

  for (auto& [handle, target] : state_->targets) {
    if (std::ranges::none_of(
          target.slots, [](const auto& entry) { return entry.second.apply; })) {
      continue;
    }
    const auto node = scene.GetNode(handle);
    auto renderable = node->GetRenderable();
    const auto geometry = renderable.GetGeometry();
    for (auto& [id, slot] : target.slots) {
      if (!slot.apply || target.geometry_pending || target.geometry_failed
        || !geometry) {
        continue;
      }
      slot.apply = false;
      const bool same_target = GeometryIdentity(target.geometry_uri)
          == GeometryIdentity(slot.target.geometry_uri)
        && slot.geometry_generation == target.geometry_generation;
      const bool revision_valid = slot.accepted_geometry
        ? *slot.accepted_geometry == geometry->GetAssetKey()
        : slot.intent == MaterialSlotAssignmentIntent::kRetainedAssignment
          || slot.target.layout_revision
            == geometry->MaterialSlots().layout_revision;
      const bool slot_exists = geometry->FindMaterialSlot(id) != nullptr;
      if (!same_target || !revision_valid || (slot.material && !slot_exists)) {
        state_->Report(handle, slot.uri.value_or("geometry default"),
          "material slot target is stale or unavailable", false,
          slot.generation, slot.on_failure, id);
        slot.material.reset();
        slot.rejected_slot = true;
        continue;
      }
      if (slot_exists && !renderable.SetMaterialOverride(id, slot.material)) {
        state_->Report(handle, slot.uri.value_or("geometry default"),
          "material slot assignment could not be applied", false,
          slot.generation, slot.on_failure, id);
        slot.material.reset();
        slot.rejected_slot = true;
        continue;
      }
      changed = changed || slot_exists;
      slot.accepted_geometry = geometry->GetAssetKey();
      slot.target.layout_revision = geometry->MaterialSlots().layout_revision;
      slot.material.reset();
      slot.rejected_slot = !slot_exists;
      if (slot.on_success) {
        slot.on_success(slot.generation);
      }
    }
  }
  return changed;
}

} // namespace oxygen::interop::module
