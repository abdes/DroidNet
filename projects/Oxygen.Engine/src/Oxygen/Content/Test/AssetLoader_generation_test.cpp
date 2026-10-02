//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iterator>
#include <latch>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "AssetLoader_test.h"
#include "Fixtures/LooseCookedTestWriter.h"

#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/ContentMounts.h>
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Content/Loaders/ScriptLoader.h>
#include <Oxygen/Content/OperationCancelledException.h>
#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Data/MaterialSlotInventory.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/SourceOrigin.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Serio/FileLock.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {
namespace {

  using co::testing::TestEventLoop;

  struct GenerationRecipe {
    data::AssetKey material {};
    float emission = 0.0F;
    std::uint8_t payload = 0;
    data::AssetKey scene_material {};
    data::AssetKey script {};
  };

  auto SceneKey() -> data::AssetKey
  {
    return data::AssetKey::FromVirtualPath("/Test/generation.oscene");
  }

  auto MaterialKey() -> data::AssetKey
  {
    return data::AssetKey::FromVirtualPath("/Test/material.omat");
  }

  auto SceneBytes(const data::AssetKey& material) -> std::vector<std::byte>
  {
    using namespace data::pak::world;
    SceneAssetDesc descriptor {};
    descriptor.header.asset_type
      = static_cast<std::uint8_t>(data::AssetType::kScene);
    descriptor.header.version = kSceneAssetVersion;
    descriptor.nodes.offset = sizeof(SceneAssetDesc);
    descriptor.nodes.count = 1U;
    descriptor.nodes.entry_size = sizeof(NodeRecord);
    const auto strings = std::array { '\0', 'r', '\0' };
    descriptor.scene_strings.offset
      = sizeof(SceneAssetDesc) + sizeof(NodeRecord);
    descriptor.scene_strings.size
      = static_cast<data::pak::core::StringTableSizeT>(strings.size());
    std::array<SceneComponentTableDesc, 2> tables {};
    RenderableRecord renderable {};
    MaterialOverrideRecord assignment {};
    if (!material.IsNil()) {
      descriptor.component_table_count = static_cast<uint32_t>(tables.size());
      descriptor.component_table_directory_offset
        = descriptor.scene_strings.offset + strings.size();
      tables.at(0).component_type
        = static_cast<uint32_t>(data::ComponentType::kRenderable);
      tables.at(0).table.offset
        = descriptor.component_table_directory_offset + sizeof(tables);
      tables.at(0).table.count = 1U;
      tables.at(0).table.entry_size = sizeof(renderable);
      tables.at(1).component_type
        = static_cast<uint32_t>(data::ComponentType::kMaterialOverride);
      tables.at(1).table.offset
        = tables.at(0).table.offset + sizeof(renderable);
      tables.at(1).table.count = 1U;
      tables.at(1).table.entry_size = sizeof(assignment);
      renderable.geometry_key
        = data::AssetKey::FromVirtualPath("/Test/bound-cube.ogeo");
      assignment.material_key = material;
      assignment.slot_id
        = data::MaterialSlotId::FromStableIdentity("generation-test-slot");
      const std::array slots { data::MaterialSlot {
        .slot_id = assignment.slot_id,
        .display_name = "fixture",
        .bindings = { { .lod_index = 0,
          .submesh_index = 0,
          .default_material_key = material } } } };
      const auto revision = data::ComputeMaterialSlotLayoutRevision(slots);
      if (!revision) {
        throw std::logic_error("Invalid fixture material slot layout");
      }
      assignment.layout_revision = revision.value();
    }
    NodeRecord node {};
    node.node_id = SceneKey();
    node.parent_index = 0U;
    node.scene_name_offset = 1U;
    SceneEnvironmentBlockHeader environment {};
    environment.byte_size = sizeof(environment);
    std::vector<std::byte> bytes;
    const auto append = [&bytes](const auto& record) -> auto {
      const auto encoded = std::as_bytes(std::span { &record, 1U });
      bytes.insert(bytes.end(), encoded.begin(), encoded.end());
    };
    append(descriptor);
    append(node);
    append(strings);
    if (!material.IsNil()) {
      append(tables);
      append(renderable);
      append(assignment);
    }
    append(environment);
    return bytes;
  }

  auto WriteGeneration(const std::filesystem::path& root,
    const GenerationRecipe recipe, data::SourceKey source = {})
    -> data::SourceKey
  {
    std::filesystem::create_directories(root);
    auto exclusive = serio::FileLock::TryAcquire(
      root / data::loose_cooked::kGenerationLeaseFileName,
      serio::FileLockMode::kExclusive, serio::FileLockOpenMode::kOpenOrCreate);
    if (!exclusive) {
      throw std::system_error(exclusive.error());
    }
    if (source.IsNil()) {
      source = data::SourceKey { Uuid::Generate() };
    }
    LooseCookedTestWriter writer(root);
    writer.SetSourceKey(source);
    const auto scene = SceneBytes(recipe.scene_material);
    std::vector<data::KeyReference> scene_keys;
    if (!recipe.scene_material.IsNil()) {
      const auto geometry_key
        = data::AssetKey::FromVirtualPath("/Test/bound-cube.ogeo");
      scene_keys = {
        {
          .key = geometry_key,
          .kind = data::KeyReferenceKind::kAsset,
          .expected_type = data::AssetType::kGeometry,
        },
        {
          .key = recipe.scene_material,
          .kind = data::KeyReferenceKind::kAsset,
          .expected_type = data::AssetType::kMaterial,
        },
      };
      data::pak::geometry::GeometryAssetDesc geometry {};
      geometry.header.asset_type
        = static_cast<uint8_t>(data::AssetType::kGeometry);
      geometry.header.version = data::pak::geometry::kGeometryAssetVersion;
      geometry.lod_count = 1U;
      data::pak::geometry::MeshDesc mesh {};
      std::ranges::copy(
        std::string_view { "Cube/Mesh" }, std::begin(mesh.name));
      mesh.mesh_type = static_cast<uint8_t>(data::MeshType::kProcedural);
      mesh.info.procedural = {};
      mesh.submesh_count = 1U;
      mesh.mesh_view_count = 1U;
      data::pak::geometry::SubMeshDesc submesh {};
      std::ranges::copy(
        std::string_view { "fixture" }, std::begin(submesh.name));
      submesh.slot_id
        = data::MaterialSlotId::FromStableIdentity("generation-test-slot");
      submesh.material_asset_key = recipe.scene_material;
      submesh.mesh_view_count = 1U;
      constexpr data::pak::geometry::MeshViewDesc view {
        .first_index = 0U,
        .index_count = 36U,
        .first_vertex = 0U,
        .vertex_count = 24U,
      };
      std::vector<std::byte> geometry_bytes;
      const auto append = [&geometry_bytes](const auto& record) -> void {
        const auto bytes = std::as_bytes(std::span(&record, 1U));
        geometry_bytes.insert(geometry_bytes.end(), bytes.begin(), bytes.end());
      };
      append(geometry);
      append(mesh);
      append(submesh);
      append(view);
      writer.WriteAssetDescriptor(geometry_key, data::AssetType::kGeometry,
        "/Test/bound-cube.ogeo", "bound-cube.ogeo", geometry_bytes,
        data::AssetReferences::Create({},
          {
            {
              .key = recipe.scene_material,
              .kind = data::KeyReferenceKind::kAsset,
              .expected_type = data::AssetType::kMaterial,
            },
          })
          .value());
    }
    writer.WriteAssetDescriptor(SceneKey(), data::AssetType::kScene,
      "/Test/generation.oscene", "scene.oscene", scene,
      data::AssetReferences::Create({}, std::move(scene_keys)).value());
    if (!recipe.material.IsNil()) {
      data::pak::render::MaterialAssetDesc material {};
      material.header.asset_type
        = static_cast<std::uint8_t>(data::AssetType::kMaterial);
      material.header.version = data::pak::render::kMaterialAssetVersion;
      material.emissive_factor[0] = recipe.emission;
      writer.WriteAssetDescriptor(recipe.material, data::AssetType::kMaterial,
        "/Test/material.omat", "material.omat",
        std::as_bytes(std::span { &material, 1U }));
    }
    {
      const auto script_key = recipe.script.IsNil()
        ? data::AssetKey::FromVirtualPath("/Test/generation.oscript")
        : recipe.script;
      data::pak::scripting::ScriptAssetDesc descriptor {};
      descriptor.header.asset_type
        = static_cast<uint8_t>(data::AssetType::kScript);
      descriptor.header.version = data::pak::scripting::kScriptAssetVersion;
      descriptor.bytecode_resource_index = data::ResourceReferenceIndex { 0U };
      writer.WriteAssetDescriptor(script_key, data::AssetType::kScript,
        "/Test/reload.oscript", "reload.oscript",
        std::as_bytes(std::span { &descriptor, 1U }),
        data::AssetReferences::Create(
          {
            { .kind = data::ResourceKind::kScript,
              .index = ResourceIndexT { 1U } },
          },
          {})
          .value());
    }
    data::pak::scripting::ScriptResourceDesc script {};
    script.size_bytes = 1U;
    const auto table
      = std::array { data::pak::scripting::ScriptResourceDesc {}, script };
    writer.WriteFile(data::loose_cooked::FileKind::kScriptsTable,
      "scripts.table", std::as_bytes(std::span { table }));
    const auto payload = std::array { recipe.payload };
    writer.WriteFile(data::loose_cooked::FileKind::kScriptsData, "scripts.data",
      std::as_bytes(std::span { payload }));
    static_cast<void>(writer.Finish());
    return source;
  }

  auto CookedScriptBytes(const uint8_t payload) -> std::vector<uint8_t>
  {
    data::pak::scripting::ScriptResourceDesc descriptor {};
    descriptor.data_offset = sizeof(descriptor);
    descriptor.size_bytes = 1;
    std::vector<uint8_t> bytes(sizeof(descriptor) + 1);
    std::memcpy(bytes.data(), &descriptor, sizeof(descriptor));
    bytes.back() = payload;
    return bytes;
  }

  auto ExpectLeased(const std::filesystem::path& root) -> void
  {
    const auto lock = serio::FileLock::TryAcquire(
      root / data::loose_cooked::kGenerationLeaseFileName,
      serio::FileLockMode::kExclusive);
    ASSERT_FALSE(lock);
    EXPECT_EQ(lock.error(), std::errc::device_or_resource_busy);
  }

  auto ExerciseRetainedGeneration(
    TestEventLoop* loop, std::filesystem::path root) -> co::Co<>
  {
    const auto old_root = root / "old";
    const auto new_root = root / "new";
    const auto unrelated_root = root / "unrelated";
    const auto material_key = MaterialKey();
    const auto unrelated_key
      = data::AssetKey::FromVirtualPath("/Other/material.omat");
    const auto old_source = WriteGeneration(
      old_root, { .material = material_key, .emission = 1.0F, .payload = 1U });
    const auto new_source = WriteGeneration(
      new_root, { .material = material_key, .emission = 2.0F, .payload = 2U });
    static_cast<void>(WriteGeneration(unrelated_root,
      { .material = unrelated_key, .emission = 3.0F, .payload = 3U }));
    co::ThreadPool pool(*loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    AssetLoader loader(engine::internal::EngineTagFactory::Get(), config);
    loader.RegisterLoader(loaders::LoadSceneAsset);
    loader.RegisterLoader(loaders::LoadMaterialAsset);
    loader.RegisterLoader(loaders::LoadScriptResource);
    loader.RegisterLoader(loaders::LoadScriptAsset);
    OXCO_WITH_NURSERY(nursery)
    {
      co_await nursery.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      loader.AddLooseCookedRoot(unrelated_root);
      const auto unrelated
        = co_await loader.LoadAssetAsync<data::MaterialAsset>(unrelated_key);
      EXPECT_EQ(loader.MountLooseCookedGeneration(old_root), old_source);
      auto old_scene
        = co_await loader.LoadAssetAsync<data::SceneAsset>(SceneKey());
      if (!old_scene || !unrelated) {
        ADD_FAILURE() << "Generation fixture failed to decode";
        loader.Stop();
        co_return co::kJoin;
      }
      EXPECT_EQ(
        loader.MountLooseCookedGeneration(new_root, old_source), new_source);
      EXPECT_EQ(loader.GetMaterialAsset(unrelated_key), unrelated);
      auto new_scene
        = co_await loader.LoadAssetAsync<data::SceneAsset>(SceneKey());
      auto new_material
        = co_await loader.LoadAssetAsync<data::MaterialAsset>(material_key);
      auto old_material = co_await loader.LoadAssetAsync<data::MaterialAsset>(
        material_key, old_scene->GetSourceKey());
      if (!new_scene || !new_material || !old_material) {
        ADD_FAILURE() << "Both generations must remain readable";
        loader.Stop();
        co_return co::kJoin;
      }
      EXPECT_EQ(old_material->GetSourceKey(), old_source);
      EXPECT_EQ(new_material->GetSourceKey(), new_source);
      EXPECT_EQ(old_scene->GetSourceKey(), old_source);
      EXPECT_NE(old_scene, new_scene);
      EXPECT_FLOAT_EQ(old_material->GetEmissiveFactor().at(0), 1.0F);
      EXPECT_FLOAT_EQ(new_material->GetEmissiveFactor().at(0), 2.0F);
      auto old_script = co_await loader.LoadAssetAsync<data::ScriptAsset>(
        data::AssetKey::FromVirtualPath("/Test/generation.oscript"),
        old_scene->GetSourceKey());
      if (!old_script) {
        ADD_FAILURE() << "Generation script must load";
        loader.Stop();
        co_return co::kJoin;
      }
      const auto old_key = loader.MakeScriptResourceKeyForAsset(
        *old_script, data::ResourceReferenceIndex { 0U });
      auto new_script = co_await loader.LoadAssetAsync<data::ScriptAsset>(
        data::AssetKey::FromVirtualPath("/Test/generation.oscript"),
        new_scene->GetSourceKey());
      if (!new_script) {
        ADD_FAILURE() << "Replacement script must load";
        loader.Stop();
        co_return co::kJoin;
      }
      const auto new_key = loader.MakeScriptResourceKeyForAsset(
        *new_script, data::ResourceReferenceIndex { 0U });
      old_script.reset();
      new_script.reset();
      if (!old_key || !new_key) {
        ADD_FAILURE() << "Both script tables must remain addressable";
        loader.Stop();
        co_return co::kJoin;
      }
      EXPECT_NE(old_key, new_key);
      auto old_resource = co_await loader.LoadScriptResourceAsync(*old_key);
      const auto new_resource
        = co_await loader.LoadScriptResourceAsync(*new_key);
      if (!old_resource || !new_resource) {
        ADD_FAILURE() << "Both generation payloads must load";
        loader.Stop();
        co_return co::kJoin;
      }
      EXPECT_THAT(old_resource->GetData(), ::testing::ElementsAre(1U));
      EXPECT_THAT(new_resource->GetData(), ::testing::ElementsAre(2U));
      old_resource.reset();
      loader.TrimCache();
      EXPECT_EQ(loader.GetScriptResource(*old_key), nullptr);
      auto reloaded = co_await loader.LoadScriptResourceAsync(*old_key);
      if (!reloaded) {
        ADD_FAILURE() << "A retained source locator must reload after eviction";
        loader.Stop();
        co_return co::kJoin;
      }
      EXPECT_THAT(reloaded->GetData(), ::testing::ElementsAre(1U));
      reloaded.reset();
      loader.TrimCache();
      EXPECT_EQ(loader.GetMaterialAsset(material_key), new_material);
      EXPECT_EQ(loader.GetMaterialAsset(unrelated_key), unrelated);
      ExpectLeased(old_root);
      const std::weak_ptr<data::SceneAsset> weak_scene = old_scene;
      old_scene.reset();
      old_material.reset();
      ExpectLeased(old_root);
      old_resource.reset();
      loader.TrimCache();
      EXPECT_TRUE(weak_scene.expired());
      EXPECT_TRUE(serio::FileLock::TryAcquire(
        old_root / data::loose_cooked::kGenerationLeaseFileName,
        serio::FileLockMode::kExclusive));
      EXPECT_TRUE(loader.RetireLooseCookedGeneration(new_source));
      EXPECT_FALSE(loader.GetAsset<data::SceneAsset>(SceneKey()));
      // Reference-free scenes retain their physical cache identity.
      const auto retained_new_scene
        = co_await loader.LoadAssetAsync<data::SceneAsset>(
          SceneKey(), new_source);
      EXPECT_EQ(retained_new_scene, new_scene);
      EXPECT_FALSE(loader.GetMaterialAsset(material_key, *new_scene));
      ExpectLeased(new_root);
      loader.Stop();
      co_return co::kJoin;
    };
  }

  auto ExerciseMutableRefresh(TestEventLoop* loop, std::filesystem::path root)
    -> co::Co<>
  {
    const auto source = WriteGeneration(root, { .payload = 1U });
    std::filesystem::remove(
      root / data::loose_cooked::kGenerationLeaseFileName);
    co::ThreadPool pool(*loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    AssetLoader loader(engine::internal::EngineTagFactory::Get(), config);
    loader.RegisterLoader(loaders::LoadSceneAsset);
    loader.RegisterLoader(loaders::LoadScriptResource);
    loader.RegisterLoader(loaders::LoadScriptAsset);
    OXCO_WITH_NURSERY(nursery)
    {
      co_await nursery.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      loader.AddLooseCookedRoot(root);
      const auto old_scene
        = co_await loader.LoadAssetAsync<data::SceneAsset>(SceneKey());
      if (!old_scene) {
        ADD_FAILURE() << "Initial scene must load";
        loader.Stop();
        co_return co::kJoin;
      }
      auto old_script = co_await loader.LoadAssetAsync<data::ScriptAsset>(
        data::AssetKey::FromVirtualPath("/Test/generation.oscript"),
        old_scene->GetSourceKey());
      if (!old_script) {
        ADD_FAILURE() << "Generation script must load";
        loader.Stop();
        co_return co::kJoin;
      }
      const auto old_key = loader.MakeScriptResourceKeyForAsset(
        *old_script, data::ResourceReferenceIndex { 0U });
      if (!old_key) {
        ADD_FAILURE() << "Initial payload must resolve";
        loader.Stop();
        co_return co::kJoin;
      }
      const auto old_resource
        = co_await loader.LoadScriptResourceAsync(*old_key);
      if (!old_resource) {
        ADD_FAILURE() << "Initial payload must load";
        loader.Stop();
        co_return co::kJoin;
      }
      EXPECT_THAT(old_resource->GetData(), ::testing::ElementsAre(1U));
      const auto injected = CookedScriptBytes(9U);
      EXPECT_EQ(co_await loader.LoadResourceAsync<data::ScriptResource>(
                  CookedResourceData<data::ScriptResource> {
                    .key = *old_key, .bytes = injected }),
        nullptr);

      EXPECT_EQ(WriteGeneration(root, { .payload = 2U }, source), source);
      std::filesystem::remove(
        root / data::loose_cooked::kGenerationLeaseFileName);
      loader.AddLooseCookedRoot(root);
      const auto new_scene
        = co_await loader.LoadAssetAsync<data::SceneAsset>(SceneKey());
      if (!new_scene) {
        ADD_FAILURE() << "Refreshed scene must load";
        loader.Stop();
        co_return co::kJoin;
      }
      EXPECT_EQ(old_scene->GetSourceKey(), new_scene->GetSourceKey());
      EXPECT_NE(old_scene->GetSourceOrigin().instance,
        new_scene->GetSourceOrigin().instance);
      EXPECT_FALSE(loader.MakeScriptResourceKeyForAsset(
        *old_script, data::ResourceReferenceIndex { 0U }));
      EXPECT_EQ(co_await loader.LoadScriptResourceAsync(*old_key), nullptr);
      EXPECT_EQ(co_await loader.LoadResourceAsync<data::ScriptResource>(
                  CookedResourceData<data::ScriptResource> {
                    .key = *old_key, .bytes = injected }),
        nullptr);
      const auto synthetic_key = loader.MintSyntheticScriptKey();
      auto synthetic = co_await loader.LoadResourceAsync<data::ScriptResource>(
        CookedResourceData<data::ScriptResource> {
          .key = synthetic_key, .bytes = injected });
      if (!synthetic) {
        ADD_FAILURE() << "Synthetic script bytes must load";
        loader.Stop();
        co_return co::kJoin;
      }
      EXPECT_THAT(synthetic->GetData(), ::testing::ElementsAre(9U));
      synthetic.reset();
      loader.TrimCache();
      synthetic = co_await loader.LoadResourceAsync<data::ScriptResource>(
        CookedResourceData<data::ScriptResource> {
          .key = synthetic_key, .bytes = injected });
      EXPECT_TRUE(synthetic);

      auto new_script = co_await loader.LoadAssetAsync<data::ScriptAsset>(
        data::AssetKey::FromVirtualPath("/Test/generation.oscript"),
        new_scene->GetSourceKey());
      if (!new_script) {
        ADD_FAILURE() << "Replacement script must load";
        loader.Stop();
        co_return co::kJoin;
      }
      const auto new_key = loader.MakeScriptResourceKeyForAsset(
        *new_script, data::ResourceReferenceIndex { 0U });
      old_script.reset();
      new_script.reset();
      if (!new_key) {
        ADD_FAILURE() << "Refreshed payload must resolve";
        loader.Stop();
        co_return co::kJoin;
      }
      EXPECT_NE(old_key, new_key);
      const auto new_resource
        = co_await loader.LoadScriptResourceAsync(*new_key);
      if (!new_resource) {
        ADD_FAILURE() << "Refreshed payload must load";
        loader.Stop();
        co_return co::kJoin;
      }
      EXPECT_THAT(new_resource->GetData(), ::testing::ElementsAre(2U));
      EXPECT_THAT(old_resource->GetData(), ::testing::ElementsAre(1U));
      loader.Stop();
      co_return co::kJoin;
    };
  }

  auto ExerciseExternalBinding(TestEventLoop* loop, std::filesystem::path root)
    -> co::Co<>
  {
    const auto owner_root = root / "owner";
    const auto first_root = root / "first";
    const auto second_root = root / "second";
    const auto material_key = MaterialKey();
    const auto owner_source
      = WriteGeneration(owner_root, { .scene_material = material_key });
    const auto first_source = WriteGeneration(first_root,
      { .material = material_key, .emission = 1.0F, .payload = 1U });
    const auto second_source = WriteGeneration(second_root,
      { .material = material_key, .emission = 2.0F, .payload = 2U });
    co::ThreadPool pool(*loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    AssetLoader loader(engine::internal::EngineTagFactory::Get(), config);
    loader.RegisterLoader(loaders::LoadSceneAsset);
    loader.RegisterLoader(loaders::LoadMaterialAsset);
    OXCO_WITH_NURSERY(nursery)
    {
      co_await nursery.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      static_cast<void>(loader.MountLooseCookedGeneration(first_root));
      static_cast<void>(loader.MountLooseCookedGeneration(owner_root));
      const auto original_scope = loader.BeginLoadScope();
      const auto owner = co_await loader.LoadAssetAsync<data::SceneAsset>(
        SceneKey(), owner_source, LoadRequest { .scope = original_scope });
      const auto first
        = co_await loader.LoadAssetAsync<data::MaterialAsset>(material_key);
      if (!owner || !first) {
        ADD_FAILURE() << "External binding fixture failed to decode";
        loader.Stop();
        co_return co::kJoin;
      }
      EXPECT_EQ(loader.GetMaterialAsset(material_key, *owner), first);
      EXPECT_EQ(loader.MountLooseCookedGeneration(second_root, first_source),
        second_source);
      const auto second
        = co_await loader.LoadAssetAsync<data::MaterialAsset>(material_key);
      EXPECT_NE(first, second);
      EXPECT_EQ(loader.GetMaterialAsset(material_key), second);
      EXPECT_EQ(loader.GetMaterialAsset(material_key, *owner), first);
      const auto exact_missing
        = co_await loader.LoadAssetAsync<data::MaterialAsset>(
          material_key, owner_source);
      EXPECT_FALSE(exact_missing);
      const auto repeated = co_await loader.LoadAssetAsync<data::SceneAsset>(
        SceneKey(), owner_source);
      if (!repeated) {
        ADD_FAILURE() << "Updated binding view failed to load";
        loader.Stop();
        co_return co::kJoin;
      }
      EXPECT_NE(repeated, owner);
      EXPECT_EQ(loader.GetMaterialAsset(material_key, *repeated), second);
      const auto scoped = co_await loader.LoadAssetAsync<data::SceneAsset>(
        SceneKey(), owner_source, LoadRequest { .scope = original_scope });
      EXPECT_EQ(scoped, owner);
      EXPECT_EQ(loader.GetMaterialAsset(material_key, *owner), first);
      ExpectLeased(first_root);
      loader.Stop();
      co_return co::kJoin;
    };
  }

  struct DecodePause {
    co::Event entered;
    co::Event completed;
    std::shared_ptr<data::MaterialAsset> decoded;
    std::latch resumed { 1 };
    std::atomic<bool> released { false };

    auto Release() noexcept -> void
    {
      if (!released.exchange(true)) {
        resumed.count_down();
      }
    }
  };

  auto ExerciseRevokedGeometryDependencies(
    TestEventLoop* loop, std::filesystem::path root) -> co::Co<>
  {
    const auto geometry_root = root / "geometry";
    const auto material_root = root / "materials";
    const auto geometry_key
      = data::AssetKey::FromVirtualPath("/Test/geometry.ogeo");
    data::pak::geometry::GeometryAssetDesc geometry_desc {};
    geometry_desc.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kGeometry);
    geometry_desc.header.version = data::pak::geometry::kGeometryAssetVersion;
    LooseCookedTestWriter writer(geometry_root);
    writer.WriteAssetDescriptor(geometry_key, data::AssetType::kGeometry,
      "/Test/geometry.ogeo", "geometry.ogeo",
      std::as_bytes(std::span { &geometry_desc, 1U }));
    static_cast<void>(writer.Finish());
    static_cast<void>(WriteGeneration(
      material_root, { .material = MaterialKey(), .emission = 2.0F }));
    co::ThreadPool pool(*loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    AssetLoader loader(engine::internal::EngineTagFactory::Get(), config);
    const auto pause = std::make_shared<DecodePause>();
    loader.RegisterLoader([geometry_desc](LoaderContext context) {
      context.dependency_collector->AddAssetDependency(MaterialKey());
      return std::make_unique<data::GeometryAsset>(context.current_asset_key,
        geometry_desc, std::vector<std::shared_ptr<data::Mesh>> {},
        data::SourceOrigin { context.source_key, context.source_instance });
    });
    loader.RegisterLoader([loop, pause](LoaderContext context) {
      loop->Schedule(std::chrono::milliseconds::zero(),
        [pause] { pause->entered.Trigger(); });
      pause->resumed.wait();
      return loaders::LoadMaterialAsset(std::move(context));
    });
    OXCO_WITH_NURSERY(nursery)
    {
      const auto release_guard
        = ScopeGuard([pause]() noexcept { pause->Release(); });
      co_await nursery.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      static_cast<void>(loader.MountLooseCookedGeneration(material_root));
      loader.AddLooseCookedRoot(geometry_root);
      std::shared_ptr<data::GeometryAsset> result;
      loader.StartLoadGeometryAsset(
        geometry_key, [&result, pause](auto geometry) {
          result = std::move(geometry);
          pause->completed.Trigger();
        });
      co_await pause->entered;
      loader.AddLooseCookedRoot(geometry_root);
      pause->Release();
      co_await pause->completed;
      co_await loader.WaitForPendingLoadsAsync();
      EXPECT_EQ(result, nullptr);
      EXPECT_FALSE(loader.HasGeometryAsset(geometry_key));
      EXPECT_TRUE(loader.HasMaterialAsset(MaterialKey()));
      loader.TrimCache();
      EXPECT_FALSE(loader.HasMaterialAsset(MaterialKey()));
      EXPECT_EQ(loader.GetTelemetryStats().cache.entries, 0U);
      loader.Stop();
      co_return co::kJoin;
    };
  }

  auto ExerciseInFlightGeneration(
    TestEventLoop* loop, std::filesystem::path root) -> co::Co<>
  {
    const auto old_root = root / "old";
    const auto new_root = root / "new";
    const auto material_key = MaterialKey();
    const auto old_source = WriteGeneration(
      old_root, { .material = material_key, .emission = 1.0F, .payload = 1U });
    const auto new_source = WriteGeneration(
      new_root, { .material = material_key, .emission = 2.0F, .payload = 2U });
    co::ThreadPool pool(*loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    AssetLoader loader(engine::internal::EngineTagFactory::Get(), config);
    const auto pause = std::make_shared<DecodePause>();
    loader.RegisterLoader(
      [loop, pause, old_source](
        LoaderContext context) -> std::unique_ptr<data::MaterialAsset> {
        if (context.source_key == old_source) {
          loop->Schedule(std::chrono::milliseconds::zero(),
            [pause] -> void { pause->entered.Trigger(); });
          pause->resumed.wait();
        }
        return loaders::LoadMaterialAsset(std::move(context));
      });
    OXCO_WITH_NURSERY(nursery)
    {
      const auto release_guard
        = ScopeGuard([pause] noexcept -> void { pause->Release(); });
      co_await nursery.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      static_cast<void>(loader.MountLooseCookedGeneration(old_root));
      loader.StartLoadMaterialAsset(material_key, [pause](auto result) -> auto {
        pause->decoded = std::move(result);
        pause->completed.Trigger();
      });
      co_await pause->entered;
      EXPECT_EQ(
        loader.MountLooseCookedGeneration(new_root, old_source), new_source);
      ExpectLeased(old_root);
      pause->Release();
      co_await pause->completed;
      co_await loader.WaitForPendingLoadsAsync();
      EXPECT_TRUE(pause->decoded);
      if (pause->decoded) {
        EXPECT_EQ(pause->decoded->GetSourceKey(), old_source);
        EXPECT_FLOAT_EQ(pause->decoded->GetEmissiveFactor().at(0), 1.0F);
        loader.TrimCache();
        ExpectLeased(old_root);
        pause->decoded.reset();
        loader.TrimCache();
        EXPECT_TRUE(serio::FileLock::TryAcquire(
          old_root / data::loose_cooked::kGenerationLeaseFileName,
          serio::FileLockMode::kExclusive));
      }
      loader.Stop();
      co_return co::kJoin;
    };
  }

  auto ExerciseReloadGenerationIsolation(
    TestEventLoop* loop, std::filesystem::path root) -> co::Co<>
  {
    const auto old_root = root / "script-old";
    const auto new_root = root / "script-new";
    const auto key = data::AssetKey::FromVirtualPath("/Test/reload.oscript");
    const auto old_source
      = WriteGeneration(old_root, { .payload = 1U, .script = key });
    static_cast<void>(
      WriteGeneration(new_root, { .payload = 2U, .script = key }));
    co::ThreadPool pool(*loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    AssetLoader loader(engine::internal::EngineTagFactory::Get(), config);
    const auto pause = std::make_shared<DecodePause>();
    OXCO_WITH_NURSERY(nursery)
    {
      const auto unblock = ScopeGuard([pause]() noexcept { pause->Release(); });
      co_await nursery.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      static_cast<void>(loader.MountLooseCookedGeneration(old_root));
      const auto original
        = co_await loader.LoadAssetAsync<data::ScriptAsset>(key);
      if (!original) {
        ADD_FAILURE() << "Original script must load";
        loader.Stop();
        co_return co::kJoin;
      }
      loader.RegisterLoader([loop, pause, old_source](LoaderContext context) {
        if (context.source_key == old_source) {
          loop->Schedule(std::chrono::milliseconds::zero(),
            [pause] { pause->entered.Trigger(); });
          pause->resumed.wait();
        }
        return loaders::LoadScriptAsset(context);
      });
      std::shared_ptr<const data::ScriptResource> notified;
      auto subscription = loader.SubscribeScriptReload(
        [&notified](const data::AssetKey&,
          std::shared_ptr<const data::ScriptResource> bytes) {
          notified = std::move(bytes);
        });
      loader.ReloadAllScripts();
      co_await pause->entered;
      static_cast<void>(
        loader.MountLooseCookedGeneration(new_root, old_source));
      const auto replacement
        = co_await loader.LoadAssetAsync<data::ScriptAsset>(key);
      EXPECT_NE(replacement, nullptr);
      pause->Release();
      co_await loader.WaitForPendingLoadsAsync();
      if (notified) {
        EXPECT_THAT(notified->GetData(), ::testing::ElementsAre(1U));
      } else {
        ADD_FAILURE() << "Reload must publish bytecode from its completed "
                         "old-generation asset";
      }
      loader.Stop();
      co_return co::kJoin;
    };
  }

  NOLINT_TEST_F(
    AssetLoaderBasicTest, ScriptReloadKeepsItsExactGenerationBytecode)
  {
    TestEventLoop loop;
    co::Run(loop, ExerciseReloadGenerationIsolation(&loop, temp_dir_));
  }

  auto ExerciseAtomicMountSet(TestEventLoop* loop, std::filesystem::path root)
    -> co::Co<>
  {
    const auto old_root = root / "old";
    const auto new_root = root / "new";
    const auto old_key = MaterialKey();
    const auto new_key
      = data::AssetKey::FromVirtualPath("/Test/replacement.omat");
    const auto old_source
      = WriteGeneration(old_root, { .material = old_key, .emission = 1.0F });
    static_cast<void>(
      WriteGeneration(new_root, { .material = new_key, .emission = 2.0F }));
    co::ThreadPool pool(*loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    AssetLoader loader(engine::internal::EngineTagFactory::Get(), config);
    loader.RegisterLoader(loaders::LoadMaterialAsset);
    VirtualPathResolver resolver;
    OXCO_WITH_NURSERY(nursery)
    {
      co_await nursery.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      loader.AddLooseCookedRoot(old_root);
      resolver.AddLooseCookedRoot(old_root);
      const auto original
        = co_await loader.LoadAssetAsync<data::MaterialAsset>(old_key);
      EXPECT_NE(original, nullptr);

      std::vector failed_roots { new_root, root / "missing" };
      auto rejected = false;
      try {
        auto failed = co_await loader.PrepareLooseCookedRootsAsync(
          std::move(failed_roots));
      } catch (const std::exception&) {
        rejected = true;
      }
      EXPECT_TRUE(rejected);
      EXPECT_EQ(resolver.ResolveAssetKey("/Test/material.omat"), old_key);
      EXPECT_EQ(loader.GetMaterialAsset(old_key), original);

      std::vector stale_roots { new_root };
      auto stale
        = co_await loader.PrepareLooseCookedRootsAsync(std::move(stale_roots));
      loader.AddLooseCookedRoot(old_root);
      EXPECT_THROW(
        static_cast<void>(loader.CommitPreparedMounts(std::move(stale))),
        OperationCancelledException);
      EXPECT_EQ(loader.GetMaterialAsset(old_key), original);

      std::shared_ptr<data::MaterialAsset> reloaded;
      auto notified = false;
      auto canceled_notifications = 0;
      IAssetLoader::EvictionSubscription canceled;
      auto observer = loader.SubscribeResourceEvictions(
        data::MaterialAsset::ClassTypeId(), [&](const EvictionEvent& event) {
          if (event.reason != EvictionReason::kClear) {
            return;
          }
          notified = true;
          EXPECT_EQ(resolver.ResolveAssetKey("/Test/material.omat"), new_key);
          canceled.Cancel();
          loader.StartLoadMaterialAsset(new_key,
            [&reloaded](auto loaded) { reloaded = std::move(loaded); });
        });
      canceled
        = loader.SubscribeResourceEvictions(data::MaterialAsset::ClassTypeId(),
          [&](const EvictionEvent&) { ++canceled_notifications; });
      std::vector roots { new_root };
      auto prepared
        = co_await loader.PrepareLooseCookedRootsAsync(std::move(roots));
      VirtualPathResolver next;
      next.AddLooseCookedRoot(new_root);
      {
        auto retirement = loader.CommitPreparedMounts(std::move(prepared));
        EXPECT_FALSE(notified);
        resolver.Swap(next);
      }
      EXPECT_TRUE(notified);
      EXPECT_EQ(canceled_notifications, 0);
      co_await loader.WaitForPendingLoadsAsync();
      EXPECT_NE(reloaded, nullptr);
      EXPECT_EQ(loader.GetMaterialAsset(new_key), reloaded);
      const auto old_again
        = co_await loader.LoadAssetAsync<data::MaterialAsset>(
          old_key, old_source);
      EXPECT_NE(old_again, nullptr);
      ExpectLeased(old_root);
      loader.Stop();
      co_return co::kJoin;
    };
  }

  NOLINT_TEST_F(
    AssetLoaderBasicTest, PreparedMountSetCommitsBeforeRetirementCallbacks)
  {
    TestEventLoop loop;
    co::Run(loop, ExerciseAtomicMountSet(&loop, temp_dir_));
  }

  auto PrepareIdleMountSet(AssetLoader* loader, std::filesystem::path old_root,
    std::filesystem::path new_root) -> co::Co<PreparedMountSet>
  {
    std::optional<PreparedMountSet> prepared;
    OXCO_WITH_NURSERY(nursery)
    {
      co_await nursery.Start(&AssetLoader::ActivateAsync, loader);
      loader->Run();
      loader->AddLooseCookedRoot(old_root);
      EXPECT_NE(
        co_await loader->LoadAssetAsync<data::MaterialAsset>(MaterialKey()),
        nullptr);
      std::vector roots { std::move(new_root) };
      prepared.emplace(
        co_await loader->PrepareLooseCookedRootsAsync(std::move(roots)));
      co_return co::kCancel;
    };
    if (!prepared.has_value()) {
      throw std::logic_error("Fixture did not prepare a mount set");
    }
    co_return std::move(prepared).value();
  }

  NOLINT_TEST_F(AssetLoaderBasicTest, PreparedMountSetRejectsRestartedLoader)
  {
    const auto old_root = temp_dir_ / "old";
    const auto new_root = temp_dir_ / "new";
    static_cast<void>(WriteGeneration(old_root, { .material = MaterialKey() }));
    static_cast<void>(WriteGeneration(new_root, { .material = MaterialKey() }));
    TestEventLoop loop;
    co::ThreadPool pool(loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    AssetLoader loader(engine::internal::EngineTagFactory::Get(), config);
    loader.RegisterLoader(loaders::LoadMaterialAsset);
    auto prepared
      = co::Run(loop, PrepareIdleMountSet(&loader, old_root, new_root));
    loader.Stop();
    loader.Run();
    EXPECT_THROW(
      static_cast<void>(loader.CommitPreparedMounts(std::move(prepared))),
      OperationCancelledException);
    loader.Stop();
  }

  NOLINT_TEST_F(
    AssetLoaderBasicTest, UnstartedMountPreparationDoesNotBorrowLoader)
  {
    TestEventLoop loop;
    co::ThreadPool pool(loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    auto loader = std::make_unique<AssetLoader>(
      engine::internal::EngineTagFactory::Get(), config);
    auto work = loader->PrepareLooseCookedRootsAsync({});
    loader.reset();
    EXPECT_THROW(static_cast<void>(co::Run(loop, std::move(work))),
      OperationCancelledException);
  }

  NOLINT_TEST_F(AssetLoaderBasicTest, MountRetirementObserverMayDestroyLoader)
  {
    const auto old_root = temp_dir_ / "old";
    const auto new_root = temp_dir_ / "new";
    static_cast<void>(WriteGeneration(old_root, { .material = MaterialKey() }));
    static_cast<void>(WriteGeneration(new_root, { .material = MaterialKey() }));
    TestEventLoop loop;
    co::ThreadPool pool(loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    auto loader = std::make_unique<AssetLoader>(
      engine::internal::EngineTagFactory::Get(), config);
    loader->RegisterLoader(loaders::LoadMaterialAsset);
    auto prepared
      = co::Run(loop, PrepareIdleMountSet(loader.get(), old_root, new_root));
    unsigned notifications = 0;
    auto subscription = loader->SubscribeResourceEvictions(
      data::MaterialAsset::ClassTypeId(), [&](const EvictionEvent&) {
        ++notifications;
        loader.reset();
      });
    auto retirement = loader->CommitPreparedMounts(std::move(prepared));
    EXPECT_NE(loader, nullptr);
    retirement.Finish();
    EXPECT_EQ(notifications, 1U);
    EXPECT_EQ(loader, nullptr);
    retirement.Finish();
  }

  NOLINT_TEST_F(AssetLoaderBasicTest, SidecarDiscoveryUsesCapturedLayerView)
  {
    const auto sidecar_key
      = data::AssetKey::FromVirtualPath("/Test/scene.opscene");
    const auto unrelated_scene
      = data::AssetKey::FromVirtualPath("/Test/other.oscene");
    const auto write_sidecar = [&](const std::filesystem::path& root,
                                 const data::AssetKey& target) {
      LooseCookedTestWriter writer(root);
      writer.SetSourceKey(data::SourceKey { Uuid::Generate() });
      data::pak::physics::PhysicsSceneAssetDesc descriptor {};
      descriptor.header.asset_type
        = static_cast<uint8_t>(data::AssetType::kPhysicsScene);
      descriptor.header.version = data::pak::physics::kPhysicsSceneAssetVersion;
      descriptor.target_scene_key = target;
      writer.WriteAssetDescriptor(sidecar_key, data::AssetType::kPhysicsScene,
        "/Test/scene.opscene", "scene.opscene",
        std::as_bytes(std::span { &descriptor, 1U }));
      static_cast<void>(writer.Finish());
    };
    const auto base = temp_dir_ / "base";
    const auto replacement = temp_dir_ / "replacement";
    write_sidecar(base, SceneKey());
    write_sidecar(replacement, unrelated_scene);
    AssetLoader loader(engine::internal::EngineTagFactory::Get());
    loader.AddLooseCookedRoot(base);
    const auto original = loader.BeginLoadScope();
    const auto scene_bytes = SceneBytes({});
    data::SceneAsset scene(SceneKey(), scene_bytes);
    EXPECT_EQ(
      loader.FindPhysicsSidecarAssetKeyForScene(scene, original), sidecar_key);
    loader.AddLooseCookedRoot(replacement);
    EXPECT_FALSE(loader.FindPhysicsSidecarAssetKeyForScene(
      scene, loader.BeginLoadScope()));
    EXPECT_EQ(
      loader.FindPhysicsSidecarAssetKeyForScene(scene, original), sidecar_key);
    AssetLoader foreign(engine::internal::EngineTagFactory::Get());
    EXPECT_THROW(static_cast<void>(
                   foreign.FindPhysicsSidecarAssetKeyForScene(scene, original)),
      std::invalid_argument);
  }

  NOLINT_TEST_F(AssetLoaderBasicTest,
    RevocationDuringDependenciesReleasesTemporaryCheckouts)
  {
    TestEventLoop loop;
    co::Run(loop, ExerciseRevokedGeometryDependencies(&loop, temp_dir_));
  }

  NOLINT_TEST_F(
    AssetLoaderBasicTest, SameSourceKeyRefreshDoesNotAliasCachedPayloads)
  {
    TestEventLoop loop;
    co::Run(loop, ExerciseMutableRefresh(&loop, temp_dir_ / "mutable"));
  }

  NOLINT_TEST_F(
    AssetLoaderBasicTest, ExternalDependencyBindingsSurviveRootReplacement)
  {
    TestEventLoop loop;
    co::Run(loop, ExerciseExternalBinding(&loop, temp_dir_));
  }

  NOLINT_TEST_F(AssetLoaderBasicTest,
    ReplacementRetainsAnInFlightSourceBeforeDecodeCompletes)
  {
    TestEventLoop loop;
    co::Run(loop, ExerciseInFlightGeneration(&loop, temp_dir_));
  }

  NOLINT_TEST_F(
    AssetLoaderBasicTest, RetiredGenerationsRetainTheirOwnAssetsAndPayloads)
  {
    TestEventLoop loop;
    co::Run(loop, ExerciseRetainedGeneration(&loop, temp_dir_));
  }

} // namespace
} // namespace oxygen::content::testing
