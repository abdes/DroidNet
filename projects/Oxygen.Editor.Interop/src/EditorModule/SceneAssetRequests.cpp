//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged
#include "pch.h"

#include <EditorModule/SceneAssetRequests.h>
#include <EditorModule/ThreadSafeQueue.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <Oxygen/Data/BuiltinGeometry.h>
#include <Oxygen/Data/MaterialAsset.h>

namespace oxygen::interop::module {
namespace {

auto VirtualPath(std::string_view uri, bool material) -> std::string {
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

} // namespace

struct SceneAssetRequests::State {
  struct Completion {
    scene::NodeHandle node;
    uint64_t generation;
    std::size_t slot = 0;
    bool geometry = false;
    Geometry geometry_asset;
    Material material_asset;
    std::string error;
  };
  struct Slot {
    uint64_t generation = 0;
    std::string uri;
    Material material;
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
    FailureCallback geometry_failure;
    SuccessCallback geometry_success;
    std::unordered_map<std::size_t, Slot> slots;
  };

  GeometryLoader geometry_loader;
  MaterialLoader material_loader;
  Diagnostic diagnostic;
  AssetAvailability available;
  uint64_t generation = 0;
  std::unordered_map<scene::NodeHandle, Target> targets;
  std::unordered_set<uint64_t> refresh_requests;
  std::string refresh_error;
  bool loads_paused = false;
  std::shared_ptr<ThreadSafeQueue<Completion>> inbox =
      std::make_shared<ThreadSafeQueue<Completion>>();

  auto CanRefresh(const std::string& uri, bool material) -> bool {
    try {
      return available(uri, material);
    } catch (const std::exception& error) {
      refresh_error = fmt::format("Asset '{}' could not be resolved: {}", uri, error.what());
      diagnostic(refresh_error);
      return false;
    }
  }

  void Report(scene::NodeHandle node, const std::string &uri,
              const std::string &error, bool geometry,
              uint64_t generation, const FailureCallback &on_failure,
              std::size_t slot = 0) {
    if (refresh_requests.contains(generation)) {
      refresh_error = fmt::format("{} '{}' could not be refreshed: {}",
        geometry ? "Geometry" : "Material", uri, error);
    }
    diagnostic(fmt::format("{} request '{}' on node {} slot {}: {}",
                           geometry ? "Geometry" : "Material", uri,
                           nostd::to_string(node), slot, error));
    if (on_failure) {
      on_failure(generation, error);
    }
  }
};

SceneAssetRequests::SceneAssetRequests(content::IAssetLoader &loader,
                                       content::VirtualPathResolver &resolver)
    : SceneAssetRequests(
          [&loader, &resolver](const std::string &uri,
                               GeometryCompletion complete) {
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
          [&loader, &resolver](const std::string &uri,
                               MaterialCompletion complete) {
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
          [](const std::string &message) { LOG_F(ERROR, "{}", message); },
          [&resolver](const std::string& uri, bool material) {
            const auto path = VirtualPath(uri, material);
            return (material && path == "/Engine/Generated/Materials/Default")
              || resolver.ResolveAssetKey(path).has_value();
          }) {}

SceneAssetRequests::SceneAssetRequests(GeometryLoader geometry_loader,
                                       MaterialLoader material_loader,
                                       Diagnostic diagnostic,
                                       AssetAvailability available)
    : state_(std::make_unique<State>()) {
  state_->geometry_loader = std::move(geometry_loader);
  state_->material_loader = std::move(material_loader);
  state_->diagnostic = std::move(diagnostic);
  state_->available = std::move(available);
}

SceneAssetRequests::~SceneAssetRequests() = default;

auto SceneAssetRequests::BeginGeometry(scene::NodeHandle node,
                                       const std::string &uri,
                                       FailureCallback on_failure,
                                       SuccessCallback on_success)
    -> GeometryCompletion {
  auto &target = state_->targets[node];
  const auto generation = ++state_->generation;
  target.geometry_generation = generation;
  target.geometry_uri = uri;
  target.geometry_pending = true;
  target.geometry_failure = std::move(on_failure);
  target.geometry_success = std::move(on_success);
  return [inbox = std::weak_ptr(state_->inbox), node,
          generation](Geometry asset, std::string error) {
    if (auto queue = inbox.lock()) {
      queue->Enqueue(State::Completion{.node = node,
                                       .generation = generation,
                                       .geometry = true,
                                       .geometry_asset = std::move(asset),
                                       .error = std::move(error)});
    }
  };
}

void SceneAssetRequests::LoadGeometry(const std::string &uri,
                                      GeometryCompletion complete) {
  if (state_->loads_paused) {
    return;
  }
  try {
    state_->geometry_loader(uri, complete);
  } catch (const std::exception &ex) {
    complete({}, ex.what());
  } catch (...) {
    complete({}, "unknown exception starting asset load");
  }
}

void SceneAssetRequests::SetMaterial(scene::NodeHandle node, std::size_t slot,
                                     const std::string &uri,
                                     FailureCallback on_failure,
                                     SuccessCallback on_success) {
  const auto generation = ++state_->generation;
  state_->targets[node].slots[slot] =
      State::Slot{.generation = generation, .uri = uri,
                  .on_failure = std::move(on_failure),
                  .on_success = std::move(on_success)};
  if (state_->loads_paused) {
    return;
  }
  auto complete = [inbox = std::weak_ptr(state_->inbox), node, slot,
                   generation](Material asset, std::string error) {
    if (auto queue = inbox.lock()) {
      queue->Enqueue(State::Completion{.node = node,
                                       .generation = generation,
                                       .slot = slot,
                                       .material_asset = std::move(asset),
                                       .error = std::move(error)});
    }
  };
  if (uri.empty()) {
    complete({}, {});
    return;
  }
  try {
    state_->material_loader(uri, complete);
  } catch (const std::exception &ex) {
    complete({}, ex.what());
  } catch (...) {
    complete({}, "unknown exception starting asset load");
  }
}

void SceneAssetRequests::Detach(scene::NodeHandle node) {
  // Generations are session-wide, so erasing and reattaching cannot reuse one.
  state_->targets.erase(node);
}

void SceneAssetRequests::Refresh(scene::Scene &scene) {
  const auto was_paused = state_->loads_paused;
  state_->loads_paused = false;
  state_->refresh_requests.clear();
  state_->refresh_error.clear();
  for (auto &[handle, target] : state_->targets) {
    const auto node = scene.GetNode(handle);
    if (!node || !node->IsAlive()) {
      continue;
    }
    if (!target.geometry_uri.empty() &&
        !data::IsBuiltinGeometryUri(target.geometry_uri) &&
        state_->CanRefresh(target.geometry_uri, false)) {
      auto complete = BeginGeometry(handle, target.geometry_uri,
                                    target.geometry_failure, target.geometry_success);
      state_->refresh_requests.insert(state_->generation);
      LoadGeometry(target.geometry_uri, std::move(complete));
    }
    // Copy the authored requests before SetMaterial replaces their slot state.
    const auto slots = target.slots;
    for (const auto &[index, slot] : slots) {
      if (!slot.rejected_slot && (slot.uri.empty() || state_->CanRefresh(slot.uri, true))) {
        SetMaterial(handle, index, slot.uri, slot.on_failure, slot.on_success);
        state_->refresh_requests.insert(state_->generation);
      }
    }
  }
  state_->loads_paused = was_paused;
}

void SceneAssetRequests::SuspendLoads() { state_->loads_paused = true; }

void SceneAssetRequests::ResumeLoads(scene::Scene& scene) {
  state_->loads_paused = false;
  Refresh(scene);
}

auto SceneAssetRequests::IsRefreshPending() const -> bool {
  for (const auto& [handle, target] : state_->targets) {
    if (target.geometry_pending && state_->refresh_requests.contains(target.geometry_generation)) {
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

auto SceneAssetRequests::RefreshError() const -> std::string {
  return state_->refresh_error;
}

void SceneAssetRequests::Drain(scene::Scene &scene) {
  std::erase_if(state_->targets, [&scene](const auto &entry) {
    const auto node = scene.GetNode(entry.first);
    return !node || !node->IsAlive();
  });
  state_->inbox->Drain([this, &scene](State::Completion &result) {
    const auto found = state_->targets.find(result.node);
    if (found == state_->targets.end()) {
      return;
    }
    auto &target = found->second;
    const auto node = scene.GetNode(result.node);
    if (result.geometry) {
      if (target.geometry_generation != result.generation ||
          !target.geometry_pending) {
        return;
      }
      target.geometry_pending = false;
      if (!result.geometry_asset || !result.error.empty()) {
        state_->Report(
            result.node, target.geometry_uri,
            result.error.empty() ? "asset load failed" : result.error, true,
            result.generation, target.geometry_failure);
        return;
      }
      node->GetRenderable().SetGeometry(std::move(result.geometry_asset));
      if (target.geometry_success) {
        target.geometry_success(result.generation);
      }
      const auto geometry = node->GetRenderable().GetGeometry();
      const auto slot_count = geometry && geometry->LodCount() > 0 && geometry->MeshAt(0)
          ? geometry->MeshAt(0)->SubMeshes().size() : 0;
      for (auto &[index, slot] : target.slots) {
        if (slot.ready && !slot.apply && index >= slot_count) {
          slot.rejected_slot = true;
        }
      }
      // Existing overrides are preserved by the engine for surviving slots.
      // Only material results still awaiting geometry need application below.
    } else {
      const auto slot_found = target.slots.find(result.slot);
      if (slot_found == target.slots.end() ||
          slot_found->second.generation != result.generation ||
          slot_found->second.ready) {
        return;
      }
      auto &slot = slot_found->second;
      if (!result.error.empty() ||
          (!result.material_asset && !slot.uri.empty())) {
        slot.ready = true;
        state_->Report(result.node, slot.uri,
                       result.error.empty() ? "asset load failed"
                                            : result.error,
                       false, result.generation, slot.on_failure, result.slot);
        return;
      }
      slot.material = std::move(result.material_asset);
      slot.ready = true;
      slot.apply = true;
    }
  });

  for (auto &[handle, target] : state_->targets) {
    if (std::ranges::none_of(target.slots, [](const auto &entry) {
          return entry.second.apply;
        })) {
      continue;
    }
    const auto node = scene.GetNode(handle);
    auto renderable = node->GetRenderable();
    const auto geometry = renderable.GetGeometry();
    for (auto &[index, slot] : target.slots) {
      if (!slot.apply || (target.geometry_pending && slot.material)) {
        continue;
      }
      slot.apply = false;
      constexpr std::size_t lod = 0;
      if (!geometry || geometry->LodCount() == 0 || !geometry->MeshAt(lod) ||
          index >= geometry->MeshAt(lod)->SubMeshes().size()) {
        // Clearing a missing slot is already satisfied. A rejected material
        // must not resurface if a later geometry happens to reuse this index.
        if (slot.material) {
          state_->Report(handle, slot.uri, "material slot is unavailable",
                         false, slot.generation, slot.on_failure, index);
        } else if (slot.on_success) {
          slot.on_success(slot.generation);
        }
        slot.material.reset();
        slot.rejected_slot = true;
        continue;
      }
      if (slot.material) {
        renderable.SetMaterialOverride(lod, index, slot.material);
      } else {
        renderable.ClearMaterialOverride(lod, index);
      }
      slot.material.reset();
      if (slot.on_success) {
        slot.on_success(slot.generation);
      }
    }
  }
}

} // namespace oxygen::interop::module
