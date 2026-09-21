//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma managed(push, off)

#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Commands/DetachGeometryCommand.h>
#include <Commands/RemoveSceneNodeCommand.h>
#include <Commands/SetGeometryCommand.h>
#include <Commands/SetMaterialOverrideCommand.h>
#include <Commands/SetEnvironmentCommand.h>
#include <EditorModule/SceneAssetRequests.h>
#include <EditorModule/ThreadSafeQueue.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

namespace {
using namespace oxygen::interop::module;
using Requests = SceneAssetRequests;

void Require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

auto MakeGeometry(const std::string &name) -> Requests::Geometry {
  auto [vertices, indices] = *oxygen::data::MakeCubeMeshAsset();
  oxygen::data::pak::geometry::MeshViewDesc view{};
  view.vertex_count = static_cast<uint32_t>(vertices.size());
  view.index_count = static_cast<uint32_t>(indices.size());
  const auto material = oxygen::data::MaterialAsset::CreateDefault();
  auto mesh = oxygen::data::MeshBuilder(0, name)
                  .WithVertices(vertices)
                  .WithIndices(indices)
                  .BeginSubMesh("first", material)
                  .WithMeshView(view)
                  .EndSubMesh()
                  .BeginSubMesh("second", material)
                  .WithMeshView(view)
                  .EndSubMesh()
                  .Build();
  oxygen::data::pak::geometry::GeometryAssetDesc desc{};
  desc.lod_count = 1;
  std::vector<std::shared_ptr<oxygen::data::Mesh>> meshes;
  meshes.push_back(std::move(mesh));
  return std::make_shared<oxygen::data::GeometryAsset>(
      oxygen::data::AssetKey::FromVirtualPath(name), desc, std::move(meshes));
}

auto MakeMaterial(const std::string &name) -> Requests::Material {
  return std::make_shared<oxygen::data::MaterialAsset>(
      oxygen::data::AssetKey::FromVirtualPath(name),
      oxygen::data::pak::render::MaterialAssetDesc{});
}

struct Fixture {
  std::shared_ptr<oxygen::scene::Scene> scene =
      std::make_shared<oxygen::scene::Scene>("Requests", 32U);
  oxygen::scene::SceneNode node = scene->CreateNode("target");
  Requests::Geometry a = MakeGeometry("/a");
  Requests::Geometry b = MakeGeometry("/b");
  Requests::Material red = MakeMaterial("/red");
  Requests::Material blue = MakeMaterial("/blue");
  std::vector<Requests::GeometryCompletion> geometry_loads;
  std::vector<Requests::MaterialCompletion> material_loads;
  std::vector<Requests::TextureCompletion> texture_loads;
  std::unordered_map<std::string, Requests::Geometry> geometry_cache;
  std::unordered_map<std::string, Requests::Material> material_cache;
  std::vector<std::string> diagnostics;
  std::unordered_set<std::string> unavailable;
  std::unique_ptr<Requests> requests = NewRequests();

  auto NewRequests() -> std::unique_ptr<Requests> {
    return std::make_unique<Requests>(
        [this](const std::string &uri, Requests::GeometryCompletion complete) {
          if (uri == "throw") {
            throw std::runtime_error("loader exception");
          }
          if (geometry_cache.contains(uri)) {
            complete(geometry_cache.at(uri), {});
          } else {
            geometry_loads.push_back(std::move(complete));
          }
        },
        [this](const std::string &uri, Requests::MaterialCompletion complete) {
          if (uri == "throw") {
            throw std::runtime_error("loader exception");
          }
          if (material_cache.contains(uri)) {
            complete(material_cache.at(uri), {});
          } else {
            material_loads.push_back(std::move(complete));
          }
        },
        [this](const std::string &message) { diagnostics.push_back(message); },
        [this](const std::string& uri, bool) { return !unavailable.contains(uri); },
        [this](const oxygen::content::TextureResourceLocator&, Requests::TextureCompletion complete) {
          texture_loads.push_back(std::move(complete));
        });
  }

  void Execute(EditorCommand &command) {
    CommandContext context{.Scene = oxygen::observer_ptr(scene.get()),
                           .AssetRequests =
                               oxygen::observer_ptr(requests.get())};
    command.Execute(context);
  }
  void Geometry(const std::string &uri) {
    SetGeometryCommand command(node.GetHandle(), uri);
    Execute(command);
  }
  void Material(const std::string &uri, std::size_t slot = 0) {
    SetMaterialOverrideCommand command(node.GetHandle(), slot, uri);
    Execute(command);
  }
  void Detach() {
    DetachGeometryCommand command(node.GetHandle());
    Execute(command);
  }
  void Drain() { requests->Drain(*scene); }
  auto Geometry() const -> Requests::Geometry {
    return node.GetRenderable().GetGeometry();
  }
  auto CurrentMaterial(std::size_t slot = 0) const -> Requests::Material {
    return node.GetRenderable().ResolveSubmeshMaterial(0, slot);
  }
  void Attach() {
    geometry_cache["initial"] = a;
    Geometry("initial");
    Drain();
  }
};

void GeometryOrdering(bool reverse) {
  Fixture f;
  f.Geometry("A");
  f.Geometry("B");
  if (reverse) {
    f.geometry_loads[1](f.b, {});
    f.Drain();
    f.geometry_loads[0](f.a, {});
  } else {
    f.geometry_loads[0](f.a, {});
    f.Drain();
    Require(!f.Geometry(), "A applied despite newer B intent");
    f.geometry_loads[1](f.b, {});
  }
  f.Drain();
  Require(f.Geometry() == f.b, "latest geometry did not win");
}

void MaterialOrdering(bool reverse) {
  Fixture f;
  f.Attach();
  f.Material("red");
  f.Material("blue");
  if (reverse) {
    f.material_loads[1](f.blue, {});
    f.Drain();
    f.material_loads[0](f.red, {});
  } else {
    f.material_loads[0](f.red, {});
    f.Drain();
    Require(f.CurrentMaterial() != f.red, "superseded red applied");
    f.material_loads[1](f.blue, {});
  }
  f.Drain();
  Require(f.CurrentMaterial() == f.blue, "latest material did not win");
}

void CachedAndProceduralReplacement() {
  Fixture f;
  f.Geometry("A");
  f.geometry_cache["B"] = f.b;
  f.Geometry("B");
  f.Drain();
  f.geometry_loads[0](f.a, {});
  f.Drain();
  Require(f.Geometry() == f.b, "cached replacement overwritten");
  f.Geometry("A");
  f.Geometry("asset:///Engine/Generated/BasicShapes/Cube");
  f.Drain();
  auto cube = f.Geometry();
  Require(cube && cube != f.a && cube != f.b, "procedural mesh missing");
  f.geometry_loads[1](f.a, {});
  f.Drain();
  Require(f.Geometry() == cube, "procedural replacement overwritten");
  f.Geometry("A");
  f.Geometry("asset:///Engine/Generated/BasicShapes/Cube");
  f.geometry_loads[2](f.a, {});
  f.Drain();
  Require(f.Geometry() == cube, "cached procedural replacement overwritten");
}

void CachedMaterialAndClear() {
  Fixture f;
  f.Attach();
  f.Material("red");
  f.material_cache["blue"] = f.blue;
  f.Material("blue");
  f.Drain();
  f.material_loads[0](f.red, {});
  f.Drain();
  Require(f.CurrentMaterial() == f.blue, "cached material overwritten");
  f.Material("red");
  f.Material("");
  f.Drain();
  f.material_loads[1](f.red, {});
  f.Drain();
  Require(f.CurrentMaterial() == oxygen::data::MaterialAsset::CreateDefault(),
          "clear reversed by late callback");
}

void DetachAndReattach() {
  Fixture f;
  f.Geometry("A");
  f.Material("red");
  f.Detach(); // No component exists yet.
  f.geometry_loads[0](f.a, {});
  f.material_loads[0](f.red, {});
  f.Drain();
  Require(!f.Geometry(), "pending load reversed detach");
  f.Attach();
  f.Material("red");
  f.Detach();
  f.Attach();
  f.material_loads[1](f.red, {});
  f.Drain();
  Require(f.CurrentMaterial() != f.red,
          "old component material revived on reattach");
}

void UndoRedo() {
  Fixture f;
  f.Attach();
  f.Geometry("B");
  f.Geometry("initial"); // Undo B via the same production command path.
  f.Drain();
  f.geometry_loads[0](f.b, {});
  f.Drain();
  Require(f.Geometry() == f.a, "undo geometry was reversed");
  f.Geometry("B"); // Redo creates a fresh request.
  f.geometry_loads[1](f.b, {});
  f.Drain();
  Require(f.Geometry() == f.b, "redo geometry did not apply");
  f.Material("red");
  f.Material(""); // Undo the override.
  f.material_loads[0](f.red, {});
  f.Drain();
  Require(f.CurrentMaterial() != f.red, "undo material was reversed");
  f.Material("red");
  f.material_loads[1](f.red, {});
  f.Drain();
  Require(f.CurrentMaterial() == f.red, "redo material did not apply");
}

void DestroyedTarget() {
  Fixture f;
  f.Geometry("A");
  f.Material("red");
  RemoveSceneNodeCommand remove(f.node.GetHandle());
  f.Execute(remove);
  f.node = f.scene->CreateNode("replacement");
  f.geometry_loads[0](f.a, {});
  f.material_loads[0](f.red, {});
  f.Drain();
  Require(!f.Geometry(), "old load applied to recreated node");
  Require(f.diagnostics.empty(), "destroyed target reported current failure");
}

void DestroyedDescendant() {
  Fixture f;
  auto parent = f.node;
  f.node = *f.scene->CreateChildNode(parent, "child");
  f.Geometry("A");
  f.Material("red");
  RemoveSceneNodeCommand remove(parent.GetHandle());
  f.Execute(remove);
  f.node = f.scene->CreateNode("replacement");
  f.geometry_loads[0](f.a, {});
  f.material_loads[0](f.red, {});
  f.Drain();
  Require(!f.Geometry(), "deleted descendant callback mutated replacement");
  Require(f.diagnostics.empty(), "deleted descendant reported current failure");
}

void FailedIntentSupersedesOlderSuccess() {
  Fixture f;
  f.Attach();
  f.Geometry("A");
  f.Geometry("asset:///Engine/Generated/BasicShapes/Unknown");
  f.geometry_loads[0](f.b, {});
  f.Drain();
  Require(f.Geometry() == f.a, "failed procedural intent revived older load");
  f.Material("red");
  f.Material("throw");
  f.material_loads[0](f.red, {});
  f.Drain();
  Require(f.CurrentMaterial() != f.red,
          "failed material intent revived older load");
  Require(f.diagnostics.size() == 2, "failed current requests not diagnosed");
}

void SceneSessionLifetime() {
  Fixture f;
  f.Geometry("A");
  f.Material("red");
  f.requests.reset();
  // Even an externally retained old scene must not receive a late completion.
  auto old_scene = f.scene;
  auto old_node = f.node;
  f.scene = std::make_shared<oxygen::scene::Scene>("replacement", 32U);
  f.node = f.scene->CreateNode("target");
  f.requests = f.NewRequests();
  f.Attach();
  f.geometry_loads[0](f.b, {});
  f.material_loads[0](f.red, {});
  f.Drain();
  Require(!old_node.GetRenderable().GetGeometry(), "retired scene mutated");
  Require(f.Geometry() == f.a && f.CurrentMaterial() != f.red,
          "old scene callbacks affected new scene");
  f.Geometry("late");
  f.requests.reset();
  f.geometry_loads[1](f.b, {}); // Shutdown must leave a safe no-op callback.
  Require(f.Geometry() == f.a, "shutdown callback mutated scene");
}

void IndependentTargetsAndSlots() {
  Fixture f;
  f.Attach();
  auto first = f.node;
  f.Material("red", 0);
  f.Material("blue", 1);
  f.node = f.scene->CreateNode("other");
  f.Geometry("B");
  f.material_loads[1](f.blue, {});
  f.geometry_loads[0](f.b, {});
  f.material_loads[0](f.red, {});
  f.Drain();
  Require(f.Geometry() == f.b, "unrelated node request blocked");
  f.node = first;
  Require(f.CurrentMaterial(0) == f.red && f.CurrentMaterial(1) == f.blue,
          "unrelated slot superseded");
}

void MaterialBeforeGeometry() {
  Fixture f;
  f.Geometry("A");
  f.Material("red", 1);
  f.material_loads[0](f.red, {});
  f.Drain();
  Require(!f.Geometry() && f.diagnostics.empty(), "material applied too early");
  f.geometry_loads[0](f.a, {});
  f.Drain();
  Require(f.CurrentMaterial(1) == f.red, "material lost before geometry ready");
  f.Geometry("B");
  f.geometry_loads[1](f.b, {});
  f.Drain();
  Require(f.CurrentMaterial(1) == f.red, "geometry replacement lost override");
}

void RemovedSlotDoesNotReviveOverride() {
  Fixture f;
  f.Attach();
  f.material_cache["red"] = f.red;
  f.Material("red", 1);
  f.Drain();
  Require(f.CurrentMaterial(1) == f.red, "initial slot override missing");
  f.Geometry("asset:///Engine/Generated/BasicShapes/Cube");
  f.Drain();
  f.Geometry("B");
  f.geometry_loads[0](f.b, {});
  f.Drain();
  Require(f.CurrentMaterial(1) != f.red,
    "override from a removed slot resurfaced when the index returned");
}

void ClearBeforeGeometry() {
  Fixture f;
  f.Geometry("A");
  f.Material("red");
  f.Material("");
  f.material_loads[0](f.red, {});
  f.Drain();
  f.geometry_loads[0](f.a, {});
  f.Drain();
  Require(f.CurrentMaterial() != f.red, "clear before attachment reversed");
  Require(f.diagnostics.empty(), "clear of absent component reported failure");
}

void ClearWhileReplacementLoads() {
  Fixture f;
  f.Attach();
  f.material_cache["red"] = f.red;
  f.Material("red");
  f.Drain();
  Require(f.CurrentMaterial() == f.red, "initial override missing");
  f.Geometry("B");
  f.Material("");
  f.Drain();
  Require(f.Geometry() == f.a && f.CurrentMaterial() != f.red,
          "clear waited for replacement geometry instead of clearing visible "
          "override");
  f.geometry_loads[0](f.b, {});
  f.Drain();
  Require(f.CurrentMaterial() != f.red, "replacement geometry reversed clear");
}

void FailureDiagnostics() {
  Fixture f;
  f.Attach();
  f.Geometry("old");
  f.Geometry("current");
  f.geometry_loads[0]({}, "obsolete failure");
  f.geometry_loads[1]({}, "current failure");
  f.Drain();
  Require(f.Geometry() == f.a, "failed load removed previous geometry");
  Require(
      f.diagnostics.size() == 1 &&
          f.diagnostics[0].find("current failure") != std::string::npos &&
          f.diagnostics[0].find("current") != std::string::npos,
      "current geometry failure diagnostic missing or stale failure reported");
  f.Material("old");
  f.Material("current");
  f.material_loads[0]({}, "obsolete failure");
  f.material_loads[1]({}, "material failure");
  f.Drain();
  Require(f.diagnostics.size() == 2 &&
              f.diagnostics[1].find("material failure") != std::string::npos,
          "current material failure diagnostic missing");
  f.Geometry("throw");
  f.Material("throw");
  f.Drain();
  Require(f.diagnostics.size() == 4,
          "loader exceptions escaped diagnostic path");
}

// Confirms failure delivery uses the native generation after mutation-phase
// validation, discards superseded callbacks, and terminates failed requests.
void CorrelatedFailureDelivery() {
  Fixture f;
  f.Attach();
  std::vector<uint64_t> generations;
  auto on_failure = [&generations](uint64_t generation,
                                  const std::string&) {
    generations.push_back(generation);
  };
  SetGeometryCommand old_geometry(f.node.GetHandle(), "old");
  old_geometry.SetFailureCallback(on_failure);
  f.Execute(old_geometry);
  SetGeometryCommand current_geometry(f.node.GetHandle(), "current");
  current_geometry.SetFailureCallback(on_failure);
  f.Execute(current_geometry);
  f.geometry_loads[0]({}, "obsolete");
  f.geometry_loads[1]({}, "current");
  Require(generations.empty(), "failure escaped the mutation boundary");
  f.Drain();
  Require(generations.size() == 1 && generations[0] > 0,
          "native generation was lost or an obsolete failure was delivered");

  SetMaterialOverrideCommand material(f.node.GetHandle(), 0, "missing");
  material.SetFailureCallback(on_failure);
  f.Execute(material);
  f.material_loads[0]({}, "failed");
  f.material_loads[0]({}, "duplicate failure");
  f.Drain();
  Require(generations.size() == 2 && generations[1] > generations[0],
          "material failure was duplicated or assigned another generation");

  SetGeometryCommand detached(f.node.GetHandle(), "detached");
  detached.SetFailureCallback(on_failure);
  f.Execute(detached);
  f.Detach();
  f.geometry_loads[2]({}, "detached failure");
  f.Drain();
  Require(generations.size() == 2, "detached target reported stale failure");
}

void MutationBoundary() {
  Fixture f;
  f.Geometry("A");
  std::thread worker([&f] { f.geometry_loads[0](f.a, {}); });
  worker.join();
  Require(!f.Geometry(), "callback mutated scene outside mutation drain");
  ThreadSafeQueue<std::unique_ptr<EditorCommand>> commands;
  f.geometry_cache["B"] = f.b;
  commands.Enqueue(
      std::make_unique<SetGeometryCommand>(f.node.GetHandle(), "B"));
  commands.Drain([&f](auto &command) { f.Execute(*command); });
  f.Drain();
  Require(f.Geometry() == f.b, "new queued intent lost to completion");
}

thread_local char last_error[1024]{};

void RefreshRebindsSameIdentityWithoutRecreatingNode() {
  Fixture f;
  f.Attach();
  f.material_cache["red"] = f.red;
  f.Material("red");
  f.Drain();
  const auto handle = f.node.GetHandle();
  const auto updated = MakeMaterial("/red");
  f.material_cache["red"] = updated;
  f.requests->Refresh(*f.scene);
  Require(f.requests->IsRefreshPending(), "refresh completed before queued replacements were applied");
  Require(f.CurrentMaterial() == f.red, "refresh changed scene outside mutation drain");
  f.Drain();
  Require(!f.requests->IsRefreshPending(), "applied replacements did not complete refresh");
  Require(f.CurrentMaterial() == updated, "same-key material was not rebound");
  Require(f.node.GetHandle() == handle, "refresh replaced the scene node");
}

void RefreshPreservesNewerEditsAndPreviousMaterialOnFailure() {
  Fixture f;
  f.Attach();
  f.material_cache["red"] = f.red;
  f.Material("red");
  f.Drain();
  f.material_cache.clear();
  f.requests->Refresh(*f.scene);
  f.material_loads[0]({}, "refresh failed");
  f.Drain();
  Require(!f.requests->IsRefreshPending() && !f.requests->RefreshError().empty(), "failed refresh did not settle with its error");
  Require(f.CurrentMaterial() == f.red, "failed refresh removed the visible material");
  f.requests->Refresh(*f.scene);
  f.Material("blue");
  f.material_loads[2](f.blue, {});
  f.material_loads[1](f.red, {});
  f.Drain();
  Require(f.CurrentMaterial() == f.blue, "refresh reversed a newer assignment");
}

void RefreshDoesNotReviveClearedOrRemovedSlots() {
  Fixture f;
  f.Attach();
  f.material_cache["red"] = f.red;
  f.Material("red", 1);
  f.Drain();
  f.Geometry("asset:///Engine/Generated/BasicShapes/Cube");
  f.Drain();
  f.geometry_cache["B"] = f.b;
  f.Geometry("B");
  f.Drain();
  f.requests->Refresh(*f.scene);
  f.Drain();
  Require(f.CurrentMaterial(1) != f.red, "refresh resurrected a removed slot override");
  f.Material("red");
  f.Drain();
  f.Material("");
  f.Drain();
  f.requests->Refresh(*f.scene);
  f.Drain();
  Require(f.CurrentMaterial() != f.red, "refresh reversed None");
}

void RefreshLeavesUncookedIntentForItsOwnPublication() {
  Fixture f;
  f.Attach();
  f.material_cache["red"] = f.red;
  f.Material("red");
  f.Drain();
  f.Material("uncooked");
  f.material_loads[0]({}, "not cooked yet");
  f.Drain();
  f.unavailable.insert("uncooked");
  f.requests->Refresh(*f.scene);
  f.Drain();
  Require(f.material_loads.size() == 1, "unrelated publication retried an uncooked intent");
  Require(f.requests->RefreshError().empty(), "unrelated uncooked intent failed publication");
  Require(f.CurrentMaterial() == f.red, "unrelated publication removed last visible material");
  f.unavailable.clear();
  f.material_cache["uncooked"] = f.blue;
  f.requests->Refresh(*f.scene);
  f.Drain();
  Require(f.CurrentMaterial() == f.blue, "newly published intent was not resolved");
}

void SuspendedLoadsRetainLatestIntentAndClear() {
  Fixture f;
  f.Attach();
  f.material_cache["red"] = f.red;
  f.Material("red");
  f.Drain();
  f.requests->SuspendLoads();
  f.Material("blue");
  f.Material("");
  f.Drain();
  Require(f.material_loads.empty(), "suspended authoring started file reads");
  Require(f.CurrentMaterial() == f.red, "suspension changed the last applied material");
  f.requests->ResumeLoads(*f.scene);
  f.Drain();
  Require(f.CurrentMaterial() != f.red, "resume lost the newer None intent");
  Require(!f.requests->IsRefreshPending(), "clear remained pending after resume");
}

void PublicationRefreshWorksWhileAuthoringLoadsStaySuspended() {
  Fixture f;
  f.Attach();
  f.requests->SuspendLoads();
  f.Material("blue");
  f.material_cache["blue"] = f.blue;
  f.requests->Refresh(*f.scene);
  f.Drain();
  Require(f.CurrentMaterial() == f.blue, "publication did not bind staged material while paused");
  f.Material("red");
  f.Drain();
  Require(f.material_loads.empty(), "publication accidentally resumed ordinary loading");
  f.material_cache["red"] = f.red;
  f.requests->ResumeLoads(*f.scene);
  f.Drain();
  Require(f.CurrentMaterial() == f.red, "resume did not apply the newer authoring intent");
}

void SuccessfulRefreshAcknowledgesCurrentGeneration() {
  Fixture f;
  f.Attach();
  uint64_t failed = 0;
  std::vector<uint64_t> applied;
  SetMaterialOverrideCommand material(f.node.GetHandle(), 0, "retry");
  material.SetFailureCallback([&](uint64_t generation, const std::string&) {
    failed = generation;
  });
  material.SetSuccessCallback([&](uint64_t generation) {
    Require(f.CurrentMaterial() == f.red, "success preceded material application");
    applied.push_back(generation);
  });
  f.Execute(material);
  f.material_loads.back()({}, "first attempt failed");
  f.Drain();
  Require(failed > 0 && applied.empty(), "failed load was acknowledged as applied");
  f.material_cache["retry"] = f.red;
  f.requests->Refresh(*f.scene);
  Require(applied.empty(), "refresh escaped the mutation boundary");
  f.Drain();
  Require(applied.size() == 1 && applied[0] > failed,
          "retry did not report its newly applied native generation");
  f.material_loads.front()({}, "late failure");
  f.Drain();
  Require(applied.size() == 1, "obsolete completion produced another acknowledgement");
}

void SuccessWaitsForCurrentGeometryAndMaterialApplication() {
  Fixture f;
  int geometry_count = 0;
  int material_count = 0;
  SetGeometryCommand geometry(f.node.GetHandle(), "geometry");
  geometry.SetSuccessCallback([&](uint64_t) {
    Require(f.Geometry() == f.a, "geometry success preceded application");
    ++geometry_count;
  });
  SetMaterialOverrideCommand material(f.node.GetHandle(), 0, "material");
  material.SetSuccessCallback([&](uint64_t) {
    Require(f.CurrentMaterial() == f.red, "material success preceded application");
    ++material_count;
  });
  f.Execute(geometry);
  f.Execute(material);
  f.material_loads.back()(f.red, {});
  f.Drain();
  Require(material_count == 0, "material waiting for geometry was acknowledged");
  f.geometry_loads.back()(f.a, {});
  f.Drain();
  Require(geometry_count == 1 && material_count == 1, "current applications were not acknowledged");
  f.geometry_loads.back()(f.b, {});
  f.Drain();
  Require(geometry_count == 1, "duplicate geometry completion was acknowledged");
}

void MaterialRecoversAfterGeometryFailure(bool material_first) {
  Fixture f;
  std::vector<uint64_t> geometry_failures;
  std::vector<uint64_t> geometry_successes;
  std::vector<uint64_t> material_successes;
  int material_failures = 0;
  SetGeometryCommand geometry(f.node.GetHandle(), "retry-geometry");
  geometry.SetFailureCallback([&](uint64_t generation, const std::string&) {
    geometry_failures.push_back(generation);
  });
  geometry.SetSuccessCallback([&](uint64_t generation) {
    Require(f.Geometry() == f.a, "geometry recovery acknowledged before application");
    geometry_successes.push_back(generation);
  });
  SetMaterialOverrideCommand material(f.node.GetHandle(), 0, "red");
  material.SetFailureCallback([&](uint64_t, const std::string&) { ++material_failures; });
  material.SetSuccessCallback([&](uint64_t generation) {
    Require(f.CurrentMaterial() == f.red, "material recovery acknowledged before application");
    material_successes.push_back(generation);
  });
  f.Execute(geometry);
  f.Execute(material);
  const auto original_geometry = f.geometry_loads.back();
  const auto original_material = f.material_loads.back();
  if (material_first) {
    original_material(f.red, {});
    f.Drain();
    Require(material_successes.empty(), "material applied while geometry was loading");
  }
  original_geometry({}, "temporary geometry failure");
  f.Drain();
  if (!material_first) {
    original_material(f.red, {});
    f.Drain();
  }
  Require(!f.Geometry(), "failed geometry created an attachment");
  Require(geometry_failures.size() == 1 && material_successes.empty(),
          "failure acknowledged an unavailable material application");
  Require(material_failures == 0, "unavailable geometry rejected an unverified slot");

  // A failed publication refresh settles without discarding authored intent.
  f.requests->Refresh(*f.scene);
  Require(f.requests->IsRefreshPending(), "refresh finished before its completions");
  f.material_loads.back()(f.red, {});
  f.geometry_loads.back()({}, "geometry still unavailable");
  f.Drain();
  Require(!f.requests->IsRefreshPending() && !f.requests->RefreshError().empty(),
          "failed geometry refresh did not settle with its diagnostic");
  Require(geometry_failures.size() == 2 && material_successes.empty(),
          "failed refresh claimed material success");

  f.geometry_cache["retry-geometry"] = f.a;
  f.material_cache["red"] = f.red;
  f.requests->Refresh(*f.scene);
  Require(material_successes.empty(), "cached refresh escaped the mutation boundary");
  f.Drain();
  Require(geometry_successes.size() == 1 && material_successes.size() == 1,
          "publication did not recover both current requests");
  Require(material_successes.front() > geometry_failures.back(),
          "recovery did not acknowledge the refreshed material generation");
  Require(!f.requests->IsRefreshPending() && f.requests->RefreshError().empty(),
          "successful recovery retained the failed refresh state");
  original_geometry(f.b, {});
  original_material(f.blue, {});
  f.Drain();
  Require(f.Geometry() == f.a && f.CurrentMaterial() == f.red,
          "obsolete completions reversed recovered content");
  Require(geometry_successes.size() == 1 && material_successes.size() == 1
            && material_failures == 0,
          "obsolete completions produced another acknowledgement");
}

void FailedReplacementDoesNotValidateSlotsAgainstPreviousGeometry() {
  Fixture f;
  f.Geometry("asset:///Engine/Generated/BasicShapes/Cube");
  f.Drain();
  const auto previous_geometry = f.Geometry();
  Require(previous_geometry && previous_geometry->MeshAt(0)->SubMeshes().size() == 1,
          "replacement scenario needs a one-slot visible geometry");
  f.material_cache["red"] = f.red;
  f.Material("red");
  f.Drain();
  int failures = 0;
  int successes = 0;
  f.Geometry("replacement");
  SetMaterialOverrideCommand material(f.node.GetHandle(), 1, "blue");
  material.SetFailureCallback([&](uint64_t, const std::string&) { ++failures; });
  material.SetSuccessCallback([&](uint64_t) {
    Require(f.CurrentMaterial(1) == f.blue, "replacement material acknowledged too early");
    ++successes;
  });
  f.Execute(material);
  f.material_loads.back()(f.blue, {});
  f.geometry_loads.back()({}, "replacement load failed");
  f.Drain();
  Require(f.Geometry() == previous_geometry && f.CurrentMaterial() == f.red,
          "failed replacement changed the visible content");
  Require(failures == 0 && successes == 0,
          "old geometry decided the failed replacement's slot validity");
  f.geometry_cache["replacement"] = f.b;
  f.material_cache["blue"] = f.blue;
  f.requests->Refresh(*f.scene);
  f.Drain();
  Require(f.Geometry() == f.b && f.CurrentMaterial(1) == f.blue,
          "replacement publication did not apply the pending slot");
  Require(f.CurrentMaterial() == f.red && successes == 1 && failures == 0,
          "replacement recovery lost a surviving slot or its acknowledgement");
}

void LatestMaterialIntentWinsAfterGeometryFailure() {
  for (const auto clear : { false, true }) {
    Fixture f;
    f.Geometry("retry-geometry");
    int obsolete_successes = 0;
    SetMaterialOverrideCommand original(f.node.GetHandle(), 0, "red");
    original.SetSuccessCallback([&](uint64_t) { ++obsolete_successes; });
    f.Execute(original);
    const auto original_material = f.material_loads.back();
    original_material(f.red, {});
    f.geometry_loads.back()({}, "temporary failure");
    f.Drain();
    int latest_successes = 0;
    SetMaterialOverrideCommand latest(f.node.GetHandle(), 0, clear ? "" : "blue");
    latest.SetSuccessCallback([&](uint64_t) { ++latest_successes; });
    f.Execute(latest);
    if (!clear) {
      f.material_loads.back()(f.blue, {});
    }
    f.Drain();
    Require(latest_successes == (clear ? 1 : 0),
            "clear did not apply immediately or material applied without geometry");
    original_material(f.red, {});
    f.geometry_cache["retry-geometry"] = f.a;
    f.material_cache["red"] = f.red;
    f.material_cache["blue"] = f.blue;
    f.requests->Refresh(*f.scene);
    f.Drain();
    Require(f.Geometry() == f.a && obsolete_successes == 0,
            "recovery acknowledged superseded material intent");
    Require(f.CurrentMaterial() == (clear ? oxygen::data::MaterialAsset::CreateDefault() : f.blue),
            "recovery revived an older material over the latest assignment or None");
    Require(latest_successes == 1, "latest intent did not receive exactly one acknowledgement");
  }
}

void ConfirmedInvalidSlotRequiresNewExplicitAssignment() {
  Fixture f;
  f.Geometry("asset:///Engine/Generated/BasicShapes/Cube");
  f.Drain();
  f.material_cache["red"] = f.red;
  uint64_t failed_generation = 0;
  uint64_t applied_generation = 0;
  int failures = 0;
  int successes = 0;
  auto assign = [&] {
    SetMaterialOverrideCommand material(f.node.GetHandle(), 1, "red");
    material.SetFailureCallback([&](uint64_t generation, const std::string&) {
      failed_generation = generation;
      ++failures;
    });
    material.SetSuccessCallback([&](uint64_t generation) {
      applied_generation = generation;
      ++successes;
    });
    f.Execute(material);
  };
  assign();
  f.Drain();
  Require(failures == 1 && successes == 0, "known invalid slot was not rejected");
  f.requests->Refresh(*f.scene);
  f.Drain();
  f.geometry_cache["replacement"] = f.b;
  f.Geometry("replacement");
  f.Drain();
  f.requests->Refresh(*f.scene);
  f.Drain();
  Require(f.CurrentMaterial(1) != f.red && failures == 1 && successes == 0,
          "publication resurrected a terminally rejected slot");
  assign();
  f.Drain();
  Require(f.CurrentMaterial(1) == f.red && successes == 1 && failures == 1,
          "explicit fresh assignment did not replace the rejected intent");
  Require(applied_generation > failed_generation,
          "explicit assignment reused the rejected request generation");
}

auto MakeMaskTexture() -> Requests::Texture {
  using namespace oxygen::data::pak;
  auto header = render::TexturePayloadHeader {};
  header.subresource_count = 1U;
  header.layouts_offset_bytes = sizeof(header);
  header.data_offset_bytes = sizeof(header) + sizeof(render::SubresourceLayout);
  header.total_payload_size = header.data_offset_bytes + 4U;
  const auto layout = render::SubresourceLayout { .offset_bytes = 0U,
    .row_pitch_bytes = 4U, .size_bytes = 4U };
  auto bytes = std::vector<uint8_t>(header.total_payload_size, 0U);
  std::memcpy(bytes.data(), &header, sizeof(header));
  std::memcpy(bytes.data() + header.layouts_offset_bytes, &layout, sizeof(layout));
  auto descriptor = core::TextureResourceDesc {};
  descriptor.size_bytes = header.total_payload_size;
  descriptor.texture_type = static_cast<uint8_t>(oxygen::TextureType::kTexture2D);
  descriptor.width = 1U;
  descriptor.height = 1U;
  descriptor.depth = 1U;
  descriptor.array_layers = 1U;
  descriptor.mip_levels = 1U;
  descriptor.format = static_cast<uint8_t>(oxygen::Format::kRGBA8UNorm);
  descriptor.alignment = 256U;
  return std::make_shared<oxygen::data::TextureResource>(descriptor, std::move(bytes));
}

void ExposureMaskRequestsAreAtomicAndLatestWins() {
  Fixture f;
  const auto texture = MakeMaskTexture();
  auto failures = 0;
  auto successes = 0;
  const auto request = [&](float ev, const char* descriptor) {
    auto post = PostProcessParams {};
    post.manual_exposure_ev = ev;
    if (descriptor) {
      post.auto_exposure_metering_mask = oxygen::content::TextureResourceLocator {
        .cooked_root = "C:/Cooked", .descriptor_relative_path = descriptor };
    }
    auto command = SetEnvironmentCommand(SkyAtmosphereParams {}, post);
    command.SetFailureCallback([&](uint64_t, const std::string&) { ++failures; });
    command.SetSuccessCallback([&](uint64_t) { ++successes; });
    f.Execute(command);
  };
  const auto exposure = [&] {
    return f.scene->GetEnvironment()
      ->TryGetSystem<oxygen::scene::environment::PostProcessVolume>()->GetExposureSettings();
  };
  request(4.0F, nullptr);
  request(6.0F, "A.otex");
  request(8.0F, "B.otex");
  Require(exposure().manual_ev == 4.0F, "pending mask partially applied exposure");
  f.texture_loads.at(1)(oxygen::content::ResourceKey { 41U }, texture, {});
  f.Drain();
  Require(exposure().manual_ev == 8.0F && exposure().metering_mask.get() == 41U,
    "latest mask did not apply its complete revision");
  f.texture_loads.at(0)({}, {}, "obsolete failure");
  f.Drain();
  Require(failures == 0 && f.diagnostics.empty(), "obsolete mask failure was reported");
  request(10.0F, "C.otex");
  f.texture_loads.at(2)({}, {}, "missing texture");
  f.Drain();
  Require(exposure().manual_ev == 8.0F && exposure().metering_mask.get() == 41U,
    "failed mask replaced the previously accepted revision");
  Require(failures == 1 && !f.requests->InspectExposureMask().error.empty(),
    "current mask failure was not observable");
  f.requests->Refresh(*f.scene);
  Require(f.requests->IsRefreshPending(), "mask refresh was not tracked");
  f.texture_loads.at(3)(oxygen::content::ResourceKey { 99U }, texture, {});
  f.Drain();
  Require(exposure().manual_ev == 10.0F && exposure().metering_mask.get() == 99U,
    "mask refresh did not retry the retained authored intent");
  Require(!f.requests->IsRefreshPending() && f.requests->RefreshError().empty(),
    "successful mask refresh remained pending or failed");
  request(12.0F, "D.otex");
  request(14.0F, nullptr);
  f.texture_loads.at(4)(oxygen::content::ResourceKey { 101U }, texture, {});
  f.Drain();
  Require(exposure().manual_ev == 14.0F && exposure().metering_mask.get() == 0U,
    "late mask completion undid a clear request");
  Require(successes == 4, "accepted environment requests were not acknowledged exactly once");
}

void ExposureMaskRequestsRespectPauseAndSceneLifetime() {
  Fixture f;
  const auto texture = MakeMaskTexture();
  auto applied = 0;
  const auto locator = oxygen::content::TextureResourceLocator {
    .cooked_root = "C:/Cooked", .descriptor_relative_path = "Mask.otex" };
  f.requests->SuspendLoads();
  f.requests->SetExposureMask(*f.scene, locator,
    [&](auto&, auto) { ++applied; });
  Require(f.texture_loads.empty(), "suspended mask request started a load");
  f.requests->ResumeLoads(*f.scene);
  Require(f.texture_loads.size() == 1U, "resuming did not start the mask request");
  auto complete = f.texture_loads.front();
  f.requests.reset();
  complete(oxygen::content::ResourceKey { 41U }, texture, {});
  Require(applied == 0, "mask completion escaped its scene session lifetime");
}

auto RunScenario(int scenario) -> const char * {
  try {
    switch (scenario) {
    case 33:
      ExposureMaskRequestsAreAtomicAndLatestWins();
      break;
    case 34:
      ExposureMaskRequestsRespectPauseAndSceneLifetime();
      break;
    case 28:
      MaterialRecoversAfterGeometryFailure(true);
      break;
    case 29:
      MaterialRecoversAfterGeometryFailure(false);
      break;
    case 30:
      FailedReplacementDoesNotValidateSlotsAgainstPreviousGeometry();
      break;
    case 31:
      LatestMaterialIntentWinsAfterGeometryFailure();
      break;
    case 32:
      ConfirmedInvalidSlotRequiresNewExplicitAssignment();
      break;
    case 26:
      SuccessfulRefreshAcknowledgesCurrentGeneration();
      break;
    case 27:
      SuccessWaitsForCurrentGeometryAndMaterialApplication();
      break;
    case 24:
      SuspendedLoadsRetainLatestIntentAndClear();
      break;
    case 25:
      PublicationRefreshWorksWhileAuthoringLoadsStaySuspended();
      break;
    case 23:
      RefreshLeavesUncookedIntentForItsOwnPublication();
      break;
    case 20:
      RefreshRebindsSameIdentityWithoutRecreatingNode();
      break;
    case 21:
      RefreshPreservesNewerEditsAndPreviousMaterialOnFailure();
      break;
    case 22:
      RefreshDoesNotReviveClearedOrRemovedSlots();
      break;
    case 0:
      GeometryOrdering(false);
      break;
    case 1:
      GeometryOrdering(true);
      break;
    case 2:
      MaterialOrdering(false);
      break;
    case 3:
      MaterialOrdering(true);
      break;
    case 4:
      CachedAndProceduralReplacement();
      break;
    case 5:
      CachedMaterialAndClear();
      break;
    case 6:
      DetachAndReattach();
      break;
    case 7:
      UndoRedo();
      break;
    case 8:
      DestroyedTarget();
      break;
    case 9:
      SceneSessionLifetime();
      break;
    case 10:
      IndependentTargetsAndSlots();
      break;
    case 11:
      MaterialBeforeGeometry();
      break;
    case 12:
      ClearBeforeGeometry();
      break;
    case 13:
      FailureDiagnostics();
      break;
    case 14:
      MutationBoundary();
      break;
    case 15:
      DestroyedDescendant();
      break;
    case 16:
      FailedIntentSupersedesOlderSuccess();
      break;
    case 17:
      ClearWhileReplacementLoads();
      break;
    case 18:
      RemovedSlotDoesNotReviveOverride();
      break;
    case 19:
      CorrelatedFailureDelivery();
      break;
    default:
      throw std::runtime_error("unknown scenario");
    }
    return nullptr;
  } catch (const std::exception &ex) {
    strncpy_s(last_error, ex.what(), _TRUNCATE);
    return last_error;
  }
}
} // namespace

#pragma managed(pop)

using namespace Microsoft::VisualStudio::TestTools::UnitTesting;

namespace InteropTests {

[TestClass]
public ref class AssetRequestTests {
private:
  static void Check(int scenario) {
    const auto error = RunScenario(scenario);
    Assert::IsTrue(error == nullptr,
      gcnew System::String(error != nullptr ? error : ""));
  }

public:
  [TestMethod]
  void ExposureMaskRevisionIsAtomicAcrossOrderingFailureRefreshAndClear() { Check(33); }

  [TestMethod]
  void ExposureMaskLoadRespectsPauseAndSceneLifetime() { Check(34); }

  [TestMethod]
  void LoadedMaterialRecoversAfterGeometryFailure() { Check(28); }

  [TestMethod]
  void MaterialLoadedAfterGeometryFailureRecovers() { Check(29); }

  [TestMethod]
  void FailedReplacementPreservesMaterialForItsOwnSlotLayout() { Check(30); }

  [TestMethod]
  void LatestMaterialOrClearWinsAfterGeometryRecovery() { Check(31); }

  [TestMethod]
  void ConfirmedInvalidSlotRequiresExplicitReassignment() { Check(32); }

  [TestMethod]
  void SuccessfulRefreshAcknowledgesCurrentGeneration() { Check(26); }

  [TestMethod]
  void SuccessWaitsForCurrentGeometryAndMaterialApplication() { Check(27); }

  [TestMethod]
  void SuspendedLoadsRetainLatestIntentAndClear() { Check(24); }

  [TestMethod]
  void PublicationRefreshWorksWhileAuthoringLoadsStaySuspended() { Check(25); }

  [TestMethod]
  void RefreshLeavesUncookedIntentForItsOwnPublication() { Check(23); }

  [TestMethod]
  void RefreshRebindsSameIdentityWithoutRecreatingNode() { Check(20); }

  [TestMethod]
  void RefreshPreservesNewerEditsAndPreviousMaterialOnFailure() { Check(21); }

  [TestMethod]
  void RefreshDoesNotReviveClearedOrRemovedSlots() { Check(22); }

  [TestMethod]
  void CorrelatedFailuresRespectGenerationAndLifetime() {
    Check(19);
  }

  [TestMethod]
  void GeometryInRequestOrder() {
    Check(0);
  }

  [TestMethod]
  void GeometryInReverseOrder() {
    Check(1);
  }

  [TestMethod]
  void MaterialsInRequestOrder() {
    Check(2);
  }

  [TestMethod]
  void MaterialsInReverseOrder() {
    Check(3);
  }

  [TestMethod]
  void CachedAndProceduralGeometryWins() {
    Check(4);
  }

  [TestMethod]
  void CachedMaterialAndClearWin() {
    Check(5);
  }

  [TestMethod]
  void DetachInvalidatesComponentLifetime() {
    Check(6);
  }

  [TestMethod]
  void UndoRedoSupersedesLoads() {
    Check(7);
  }

  [TestMethod]
  void DestroyedTargetRejectsLoads() {
    Check(8);
  }

  [TestMethod]
  void SceneReplacementAndShutdownRejectLoads() {
    Check(9);
  }

  [TestMethod]
  void IndependentNodesAndSlots() {
    Check(10);
  }

  [TestMethod]
  void MaterialWaitsForGeometry() {
    Check(11);
  }

  [TestMethod]
  void ClearBeforeAttachmentWins() {
    Check(12);
  }

  [TestMethod]
  void CurrentFailuresReachDiagnostics() {
    Check(13);
  }

  [TestMethod]
  void CallbacksWaitForMutationPhase() {
    Check(14);
  }

  [TestMethod]
  void DeletedDescendantsRejectLoads() {
    Check(15);
  }

  [TestMethod]
  void FailedIntentRejectsOlderSuccess() {
    Check(16);
  }

  [TestMethod]
  void ClearAppliesWhileReplacementLoads() {
    Check(17);
  }

  [TestMethod]
  void RemovedSlotDoesNotRestoreHistoricalOverride() {
    Check(18);
  }
};

} // namespace InteropTests
