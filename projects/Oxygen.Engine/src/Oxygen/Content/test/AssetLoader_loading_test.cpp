//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

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
#include <Oxygen/Content/test/Fakes/StubAssetLoader.h>
#include <Oxygen/Cooker/testing/AssetDescriptors.h>
#include <Oxygen/Cooker/testing/CookedBytes.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/InputActionAsset.h>
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
using oxygen::content::testing::AssetLoaderPoolTest;

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

//! The keys of the input action and mapping context of a loose root.
struct LooseInputKeys final {
  oxygen::data::AssetKey action;
  oxygen::data::AssetKey context;
};

//! The mapping context a loose root holds: its name and default priority,
//! and the seed of the root's source key, distinct for roots mounted together.
struct LooseInputContext final {
  std::string_view name { "HydratedContext" };
  int32_t priority { 77 };
  uint8_t seed { 1U };
};

//! Writes into `cooked_root` an input action `Move` and the mapping context
//! `context`, which maps it to `Space`; returns their keys.
auto WriteLooseCookedInputAssets(const std::filesystem::path& cooked_root,
  const LooseInputContext& context_spec = {}) -> LooseInputKeys
{
  using oxygen::data::pak::input::InputActionAssetDesc;
  using oxygen::data::pak::input::InputActionAssetFlags;
  using oxygen::data::pak::input::InputMappingContextAssetDesc;
  using oxygen::data::pak::input::InputMappingContextFlags;
  using oxygen::data::pak::input::InputTriggerRecord;
  using oxygen::data::pak::input::InputTriggerType;

  auto root = LooseCookedRoot { "Game", context_spec.seed };
  InputActionAssetDesc action_record {};
  action_record.value_type
    = oxygen::data::pak::input::InputActionValueType::kBool;
  action_record.flags = InputActionAssetFlags::kConsumesInput;
  const auto action_key = root.InputAction("Input/Move.oiact", action_record);

  InputMappingContextAssetDesc context_record {};
  context_record.flags = InputMappingContextFlags::kNone;
  context_record.default_priority = context_spec.priority;

  InputTriggerRecord trigger {};
  trigger.type = InputTriggerType::kPressed;
  trigger.actuation_threshold = 0.5F;

  auto context
    = oxygen::cooker::testing::InputContextBuilder { context_spec.name,
        context_record };
  context.Mapping(action_key, "Space", { trigger });

  const auto context_key = root.InputContext("Input/Hydrated.oimap", context);
  oxygen::cooker::testing::Materialize(root.Files(), cooked_root);
  return { .action = action_key, .context = context_key };
}

//=== AssetLoader Basic Functionality Tests ===-----------------------------//

//! A mounted context lists the name, priority and flags its descriptor holds.
NOLINT_TEST_F(AssetLoaderPoolTest, MountedInputContextsListTheirDescriptors)
{
  const auto cooked_root = TempDir() / "input_contexts";
  const auto keys = WriteLooseCookedInputAssets(
    cooked_root, { .name = "Gameplay", .priority = 77 });
  loader_.AddLooseCookedRoot(cooked_root);

  const auto contexts
    = oxygen::co::Run(loop_, loader_.EnumerateMountedInputContextsAsync());

  ASSERT_EQ(contexts.size(), 1U);
  const auto& context = contexts.front();
  EXPECT_EQ(context.asset_key, keys.context);
  EXPECT_EQ(context.name, "Gameplay");
  EXPECT_EQ(context.default_priority, 77);
  EXPECT_EQ(
    context.flags, oxygen::data::pak::input::InputMappingContextFlags::kNone);
}

//! A context a newer mount also holds is listed once, as the newer one.
NOLINT_TEST_F(AssetLoaderPoolTest, MountedInputContextsFollowMountPrecedence)
{
  const auto older = TempDir() / "older";
  const auto newer = TempDir() / "newer";
  const auto keys = WriteLooseCookedInputAssets(
    older, { .name = "Older", .priority = 1, .seed = 1U });
  ASSERT_EQ(WriteLooseCookedInputAssets(
              newer, { .name = "Newer", .priority = 2, .seed = 2U })
              .context,
    keys.context);
  loader_.AddLooseCookedRoot(older);
  loader_.AddLooseCookedRoot(newer);

  const auto contexts
    = oxygen::co::Run(loop_, loader_.EnumerateMountedInputContextsAsync());

  ASSERT_EQ(contexts.size(), 1U);
  EXPECT_EQ(contexts.front().name, "Newer");
  EXPECT_EQ(contexts.front().default_priority, 2);
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

//! A hydration takes the action assets of one loader but those it keeps back.
class PartialActionLoader final
  : public oxygen::content::testing::StubAssetLoader {
public:
  using StubAssetLoader::GetInputActionAsset;

  PartialActionLoader(oxygen::content::IAssetLoader& loader,
    const oxygen::data::AssetKey resolvable)
    : loader_(&loader)
    , resolvable_(resolvable)
  {
  }

  auto GetInputActionAsset(
    const oxygen::data::AssetKey& key, const oxygen::data::Asset& context)
    -> std::shared_ptr<oxygen::data::InputActionAsset> override
  {
    return key == resolvable_ ? loader_->GetInputActionAsset(key, context)
                              : nullptr;
  }

private:
  oxygen::observer_ptr<oxygen::content::IAssetLoader> loader_;
  oxygen::data::AssetKey resolvable_;
};

//! A context builds all or nothing: when the second action it maps does not
//! resolve, hydration returns null and the input system holds neither action
//! nor the context, though the first action resolved.
NOLINT_TEST_F(
  AssetLoaderPoolTest, HydrateInputContextWithUnresolvedActionRegistersNothing)
{
  const auto cooked_root = TempDir() / "hydrate_unresolved_action";
  auto root = LooseCookedRoot { "Game" };
  const auto first = root.InputAction("Input/First.oiact");
  const auto second = root.InputAction("Input/Second.oiact");
  auto context = oxygen::cooker::testing::InputContextBuilder { "Pair" };
  context.Mapping(first, "Space").Mapping(second, "W");
  const auto context_key = root.InputContext("Input/Pair.oimap", context);
  oxygen::cooker::testing::Materialize(root.Files(), cooked_root);
  loader_.AddLooseCookedRoot(cooked_root);
  oxygen::co::BroadcastChannel<oxygen::platform::InputEvent> input_channel;
  oxygen::engine::InputSystem input_system(input_channel.ForRead());

  const auto hydrated_run = WhileActive(
    [&] -> Co<std::shared_ptr<oxygen::input::InputMappingContext>> {
      const auto asset
        = co_await loader_.LoadAssetAsync<InputMappingContextAsset>(
          context_key);
      if (!asset) {
        ADD_FAILURE() << "the context did not load";
        co_return nullptr;
      }
      auto partial = PartialActionLoader { loader_, first };
      co_return oxygen::content::HydrateInputContext(
        *asset, partial, input_system);
    }());
  ASSERT_HAS_VALUE(hydrated_run);
  const auto& hydrated = *hydrated_run;

  EXPECT_THAT(hydrated, IsNull());
  EXPECT_THAT(input_system.GetActionByKey(first), IsNull());
  EXPECT_THAT(input_system.GetActionByKey(second), IsNull());
  EXPECT_THAT(input_system.GetMappingContextByKey(context_key), IsNull());
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

//! The scene catalog lists each mounted scene by its key, source and virtual
//! path; a cache trim keeps it and clearing the mounts empties it.
NOLINT_TEST_F(AssetLoaderBasicTest, SceneCatalogFollowsTheMountsNotTheCache)
{
  constexpr auto kLeaf = std::string_view { "Scenes/Loose.oscene" };
  auto content = LooseCookedRoot { "test" };
  const auto scene_key = content.Scene(kLeaf);
  const auto cooked_root = TempDir() / "scene_catalog";
  oxygen::cooker::testing::Materialize(content.Files(), cooked_root);
  asset_loader_->AddLooseCookedRoot(cooked_root);

  const auto scenes = asset_loader_->EnumerateMountedScenes();

  ASSERT_EQ(scenes.size(), 1U);
  EXPECT_EQ(scenes.front().scene_key, scene_key);
  EXPECT_EQ(scenes.front().virtual_path, content.VirtualPath(kLeaf));
  EXPECT_EQ(scenes.front().source_key, content.SourceKey());
  asset_loader_->TrimCache();
  EXPECT_EQ(asset_loader_->EnumerateMountedScenes().size(), 1U);
  asset_loader_->ClearMounts();
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
    oxygen::data::CookedFormatError);
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
