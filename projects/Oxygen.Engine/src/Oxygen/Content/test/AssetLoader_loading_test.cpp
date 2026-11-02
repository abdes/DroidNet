//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "./AssetLoader_test.h"
#include "Fixtures/AssetLoaderSources.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/InputContextHydration.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Cooker/testing/AssetDescriptors.h>
#include <Oxygen/Cooker/testing/CookedBytes.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/OxCo/BroadcastChannel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/OxCo/test/Utils/TestEventLoop.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Testing/GTest.h>

using testing::IsNull;
using testing::NotNull;

using oxygen::observer_ptr;
using oxygen::co::Co;
using oxygen::co::testing::TestEventLoop;
using oxygen::data::InputMappingContextAsset;
using oxygen::data::MaterialAsset;
using oxygen::data::TextureResource;

using oxygen::content::testing::AssetLoaderBasicTest;

namespace {

using oxygen::cooker::testing::LooseCookedRoot;

auto WriteLooseCookedIndexWithInvalidTexturesTable(
  const std::filesystem::path& cooked_root) -> void
{
  auto root = LooseCookedRoot { "test" };
  static_cast<void>(root.AddTexture(oxygen::cooker::testing::SeedBytes(0x7FU)));
  auto files = root.Files();
  oxygen::cooker::testing::CookedBytes::GivePartialRow(
    files, oxygen::data::loose_cooked::FileKind::kTexturesTable);
  oxygen::cooker::testing::Materialize(files, cooked_root);
}

auto WriteLooseCookedSceneForCatalog(const std::filesystem::path& cooked_root,
  const oxygen::data::AssetKey& key) -> void
{
  using oxygen::data::AssetType;

  auto scene = oxygen::cooker::testing::SceneBuilder { "LooseScene" };
  auto root = LooseCookedRoot { "test" };
  root.Add({
    .type = AssetType::kScene,
    .leaf = "Scenes/LooseScene.scene",
    .descriptor = scene.Bytes(),
    .references = scene.References(),
    .key = key,
  });
  oxygen::cooker::testing::Materialize(root.Files(), cooked_root);
}

//! The keys of the input action and mapping context of a loose root.
struct LooseInputKeys final {
  oxygen::data::AssetKey action;
  oxygen::data::AssetKey context;
};

//! Writes into `cooked_root` an input action `Move` and a mapping context that
//! maps it to `Space`; returns their keys.
auto WriteLooseCookedInputAssets(const std::filesystem::path& cooked_root)
  -> LooseInputKeys
{
  using oxygen::data::pak::input::InputActionAssetDesc;
  using oxygen::data::pak::input::InputActionAssetFlags;
  using oxygen::data::pak::input::InputMappingContextAssetDesc;
  using oxygen::data::pak::input::InputMappingContextFlags;
  using oxygen::data::pak::input::InputTriggerRecord;
  using oxygen::data::pak::input::InputTriggerType;

  auto root = LooseCookedRoot { "Game" };
  InputActionAssetDesc action_record {};
  action_record.value_type
    = oxygen::data::pak::input::InputActionValueType::kBool;
  action_record.flags = InputActionAssetFlags::kConsumesInput;
  const auto action_key = root.InputAction("Input/Move.oiact", action_record);

  InputMappingContextAssetDesc context_record {};
  context_record.flags = InputMappingContextFlags::kNone;
  constexpr int32_t kInputPriority = 77;
  context_record.default_priority = kInputPriority;

  InputTriggerRecord trigger {};
  trigger.type = InputTriggerType::kPressed;
  trigger.actuation_threshold = 0.5F;

  auto context
    = oxygen::cooker::testing::InputContextBuilder { "HydratedContext",
        context_record };
  context.Mapping(action_key, "Space", { trigger });

  const auto context_key = root.InputContext("Input/Hydrated.oimap", context);
  oxygen::cooker::testing::Materialize(root.Files(), cooked_root);
  return { .action = action_key, .context = context_key };
}

//=== AssetLoader Basic Functionality Tests ===-----------------------------//

//! Duplicate AssetKey conflict policy: newest mount wins by default.

//! Patch tombstones must block fallback to lower-priority base mounts.

//! Characterization: duplicate-key resolution follows mount order.
/*!
 Scenario: Mount two sources that define the same Material AssetKey and
 assert lookup result changes when mount order is reversed. This captures
 current behavior where explicit load priority metadata is not part of the
 runtime load API surface.
*/

//! Preferred-source override policy: dependency loads follow the parent source.

NOLINT_TEST_F(AssetLoaderBasicTest,
  EnumerateMountedInputContextsLooseCookedExpectedToExposeEntries)
{
  const auto cooked_root = TempDir() / "mounted_input_contexts_loose";
  const auto [action_key, context_key]
    = WriteLooseCookedInputAssets(cooked_root);

  asset_loader_->AddLooseCookedRoot(cooked_root);

  const auto mounted_contexts = asset_loader_->EnumerateMountedInputContexts();
  ASSERT_FALSE(mounted_contexts.empty());

  const auto found = std::ranges::find_if(mounted_contexts,
    [&](const auto& entry) -> auto { return entry.asset_key == context_key; });
  ASSERT_NE(found, mounted_contexts.end());
  EXPECT_EQ(found->name, "HydratedContext");
  EXPECT_EQ(found->default_priority, 77);
  EXPECT_EQ(
    found->flags, oxygen::data::pak::input::InputMappingContextFlags::kNone);
}

NOLINT_TEST_F(AssetLoaderBasicTest,
  HydrateInputContextExpectedToRegisterActionsAndBuildMappingContext)
{
  const auto cooked_root = TempDir() / "hydrate_input_context_loose";
  const auto [action_key, context_key]
    = WriteLooseCookedInputAssets(cooked_root);

  struct InputKeys {
    oxygen::data::AssetKey action;
    oxygen::data::AssetKey context;
  };
  TestEventLoop el;
  oxygen::co::Run(el,
    [](InputKeys keys, std::filesystem::path cooked_root,
      TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
      using oxygen::content::AssetLoader;
      using oxygen::content::AssetLoaderConfig;

      oxygen::co::ThreadPool pool(el, 2);
      AssetLoaderConfig config {};
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
      AssetLoader loader(Tag::Get(), config);

      oxygen::co::BroadcastChannel<oxygen::platform::InputEvent> input_channel;
      oxygen::engine::InputSystem input_system(input_channel.ForRead());

      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();

        loader.AddLooseCookedRoot(cooked_root);

        const auto context_asset
          = co_await loader.LoadAssetAsync<InputMappingContextAsset>(
            keys.context);
        EXPECT_THAT(context_asset, NotNull());
        EXPECT_THAT(loader.GetInputActionAsset(keys.action), NotNull());

        if (context_asset) {
          const auto hydrated = oxygen::content::HydrateInputContext(
            *context_asset, loader, input_system);
          EXPECT_THAT(hydrated, NotNull());
          if (hydrated) {
            EXPECT_EQ(hydrated->GetName(), "HydratedContext");
            EXPECT_EQ(hydrated->GetAssetKey(), keys.context);
          }

          const auto action = input_system.GetActionByKey(keys.action);
          EXPECT_THAT(action, NotNull());
          if (action) {
            EXPECT_EQ(action->GetName(), "Move");
            EXPECT_TRUE(action->ConsumesInput());
          }
        }

        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(InputKeys { .action = action_key, .context = context_key }, cooked_root,
                             &el));
}

//! StartLoadInputMappingContextAsset loads a context with the actions it maps
//! and reports it to the callback.
NOLINT_TEST_F(
  AssetLoaderBasicTest, StartLoadInputMappingContextAssetLoadsContextAndActions)
{
  const auto cooked_root = TempDir() / "start_load_input_context_loose";
  const auto [action_key, context_key]
    = WriteLooseCookedInputAssets(cooked_root);

  struct InputKeys {
    oxygen::data::AssetKey action;
    oxygen::data::AssetKey context;
  };
  TestEventLoop el;
  oxygen::co::Run(el,
    [](InputKeys keys, std::filesystem::path cooked_root,
      TestEventLoop* loop) -> Co<> {
      using oxygen::content::AssetLoader;
      using oxygen::content::AssetLoaderConfig;

      oxygen::co::ThreadPool pool(*loop, 2);
      AssetLoaderConfig config {};
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
      AssetLoader loader(Tag::Get(), config);

      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();
        loader.AddLooseCookedRoot(cooked_root);

        auto loaded = std::shared_ptr<InputMappingContextAsset> {};
        oxygen::co::Event done;
        loader.StartLoadInputMappingContextAsset(keys.context,
          [&loaded, &done](
            std::shared_ptr<InputMappingContextAsset> asset) -> void {
            loaded = std::move(asset);
            done.Trigger();
          });
        co_await done;

        EXPECT_THAT(loaded, NotNull());
        if (loaded) {
          EXPECT_EQ(loaded->GetAssetKey(), keys.context);
        }
        EXPECT_THAT(loader.GetInputActionAsset(keys.action), NotNull());

        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(InputKeys { .action = action_key, .context = context_key }, cooked_root,
                             &el));
}

NOLINT_TEST_F(AssetLoaderBasicTest,
  TrimCacheExpectedToPreserveMountedCatalogClearMountsExpectedToClearCatalog)
{
  const auto loose_scene_key = oxygen::data::AssetKey::FromVirtualPath(
    "/test/loose_scene_catalog_2.asset");
  const auto cooked_root = TempDir() / "mounted_scenes_loose_2";
  WriteLooseCookedSceneForCatalog(cooked_root, loose_scene_key);

  asset_loader_->AddLooseCookedRoot(cooked_root);

  const auto sources_before = asset_loader_->EnumerateMountedSources();
  const auto scenes_before = asset_loader_->EnumerateMountedScenes();
  ASSERT_FALSE(sources_before.empty());
  ASSERT_FALSE(scenes_before.empty());

  asset_loader_->TrimCache();

  const auto sources_after_trim = asset_loader_->EnumerateMountedSources();
  const auto scenes_after_trim = asset_loader_->EnumerateMountedScenes();
  EXPECT_EQ(sources_after_trim.size(), sources_before.size());
  EXPECT_EQ(scenes_after_trim.size(), scenes_before.size());

  asset_loader_->ClearMounts();
  EXPECT_TRUE(asset_loader_->EnumerateMountedSources().empty());
  EXPECT_TRUE(asset_loader_->EnumerateMountedScenes().empty());
}

NOLINT_TEST_F(
  AssetLoaderBasicTest, LoadAssetLooseCookedMaterialLoadsWithTexture)
{
  // Arrange
  const auto cooked_root = TempDir() / "loose_cooked";
  const auto material_key
    = oxygen::data::AssetKey::FromVirtualPath("/test/loose_material.asset");
  oxygen::content::testing::WriteTexturedMaterialSource(
    cooked_root, material_key);

  TestEventLoop el;

  // Act + Assert
  oxygen::co::Run(el,
    [](std::filesystem::path cooked_root, oxygen::data::AssetKey material_key,
      TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
      using oxygen::content::AssetLoader;
      using oxygen::content::AssetLoaderConfig;

      oxygen::co::ThreadPool pool(el, 2);
      AssetLoaderConfig config {};
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
      AssetLoader loader(Tag::Get(), config);

      loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
      loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);

      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();

        loader.AddLooseCookedRoot(cooked_root);

        const auto material
          = co_await loader.LoadAssetAsync<MaterialAsset>(material_key);
        EXPECT_THAT(material, NotNull());

        if (material) {
          // Both slots name one texture, bound concurrently: each slot sees
          // the bound texture.
          const auto base_color_key = material->GetBaseColorTextureKey();
          EXPECT_NE(base_color_key.get(), 0U);
          EXPECT_EQ(material->GetNormalTextureKey(), base_color_key);
          EXPECT_THAT(
            loader.GetResource<TextureResource>(base_color_key), NotNull());
        }

        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(cooked_root, material_key, &el));
}

NOLINT_TEST_F(
  AssetLoaderBasicTest, LoadAssetLooseCookedMultipleRootsAssignsStableIds)
{
  // Arrange
  const auto cooked_root_a = TempDir() / "loose_cooked_a";
  const auto cooked_root_b = TempDir() / "loose_cooked_b";

  const auto material_key_a
    = oxygen::data::AssetKey::FromVirtualPath("/test/loose_material_a.asset");
  const auto material_key_b
    = oxygen::data::AssetKey::FromVirtualPath("/test/loose_material_b.asset");

  oxygen::content::testing::WriteTexturedMaterialSource(
    cooked_root_a, material_key_a);
  oxygen::content::testing::WriteTexturedMaterialSource(
    cooked_root_b, material_key_b);

  TestEventLoop el;

  // Act + Assert
  oxygen::co::Run(el,
    [](std::filesystem::path cooked_root_a, std::filesystem::path cooked_root_b,
      oxygen::data::AssetKey material_key_a,
      oxygen::data::AssetKey material_key_b, TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
      using oxygen::content::AssetLoader;
      using oxygen::content::AssetLoaderConfig;

      oxygen::co::ThreadPool pool(el, 2);
      AssetLoaderConfig config {};
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
      AssetLoader loader(Tag::Get(), config);

      loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
      loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);

      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();

        loader.AddLooseCookedRoot(cooked_root_a);
        loader.AddLooseCookedRoot(cooked_root_b);

        const auto material_a
          = co_await loader.LoadAssetAsync<MaterialAsset>(material_key_a);
        const auto material_b
          = co_await loader.LoadAssetAsync<MaterialAsset>(material_key_b);

        EXPECT_THAT(material_a, NotNull());
        EXPECT_THAT(material_b, NotNull());

        if (material_a && material_b) {
          const auto tex_key_a = material_a->GetBaseColorTextureKey();
          const auto tex_key_b = material_b->GetBaseColorTextureKey();

          EXPECT_NE(tex_key_a.get(), 0U);
          EXPECT_NE(tex_key_b.get(), 0U);
          EXPECT_NE(tex_key_a.get(), tex_key_b.get());

          EXPECT_THAT(
            loader.GetResource<TextureResource>(tex_key_a), NotNull());
          EXPECT_THAT(
            loader.GetResource<TextureResource>(tex_key_b), NotNull());
        }

        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(cooked_root_a, cooked_root_b, material_key_a, material_key_b, &el));
}

NOLINT_TEST_F(AssetLoaderBasicTest, AddLooseCookedRootInvalidTexturesTable)
{
  // Arrange
  const auto cooked_root = TempDir() / "loose_cooked_invalid_tex_table";
  WriteLooseCookedIndexWithInvalidTexturesTable(cooked_root);

  // Act & Assert
  NOLINT_EXPECT_THROW(
    { asset_loader_->AddLooseCookedRoot(cooked_root); },
    oxygen::data::CookedOpenError);
}

NOLINT_TEST_F(AssetLoaderBasicTest, LoadAssetNonExistentReturnsNull)
{
  // Arrange
  const auto non_existent_key
    = oxygen::data::AssetKey::FromVirtualPath("/test/non_existent_asset.asset");

  TestEventLoop el;

  // Act + Assert
  oxygen::co::Run(el,
    [](oxygen::data::AssetKey non_existent_key, TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
      using oxygen::content::AssetLoader;
      using oxygen::content::AssetLoaderConfig;

      oxygen::co::ThreadPool pool(el, 2);
      AssetLoaderConfig config {};
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
      AssetLoader loader(Tag::Get(), config);

      loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
      loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);

      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();

        const auto result
          = co_await loader.LoadAssetAsync<MaterialAsset>(non_existent_key);
        EXPECT_THAT(result, IsNull());

        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(non_existent_key, &el));
}

} // namespace
