//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "pch.h"

#pragma managed(push, off)

#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Commands/DetachGeometryCommand.h>
#include <Commands/RemoveSceneNodeCommand.h>
#include <Commands/SetEnvironmentCommand.h>
#include <Commands/SetGeometryCommand.h>
#include <Commands/SetOverridesCommand.h>
#include <EditorModule/EditorModule.h>
#include <EditorModule/SceneAssetRequests.h>
#include <EditorModule/ThreadSafeQueue.h>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Data/GeometryIndices.h>

namespace {
using namespace oxygen::interop::module;
using Requests = SceneAssetRequests;
using oxygen::data::LodIndex;
using oxygen::data::SubmeshIndex;

void Require(bool condition, const char* message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

constexpr auto kCube = std::string_view { "/Engine/Generated/BasicShapes/Cube.ogeo" };

auto TestSurfaceId(std::size_t index) -> oxygen::data::SurfaceId
{
  return oxygen::data::SurfaceId::FromStableIdentity(
    "asset-request-test/declaration-" + std::to_string(index));
}

//! A two-surface geometry; `single_surface` keeps only the first surface.
auto MakeGeometry(const std::string& name,
  Requests::Material material = oxygen::data::MaterialAsset::CreateDefault(),
  const bool single_surface = false) -> Requests::Geometry
{
  auto [vertices, indices] = *oxygen::data::MakeCubeMeshAsset();
  oxygen::data::pak::geometry::MeshViewDesc view {};
  view.vertex_count = static_cast<uint32_t>(vertices.size());
  view.index_count = static_cast<uint32_t>(indices.size());
  auto builder = oxygen::data::MeshBuilder(oxygen::data::LodIndex {}, name);
  builder.WithVertices(vertices)
    .WithIndices(indices)
    .BeginSubMesh("first", material)
    .WithSurfaceId(TestSurfaceId(0U))
    .WithMeshView(view)
    .EndSubMesh();
  if (!single_surface) {
    builder.BeginSubMesh("second", material)
      .WithSurfaceId(TestSurfaceId(1U))
      .WithMeshView(view)
      .EndSubMesh();
  }
  auto mesh = builder.Build();
  oxygen::data::pak::geometry::GeometryAssetDesc desc {};
  desc.lod_count = 1;
  std::vector<std::shared_ptr<oxygen::data::Mesh>> meshes;
  meshes.push_back(std::move(mesh));
  return std::make_shared<oxygen::data::GeometryAsset>(
    oxygen::data::AssetKey::FromVirtualPath(name), desc, std::move(meshes));
}

auto MakeMaterial(const std::string& name) -> Requests::Material
{
  return std::make_shared<oxygen::data::MaterialAsset>(
    oxygen::data::AssetKey::FromVirtualPath(name),
    oxygen::data::pak::render::MaterialAssetDesc {});
}

//! The override entries of a surface that carry only a material, or none.
auto MaterialEntries(std::optional<std::string> uri)
  -> std::vector<OverrideEntry>
{
  auto entries = std::vector<OverrideEntry> {};
  if (uri) {
    entries.push_back(OverrideEntry {
      .id = oxygen::data::schema::OverrideId::kGeometrySurfaceMaterial,
      .value = OverrideAsset { .virtual_path = std::move(*uri) } });
  }
  return entries;
}

auto BoolEntry(const oxygen::data::schema::OverrideId id, const bool value)
  -> OverrideEntry
{
  return OverrideEntry { .id = id, .value = value };
}

auto FixedLodPolicyEntry(const std::uint8_t lod_index) -> OverrideEntry
{
  auto policy = oxygen::data::pak::geometry::LodPolicyRecord {};
  policy.type = oxygen::data::pak::geometry::LodPolicyType::kFixed;
  policy.lod_index = lod_index;
  return OverrideEntry {
    .id = oxygen::data::schema::OverrideId::kGeometryLodPolicy, .value = policy
  };
}

struct Fixture {
  std::shared_ptr<oxygen::scene::Scene> scene
    = std::make_shared<oxygen::scene::Scene>("Requests", 32U);
  oxygen::scene::SceneNode node = scene->CreateNode("target");
  Requests::Geometry a = MakeGeometry("/a");
  Requests::Geometry b = MakeGeometry("/b");
  Requests::Material red = MakeMaterial("/red");
  Requests::Material blue = MakeMaterial("/blue");
  //! The engine Cube: an ordinary asset with one surface, cached.
  Requests::Geometry cube = MakeGeometry(std::string(kCube),
    oxygen::data::MaterialAsset::CreateDefault(), true);
  std::vector<Requests::GeometryCompletion> geometry_loads;
  std::vector<Requests::MaterialCompletion> material_loads;
  std::vector<Requests::TextureCompletion> texture_loads;
  std::vector<std::string> texture_paths;
  std::unordered_map<std::string, Requests::Geometry> geometry_cache {
    { std::string(kCube), cube },
  };
  std::unordered_map<std::string, Requests::Material> material_cache;
  std::vector<std::string> diagnostics;
  std::unordered_map<oxygen::scene::NodeHandle, std::string> geometry_uris;
  std::unordered_set<std::string> unavailable;
  std::unique_ptr<Requests> requests = NewRequests();

  auto NewRequests() -> std::unique_ptr<Requests>
  {
    return std::make_unique<Requests>(
      [this](const std::string& uri, Requests::GeometryCompletion complete) {
        if (uri == "throw") {
          throw std::runtime_error("loader exception");
        }
        if (geometry_cache.contains(uri)) {
          complete(geometry_cache.at(uri), {});
        } else {
          geometry_loads.push_back(std::move(complete));
        }
      },
      [this](const std::string& uri, Requests::MaterialCompletion complete) {
        if (uri == "throw") {
          throw std::runtime_error("loader exception");
        }
        if (material_cache.contains(uri)) {
          complete(material_cache.at(uri), {});
        } else {
          material_loads.push_back(std::move(complete));
        }
      },
      [this](const std::string& message) { diagnostics.push_back(message); },
      [this](
        const std::string& uri, bool) { return !unavailable.contains(uri); },
      [this](const std::string& virtual_path,
        Requests::TextureCompletion complete) {
        texture_paths.push_back(virtual_path);
        texture_loads.push_back(std::move(complete));
      });
  }

  void Execute(EditorCommand& command)
  {
    CommandContext context { .Scene = oxygen::observer_ptr(scene.get()),
      .AssetRequests = oxygen::observer_ptr(requests.get()) };
    command.Execute(context);
  }
  void Geometry(const std::string& uri)
  {
    geometry_uris.insert_or_assign(node.GetHandle(), uri);
    SetGeometryCommand command(node.GetHandle(), uri);
    Execute(command);
  }
  auto Target(std::size_t surface, std::string geometry_uri = {}) const
    -> OverrideTarget
  {
    if (geometry_uri.empty()) {
      const auto found = geometry_uris.find(node.GetHandle());
      geometry_uri = found == geometry_uris.end() ? "initial" : found->second;
    }
    auto geometry = a;
    if (const auto found = geometry_cache.find(geometry_uri);
      found != geometry_cache.end()) {
      geometry = found->second;
    }
    const auto& inventory = geometry->Surfaces();
    return OverrideTarget { .geometry_uri = std::move(geometry_uri),
      .surface_id = surface < inventory.surfaces.size()
        ? inventory.surfaces[surface].surface_id
        : TestSurfaceId(surface),
      .layout_revision = inventory.layout_revision };
  }
  //! The geometry component itself: a nil surface and no layout revision.
  auto ComponentTarget(std::string geometry_uri = {}) const -> OverrideTarget
  {
    auto target = Target(0, std::move(geometry_uri));
    target.surface_id = {};
    target.layout_revision = {};
    return target;
  }
  void Material(const std::optional<std::string>& uri, std::size_t surface = 0)
  {
    SetOverridesCommand command(node.GetHandle(), Target(surface),
      MaterialEntries(uri), OverrideIntent::kObservedEdit);
    Execute(command);
  }
  void Detach()
  {
    DetachGeometryCommand command(node.GetHandle());
    Execute(command);
  }
  void Drain() { requests->Drain(*scene); }
  auto Geometry() const -> Requests::Geometry
  {
    return node.GetRenderable().GetGeometry();
  }
  auto CurrentMaterial(const SubmeshIndex submesh = {}) const
    -> Requests::Material
  {
    return node.GetRenderable().ResolveSubmeshMaterial(LodIndex {}, submesh);
  }
  void Attach()
  {
    geometry_cache["initial"] = a;
    Geometry("initial");
    Drain();
  }
};

void GeometryOrdering(bool reverse)
{
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

void MaterialOrdering(bool reverse)
{
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

void CachedReplacement()
{
  Fixture f;
  f.Geometry("A");
  f.geometry_cache["B"] = f.b;
  f.Geometry("B");
  f.Drain();
  f.geometry_loads[0](f.a, {});
  f.Drain();
  Require(f.Geometry() == f.b, "cached replacement overwritten");
  f.Geometry("A");
  f.Geometry(std::string(kCube));
  f.Drain();
  auto cube = f.Geometry();
  Require(cube == f.cube, "cached engine geometry missing");
  f.geometry_loads[1](f.a, {});
  f.Drain();
  Require(f.Geometry() == cube, "cached engine geometry overwritten");
  f.Geometry("A");
  f.Geometry(std::string(kCube));
  f.geometry_loads[2](f.a, {});
  f.Drain();
  Require(f.Geometry() == cube, "cached engine geometry replaced again");
}

void CachedMaterialAndClear()
{
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
  f.Material(std::nullopt);
  f.Drain();
  f.material_loads[1](f.red, {});
  f.Drain();
  Require(f.CurrentMaterial() == oxygen::data::MaterialAsset::CreateDefault(),
    "clear reversed by late callback");
}

void DetachAndReattach()
{
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
  Require(
    f.CurrentMaterial() != f.red, "old component material revived on reattach");
}

void UndoRedo()
{
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
  f.Material(std::nullopt); // Undo the override.
  f.material_loads[0](f.red, {});
  f.Drain();
  Require(f.CurrentMaterial() != f.red, "undo material was reversed");
  f.Material("red");
  f.material_loads[1](f.red, {});
  f.Drain();
  Require(f.CurrentMaterial() == f.red, "redo material did not apply");
}

void DestroyedTarget()
{
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

void DestroyedDescendant()
{
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

void FailedIntentSupersedesOlderSuccess()
{
  Fixture f;
  f.Attach();
  f.Geometry("A");
  f.Geometry("throw");
  f.geometry_loads[0](f.b, {});
  f.Drain();
  Require(f.Geometry() == f.a, "failed geometry intent revived older load");
  f.Material("red");
  f.Material("throw");
  f.material_loads[0](f.red, {});
  f.Drain();
  Require(
    f.CurrentMaterial() != f.red, "failed material intent revived older load");
  Require(f.diagnostics.size() == 2, "failed current requests not diagnosed");
}

void SceneSessionLifetime()
{
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

void IndependentTargetsAndSurfaces()
{
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
  Require(f.CurrentMaterial(SubmeshIndex { 0U }) == f.red
      && f.CurrentMaterial(SubmeshIndex { 1U }) == f.blue,
    "unrelated surface superseded");
}

void MaterialBeforeGeometry()
{
  Fixture f;
  f.Geometry("A");
  f.Material("red", 1);
  f.material_loads[0](f.red, {});
  f.Drain();
  Require(!f.Geometry() && f.diagnostics.empty(), "material applied too early");
  f.geometry_loads[0](f.a, {});
  f.Drain();
  Require(f.CurrentMaterial(SubmeshIndex { 1U }) == f.red,
    "material lost before geometry ready");
  f.Geometry("B");
  f.geometry_loads[1](f.b, {});
  f.Drain();
  Require(f.CurrentMaterial(SubmeshIndex { 1U }) != f.red,
    "unrelated geometry inherited an old override");
}

void RemovedSurfaceDoesNotReviveOverride()
{
  Fixture f;
  f.Attach();
  f.material_cache["red"] = f.red;
  f.Material("red", 1);
  f.Drain();
  Require(f.CurrentMaterial(SubmeshIndex { 1U }) == f.red,
    "initial surface override missing");
  f.Geometry(std::string(kCube));
  f.Drain();
  f.Geometry("B");
  f.geometry_loads[0](f.b, {});
  f.Drain();
  Require(f.CurrentMaterial(SubmeshIndex { 1U }) != f.red,
    "override from a removed surface resurfaced when the index returned");
}

void ClearBeforeGeometry()
{
  Fixture f;
  f.Geometry("A");
  f.Material("red");
  f.Material(std::nullopt);
  f.material_loads[0](f.red, {});
  f.Drain();
  f.geometry_loads[0](f.a, {});
  f.Drain();
  Require(f.CurrentMaterial() != f.red, "clear before attachment reversed");
  Require(f.diagnostics.empty(), "clear of absent component reported failure");
}

void ClearWhileReplacementLoads()
{
  Fixture f;
  f.Attach();
  f.material_cache["red"] = f.red;
  f.Material("red");
  f.Drain();
  Require(f.CurrentMaterial() == f.red, "initial override missing");
  f.Geometry("B");
  f.Material(std::nullopt);
  f.Drain();
  Require(f.Geometry() == f.a && f.CurrentMaterial() == f.red,
    "clear for pending replacement mutated a different geometry target");
  f.geometry_loads[0](f.b, {});
  f.Drain();
  Require(f.CurrentMaterial() != f.red, "replacement geometry reversed clear");
}

void FailureDiagnostics()
{
  Fixture f;
  f.Attach();
  f.Geometry("old");
  f.Geometry("current");
  f.geometry_loads[0]({}, "obsolete failure");
  f.geometry_loads[1]({}, "current failure");
  f.Drain();
  Require(f.Geometry() == f.a, "failed load removed previous geometry");
  Require(f.diagnostics.size() == 1
      && f.diagnostics[0].find("current failure") != std::string::npos
      && f.diagnostics[0].find("current") != std::string::npos,
    "current geometry failure diagnostic missing or stale failure reported");
  f.Material("old");
  f.Material("current");
  f.material_loads[0]({}, "obsolete failure");
  f.material_loads[1]({}, "material failure");
  f.Drain();
  Require(f.diagnostics.size() == 2
      && f.diagnostics[1].find("material failure") != std::string::npos,
    "current material failure diagnostic missing");
  f.Geometry("throw");
  f.Material("throw");
  f.Drain();
  Require(
    f.diagnostics.size() == 4, "loader exceptions escaped diagnostic path");
}

// Confirms failure delivery uses the native generation after mutation-phase
// validation, discards superseded callbacks, and terminates failed requests.
void CorrelatedFailureDelivery()
{
  Fixture f;
  f.Attach();
  std::vector<uint64_t> generations;
  auto on_failure = [&generations](uint64_t generation, const std::string&) {
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

  SetOverridesCommand material(f.node.GetHandle(), f.Target(0, "current"),
    MaterialEntries("missing"), OverrideIntent::kObservedEdit);
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

void MutationBoundary()
{
  Fixture f;
  f.Geometry("A");
  std::thread worker([&f] { f.geometry_loads[0](f.a, {}); });
  worker.join();
  Require(!f.Geometry(), "callback mutated scene outside mutation drain");
  ThreadSafeQueue<std::unique_ptr<EditorCommand>> commands;
  f.geometry_cache["B"] = f.b;
  commands.Enqueue(
    std::make_unique<SetGeometryCommand>(f.node.GetHandle(), "B"));
  commands.Drain([&f](auto& command) { f.Execute(*command); });
  f.Drain();
  Require(f.Geometry() == f.b, "new queued intent lost to completion");
}

thread_local char last_error[1024] {};

void RefreshRebindsSameIdentityWithoutRecreatingNode()
{
  Fixture f;
  f.Attach();
  f.material_cache["red"] = f.red;
  f.Material("red");
  f.Drain();
  const auto handle = f.node.GetHandle();
  const auto updated = MakeMaterial("/red");
  f.material_cache["red"] = updated;
  f.requests->Refresh(*f.scene);
  Require(f.requests->IsRefreshPending(),
    "refresh completed before queued replacements were applied");
  Require(f.CurrentMaterial() == f.red,
    "refresh changed scene outside mutation drain");
  f.Drain();
  Require(!f.requests->IsRefreshPending(),
    "applied replacements did not complete refresh");
  Require(f.CurrentMaterial() == updated, "same-key material was not rebound");
  Require(f.node.GetHandle() == handle, "refresh replaced the scene node");
}

void RefreshPreservesNewerEditsAndPreviousMaterialOnFailure()
{
  Fixture f;
  f.Attach();
  f.material_cache["red"] = f.red;
  f.Material("red");
  f.Drain();
  f.material_cache.clear();
  f.requests->Refresh(*f.scene);
  f.material_loads[0]({}, "refresh failed");
  f.Drain();
  Require(
    !f.requests->IsRefreshPending() && !f.requests->RefreshError().empty(),
    "failed refresh did not settle with its error");
  Require(f.CurrentMaterial() == f.red,
    "failed refresh removed the visible material");
  f.requests->Refresh(*f.scene);
  f.Material("blue");
  f.material_loads[2](f.blue, {});
  f.material_loads[1](f.red, {});
  f.Drain();
  Require(f.CurrentMaterial() == f.blue, "refresh reversed a newer assignment");
}

void RefreshDoesNotReviveClearedOrRemovedSurfaces()
{
  Fixture f;
  f.Attach();
  f.material_cache["red"] = f.red;
  f.Material("red", 1);
  f.Drain();
  f.Geometry(std::string(kCube));
  f.Drain();
  f.geometry_cache["B"] = f.b;
  f.Geometry("B");
  f.Drain();
  f.requests->Refresh(*f.scene);
  f.Drain();
  Require(f.CurrentMaterial(SubmeshIndex { 1U }) != f.red,
    "refresh resurrected a removed surface override");
  f.Material("red");
  f.Drain();
  f.Material(std::nullopt);
  f.Drain();
  f.requests->Refresh(*f.scene);
  f.Drain();
  Require(f.CurrentMaterial() != f.red, "refresh reversed None");
}

void RefreshLeavesUncookedIntentForItsOwnPublication()
{
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
  Require(f.material_loads.size() == 1,
    "unrelated publication retried an uncooked intent");
  Require(f.requests->RefreshError().empty(),
    "unrelated uncooked intent failed publication");
  Require(f.CurrentMaterial() == f.red,
    "unrelated publication removed last visible material");
  f.unavailable.clear();
  f.material_cache["uncooked"] = f.blue;
  f.requests->Refresh(*f.scene);
  f.Drain();
  Require(
    f.CurrentMaterial() == f.blue, "newly published intent was not resolved");
}

void SuspendedLoadsRetainLatestIntentAndClear()
{
  Fixture f;
  f.Attach();
  f.material_cache["red"] = f.red;
  f.Material("red");
  f.Drain();
  f.requests->SuspendLoads();
  f.Material("blue");
  f.Material(std::nullopt);
  f.Drain();
  Require(f.material_loads.empty(), "suspended authoring started file reads");
  Require(f.CurrentMaterial() == f.red,
    "suspension changed the last applied material");
  f.requests->ResumeLoads(*f.scene);
  f.Drain();
  Require(f.CurrentMaterial() != f.red, "resume lost the newer None intent");
  Require(
    !f.requests->IsRefreshPending(), "clear remained pending after resume");
}

void ResumeStartsHeldRequestsTheContentCannotResolve()
{
  Fixture f;
  std::vector<std::string> failures;
  f.requests->SuspendLoads();
  f.unavailable.insert("missing");
  SetGeometryCommand geometry(f.node.GetHandle(), "missing");
  geometry.SetFailureCallback([&](uint64_t, const std::string& error) {
    failures.push_back(error);
  });
  f.Execute(geometry);
  f.Drain();
  Require(f.geometry_loads.empty(), "a held request started while paused");
  f.requests->ResumeLoads(*f.scene);
  Require(f.geometry_loads.size() == 1,
    "resume left an unresolvable request pending");
  Require(!f.requests->IsRefreshPending(),
    "a held request was counted as a content refresh");
  f.geometry_loads[0]({}, "asset path could not be resolved");
  f.Drain();
  Require(failures.size() == 1, "the held request did not report its failure");
  Require(f.requests->RefreshError().empty(),
    "a held request failed the content refresh");
}

void PublicationRefreshWorksWhileAuthoringLoadsStaySuspended()
{
  Fixture f;
  f.Attach();
  f.requests->SuspendLoads();
  f.Material("blue");
  f.material_cache["blue"] = f.blue;
  f.requests->Refresh(*f.scene);
  f.Drain();
  Require(f.CurrentMaterial() == f.blue,
    "publication did not bind staged material while paused");
  f.Material("red");
  f.Drain();
  Require(f.material_loads.empty(),
    "publication accidentally resumed ordinary loading");
  f.material_cache["red"] = f.red;
  f.requests->ResumeLoads(*f.scene);
  f.Drain();
  Require(f.CurrentMaterial() == f.red,
    "resume did not apply the newer authoring intent");
}

void SuccessfulRefreshAcknowledgesCurrentGeneration()
{
  Fixture f;
  f.Attach();
  uint64_t failed = 0;
  std::vector<uint64_t> applied;
  SetOverridesCommand material(f.node.GetHandle(), f.Target(0),
    MaterialEntries("retry"), OverrideIntent::kObservedEdit);
  material.SetFailureCallback(
    [&](uint64_t generation, const std::string&) { failed = generation; });
  material.SetSuccessCallback([&](uint64_t generation) {
    Require(
      f.CurrentMaterial() == f.red, "success preceded material application");
    applied.push_back(generation);
  });
  f.Execute(material);
  f.material_loads.back()({}, "first attempt failed");
  f.Drain();
  Require(
    failed > 0 && applied.empty(), "failed load was acknowledged as applied");
  f.material_cache["retry"] = f.red;
  f.requests->Refresh(*f.scene);
  Require(applied.empty(), "refresh escaped the mutation boundary");
  f.Drain();
  Require(applied.size() == 1 && applied[0] > failed,
    "retry did not report its newly applied native generation");
  f.material_loads.front()({}, "late failure");
  f.Drain();
  Require(applied.size() == 1,
    "obsolete completion produced another acknowledgement");
}

void SuccessWaitsForCurrentGeometryAndMaterialApplication()
{
  Fixture f;
  int geometry_count = 0;
  int material_count = 0;
  SetGeometryCommand geometry(f.node.GetHandle(), "geometry");
  geometry.SetSuccessCallback([&](uint64_t) {
    Require(f.Geometry() == f.a, "geometry success preceded application");
    ++geometry_count;
  });
  SetOverridesCommand material(f.node.GetHandle(), f.Target(0, "geometry"),
    MaterialEntries("material"), OverrideIntent::kObservedEdit);
  material.SetSuccessCallback([&](uint64_t) {
    Require(
      f.CurrentMaterial() == f.red, "material success preceded application");
    ++material_count;
  });
  f.Execute(geometry);
  f.Execute(material);
  f.material_loads.back()(f.red, {});
  f.Drain();
  Require(
    material_count == 0, "material waiting for geometry was acknowledged");
  f.geometry_loads.back()(f.a, {});
  f.Drain();
  Require(geometry_count == 1 && material_count == 1,
    "current applications were not acknowledged");
  f.geometry_loads.back()(f.b, {});
  f.Drain();
  Require(
    geometry_count == 1, "duplicate geometry completion was acknowledged");
}

void MaterialRecoversAfterGeometryFailure(bool material_first)
{
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
    Require(
      f.Geometry() == f.a, "geometry recovery acknowledged before application");
    geometry_successes.push_back(generation);
  });
  SetOverridesCommand material(f.node.GetHandle(),
    f.Target(0, "retry-geometry"), MaterialEntries("red"),
    OverrideIntent::kObservedEdit);
  material.SetFailureCallback(
    [&](uint64_t, const std::string&) { ++material_failures; });
  material.SetSuccessCallback([&](uint64_t generation) {
    Require(f.CurrentMaterial() == f.red,
      "material recovery acknowledged before application");
    material_successes.push_back(generation);
  });
  f.Execute(geometry);
  f.Execute(material);
  const auto original_geometry = f.geometry_loads.back();
  const auto original_material = f.material_loads.back();
  if (material_first) {
    original_material(f.red, {});
    f.Drain();
    Require(material_successes.empty(),
      "material applied while geometry was loading");
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
  Require(
    material_failures == 0, "unavailable geometry rejected an unverified surface");

  // A failed publication refresh settles without discarding authored intent.
  f.requests->Refresh(*f.scene);
  Require(
    f.requests->IsRefreshPending(), "refresh finished before its completions");
  f.material_loads.back()(f.red, {});
  f.geometry_loads.back()({}, "geometry still unavailable");
  f.Drain();
  Require(
    !f.requests->IsRefreshPending() && !f.requests->RefreshError().empty(),
    "failed geometry refresh did not settle with its diagnostic");
  Require(geometry_failures.size() == 2 && material_successes.empty(),
    "failed refresh claimed material success");

  f.geometry_cache["retry-geometry"] = f.a;
  f.material_cache["red"] = f.red;
  f.requests->Refresh(*f.scene);
  Require(
    material_successes.empty(), "cached refresh escaped the mutation boundary");
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

void FailedReplacementDoesNotValidateSurfacesAgainstPreviousGeometry()
{
  Fixture f;
  f.Geometry(std::string(kCube));
  f.Drain();
  const auto previous_geometry = f.Geometry();
  Require(
    previous_geometry && previous_geometry->MeshAt(oxygen::data::LodIndex {})->SubMeshes().size() == 1,
    "replacement scenario needs a one-surface visible geometry");
  f.material_cache["red"] = f.red;
  f.Material("red");
  f.Drain();
  int failures = 0;
  int successes = 0;
  f.Geometry("replacement");
  SetOverridesCommand material(f.node.GetHandle(), f.Target(1),
    MaterialEntries("blue"), OverrideIntent::kObservedEdit);
  material.SetFailureCallback(
    [&](uint64_t, const std::string&) { ++failures; });
  material.SetSuccessCallback([&](uint64_t) {
    Require(f.CurrentMaterial(SubmeshIndex { 1U }) == f.blue,
      "replacement material acknowledged too early");
    ++successes;
  });
  f.Execute(material);
  f.material_loads.back()(f.blue, {});
  f.geometry_loads.back()({}, "replacement load failed");
  f.Drain();
  Require(f.Geometry() == previous_geometry && f.CurrentMaterial() == f.red,
    "failed replacement changed the visible content");
  Require(failures == 0 && successes == 0,
    "old geometry decided the failed replacement's surface validity");
  f.geometry_cache["replacement"] = f.b;
  f.material_cache["blue"] = f.blue;
  f.requests->Refresh(*f.scene);
  f.Drain();
  Require(f.Geometry() == f.b
      && f.CurrentMaterial(SubmeshIndex { 1U }) == f.blue,
    "replacement publication did not apply the pending surface");
  Require(f.CurrentMaterial() != f.red && successes == 1 && failures == 0,
    "replacement recovery reused an unrelated geometry assignment");
}

void LatestMaterialIntentWinsAfterGeometryFailure()
{
  for (const auto clear : { false, true }) {
    Fixture f;
    f.Geometry("retry-geometry");
    int obsolete_successes = 0;
    SetOverridesCommand original(f.node.GetHandle(), f.Target(0),
      MaterialEntries("red"), OverrideIntent::kObservedEdit);
    original.SetSuccessCallback([&](uint64_t) { ++obsolete_successes; });
    f.Execute(original);
    const auto original_material = f.material_loads.back();
    original_material(f.red, {});
    f.geometry_loads.back()({}, "temporary failure");
    f.Drain();
    int latest_successes = 0;
    SetOverridesCommand latest(f.node.GetHandle(), f.Target(0),
      MaterialEntries(clear ? std::nullopt : std::optional<std::string>("blue")),
      OverrideIntent::kObservedEdit);
    latest.SetSuccessCallback([&](uint64_t) { ++latest_successes; });
    f.Execute(latest);
    if (!clear) {
      f.material_loads.back()(f.blue, {});
    }
    f.Drain();
    Require(latest_successes == 0,
      "material intent was acknowledged before target geometry was available");
    original_material(f.red, {});
    f.geometry_cache["retry-geometry"] = f.a;
    f.material_cache["red"] = f.red;
    f.material_cache["blue"] = f.blue;
    f.requests->Refresh(*f.scene);
    f.Drain();
    Require(f.Geometry() == f.a && obsolete_successes == 0,
      "recovery acknowledged superseded material intent");
    Require(f.CurrentMaterial()
        == (clear ? oxygen::data::MaterialAsset::CreateDefault() : f.blue),
      "recovery revived an older material over the latest assignment or None");
    Require(latest_successes == 1,
      "latest intent did not receive exactly one acknowledgement");
  }
}

void ConfirmedInvalidSurfaceRequiresNewExplicitAssignment()
{
  Fixture f;
  f.Geometry(std::string(kCube));
  f.Drain();
  f.material_cache["red"] = f.red;
  uint64_t failed_generation = 0;
  uint64_t applied_generation = 0;
  int failures = 0;
  int successes = 0;
  auto assign = [&] {
    SetOverridesCommand material(f.node.GetHandle(), f.Target(1),
      MaterialEntries("red"), OverrideIntent::kObservedEdit);
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
  Require(
    failures == 1 && successes == 0, "known invalid surface was not rejected");
  f.requests->Refresh(*f.scene);
  f.Drain();
  f.geometry_cache["replacement"] = f.b;
  f.Geometry("replacement");
  f.Drain();
  f.requests->Refresh(*f.scene);
  f.Drain();
  Require(f.CurrentMaterial(SubmeshIndex { 1U }) != f.red && failures == 1
      && successes == 0,
    "publication resurrected a terminally rejected surface");
  assign();
  f.Drain();
  Require(f.CurrentMaterial(SubmeshIndex { 1U }) == f.red && successes == 1
      && failures == 1,
    "explicit fresh assignment did not replace the rejected intent");
  Require(applied_generation > failed_generation,
    "explicit assignment reused the rejected request generation");
}

void StaleMaterialRevisionIsRejectedButAcceptedRefreshSurvives()
{
  Fixture f;
  f.Attach();
  const auto old_target = f.Target(1U);
  f.material_cache["red"] = f.red;
  f.Material("red", 1U);
  f.Drain();
  const auto replacement = MakeGeometry("/a", MakeMaterial("/new-default"));
  Require(replacement->Surfaces().layout_revision
      != f.a->Surfaces().layout_revision,
    "refresh regression requires a changed native inventory revision");
  f.geometry_cache["initial"] = replacement;
  f.requests->Refresh(*f.scene);
  f.Drain();
  Require(f.Geometry() == replacement
      && f.CurrentMaterial(SubmeshIndex { 1U }) == f.red,
    "compatible refresh discarded an accepted stable surface assignment");
  int failures = 0;
  SetOverridesCommand stale(f.node.GetHandle(), old_target,
    MaterialEntries("blue"), OverrideIntent::kObservedEdit);
  stale.SetFailureCallback([&](uint64_t, const std::string&) { ++failures; });
  f.Execute(stale);
  f.material_loads.back()(f.blue, {});
  f.Drain();
  Require(failures == 1 && f.CurrentMaterial(SubmeshIndex { 1U }) == f.red,
    "new edit with stale inventory revision changed the material");
}

void MaterialTargetCannotCrossGeometryIdentity(
  const OverrideIntent intent)
{
  Fixture f;
  f.Attach();
  const auto old_target = f.Target(0U);
  f.geometry_cache["B"] = f.b;
  f.Geometry("B");
  f.Drain();
  int failures = 0;
  int successes = 0;
  SetOverridesCommand valid(
    f.node.GetHandle(), f.Target(0U), MaterialEntries("red"), intent);
  valid.SetSuccessCallback([&](uint64_t) { ++successes; });
  f.Execute(valid);
  const auto valid_completion = f.material_loads.back();
  const auto pending_loads = f.material_loads.size();
  SetOverridesCommand stale(
    f.node.GetHandle(), old_target, MaterialEntries("blue"), intent);
  stale.SetFailureCallback([&](uint64_t, const std::string&) { ++failures; });
  f.Execute(stale);
  Require(f.material_loads.size() == pending_loads,
    "foreign geometry request started a material load");
  valid_completion(f.red, {});
  f.Drain();
  Require(failures == 1 && successes == 1 && f.CurrentMaterial() == f.red,
    "foreign geometry request discarded the valid pending surface assignment");
}

void RetainedMaterialIdentitySurvivesInventoryRevisionChange()
{
  Fixture f;
  f.Attach();
  const auto original = f.Target(1U);
  const auto replacement = MakeGeometry("/a", MakeMaterial("/new-default"));
  f.geometry_cache["initial"] = replacement;
  f.requests->Refresh(*f.scene);
  f.Drain();
  Require(f.Geometry() == replacement
      && original.layout_revision
        != replacement->Surfaces().layout_revision,
    "retained assignment test requires a changed inventory");
  f.material_cache["blue"] = f.blue;
  SetOverridesCommand retained(f.node.GetHandle(), original,
    MaterialEntries("blue"), OverrideIntent::kRetainedOverride);
  f.Execute(retained);
  f.Drain();
  Require(f.CurrentMaterial(SubmeshIndex { 1U }) == f.blue,
    "retained stable surface was rejected after compatible inventory change");
  SetOverridesCommand clear(f.node.GetHandle(), original,
    MaterialEntries(std::nullopt), OverrideIntent::kRetainedOverride);
  f.Execute(clear);
  f.Drain();
  Require(f.CurrentMaterial(SubmeshIndex { 1U }) != f.blue,
    "retained surface clear did not restore its current geometry default");
}

void RetainedMaterialMissingIdentityRemainsUnresolved()
{
  Fixture f;
  f.Attach();
  auto missing = f.Target(0U);
  missing.surface_id = TestSurfaceId(9U);
  f.material_cache["blue"] = f.blue;
  int failures = 0;
  SetOverridesCommand retained(f.node.GetHandle(), missing,
    MaterialEntries("blue"), OverrideIntent::kRetainedOverride);
  retained.SetFailureCallback(
    [&](uint64_t, const std::string&) { ++failures; });
  f.Execute(retained);
  f.Drain();
  Require(failures == 1 && f.CurrentMaterial(SubmeshIndex { 0U }) != f.blue
      && f.CurrentMaterial(SubmeshIndex { 1U }) != f.blue,
    "missing retained identity was rebound to another surface");
}

void MaterialRequestAppliesEveryDeclaredLodBinding()
{
  Fixture f;
  oxygen::data::pak::geometry::GeometryAssetDesc desc {};
  desc.lod_count = 2U;
  std::vector<std::shared_ptr<oxygen::data::Mesh>> meshes { f.a->MeshAt(oxygen::data::LodIndex {}),
    f.a->MeshAt(oxygen::data::LodIndex {}) };
  f.a = std::make_shared<oxygen::data::GeometryAsset>(
    f.a->GetAssetKey(), desc, std::move(meshes));
  f.Attach();
  f.material_cache["red"] = f.red;
  f.Material("red", 1U);
  f.Drain();
  const auto renderable = f.node.GetRenderable();
  const auto submesh = SubmeshIndex { 1U };
  Require(renderable.ResolveSubmeshMaterial(LodIndex { 0U }, submesh) == f.red
      && renderable.ResolveSubmeshMaterial(LodIndex { 1U }, submesh) == f.red,
    "native surface transport only applied its first LOD binding");
  f.Material(std::nullopt, 1U);
  f.Drain();
  Require(renderable.ResolveSubmeshMaterial(LodIndex { 0U }, submesh) != f.red
      && renderable.ResolveSubmeshMaterial(LodIndex { 1U }, submesh) != f.red,
    "native surface clear left an LOD binding overridden");
}

auto MakeMaskTexture() -> Requests::Texture
{
  using namespace oxygen::data::pak;
  auto header = render::TexturePayloadHeader {};
  header.subresource_count = 1U;
  header.layouts_offset_bytes = sizeof(header);
  header.data_offset_bytes = sizeof(header) + sizeof(render::SubresourceLayout);
  header.total_payload_size = header.data_offset_bytes + 4U;
  const auto layout = render::SubresourceLayout {
    .offset_bytes = 0U, .row_pitch_bytes = 4U, .size_bytes = 4U
  };
  auto bytes = std::vector<uint8_t>(header.total_payload_size, 0U);
  std::memcpy(bytes.data(), &header, sizeof(header));
  std::memcpy(
    bytes.data() + header.layouts_offset_bytes, &layout, sizeof(layout));
  auto descriptor = core::TextureResourceDesc {};
  descriptor.size_bytes = header.total_payload_size;
  descriptor.texture_type
    = static_cast<uint8_t>(oxygen::TextureType::kTexture2D);
  descriptor.width = 1U;
  descriptor.height = 1U;
  descriptor.depth = 1U;
  descriptor.array_layers = 1U;
  descriptor.mip_levels = 1U;
  descriptor.format = static_cast<uint8_t>(oxygen::Format::kRGBA8UNorm);
  descriptor.alignment = 256U;
  return std::make_shared<oxygen::data::TextureResource>(
    descriptor, std::move(bytes));
}

auto MaskSources(std::string virtual_path) -> EnvironmentTextureSources
{
  auto sources = EnvironmentTextureSources {};
  sources.front() = { .virtual_path = std::move(virtual_path) };
  return sources;
}

auto TexturePath(const std::string_view name) -> std::string
{
  return "/Content/Textures/" + std::string(name);
}

void ExposureMaskRequestsAreAtomicAndLatestWins()
{
  Fixture f;
  const auto texture = MakeMaskTexture();
  auto failures = 0;
  auto successes = 0;
  const auto request = [&](float ev, const char* descriptor) {
    auto post = PostProcessParams {};
    post.manual_exposure_ev = ev;
    if (descriptor) {
      post.auto_exposure_metering_mask = TexturePath(descriptor);
    }
    auto command = SetEnvironmentCommand(SkyAtmosphereParams {}, post);
    command.SetFailureCallback(
      [&](uint64_t, const std::string&) { ++failures; });
    command.SetSuccessCallback([&](uint64_t) { ++successes; });
    f.Execute(command);
  };
  const auto exposure = [&] {
    return f.scene->GetEnvironment()
      ->TryGetSystem<oxygen::scene::environment::PostProcessVolume>()
      ->GetExposureSettings();
  };
  request(4.0F, nullptr);
  request(6.0F, "A.otex");
  request(8.0F, "B.otex");
  Require(
    exposure().manual_ev == 4.0F, "pending mask partially applied exposure");
  f.texture_loads.at(1)(oxygen::content::ResourceKey { 41U }, texture, {});
  f.Drain();
  Require(exposure().manual_ev == 8.0F && exposure().metering_mask.get() == 41U,
    "latest mask did not apply its complete revision");
  f.texture_loads.at(0)({}, {}, "obsolete failure");
  f.Drain();
  Require(failures == 0 && f.diagnostics.empty(),
    "obsolete mask failure was reported");
  request(10.0F, "C.otex");
  f.texture_loads.at(2)({}, {}, "missing texture");
  f.Drain();
  Require(exposure().manual_ev == 8.0F && exposure().metering_mask.get() == 41U,
    "failed mask replaced the previously accepted revision");
  Require(failures == 1
      && !f.requests
            ->InspectEnvironmentTexture(EnvironmentTextureSlot::kMeteringMask)
            .error.empty(),
    "current mask failure was not observable");
  f.requests->Refresh(*f.scene);
  Require(f.requests->IsRefreshPending(), "mask refresh was not tracked");
  f.texture_loads.at(3)(oxygen::content::ResourceKey { 99U }, texture, {});
  f.Drain();
  Require(
    exposure().manual_ev == 10.0F && exposure().metering_mask.get() == 99U,
    "mask refresh did not retry the retained authored intent");
  Require(!f.requests->IsRefreshPending() && f.requests->RefreshError().empty(),
    "successful mask refresh remained pending or failed");
  request(12.0F, "D.otex");
  request(14.0F, nullptr);
  f.texture_loads.at(4)(oxygen::content::ResourceKey { 101U }, texture, {});
  f.Drain();
  Require(exposure().manual_ev == 14.0F && exposure().metering_mask.get() == 0U,
    "late mask completion undid a clear request");
  Require(successes == 4,
    "accepted environment requests were not acknowledged exactly once");
}

void ExposureMaskRequestsRespectPauseAndSceneLifetime()
{
  Fixture f;
  const auto texture = MakeMaskTexture();
  auto applied = 0;
  f.requests->SuspendLoads();
  f.requests->SetEnvironmentTextures(*f.scene, MaskSources(TexturePath("Mask.otex")),
    [&](auto&, const auto&) { ++applied; });
  Require(f.texture_loads.empty(), "suspended mask request started a load");
  f.requests->ResumeLoads(*f.scene);
  Require(
    f.texture_loads.size() == 1U, "resuming did not start the mask request");
  auto complete = f.texture_loads.front();
  f.requests.reset();
  complete(oxygen::content::ResourceKey { 41U }, texture, {});
  Require(applied == 0, "mask completion escaped its scene session lifetime");
}

// Both sky cubemaps load before the edit applies; a failed one rejects it. An
// unchanged cubemap keeps its accepted texture; a refresh reloads it.
void SkyCubemapsApplyTogetherOrNotAtAll()
{
  Fixture f;
  const auto texture = MakeMaskTexture();
  auto failures = 0;
  const auto request = [&](float intensity,
                         const char* light_descriptor = "Light.otex") {
    auto sky = SkyParams {};
    sky.sky_sphere.enabled = true;
    sky.sky_sphere.intensity = intensity;
    sky.sky_sphere.cubemap.virtual_path = TexturePath("Backdrop.otex");
    sky.sky_light.source = 1;
    sky.sky_light.cubemap.virtual_path = TexturePath(light_descriptor);
    auto command = SetEnvironmentCommand(
      SkyAtmosphereParams {}, PostProcessParams {}, FogParams {}, sky);
    command.SetFailureCallback(
      [&](uint64_t, const std::string&) { ++failures; });
    f.Execute(command);
  };
  const auto sphere = [&] {
    return f.scene->GetEnvironment()
      ->TryGetSystem<oxygen::scene::environment::SkySphere>();
  };
  const auto light = [&] {
    return f.scene->GetEnvironment()
      ->TryGetSystem<oxygen::scene::environment::SkyLight>();
  };
  request(2.0F);
  Require(f.texture_loads.size() == 2U, "sky cubemaps did not both load");
  f.texture_loads.at(0)(oxygen::content::ResourceKey { 51U }, texture, {});
  f.Drain();
  Require(!f.scene->GetEnvironment() || !sphere(),
    "a partially loaded sky applied before its second cubemap");
  f.texture_loads.at(1)(oxygen::content::ResourceKey { 52U }, texture, {});
  f.Drain();
  Require(sphere() && sphere()->GetIntensity() == 2.0F
      && sphere()->GetCubemapResource().get() == 51U && light()
      && light()->GetCubemapResource().get() == 52U,
    "the sky did not apply both cubemaps together");
  request(2.5F);
  f.Drain();
  Require(f.texture_loads.size() == 2U && sphere()->GetIntensity() == 2.5F
      && sphere()->GetCubemapResource().get() == 51U
      && light()->GetCubemapResource().get() == 52U,
    "an edit with unchanged cubemaps reloaded them or did not apply");
  request(3.0F, "OtherLight.otex");
  Require(f.texture_loads.size() == 3U,
    "only the changed sky light cubemap should load");
  f.texture_loads.at(2)({}, {}, "missing cube texture");
  f.Drain();
  Require(failures == 1 && sphere()->GetIntensity() == 2.5F
      && sphere()->GetCubemapResource().get() == 51U
      && light()->GetCubemapResource().get() == 52U,
    "a failed sky cubemap replaced the previously applied sky");
  Require(!f.requests
             ->InspectEnvironmentTexture(
               EnvironmentTextureSlot::kSkyLightCubemap)
             .error.empty(),
    "the failed sky light cubemap was not observable");
  f.requests->Refresh(*f.scene);
  Require(f.texture_loads.size() == 5U,
    "a refresh did not reload both sky cubemaps");
}

// The scene names an applied cubemap by key only, and the asset loader evicts a
// texture, GPU copy included, once nothing holds it. The request holds what it
// applied until another revision replaces it.
void AppliedSkyCubemapStaysResidentUntilReplaced()
{
  Fixture f;
  const auto request = [&](const char* descriptor) {
    auto sky = SkyParams {};
    sky.sky_sphere.enabled = true;
    if (descriptor != nullptr) {
      sky.sky_sphere.cubemap.virtual_path = TexturePath(descriptor);
    }
    auto command = SetEnvironmentCommand(
      SkyAtmosphereParams {}, PostProcessParams {}, FogParams {}, sky);
    f.Execute(command);
  };
  auto applied = std::weak_ptr<const oxygen::data::TextureResource> {};
  request("Backdrop.otex");
  Require(f.texture_loads.size() == 1U, "the sky cubemap did not load");
  {
    auto texture = MakeMaskTexture();
    applied = texture;
    f.texture_loads.at(0)(oxygen::content::ResourceKey { 61U }, texture, {});
  }
  f.Drain();
  Require(!applied.expired(), "the applied sky cubemap was not held");
  request(nullptr);
  f.Drain();
  Require(applied.expired(), "a removed sky cubemap was still held");
}

//! What one override command reported.
struct OverrideOutcome {
  int successes = 0;
  std::vector<std::string> failures;
};

//! Executes one override set and records how its callbacks answered.
auto ExecuteOverrides(Fixture& f, OverrideTarget target,
  std::vector<OverrideEntry> entries,
  const OverrideIntent intent = OverrideIntent::kObservedEdit)
  -> std::shared_ptr<OverrideOutcome>
{
  auto outcome = std::make_shared<OverrideOutcome>();
  SetOverridesCommand command(
    f.node.GetHandle(), std::move(target), std::move(entries), intent);
  command.SetFailureCallback([outcome](uint64_t, const std::string& error) {
    outcome->failures.push_back(error);
  });
  command.SetSuccessCallback([outcome](uint64_t) { ++outcome->successes; });
  f.Execute(command);
  return outcome;
}

void SurfaceFlagOverridesApplyWithoutMaterial()
{
  using oxygen::data::schema::OverrideId;
  Fixture f;
  f.Attach();
  const auto renderable = f.node.GetRenderable();
  const auto first = SubmeshIndex { 0U };
  const auto second = SubmeshIndex { 1U };
  Require(renderable.IsSubmeshVisible(LodIndex {}, first)
      && renderable.SubmeshCastsShadows(LodIndex {}, first),
    "the asset surface should default to visible and shadow casting");
  const auto outcome = ExecuteOverrides(f, f.Target(0),
    { BoolEntry(OverrideId::kGeometrySurfaceVisibility, false),
      BoolEntry(OverrideId::kGeometrySurfaceCastsShadows, false) });
  f.Drain();
  Require(f.material_loads.empty(), "flag overrides started a material load");
  Require(outcome->successes == 1 && outcome->failures.empty(),
    "flag overrides were not acknowledged exactly once");
  Require(!renderable.IsSubmeshVisible(LodIndex {}, first),
    "visibility override did not hide the surface");
  Require(!renderable.SubmeshCastsShadows(LodIndex {}, first),
    "cast shadows override did not apply");
  Require(renderable.IsSubmeshVisible(LodIndex {}, second)
      && renderable.SubmeshCastsShadows(LodIndex {}, second),
    "overrides of one surface affected another");
  Require(f.diagnostics.empty(), "flag overrides reported a failure");
}

void EmptyOverridesClearThePart()
{
  using oxygen::data::schema::OverrideId;
  Fixture f;
  f.Attach();
  const auto renderable = f.node.GetRenderable();
  const auto first = SubmeshIndex { 0U };
  const auto hidden = ExecuteOverrides(f, f.Target(0),
    { BoolEntry(OverrideId::kGeometrySurfaceVisibility, false),
      BoolEntry(OverrideId::kGeometrySurfaceCastsShadows, false) });
  f.Drain();
  Require(!renderable.IsSubmeshVisible(LodIndex {}, first)
      && !renderable.SubmeshCastsShadows(LodIndex {}, first),
    "the overrides to clear were not applied");
  const auto cleared = ExecuteOverrides(f, f.Target(0), {});
  f.Drain();
  Require(cleared->successes == 1 && cleared->failures.empty(),
    "clearing the part was not acknowledged exactly once");
  Require(renderable.IsSubmeshVisible(LodIndex {}, first)
      && renderable.SubmeshCastsShadows(LodIndex {}, first),
    "an empty override set did not restore the asset defaults");
  Require(hidden->successes == 1, "the superseded set was acknowledged again");
}

void ComponentLodPolicySurvivesGeometryReplacement()
{
  using oxygen::data::schema::OverrideId;
  Fixture f;
  f.Attach();
  const auto renderable = f.node.GetRenderable();
  Require(!renderable.HasLodPolicyOverride(),
    "the geometry should start on its default LOD policy");
  const auto component
    = ExecuteOverrides(f, f.ComponentTarget(), { FixedLodPolicyEntry(1U) });
  const auto surface = ExecuteOverrides(f, f.Target(0),
    { BoolEntry(OverrideId::kGeometrySurfaceVisibility, false) });
  f.Drain();
  Require(component->successes == 1 && component->failures.empty()
      && surface->successes == 1 && surface->failures.empty(),
    "component and surface overrides were not both applied");
  Require(renderable.HasLodPolicyOverride(),
    "the component LOD policy override was not applied");
  Require(!renderable.IsSubmeshVisible(LodIndex {}, SubmeshIndex { 0U }),
    "the surface override was not applied");

  // A recook replaces the asset behind the same geometry path; a refresh
  // reloads it and re-requests every part. A path change instead drops the
  // parts, since the editor sends the new geometry's overrides itself.
  f.geometry_cache["initial"] = f.b;
  f.requests->Refresh(*f.scene);
  f.Drain();
  Require(f.Geometry() == f.b, "the replacement geometry was not applied");
  Require(renderable.HasLodPolicyOverride(),
    "the component LOD policy override did not survive the replacement");
  Require(component->failures.empty(),
    "the component overrides were rejected by a geometry replacement");
  Require(surface->failures.size() == 1
      && surface->failures[0].find(
           "surface overrides require repair after geometry replacement")
        != std::string::npos,
    "the surface overrides of the previous geometry were not rejected");
}

void MalformedPartIdentityIsRejected()
{
  Fixture f;
  f.Attach();
  auto component = f.ComponentTarget();
  component.layout_revision = f.Target(0).layout_revision;
  auto surface = f.Target(0);
  surface.layout_revision = {};
  const auto component_outcome
    = ExecuteOverrides(f, std::move(component), { FixedLodPolicyEntry(1U) });
  const auto surface_outcome = ExecuteOverrides(f, std::move(surface),
    { BoolEntry(
      oxygen::data::schema::OverrideId::kGeometrySurfaceVisibility, false) });
  f.Drain();
  for (const auto& outcome : { component_outcome, surface_outcome }) {
    Require(outcome->successes == 0 && outcome->failures.size() == 1
        && outcome->failures[0].find("overrides require geometry identity")
          != std::string::npos,
      "a malformed part identity did not fail with the identities diagnostic");
  }
  Require(!f.node.GetRenderable().HasLodPolicyOverride()
      && f.node.GetRenderable().IsSubmeshVisible(LodIndex {}, SubmeshIndex { 0U }),
    "a malformed part identity changed the renderable");
}

auto RunScenario(int scenario) -> const char*
{
  try {
    switch (scenario) {
    case 45:
      SurfaceFlagOverridesApplyWithoutMaterial();
      break;
    case 46:
      EmptyOverridesClearThePart();
      break;
    case 47:
      ComponentLodPolicySurvivesGeometryReplacement();
      break;
    case 48:
      MalformedPartIdentityIsRejected();
      break;
    case 42:
      SkyCubemapsApplyTogetherOrNotAtAll();
      break;
    case 43:
      AppliedSkyCubemapStaysResidentUntilReplaced();
      break;
    case 44:
      ResumeStartsHeldRequestsTheContentCannotResolve();
      break;
    case 38:
      RetainedMaterialIdentitySurvivesInventoryRevisionChange();
      break;
    case 39:
      MaterialTargetCannotCrossGeometryIdentity(
        OverrideIntent::kRetainedOverride);
      break;
    case 40:
      RetainedMaterialMissingIdentityRemainsUnresolved();
      break;
    case 35:
      StaleMaterialRevisionIsRejectedButAcceptedRefreshSurvives();
      break;
    case 36:
      MaterialTargetCannotCrossGeometryIdentity(
        OverrideIntent::kObservedEdit);
      break;
    case 37:
      MaterialRequestAppliesEveryDeclaredLodBinding();
      break;
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
      FailedReplacementDoesNotValidateSurfacesAgainstPreviousGeometry();
      break;
    case 31:
      LatestMaterialIntentWinsAfterGeometryFailure();
      break;
    case 32:
      ConfirmedInvalidSurfaceRequiresNewExplicitAssignment();
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
      RefreshDoesNotReviveClearedOrRemovedSurfaces();
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
      CachedReplacement();
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
      IndependentTargetsAndSurfaces();
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
      RemovedSurfaceDoesNotReviveOverride();
      break;
    case 19:
      CorrelatedFailureDelivery();
      break;
    default:
      throw std::runtime_error("unknown scenario");
    }
    return nullptr;
  } catch (const std::exception& ex) {
    strncpy_s(last_error, ex.what(), _TRUNCATE);
    return last_error;
  }
}
} // namespace

#pragma managed(pop)

using namespace Microsoft::VisualStudio::TestTools::UnitTesting;

namespace InteropTests {

[TestClass] public ref class AssetRequestTests {
private:
  static void Check(int scenario)
  {
    const auto error = RunScenario(scenario);
    Assert::IsTrue(
      error == nullptr, gcnew System::String(error != nullptr ? error : ""));
  }

public:
  [TestMethod] void SurfaceFlagOverridesApplyWithoutMaterial() { Check(45); }

  [TestMethod] void EmptyOverridesClearThePart() { Check(46); }

  [TestMethod] void ComponentLodPolicySurvivesGeometryReplacement() {
    Check(47);
  }

  [TestMethod] void MalformedPartIdentityIsRejected() { Check(48); }

  [TestMethod] void RetainedOverrideUsesCurrentStableSurface() { Check(38); }

    [TestMethod] void RetainedOverrideCannotCrossGeometry()
  {
    Check(39);
  }

  [TestMethod] void RetainedOverrideCannotGuessMissingSurface() { Check(40); }

    [TestMethod] void StaleNewEditRejectsWhileAcceptedRefreshSurvives()
  {
    Check(35);
  }

  [TestMethod] void SurfaceTargetIncludesGeometryIdentity() { Check(36); }

    [TestMethod] void SurfaceTransportAppliesAndClearsEveryLod()
  {
    Check(37);
  }

  [TestMethod] void
  ExposureMaskRevisionIsAtomicAcrossOrderingFailureRefreshAndClear() {
    Check(33);
  }

    [TestMethod] void ExposureMaskLoadRespectsPauseAndSceneLifetime()
  {
    Check(34);
  }

  [TestMethod] void SkyCubemapsApplyTogetherOrNotAtAll() {
    Check(42);
  }

  [TestMethod] void AppliedSkyCubemapStaysResidentUntilReplaced() {
    Check(43);
  }

  [TestMethod] void ResumeStartsHeldRequestsTheContentCannotResolve() {
    Check(44);
  }

    [TestMethod] void LoadedMaterialRecoversAfterGeometryFailure()
  {
    Check(28);
  }

    [TestMethod] void MaterialLoadedAfterGeometryFailureRecovers()
  {
    Check(29);
  }

  [TestMethod] void FailedReplacementPreservesMaterialForItsOwnSurfaceLayout() {
    Check(30);
  }

    [TestMethod] void LatestMaterialOrClearWinsAfterGeometryRecovery()
  {
    Check(31);
  }

  [TestMethod] void ConfirmedInvalidSurfaceRequiresExplicitReassignment() {
    Check(32);
  }

    [TestMethod] void SuccessfulRefreshAcknowledgesCurrentGeneration()
  {
    Check(26);
  }

  [TestMethod] void SuccessWaitsForCurrentGeometryAndMaterialApplication() {
    Check(27);
  }

    [TestMethod] void SuspendedLoadsRetainLatestIntentAndClear()
  {
    Check(24);
  }

  [TestMethod] void PublicationRefreshWorksWhileAuthoringLoadsStaySuspended() {
    Check(25);
  }

    [TestMethod] void RefreshLeavesUncookedIntentForItsOwnPublication()
  {
    Check(23);
  }

  [TestMethod] void RefreshRebindsSameIdentityWithoutRecreatingNode() {
    Check(20);
  }

    [TestMethod] void RefreshPreservesNewerEditsAndPreviousMaterialOnFailure()
  {
    Check(21);
  }

  [TestMethod] void RefreshDoesNotReviveClearedOrRemovedSurfaces() { Check(22); }

    [TestMethod] void CorrelatedFailuresRespectGenerationAndLifetime()
  {
    Check(19);
  }

  [TestMethod] void GeometryInRequestOrder() { Check(0); }

    [TestMethod] void GeometryInReverseOrder()
  {
    Check(1);
  }

  [TestMethod] void MaterialsInRequestOrder() { Check(2); }

    [TestMethod] void MaterialsInReverseOrder()
  {
    Check(3);
  }

  [TestMethod] void CachedGeometryWins() { Check(4); }

    [TestMethod] void CachedMaterialAndClearWin()
  {
    Check(5);
  }

  [TestMethod] void DetachInvalidatesComponentLifetime() { Check(6); }

    [TestMethod] void UndoRedoSupersedesLoads()
  {
    Check(7);
  }

  [TestMethod] void DestroyedTargetRejectsLoads() { Check(8); }

    [TestMethod] void SceneReplacementAndShutdownRejectLoads()
  {
    Check(9);
  }

  [TestMethod] void IndependentNodesAndSurfaces() { Check(10); }

    [TestMethod] void MaterialWaitsForGeometry()
  {
    Check(11);
  }

  [TestMethod] void ClearBeforeAttachmentWins() { Check(12); }

    [TestMethod] void CurrentFailuresReachDiagnostics()
  {
    Check(13);
  }

  [TestMethod] void CallbacksWaitForMutationPhase() { Check(14); }

    [TestMethod] void DeletedDescendantsRejectLoads()
  {
    Check(15);
  }

  [TestMethod] void FailedIntentRejectsOlderSuccess() { Check(16); }

    [TestMethod] void ClearAppliesWhileReplacementLoads()
  {
    Check(17);
  }

  [TestMethod] void RemovedSurfaceDoesNotRestoreHistoricalOverride() { Check(18); }
};

} // namespace InteropTests
