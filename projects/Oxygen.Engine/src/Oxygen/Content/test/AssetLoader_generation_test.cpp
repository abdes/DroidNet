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
#include <exception>
#include <filesystem>
#include <functional>
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

#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/ContentLoadError.h>
#include <Oxygen/Content/ContentMounts.h>
#include <Oxygen/Content/DependencyRequirementError.h>
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Content/Loaders/ScriptLoader.h>
#include <Oxygen/Content/OperationCancelledException.h>
#include <Oxygen/Content/ResidencyPolicy.h>
#include <Oxygen/Content/SourceKeyConflictError.h>
#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Cooker/testing/CookedBytes.h>
#include <Oxygen/Cooker/testing/CookedPak.h>
#include <Oxygen/Cooker/testing/Descriptors.h>
#include <Oxygen/Cooker/testing/GeometryBuilder.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/PhysicsSidecarBuilder.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Cooker/testing/ScriptBuilder.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Cooker/testing/TextureBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/SourceOrigin.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/SurfaceInventory.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/OxCo/test/Utils/TestEventLoop.h>
#include <Oxygen/Serio/FileLock.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {
namespace {

  using co::testing::TestEventLoop;

  using cooker::testing::CookedPak;
  using cooker::testing::LooseCookedRoot;
  using cooker::testing::SourceKeyOf;

  //! The identities of the roots a test writes side by side.
  constexpr auto kFirstSeed = std::uint8_t { 1U };
  constexpr auto kSecondSeed = std::uint8_t { 2U };
  constexpr auto kThirdSeed = std::uint8_t { 3U };
  //! The identities of the engine and content paks of the pak tests.
  constexpr auto kEngineSeed = std::uint8_t { 0x21U };
  constexpr auto kContentSeed = std::uint8_t { 0x22U };

  struct GenerationRecipe {
    data::AssetKey material;
    float emission = 0.0F;
    std::uint8_t payload = 0;
    data::AssetKey scene_material;
    data::AssetKey script;
    //! The identity of the root; rewriting a root with the same seed refreshes
    //! it, and roots mounted together take distinct seeds.
    std::uint8_t seed = kFirstSeed;
  };

  //! Loose cooked roots as a mount set, lowest precedence first.
  auto LooseRoots(const std::vector<std::filesystem::path>& roots)
    -> std::vector<ContentMount>
  {
    auto mounts = std::vector<ContentMount> {};
    for (const auto& root : roots) {
      mounts.push_back({
        .kind = ContentMount::Kind::kLooseCookedRoot,
        .path = root,
      });
    }
    return mounts;
  }

  auto SceneKey() -> data::AssetKey
  {
    return data::AssetKey::FromVirtualPath("/test/generation.oscene");
  }

  auto MaterialKey() -> data::AssetKey
  {
    return data::AssetKey::FromVirtualPath("/test/material.omat");
  }

  //! The scene of the tests: one node, which with a `material` renders the
  //! bound cube and paints its surface with it.
  auto GenerationScene(const data::AssetKey& material)
    -> cooker::testing::SceneBuilder
  {
    auto scene = cooker::testing::SceneBuilder { "test" };
    const auto root = scene.Node("root");
    if (!material.IsNil()) {
      scene.Renderable(
        root, data::AssetKey::FromVirtualPath("/test/bound-cube.ogeo"));
      const auto part
        = data::SurfaceId::FromStableIdentity("generation-test-slot");
      const std::array surfaces { data::Surface {
        .surface_id = part,
        .display_name = "fixture",
        .bindings = { { .lod_index = oxygen::data::LodIndex {},
          .submesh_index = oxygen::data::SubmeshIndex {},
          .default_material_key = material, }, },
        .defaults = std::nullopt, }, };
      const auto revision = data::ComputeSurfaceLayoutRevision(surfaces);
      if (!revision) {
        throw std::logic_error("Invalid fixture surface layout");
      }
      auto value = data::pak::world::OverrideValue {};
      value.as_asset = material;
      scene.Override(root, data::schema::OverrideId::kGeometrySurfaceMaterial,
        value, part, revision.value());
    }
    return scene;
  }

  //! The descriptor of the scene of the tests that renders nothing.
  auto PlainSceneBytes() -> std::vector<std::byte>
  {
    return GenerationScene({}).Bytes();
  }

  auto BoundCubeKey() -> data::AssetKey
  {
    return data::AssetKey::FromVirtualPath("/test/bound-cube.ogeo");
  }

  //! Adds the cube the generation scene renders, whose only submesh is
  //! painted with `material`.
  auto AddBoundCube(LooseCookedRoot& source, const data::AssetKey& material)
    -> void
  {
    auto geometry = cooker::testing::GeometryBuilder { "bound-cube" };
    geometry.Submesh(geometry.ProceduralLod(),
      data::SurfaceId::FromStableIdentity("generation-test-slot"), material);
    source.Add({
      .type = data::AssetType::kGeometry,
      .leaf = "bound-cube.ogeo",
      .descriptor = geometry.Bytes(),
      .references = geometry.References(),
      .key = BoundCubeKey(),
    });
  }

  //! Adds the script whose one-byte bytecode is `recipe.payload`.
  auto AddGenerationScript(
    LooseCookedRoot& source, const GenerationRecipe& recipe) -> void
  {
    const auto payload = std::array { recipe.payload };
    const auto row = source.AddScript(std::as_bytes(std::span { payload }));
    auto script = cooker::testing::ScriptBuilder { "reload" };
    script.Bytecode(row);
    source.Add({
      .type = data::AssetType::kScript,
      .leaf = "reload.oscript",
      .descriptor = script.Bytes(),
      .references = script.References(),
      .key = recipe.script.IsNil()
        ? data::AssetKey::FromVirtualPath("/test/generation.oscript")
        : recipe.script,
    });
  }

  //! Creates `root` and holds its generation lease exclusively while a test
  //! writes the generation, as a cook does.
  auto LockNewGeneration(const std::filesystem::path& root) -> serio::FileLock
  {
    std::filesystem::create_directories(root);
    auto exclusive = serio::FileLock::TryAcquire(
      root / data::loose_cooked::kGenerationLeaseFileName,
      serio::FileLockMode::kExclusive, serio::FileLockOpenMode::kOpenOrCreate);
    if (!exclusive) {
      throw std::system_error(exclusive.error());
    }
    return std::move(*exclusive);
  }

  auto WriteGeneration(const std::filesystem::path& root,
    const GenerationRecipe recipe) -> data::SourceKey
  {
    const auto lease = LockNewGeneration(root);
    auto source = LooseCookedRoot { "test", recipe.seed };
    if (!recipe.scene_material.IsNil()) {
      AddBoundCube(source, recipe.scene_material);
    }
    const auto scene = GenerationScene(recipe.scene_material);
    source.Add({
      .type = data::AssetType::kScene,
      .leaf = "generation.oscene",
      .descriptor = scene.Bytes(),
      .references = scene.References(),
      .key = SceneKey(),
    });
    if (!recipe.material.IsNil()) {
      auto record = cooker::testing::MaterialBuilder::Record();
      record.emissive_factor[0] = recipe.emission;
      const auto material
        = cooker::testing::MaterialBuilder { "material", record };
      source.Add({
        .type = data::AssetType::kMaterial,
        .leaf = "material.omat",
        .descriptor = material.Bytes(),
        .references = material.References(),
        .key = recipe.material,
      });
    }
    AddGenerationScript(source, recipe);
    cooker::testing::Materialize(source.Files(), root);
    return source.SourceKey();
  }

  auto CookedScriptBytes(const uint8_t payload) -> std::vector<uint8_t>
  {
    const std::array<std::byte, 1> bytes { std::byte { payload } };
    return cooker::testing::CookedResourceBytes(
      data::pak::scripting::ScriptResourceDesc {}, bytes);
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
    const auto old_source = WriteGeneration(old_root,
      {
        .material = material_key,
        .emission = 1.0F,
        .payload = 1U,
        .seed = kFirstSeed,
      });
    const auto new_source = WriteGeneration(new_root,
      {
        .material = material_key,
        .emission = 2.0F,
        .payload = 2U,
        .seed = kSecondSeed,
      });
    static_cast<void>(WriteGeneration(unrelated_root,
      {
        .material = unrelated_key,
        .emission = 3.0F,
        .payload = 3U,
        .seed = kThirdSeed,
      }));
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
        data::AssetKey::FromVirtualPath("/test/generation.oscript"),
        old_scene->GetSourceKey());
      if (!old_script) {
        ADD_FAILURE() << "Generation script must load";
        loader.Stop();
        co_return co::kJoin;
      }
      const auto old_key = loader.MakeScriptResourceKeyForAsset(
        *old_script, data::ResourceReferenceIndex { 0U });
      auto new_script = co_await loader.LoadAssetAsync<data::ScriptAsset>(
        data::AssetKey::FromVirtualPath("/test/generation.oscript"),
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
        data::AssetKey::FromVirtualPath("/test/generation.oscript"),
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

      EXPECT_EQ(WriteGeneration(root, { .payload = 2U }), source);
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
          .key = synthetic_key,
          .bytes = injected,
        });
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
          .key = synthetic_key,
          .bytes = injected,
        });
      EXPECT_TRUE(synthetic);

      auto new_script = co_await loader.LoadAssetAsync<data::ScriptAsset>(
        data::AssetKey::FromVirtualPath("/test/generation.oscript"),
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
    const auto owner_source = WriteGeneration(
      owner_root, { .scene_material = material_key, .seed = kThirdSeed });
    const auto first_source = WriteGeneration(first_root,
      {
        .material = material_key,
        .emission = 1.0F,
        .payload = 1U,
        .seed = kFirstSeed,
      });
    const auto second_source = WriteGeneration(second_root,
      {
        .material = material_key,
        .emission = 2.0F,
        .payload = 2U,
        .seed = kSecondSeed,
      });
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

  //! A material decoder that, for a descriptor of `paused_source` (of any
  //! source when none), signals `pause->entered` on the loop and waits for
  //! `pause->Release()` before it decodes.
  auto PausingMaterialDecoder(TestEventLoop* loop,
    std::shared_ptr<DecodePause> pause,
    std::optional<data::SourceKey> paused_source = std::nullopt) -> auto
  {
    return
      [loop, pause = std::move(pause), paused_source](
        const LoaderContext& context) -> std::unique_ptr<data::MaterialAsset> {
        if (!paused_source || context.source_key == *paused_source) {
          loop->Schedule(std::chrono::milliseconds::zero(),
            [pause] -> void { pause->entered.Trigger(); });
          pause->resumed.wait();
        }
        return loaders::LoadMaterialAsset(context);
      };
  }

  auto ExerciseRevokedGeometryDependencies(
    TestEventLoop* loop, std::filesystem::path root) -> co::Co<>
  {
    const auto geometry_root = root / "geometry";
    const auto material_root = root / "materials";
    const auto geometry_key
      = data::AssetKey::FromVirtualPath("/test/geometry.ogeo");
    data::pak::geometry::GeometryAssetDesc geometry_desc {};
    geometry_desc.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kGeometry);
    geometry_desc.header.version = data::pak::geometry::kGeometryAssetVersion;
    auto geometry_file = cooker::testing::GeometryBuilder { "geometry" };
    geometry_file.Submesh(geometry_file.ProceduralLod(),
      data::SurfaceId::FromStableIdentity("fixture-surface"));
    auto geometry_source = LooseCookedRoot { "test", kThirdSeed };
    geometry_source.Add({
      .type = data::AssetType::kGeometry,
      .leaf = "geometry.ogeo",
      .descriptor = geometry_file.Bytes(),
      .references = geometry_file.References(),
      .key = geometry_key,
    });
    cooker::testing::Materialize(geometry_source.Files(), geometry_root);
    static_cast<void>(WriteGeneration(material_root,
      { .material = MaterialKey(), .emission = 2.0F, .seed = kFirstSeed }));
    co::ThreadPool pool(*loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    AssetLoader loader(engine::internal::EngineTagFactory::Get(), config);
    const auto pause = std::make_shared<DecodePause>();
    loader.RegisterLoader([geometry_desc](const LoaderContext& context)
                            -> std::unique_ptr<oxygen::data::GeometryAsset> {
      context.dependency_collector->AddAssetDependency(MaterialKey());
      return std::make_unique<data::GeometryAsset>(context.current_asset_key,
        geometry_desc, std::vector<std::shared_ptr<data::Mesh>> {},
        data::SourceOrigin {
          .key = context.source_key,
          .instance = context.source_instance,
        });
    });
    loader.RegisterLoader(PausingMaterialDecoder(loop, pause));
    OXCO_WITH_NURSERY(nursery)
    {
      const auto release_guard
        = ScopeGuard([pause] noexcept -> void { pause->Release(); });
      co_await nursery.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      static_cast<void>(loader.MountLooseCookedGeneration(material_root));
      loader.AddLooseCookedRoot(geometry_root);
      std::shared_ptr<data::GeometryAsset> result;
      loader.StartLoadGeometryAsset(
        geometry_key, [&result, pause](auto geometry) -> auto {
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
    const auto old_source = WriteGeneration(old_root,
      {
        .material = material_key,
        .emission = 1.0F,
        .payload = 1U,
        .seed = kFirstSeed,
      });
    const auto new_source = WriteGeneration(new_root,
      {
        .material = material_key,
        .emission = 2.0F,
        .payload = 2U,
        .seed = kSecondSeed,
      });
    co::ThreadPool pool(*loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    AssetLoader loader(engine::internal::EngineTagFactory::Get(), config);
    const auto pause = std::make_shared<DecodePause>();
    loader.RegisterLoader(PausingMaterialDecoder(loop, pause, old_source));
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

  //! A load paused in a generation's material loader while the generation
  //! retires completes from that generation and keeps it leased until its
  //! result is released.
  auto ExerciseRetireDuringLoad(TestEventLoop* loop, std::filesystem::path root)
    -> co::Co<>
  {
    const auto material_key = MaterialKey();
    const auto source = WriteGeneration(root,
      {
        .material = material_key,
        .emission = 1.0F,
        .payload = 1U,
        .seed = kFirstSeed,
      });
    co::ThreadPool pool(*loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    AssetLoader loader(engine::internal::EngineTagFactory::Get(), config);
    const auto pause = std::make_shared<DecodePause>();
    loader.RegisterLoader(PausingMaterialDecoder(loop, pause));
    OXCO_WITH_NURSERY(nursery)
    {
      const auto release_guard
        = ScopeGuard([pause] noexcept -> void { pause->Release(); });
      co_await nursery.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      static_cast<void>(loader.MountLooseCookedGeneration(root));
      loader.StartLoadMaterialAsset(material_key, [pause](auto result) -> auto {
        pause->decoded = std::move(result);
        pause->completed.Trigger();
      });
      co_await pause->entered;
      EXPECT_TRUE(loader.RetireLooseCookedGeneration(source));
      EXPECT_TRUE(loader.EnumerateMountedSources().empty());
      pause->Release();
      co_await pause->completed;
      co_await loader.WaitForPendingLoadsAsync();
      EXPECT_TRUE(pause->decoded);
      if (pause->decoded) {
        EXPECT_EQ(pause->decoded->GetSourceKey(), source);
        EXPECT_FLOAT_EQ(pause->decoded->GetEmissiveFactor().at(0), 1.0F);
        ExpectLeased(root);
        pause->decoded.reset();
        loader.TrimCache();
        EXPECT_TRUE(serio::FileLock::TryAcquire(
          root / data::loose_cooked::kGenerationLeaseFileName,
          serio::FileLockMode::kExclusive));
      }
      loader.Stop();
      co_return co::kJoin;
    };
  }

  //! The texture key of the one texture `WriteTextureGeneration` writes.
  auto MeterTextureKey() -> data::AssetKey
  {
    return data::TextureKeyFromVirtualPath("/textures/Meter.otex");
  }

  //! A generation of the `textures` mount holding one texture, `Meter.otex`.
  struct TextureGeneration {
    data::SourceKey source;
    data::pak::core::ResourceIndexT row;
  };

  auto WriteTextureGeneration(const std::filesystem::path& root,
    const std::uint8_t seed) -> TextureGeneration
  {
    const auto lease = LockNewGeneration(root);
    auto source = LooseCookedRoot { "textures", seed };
    const auto row = source.AddTexture(cooker::testing::TextureBuilder {});
    source.DescribeTexture("Meter.otex", row);
    cooker::testing::Materialize(source.Files(), root);
    return { .source = source.SourceKey(), .row = row };
  }

  //! A generation holding the material of the tests, whose base color is
  //! the texture `MeterTextureKey` of another source.
  auto WriteTexturedMaterial(const std::filesystem::path& root) -> void
  {
    const auto lease = LockNewGeneration(root);
    auto source = LooseCookedRoot { "test", kThirdSeed };
    auto material = cooker::testing::MaterialBuilder { "material" };
    material.ExternalTexture(
      &data::pak::render::MaterialAssetDesc::base_color_texture,
      MeterTextureKey());
    source.Add({
      .type = data::AssetType::kMaterial,
      .leaf = "material.omat",
      .descriptor = material.Bytes(),
      .references = material.References(),
      .key = MaterialKey(),
    });
    cooker::testing::Materialize(source.Files(), root);
  }

  //! A material keeps the texture of another source it bound when it loaded:
  //! a later generation of that source binds only the materials loaded after
  //! it.
  auto ExerciseFrozenExternalTexture(
    TestEventLoop* loop, std::filesystem::path root) -> co::Co<>
  {
    const auto first_root = root / "first";
    const auto second_root = root / "second";
    const auto owner_root = root / "owner";
    const auto first = WriteTextureGeneration(first_root, kFirstSeed);
    const auto second = WriteTextureGeneration(second_root, kSecondSeed);
    WriteTexturedMaterial(owner_root);
    co::ThreadPool pool(*loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    AssetLoader loader(engine::internal::EngineTagFactory::Get(), config);
    loader.RegisterLoader(loaders::LoadMaterialAsset);
    OXCO_WITH_NURSERY(nursery)
    {
      co_await nursery.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      static_cast<void>(loader.MountLooseCookedGeneration(first_root));
      static_cast<void>(loader.MountLooseCookedGeneration(owner_root));
      const auto first_texture
        = loader.MakeTextureResourceKey(first.source, first.row);
      const auto old_material
        = co_await loader.LoadAssetAsync<data::MaterialAsset>(MaterialKey());
      if (!old_material || !first_texture) {
        ADD_FAILURE() << "Textured material fixture failed to load";
        loader.Stop();
        co_return co::kJoin;
      }
      EXPECT_EQ(old_material->GetBaseColorTextureKey(), *first_texture);
      EXPECT_EQ(loader.MountLooseCookedGeneration(second_root, first.source),
        second.source);
      const auto second_texture
        = loader.MakeTextureResourceKey(second.source, second.row);
      const auto new_material
        = co_await loader.LoadAssetAsync<data::MaterialAsset>(MaterialKey());
      if (!new_material || !second_texture) {
        ADD_FAILURE() << "Textured material failed to reload";
        loader.Stop();
        co_return co::kJoin;
      }

      EXPECT_EQ(loader.ResolveTextureResourceKey("/textures/Meter.otex"),
        second_texture);
      EXPECT_EQ(new_material->GetBaseColorTextureKey(), *second_texture);
      EXPECT_EQ(loader.MakeTextureResourceKeyForAsset(
                  *new_material, new_material->GetBaseColorTexture()),
        second_texture);
      EXPECT_EQ(old_material->GetBaseColorTextureKey(), *first_texture);
      EXPECT_EQ(loader.MakeTextureResourceKeyForAsset(
                  *old_material, old_material->GetBaseColorTexture()),
        first_texture);
      ExpectLeased(first_root);
      loader.Stop();
      co_return co::kJoin;
    };
  }

  auto ExerciseAtomicMountSet(TestEventLoop* loop, std::filesystem::path root)
    -> co::Co<>
  {
    const auto old_root = root / "old";
    const auto new_root = root / "new";
    const auto old_key = MaterialKey();
    const auto new_key
      = data::AssetKey::FromVirtualPath("/test/replacement.omat");
    const auto old_source = WriteGeneration(
      old_root, { .material = old_key, .emission = 1.0F, .seed = kFirstSeed });
    static_cast<void>(WriteGeneration(new_root,
      { .material = new_key, .emission = 2.0F, .seed = kSecondSeed }));
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
        auto failed
          = co_await loader.PrepareMountsAsync(LooseRoots(failed_roots));
      } catch (const std::exception&) {
        rejected = true;
      }
      EXPECT_TRUE(rejected);
      EXPECT_EQ(resolver.ResolveAssetKey("/test/material.omat"), old_key);
      EXPECT_EQ(loader.GetMaterialAsset(old_key), original);

      std::vector stale_roots { new_root };
      auto stale = co_await loader.PrepareMountsAsync(LooseRoots(stale_roots));
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
        data::MaterialAsset::ClassTypeId(),
        [&](const EvictionEvent& event) -> void {
          if (event.reason != EvictionReason::kClear) {
            return;
          }
          notified = true;
          EXPECT_EQ(resolver.ResolveAssetKey("/test/material.omat"), new_key);
          canceled.Cancel();
          loader.StartLoadMaterialAsset(new_key,
            [&reloaded](auto loaded) -> auto { reloaded = std::move(loaded); });
        });
      canceled
        = loader.SubscribeResourceEvictions(data::MaterialAsset::ClassTypeId(),
          [&](const EvictionEvent&) -> void { ++canceled_notifications; });
      std::vector roots { new_root };
      auto prepared = co_await loader.PrepareMountsAsync(LooseRoots(roots));
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
    co::Run(loop, ExerciseAtomicMountSet(&loop, TempDir()));
  }

  //! A running loader that has loaded from `old_root`, and the root of the
  //! mount set it prepares.
  struct IdleMountSetSpec final {
    AssetLoader* loader;
    std::filesystem::path old_root;
    std::filesystem::path new_root;
  };

  auto PrepareIdleMountSet(IdleMountSetSpec spec) -> co::Co<PreparedMountSet>
  {
    auto* const loader = spec.loader;
    std::optional<PreparedMountSet> prepared;
    OXCO_WITH_NURSERY(nursery)
    {
      co_await nursery.Start(&AssetLoader::ActivateAsync, loader);
      loader->Run();
      loader->AddLooseCookedRoot(spec.old_root);
      EXPECT_NE(
        co_await loader->LoadAssetAsync<data::MaterialAsset>(MaterialKey()),
        nullptr);
      std::vector roots { std::move(spec.new_root) };
      prepared.emplace(co_await loader->PrepareMountsAsync(LooseRoots(roots)));
      co_return co::kCancel;
    };
    if (!prepared.has_value()) {
      throw std::logic_error("Fixture did not prepare a mount set");
    }
    co_return std::move(prepared).value();
  }

  NOLINT_TEST_F(AssetLoaderBasicTest, PreparedMountSetRejectsRestartedLoader)
  {
    const auto old_root = TempDir() / "old";
    const auto new_root = TempDir() / "new";
    static_cast<void>(WriteGeneration(
      old_root, { .material = MaterialKey(), .seed = kFirstSeed }));
    static_cast<void>(WriteGeneration(
      new_root, { .material = MaterialKey(), .seed = kSecondSeed }));
    TestEventLoop loop;
    co::ThreadPool pool(loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    AssetLoader loader(engine::internal::EngineTagFactory::Get(), config);
    loader.RegisterLoader(loaders::LoadMaterialAsset);
    auto prepared = co::Run(loop,
      PrepareIdleMountSet({
        .loader = &loader,
        .old_root = old_root,
        .new_root = new_root,
      }));
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
    auto work = loader->PrepareMountsAsync({});
    loader.reset();
    EXPECT_THROW(static_cast<void>(co::Run(loop, std::move(work))),
      OperationCancelledException);
  }

  NOLINT_TEST_F(AssetLoaderBasicTest, MountRetirementObserverMayDestroyLoader)
  {
    const auto old_root = TempDir() / "old";
    const auto new_root = TempDir() / "new";
    static_cast<void>(WriteGeneration(
      old_root, { .material = MaterialKey(), .seed = kFirstSeed }));
    static_cast<void>(WriteGeneration(
      new_root, { .material = MaterialKey(), .seed = kSecondSeed }));
    TestEventLoop loop;
    co::ThreadPool pool(loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    auto loader = std::make_unique<AssetLoader>(
      engine::internal::EngineTagFactory::Get(), config);
    loader->RegisterLoader(loaders::LoadMaterialAsset);
    auto prepared = co::Run(loop,
      PrepareIdleMountSet({
        .loader = loader.get(),
        .old_root = old_root,
        .new_root = new_root,
      }));
    unsigned notifications = 0;
    auto subscription = loader->SubscribeResourceEvictions(
      data::MaterialAsset::ClassTypeId(), [&](const EvictionEvent&) -> void {
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

  auto EngineMaterialKey() -> data::AssetKey
  {
    return data::AssetKey::FromVirtualPath("/Engine/Default.omat");
  }

  //! The pak of the `Engine` mount that holds the default material.
  auto EnginePak() -> CookedPak
  {
    auto engine = LooseCookedRoot { "Engine" };
    static_cast<void>(engine.Material("Default.omat"));
    auto pak = CookedPak { std::move(engine) };
    pak.Identity(SourceKeyOf(kEngineSeed));
    return pak;
  }

  //! The pak of the `Content` mount whose crate is painted with the
  //! engine's default material, so it requires it of the `Engine` mount.
  auto ContentPak(const CookedPak& engine) -> CookedPak
  {
    auto content = LooseCookedRoot { "Content" };
    auto crate = cooker::testing::GeometryBuilder { "Crate" };
    crate.Submesh(crate.ProceduralLod(),
      data::SurfaceId::FromStableIdentity("crate"), EngineMaterialKey());
    static_cast<void>(content.Geometry("Crate.ogeo", crate));
    auto pak = CookedPak { std::move(content) };
    pak.DependsOn(engine).Identity(SourceKeyOf(kContentSeed));
    return pak;
  }

  //! A loader configuration whose engine content pak is `engine_pak`.
  auto EngineConfig(const std::filesystem::path& engine_pak)
    -> AssetLoaderConfig
  {
    AssetLoaderConfig config {};
    config.path_finder.emplace(PathFinderConfig::Create()
                                 .WithEngineContentPakPath(engine_pak)
                                 .BuildShared(),
      engine_pak.parent_path());
    return config;
  }

  //! Writes a generation of the `Engine` mount holding the default material
  //! the content pak requires.
  auto WriteEngineGeneration(const std::filesystem::path& root)
    -> data::SourceKey
  {
    const auto lease = LockNewGeneration(root);
    auto engine = LooseCookedRoot { "Engine", kFirstSeed };
    static_cast<void>(engine.Material("Default.omat"));
    cooker::testing::Materialize(engine.Files(), root);
    return engine.SourceKey();
  }

  //! The failed requirements that refuse `pak` on `loader`, after checking
  //! that the refusal left the mounted sources as they were.
  auto RefusalOf(AssetLoader& loader, const std::filesystem::path& pak)
    -> std::vector<FailedRequirement>
  {
    const auto before = loader.EnumerateMountedSources().size();
    try {
      loader.AddPakFile(pak);
      ADD_FAILURE() << "The pak was mounted";
    } catch (const DependencyRequirementError& error) {
      EXPECT_EQ(loader.EnumerateMountedSources().size(), before);
      return error.Failures();
    }
    return {};
  }

  NOLINT_TEST_F(AssetLoaderBasicTest, EngineContentPakStaysBeneathEveryMountSet)
  {
    // Arrange
    const auto engine_pak = TempDir() / "engine.pak";
    const auto engine_source = SourceKeyOf(kEngineSeed);
    cooker::testing::Materialize(EnginePak().Encode(), engine_pak);
    const auto project = TempDir() / "project";
    const auto project_source
      = WriteGeneration(project, { .material = MaterialKey() });
    TestEventLoop loop;
    co::ThreadPool pool(loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    config.path_finder.emplace(PathFinderConfig::Create()
                                 .WithEngineContentPakPath(engine_pak)
                                 .BuildShared(),
      TempDir());
    AssetLoader loader(engine::internal::EngineTagFactory::Get(), config);
    const auto mounted_keys = [&loader] -> std::vector<data::SourceKey> {
      auto keys = std::vector<data::SourceKey> {};
      for (const auto& source : loader.EnumerateMountedSources()) {
        keys.push_back(source.source_key);
      }
      return keys;
    };
    const auto at_construction = mounted_keys();

    // Act
    loader.AddLooseCookedRoot(project);
    loader.ClearMounts();
    const auto after_clear = mounted_keys();
    auto prepared
      = co::Run(loop, loader.PrepareMountsAsync(LooseRoots({ project })));
    auto retirement = loader.CommitPreparedMounts(std::move(prepared));
    retirement.Finish();
    const auto after_replacement = mounted_keys();

    // Assert
    EXPECT_THAT(at_construction, ::testing::ElementsAre(engine_source));
    EXPECT_THAT(after_clear, ::testing::ElementsAre(engine_source));
    EXPECT_THAT(after_replacement,
      ::testing::UnorderedElementsAre(engine_source, project_source));
  }

  //! A prepared mount set mounts pak files in their place above the engine
  //! content pak, and applies the pak layering rules before anything changes.
  NOLINT_TEST_F(AssetLoaderBasicTest, PreparedMountSetMountsPakFiles)
  {
    // Arrange
    const auto engine_pak = TempDir() / "engine.pak";
    const auto engine = EnginePak();
    cooker::testing::Materialize(engine.Encode(), engine_pak);
    const auto content_pak = TempDir() / "content.pak";
    cooker::testing::Materialize(ContentPak(engine).Encode(), content_pak);
    const auto project = TempDir() / "project";
    const auto project_source
      = WriteGeneration(project, { .material = MaterialKey() });
    TestEventLoop loop;
    co::ThreadPool pool(loop, 2);
    AssetLoaderConfig with_engine_config {};
    with_engine_config.thread_pool = observer_ptr { &pool };
    with_engine_config.path_finder.emplace(
      PathFinderConfig::Create()
        .WithEngineContentPakPath(engine_pak)
        .BuildShared(),
      TempDir());
    AssetLoader with_engine(
      engine::internal::EngineTagFactory::Get(), with_engine_config);
    AssetLoaderConfig without_engine_config {};
    without_engine_config.thread_pool = observer_ptr { &pool };
    AssetLoader without_engine(
      engine::internal::EngineTagFactory::Get(), without_engine_config);
    const auto mounts = std::vector<ContentMount> {
      { .kind = ContentMount::Kind::kPakFile, .path = content_pak },
      { .kind = ContentMount::Kind::kLooseCookedRoot, .path = project },
    };

    // Act
    auto prepared = co::Run(loop, with_engine.PrepareMountsAsync(mounts));
    auto retirement = with_engine.CommitPreparedMounts(std::move(prepared));
    retirement.Finish();
    auto keys = std::vector<data::SourceKey> {};
    for (const auto& source : with_engine.EnumerateMountedSources()) {
      keys.push_back(source.source_key);
    }

    // Assert
    EXPECT_THAT(keys,
      ::testing::UnorderedElementsAre(
        SourceKeyOf(kEngineSeed), SourceKeyOf(kContentSeed), project_source));
    NOLINT_EXPECT_THROW(static_cast<void>(co::Run(
                          loop, without_engine.PrepareMountsAsync(mounts))),
      DependencyRequirementError);
  }

  //! A verified mount set checks each pak's CRC32 while it opens it: an intact
  //! pak mounts, and one whose bytes changed after the cook is refused before
  //! anything is mounted.
  NOLINT_TEST_F(AssetLoaderBasicTest, VerifiedMountSetChecksEachPakCrc)
  {
    // Arrange
    const auto good_pak = TempDir() / "good.pak";
    cooker::testing::Materialize(EnginePak().Encode(), good_pak);
    const auto corrupt_pak = TempDir() / "corrupt.pak";
    auto corrupt_bytes = EnginePak().Encode();
    cooker::testing::CookedBytes::CorruptCrc(corrupt_bytes);
    cooker::testing::Materialize(corrupt_bytes, corrupt_pak);
    TestEventLoop loop;
    co::ThreadPool pool(loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    config.verify_content_hashes = true;
    AssetLoader loader(engine::internal::EngineTagFactory::Get(), config);

    // Act
    auto prepared = co::Run(loop,
      loader.PrepareMountsAsync(
        { { .kind = ContentMount::Kind::kPakFile, .path = good_pak } }));
    auto failure = std::optional<data::CookedDefect> {};
    try {
      static_cast<void>(co::Run(loop,
        loader.PrepareMountsAsync(
          { { .kind = ContentMount::Kind::kPakFile, .path = corrupt_pak } })));
    } catch (const data::CookedFormatError& error) {
      failure = error.Defect();
    }

    // Assert
    auto retirement = loader.CommitPreparedMounts(std::move(prepared));
    retirement.Finish();
    EXPECT_EQ(loader.EnumerateMountedSources().size(), 1U);
    ASSERT_HAS_VALUE(failure);
    EXPECT_EQ(*failure, data::CookedDefect::kDigestMismatch);
  }

  //! The rule a verified mount of the pak `bytes` refuses it for; empty when
  //! it mounts.
  auto VerifiedMountDefect(const std::filesystem::path& pak,
    const std::vector<std::byte>& bytes) -> std::optional<data::CookedDefect>
  {
    cooker::testing::Materialize(bytes, pak);
    TestEventLoop loop;
    co::ThreadPool pool(loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    config.verify_content_hashes = true;
    AssetLoader loader(engine::internal::EngineTagFactory::Get(), config);
    try {
      static_cast<void>(co::Run(loop,
        loader.PrepareMountsAsync(
          { { .kind = ContentMount::Kind::kPakFile, .path = pak } })));
    } catch (const data::CookedFormatError& error) {
      return error.Defect();
    }
    return std::nullopt;
  }

  //! A verified mount refuses a pak that holds no CRC32, which it cannot
  //! verify.
  NOLINT_TEST_F(AssetLoaderBasicTest, VerifiedMountRefusesAPakWithoutCrc)
  {
    // Arrange
    auto bytes = EnginePak().Encode();
    cooker::testing::CookedBytes::SetCrc(bytes, 0U);

    // Act
    const auto failure = VerifiedMountDefect(TempDir() / "no-crc.pak", bytes);

    // Assert
    ASSERT_HAS_VALUE(failure);
    EXPECT_EQ(*failure, data::CookedDefect::kNoIntegrityData);
  }

  //! A verified mount refuses a file with no room for a footer.
  NOLINT_TEST_F(
    AssetLoaderBasicTest, VerifiedMountRefusesAFileShorterThanAFooter)
  {
    // Arrange
    const auto bytes = std::vector<std::byte>(4);

    // Act
    const auto failure = VerifiedMountDefect(TempDir() / "short.pak", bytes);

    // Assert
    ASSERT_HAS_VALUE(failure);
    EXPECT_EQ(*failure, data::CookedDefect::kTooSmall);
  }

  //! A verified mount refuses a malformed footer for its own defect before
  //! reading its CRC field: a pak with no CRC32 and a non-zero footer reserve
  //! is malformed, not unverifiable.
  NOLINT_TEST_F(AssetLoaderBasicTest, VerifiedMountRefusesAMalformedFooterFirst)
  {
    // Arrange
    auto bytes = EnginePak().Encode();
    cooker::testing::CookedBytes::SetCrc(bytes, 0U);
    cooker::testing::CookedBytes::SetFooterReserved(bytes);

    // Act
    const auto failure
      = VerifiedMountDefect(TempDir() / "malformed.pak", bytes);

    // Assert
    ASSERT_HAS_VALUE(failure);
    EXPECT_EQ(*failure, data::CookedDefect::kReservedNotZero);
  }

  //! A pak that requires an engine asset mounts above other layers that sit on
  //! the engine pak, and refuses to mount without it.
  NOLINT_TEST_F(AssetLoaderBasicTest, DependentPakRequiresItsDependencyBelow)
  {
    // Arrange
    const auto engine_pak = TempDir() / "engine.pak";
    const auto engine = EnginePak();
    cooker::testing::Materialize(engine.Encode(), engine_pak);
    const auto content_pak = TempDir() / "content.pak";
    cooker::testing::Materialize(ContentPak(engine).Encode(), content_pak);
    const auto project = TempDir() / "project";
    static_cast<void>(WriteGeneration(project, { .material = MaterialKey() }));
    AssetLoaderConfig config {};
    config.path_finder.emplace(PathFinderConfig::Create()
                                 .WithEngineContentPakPath(engine_pak)
                                 .BuildShared(),
      TempDir());
    AssetLoader with_engine(engine::internal::EngineTagFactory::Get(), config);
    AssetLoader without_engine(engine::internal::EngineTagFactory::Get());
    with_engine.AddLooseCookedRoot(project);

    // Act
    NOLINT_EXPECT_NO_THROW(with_engine.AddPakFile(content_pak));
    const auto failures = RefusalOf(without_engine, content_pak);

    // Assert
    ASSERT_EQ(failures.size(), 1U);
    EXPECT_EQ(failures.front().failure, RequirementFailure::kMissing);
    EXPECT_EQ(failures.front().mount, "Engine");
    EXPECT_EQ(failures.front().requirement.key, EngineMaterialKey());
  }

  //! A patch of the engine pak that drops the default material deletes it,
  //! so a pak requiring it of `Engine` is refused above the patch.
  NOLINT_TEST_F(AssetLoaderBasicTest, DeletedDependencyRefusesThePak)
  {
    // Arrange
    const auto engine = EnginePak();
    const auto engine_pak = TempDir() / "engine.pak";
    cooker::testing::Materialize(engine.Encode(), engine_pak);
    auto replacement = LooseCookedRoot { "Engine" };
    static_cast<void>(replacement.Material("Other.omat"));
    auto patch = CookedPak { std::move(replacement) };
    patch.PatchOf(engine);
    const auto patch_pak = TempDir() / "patch.pak";
    cooker::testing::Materialize(patch.Encode(), patch_pak);
    const auto content_pak = TempDir() / "content.pak";
    cooker::testing::Materialize(ContentPak(engine).Encode(), content_pak);
    AssetLoader loader(
      engine::internal::EngineTagFactory::Get(), EngineConfig(engine_pak));
    loader.AddPakFile(patch_pak);

    // Act
    const auto failures = RefusalOf(loader, content_pak);

    // Assert
    ASSERT_EQ(failures.size(), 1U);
    EXPECT_EQ(failures.front().failure, RequirementFailure::kTombstoned);
    EXPECT_EQ(failures.front().requirement.key, EngineMaterialKey());
  }

  //! A patch of the engine pak that drops its texture deletes it: the texture
  //! no longer resolves above the patch, and a pak requiring it of `Engine`
  //! is refused there.
  NOLINT_TEST_F(AssetLoaderBasicTest, DroppedTextureIsTombstonedByAPatch)
  {
    // Arrange
    constexpr auto kMeter = std::string_view { "/Engine/Meter.otex" };
    auto textured = LooseCookedRoot { "Engine" };
    static_cast<void>(textured.Material("Default.omat"));
    textured.DescribeTexture(
      "Meter.otex", textured.AddTexture(cooker::testing::TextureBuilder {}));
    auto engine = CookedPak { std::move(textured) };
    engine.Identity(SourceKeyOf(kEngineSeed));
    auto dropped = LooseCookedRoot { "Engine" };
    static_cast<void>(dropped.Material("Default.omat"));
    auto patch = CookedPak { std::move(dropped) };
    patch.PatchOf(engine);
    auto user = LooseCookedRoot { "Content" };
    auto material = cooker::testing::MaterialBuilder { "Gauge" };
    material.ExternalTexture(
      &data::pak::render::MaterialAssetDesc::base_color_texture,
      data::TextureKeyFromVirtualPath(kMeter));
    static_cast<void>(user.Material("Gauge.omat", material));
    auto content = CookedPak { std::move(user) };
    content.DependsOn(engine).Identity(SourceKeyOf(kContentSeed));
    const auto engine_pak = TempDir() / "engine.pak";
    const auto patch_pak = TempDir() / "patch.pak";
    const auto content_pak = TempDir() / "content.pak";
    cooker::testing::Materialize(engine.Encode(), engine_pak);
    cooker::testing::Materialize(patch.Encode(), patch_pak);
    cooker::testing::Materialize(content.Encode(), content_pak);
    AssetLoader loader(
      engine::internal::EngineTagFactory::Get(), EngineConfig(engine_pak));
    const auto held = loader.ResolveTextureResourceKey(kMeter);

    // Act
    loader.AddPakFile(patch_pak);
    const auto deleted = loader.ResolveTextureResourceKey(kMeter);
    const auto failures = RefusalOf(loader, content_pak);

    // Assert
    EXPECT_TRUE(held.has_value());
    EXPECT_FALSE(deleted.has_value());
    ASSERT_EQ(failures.size(), 1U);
    EXPECT_EQ(
      failures.front().requirement.kind, data::KeyReferenceKind::kTexture);
    EXPECT_EQ(failures.front().failure, RequirementFailure::kTombstoned);
  }

  //! An `Engine` whose default material key holds a geometry refuses a pak
  //! that requires a material there, naming the type it found.
  NOLINT_TEST_F(AssetLoaderBasicTest, DependencyOfAnotherTypeRefusesThePak)
  {
    // Arrange
    auto geometry_engine = LooseCookedRoot { "Engine" };
    const auto geometry = cooker::testing::GeometryBuilder { "Default" };
    geometry_engine.Add({
      .type = data::AssetType::kGeometry,
      .leaf = "Default.ogeo",
      .descriptor = geometry.Bytes(),
      .references = geometry.References(),
      .key = EngineMaterialKey(),
    });
    auto engine = CookedPak { std::move(geometry_engine) };
    engine.Identity(SourceKeyOf(kEngineSeed));
    const auto engine_pak = TempDir() / "engine.pak";
    cooker::testing::Materialize(engine.Encode(), engine_pak);
    const auto content_pak = TempDir() / "content.pak";
    cooker::testing::Materialize(ContentPak(EnginePak()).Encode(), content_pak);
    AssetLoader loader(
      engine::internal::EngineTagFactory::Get(), EngineConfig(engine_pak));

    // Act
    const auto failures = RefusalOf(loader, content_pak);

    // Assert
    ASSERT_EQ(failures.size(), 1U);
    EXPECT_EQ(failures.front().failure, RequirementFailure::kTypeMismatch);
    EXPECT_EQ(failures.front().found_type, data::AssetType::kGeometry);
  }

  //! A layer of another mount that holds the required key does not meet a
  //! requirement of `Engine`, and the refusal names the mount it found.
  NOLINT_TEST_F(AssetLoaderBasicTest, DependencyUnderAnotherMountRefusesThePak)
  {
    // Arrange
    auto other = LooseCookedRoot { "Other", kFirstSeed };
    const auto material = cooker::testing::MaterialBuilder { "Default" };
    other.Add({
      .type = data::AssetType::kMaterial,
      .leaf = "Default.omat",
      .descriptor = material.Bytes(),
      .references = material.References(),
      .key = EngineMaterialKey(),
    });
    const auto other_root = TempDir() / "other";
    cooker::testing::Materialize(other.Files(), other_root);
    const auto content_pak = TempDir() / "content.pak";
    cooker::testing::Materialize(ContentPak(EnginePak()).Encode(), content_pak);
    AssetLoader loader(engine::internal::EngineTagFactory::Get());
    loader.AddLooseCookedRoot(other_root);

    // Act
    const auto failures = RefusalOf(loader, content_pak);

    // Assert
    ASSERT_EQ(failures.size(), 1U);
    EXPECT_EQ(failures.front().failure, RequirementFailure::kMountMismatch);
    EXPECT_EQ(failures.front().found_mount, "Other");
  }

  //! Retiring the only generation that meets a mounted pak's requirement is
  //! refused, and the generation stays mounted.
  NOLINT_TEST_F(AssetLoaderBasicTest, RetireKeepsAGenerationAPakRequires)
  {
    // Arrange
    const auto generation = TempDir() / "engine";
    const auto source = WriteEngineGeneration(generation);
    const auto content_pak = TempDir() / "content.pak";
    cooker::testing::Materialize(ContentPak(EnginePak()).Encode(), content_pak);
    AssetLoader loader(engine::internal::EngineTagFactory::Get());
    static_cast<void>(loader.MountLooseCookedGeneration(generation));
    loader.AddPakFile(content_pak);

    // Act
    auto failures = std::vector<FailedRequirement> {};
    try {
      static_cast<void>(loader.RetireLooseCookedGeneration(source));
      ADD_FAILURE() << "The required generation was retired";
    } catch (const DependencyRequirementError& error) {
      failures = error.Failures();
    }

    // Assert
    ASSERT_EQ(failures.size(), 1U);
    EXPECT_EQ(failures.front().failure, RequirementFailure::kMissing);
    EXPECT_EQ(loader.EnumerateMountedSources().size(), 2U);
  }

  //! A generation retires while a lower layer still meets the requirements
  //! of the paks above it.
  NOLINT_TEST_F(AssetLoaderBasicTest, RetireLeavesALowerProvider)
  {
    // Arrange
    const auto engine_pak = TempDir() / "engine.pak";
    cooker::testing::Materialize(EnginePak().Encode(), engine_pak);
    const auto generation = TempDir() / "engine";
    const auto source = WriteEngineGeneration(generation);
    const auto content_pak = TempDir() / "content.pak";
    cooker::testing::Materialize(ContentPak(EnginePak()).Encode(), content_pak);
    AssetLoader loader(
      engine::internal::EngineTagFactory::Get(), EngineConfig(engine_pak));
    static_cast<void>(loader.MountLooseCookedGeneration(generation));
    loader.AddPakFile(content_pak);

    // Act
    const auto retired = loader.RetireLooseCookedGeneration(source);

    // Assert
    EXPECT_TRUE(retired);
    EXPECT_EQ(loader.EnumerateMountedSources().size(), 2U);
  }

  //! Mounts `first`, then `second`, two roots with the SourceKey `source`:
  //! the second is refused, naming both, and the first stays mounted.
  auto ExpectSourceKeyConflict(const std::filesystem::path& first,
    const std::filesystem::path& second, const data::SourceKey& source) -> void
  {
    AssetLoader loader(engine::internal::EngineTagFactory::Get());
    loader.AddLooseCookedRoot(first);

    auto refused = std::optional<SourceKeyConflictError> {};
    try {
      loader.AddLooseCookedRoot(second);
    } catch (const SourceKeyConflictError& error) {
      refused = error;
    }

    ASSERT_HAS_VALUE(refused);
    EXPECT_EQ(refused->GetSourceKey(), source);
    EXPECT_NE(refused->Mount(), refused->ExistingMount());
    const auto mounted = loader.EnumerateMountedSources();
    ASSERT_EQ(mounted.size(), 1U);
    EXPECT_TRUE(
      std::filesystem::equivalent(mounted.front().source_path, first));
  }

  //! A mutable root with the SourceKey of a mounted one is refused.
  NOLINT_TEST_F(AssetLoaderBasicTest, SharedSourceKeyRefusesAMutableRoot)
  {
    const auto first = TempDir() / "first";
    const auto second = TempDir() / "second";
    const auto source = WriteGeneration(
      first, { .material = MaterialKey(), .seed = kFirstSeed });
    static_cast<void>(WriteGeneration(
      second, { .material = MaterialKey(), .seed = kFirstSeed }));
    std::filesystem::remove(
      first / data::loose_cooked::kGenerationLeaseFileName);
    std::filesystem::remove(
      second / data::loose_cooked::kGenerationLeaseFileName);

    ExpectSourceKeyConflict(first, second, source);
  }

  //! A generation with the SourceKey of a mounted one is refused.
  NOLINT_TEST_F(AssetLoaderBasicTest, SharedSourceKeyRefusesAGeneration)
  {
    const auto first = TempDir() / "first";
    const auto second = TempDir() / "second";
    const auto source = WriteGeneration(
      first, { .material = MaterialKey(), .seed = kFirstSeed });
    static_cast<void>(WriteGeneration(
      second, { .material = MaterialKey(), .seed = kFirstSeed }));

    ExpectSourceKeyConflict(first, second, source);
  }

  //! Clearing while the engine pak cannot be reopened fails before anything
  //! is unmounted.
  NOLINT_TEST_F(AssetLoaderBasicTest, FailedClearKeepsTheMounts)
  {
    // Arrange
    const auto engine_pak = TempDir() / "engine.pak";
    cooker::testing::Materialize(EnginePak().Encode(), engine_pak);
    const auto project = TempDir() / "project";
    static_cast<void>(WriteGeneration(project, { .material = MaterialKey() }));
    AssetLoader loader(
      engine::internal::EngineTagFactory::Get(), EngineConfig(engine_pak));
    loader.AddLooseCookedRoot(project);
    std::filesystem::rename(engine_pak, TempDir() / "moved.pak");

    // Act and Assert
    NOLINT_EXPECT_THROW(loader.ClearMounts(), data::CookedFormatError);
    EXPECT_EQ(loader.EnumerateMountedSources().size(), 2U);
  }

  //! A root that holds only the physics sidecar of the scene `target`, whose
  //! descriptor is the test's scene: the scene lives in another source.
  auto SidecarRoot(const data::AssetKey& target, const std::uint8_t seed)
    -> LooseCookedRoot
  {
    auto root = LooseCookedRoot { "test", seed };
    root.Add({
      .type = data::AssetType::kPhysicsScene,
      .leaf = "scene.opscene",
      .descriptor = cooker::testing::PhysicsSidecarBuilder {}.Bytes(
        target, PlainSceneBytes(), "PhysicsScene"),
    });
    return root;
  }

  //! Loads the scenes `keys` in the activated `loader`, then hands them to
  //! `check`, still on the loop, before stopping the loader.
  auto LoadScenesThen(AssetLoader* loader, std::vector<data::AssetKey> keys,
    std::function<void(std::span<const std::shared_ptr<data::SceneAsset>>)>
      check) -> co::Co<>
  {
    OXCO_WITH_NURSERY(nursery)
    {
      co_await nursery.Start(&AssetLoader::ActivateAsync, loader);
      loader->Run();
      auto scenes = std::vector<std::shared_ptr<data::SceneAsset>> {};
      for (const auto& key : keys) {
        scenes.push_back(
          co_await loader->LoadAssetAsync<data::SceneAsset>(key));
        EXPECT_NE(scenes.back(), nullptr) << data::to_string(key);
      }
      check(scenes);
      loader->Stop();
      co_return co::kJoin;
    };
  }

  //! A scene's load binds the sidecar that targets it; a scene no sidecar
  //! targets binds none; another loader's scene is refused.
  NOLINT_TEST_F(AssetLoaderPoolTest, LoadedSceneBindsTheSidecarThatTargetsIt)
  {
    auto content = LooseCookedRoot { "test", kFirstSeed };
    const auto scene_key = content.Scene("generation.oscene");
    const auto plain_key = content.Scene("plain.oscene");
    const auto sidecar_key = content.PhysicsSidecar("scene.opscene", scene_key);
    const auto root = TempDir() / "root";
    cooker::testing::Materialize(content.Files(), root);
    loader_.AddLooseCookedRoot(root);

    co::Run(loop_,
      LoadScenesThen(&loader_, { scene_key, plain_key },
        [&](const std::span<const std::shared_ptr<data::SceneAsset>> scenes)
          -> void {
          if (!scenes[0] || !scenes[1]) {
            return;
          }
          EXPECT_EQ(loader_.FindPhysicsSidecarAssetKeyForScene(*scenes[0]),
            sidecar_key);
          EXPECT_FALSE(
            loader_.FindPhysicsSidecarAssetKeyForScene(*scenes[1]).has_value());
          AssetLoader foreign(engine::internal::EngineTagFactory::Get());
          NOLINT_EXPECT_THROW(
            static_cast<void>(
              foreign.FindPhysicsSidecarAssetKeyForScene(*scenes[0])),
            std::invalid_argument);
        }));
  }

  //! The sidecar is frozen at load: a newer mount whose sidecar of the same key
  //! targets another scene does not change what the loaded scene binds.
  NOLINT_TEST_F(AssetLoaderPoolTest, LoadedSceneKeepsItsSidecarAcrossAMount)
  {
    auto content = LooseCookedRoot { "test", kFirstSeed };
    const auto scene_key = content.Scene("generation.oscene");
    const auto sidecar_key = content.PhysicsSidecar("scene.opscene", scene_key);
    const auto base = TempDir() / "base";
    const auto replacement = TempDir() / "replacement";
    cooker::testing::Materialize(content.Files(), base);
    cooker::testing::Materialize(
      SidecarRoot(
        data::AssetKey::FromVirtualPath("/test/other.oscene"), kSecondSeed)
        .Files(),
      replacement);
    loader_.AddLooseCookedRoot(base);

    co::Run(loop_,
      LoadScenesThen(&loader_, { scene_key },
        [&](const std::span<const std::shared_ptr<data::SceneAsset>> scenes)
          -> void {
          if (!scenes[0]) {
            return;
          }
          loader_.AddLooseCookedRoot(replacement);
          EXPECT_EQ(loader_.FindPhysicsSidecarAssetKeyForScene(*scenes[0]),
            sidecar_key);
        }));
  }

  //! A scene binds one sidecar, so two in its mounts that target it fail its
  //! load.
  NOLINT_TEST_F(AssetLoaderPoolTest, TwoSidecarsForOneSceneFailItsLoad)
  {
    auto content = LooseCookedRoot { "test", kFirstSeed };
    const auto scene_key = content.Scene("generation.oscene");
    static_cast<void>(content.PhysicsSidecar("first.opscene", scene_key));
    static_cast<void>(content.PhysicsSidecar("second.opscene", scene_key));
    const auto root = TempDir() / "root";
    cooker::testing::Materialize(content.Files(), root);
    loader_.AddLooseCookedRoot(root);

    const auto error_run = WhileActive(
      ThrownBy(loader_.LoadAssetAsync<data::SceneAsset>(scene_key)));
    ASSERT_HAS_VALUE(error_run);
    const auto& error = *error_run;

    EXPECT_EQ(LoadFailureOf(error), ContentLoadFailure::kBindingFailed);
  }

  NOLINT_TEST_F(AssetLoaderBasicTest,
    RevocationDuringDependenciesReleasesTemporaryCheckouts)
  {
    TestEventLoop loop;
    co::Run(loop, ExerciseRevokedGeometryDependencies(&loop, TempDir()));
  }

  NOLINT_TEST_F(
    AssetLoaderBasicTest, SameSourceKeyRefreshDoesNotAliasCachedPayloads)
  {
    TestEventLoop loop;
    co::Run(loop, ExerciseMutableRefresh(&loop, TempDir() / "mutable"));
  }

  NOLINT_TEST_F(
    AssetLoaderBasicTest, ExternalDependencyBindingsSurviveRootReplacement)
  {
    TestEventLoop loop;
    co::Run(loop, ExerciseExternalBinding(&loop, TempDir()));
  }

  NOLINT_TEST_F(AssetLoaderBasicTest,
    ReplacementRetainsAnInFlightSourceBeforeDecodeCompletes)
  {
    TestEventLoop loop;
    co::Run(loop, ExerciseInFlightGeneration(&loop, TempDir()));
  }

  NOLINT_TEST_F(
    AssetLoaderBasicTest, RetireDuringALoadCompletesFromTheRetiredGeneration)
  {
    TestEventLoop loop;
    co::Run(loop, ExerciseRetireDuringLoad(&loop, TempDir() / "generation"));
  }

  NOLINT_TEST_F(AssetLoaderBasicTest, ExternalTextureStaysBoundAcrossASwap)
  {
    TestEventLoop loop;
    co::Run(loop, ExerciseFrozenExternalTexture(&loop, TempDir()));
  }

  NOLINT_TEST_F(
    AssetLoaderBasicTest, RetiredGenerationsRetainTheirOwnAssetsAndPayloads)
  {
    TestEventLoop loop;
    co::Run(loop, ExerciseRetainedGeneration(&loop, TempDir()));
  }

} // namespace
} // namespace oxygen::content::testing
