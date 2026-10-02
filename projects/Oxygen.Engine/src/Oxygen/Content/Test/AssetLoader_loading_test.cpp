//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "./AssetLoader_test.h"
#include "Fixtures/AssetLoaderSources.h"
#include "Fixtures/LooseCookedTestLayout.h"
#include "Fixtures/LooseCookedTestWriter.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/InputContextHydration.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/OxCo/BroadcastChannel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Testing/GTest.h>

using testing::IsNull;
using testing::NotNull;

using oxygen::observer_ptr;
using oxygen::co::Co;
using oxygen::co::testing::TestEventLoop;
using oxygen::data::BufferResource;
using oxygen::data::GeometryAsset;
using oxygen::data::InputMappingContextAsset;
using oxygen::data::MaterialAsset;
using oxygen::data::TextureResource;

using oxygen::content::testing::AssetLoaderBasicTest;

namespace {

using oxygen::content::testing::LooseCookedLayout;

auto WriteLooseCookedIndexWithInvalidTexturesTable(
  const std::filesystem::path& cooked_root) -> void
{
  using oxygen::data::loose_cooked::FileKind;
  const LooseCookedLayout layout {};
  oxygen::content::testing::LooseCookedTestWriter writer(cooked_root);
  const auto malformed_table = std::array { std::byte { 0x7f } };
  writer.WriteFile(
    FileKind::kTexturesTable, layout.TexturesTableRelPath(), malformed_table);
  writer.WriteFile(FileKind::kTexturesData, layout.TexturesDataRelPath(), {});
  static_cast<void>(writer.Finish());
}

auto WriteLooseCookedSceneForCatalog(const std::filesystem::path& cooked_root,
  const oxygen::data::AssetKey& key) -> void
{
  using oxygen::data::AssetType;
  using oxygen::data::pak::world::SceneAssetDesc;

  const LooseCookedLayout layout {};

  SceneAssetDesc desc {};
  desc.header.asset_type = static_cast<uint8_t>(AssetType::kScene);
  std::ranges::copy(
    std::string_view { "LooseScene" }, std::begin(desc.header.name));
  desc.header.version = oxygen::data::pak::world::kSceneAssetVersion;

  const auto rel_desc
    = std::filesystem::path(layout.scenes_subdir) / "LooseScene.scene";

  oxygen::content::testing::LooseCookedTestWriter writer(cooked_root);
  writer.WriteAssetDescriptor(key, AssetType::kScene,
    std::string(layout.virtual_mount_root) + "/" + rel_desc.generic_string(),
    rel_desc.generic_string(), std::as_bytes(std::span { &desc, 1U }));
  static_cast<void>(writer.Finish());
}

auto WriteLooseCookedInputAssets(const std::filesystem::path& cooked_root,
  const oxygen::data::AssetKey& action_key,
  const oxygen::data::AssetKey& context_key) -> void
{
  using oxygen::data::AssetType;
  using oxygen::data::pak::input::InputActionAssetDesc;
  using oxygen::data::pak::input::InputActionAssetFlags;
  using oxygen::data::pak::input::InputActionMappingRecord;
  using oxygen::data::pak::input::InputMappingContextAssetDesc;
  using oxygen::data::pak::input::InputMappingContextFlags;
  using oxygen::data::pak::input::InputTriggerRecord;
  using oxygen::data::pak::input::InputTriggerType;

  const auto action_rel_desc = std::filesystem::path("Input") / "Move.oiact";
  InputActionAssetDesc action_desc {};
  action_desc.header.asset_type = static_cast<uint8_t>(AssetType::kInputAction);
  std::ranges::copy(
    std::string_view { "Move" }, std::begin(action_desc.header.name));
  action_desc.header.version
    = oxygen::data::pak::input::kInputActionAssetVersion;
  action_desc.value_type = 0;
  action_desc.flags = InputActionAssetFlags::kConsumesInput;

  const auto context_rel_desc
    = std::filesystem::path("Input") / "Hydrated.oimap";
  InputMappingContextAssetDesc context_desc {};
  context_desc.header.asset_type
    = static_cast<uint8_t>(AssetType::kInputMappingContext);
  std::ranges::copy(std::string_view { "HydratedContext" },
    std::begin(context_desc.header.name));
  context_desc.header.version
    = oxygen::data::pak::input::kInputMappingContextAssetVersion;
  context_desc.flags = InputMappingContextFlags::kAutoLoad
    | InputMappingContextFlags::kAutoActivate;
  constexpr int32_t kInputPriority = 77;
  context_desc.default_priority = kInputPriority;

  constexpr auto kSlotName = std::to_array("Space");
  const auto strings_size = static_cast<uint32_t>(sizeof(kSlotName));
  context_desc.mappings.offset = sizeof(InputMappingContextAssetDesc);
  context_desc.mappings.count = 1;
  context_desc.mappings.entry_size = sizeof(InputActionMappingRecord);
  context_desc.triggers.offset
    = context_desc.mappings.offset + sizeof(InputActionMappingRecord);
  context_desc.triggers.count = 1;
  context_desc.triggers.entry_size = sizeof(InputTriggerRecord);
  context_desc.trigger_aux.offset
    = context_desc.triggers.offset + sizeof(InputTriggerRecord);
  context_desc.trigger_aux.count = 0;
  context_desc.trigger_aux.entry_size
    = sizeof(oxygen::data::pak::input::InputTriggerAuxRecord);
  context_desc.strings.offset = context_desc.trigger_aux.offset;
  context_desc.strings.count = strings_size;
  context_desc.strings.entry_size = sizeof(char);

  InputActionMappingRecord mapping {};
  mapping.action_asset_key = action_key;
  mapping.slot_name_offset = 0;
  mapping.trigger_start_index = 0;
  mapping.trigger_count = 1;
  mapping.scale[0] = 1.0F;
  mapping.scale[1] = 1.0F;
  mapping.bias[0] = 0.0F;
  mapping.bias[1] = 0.0F;

  InputTriggerRecord trigger {};
  trigger.type = InputTriggerType::kPressed;
  trigger.actuation_threshold = 0.5F;

  const auto context_blob_size
    = static_cast<size_t>(context_desc.strings.offset)
    + static_cast<size_t>(context_desc.strings.count);
  std::vector<std::byte> context_blob(context_blob_size, std::byte { 0 });
  std::memcpy(context_blob.data(), &context_desc, sizeof(context_desc));
  std::memcpy(
    std::span(context_blob).subspan(context_desc.mappings.offset).data(),
    &mapping, sizeof(mapping));
  std::memcpy(
    std::span(context_blob).subspan(context_desc.triggers.offset).data(),
    &trigger, sizeof(trigger));
  std::memcpy(
    std::span(context_blob).subspan(context_desc.strings.offset).data(),
    kSlotName.data(), kSlotName.size());

  oxygen::content::testing::LooseCookedTestWriter writer(cooked_root);
  writer.WriteAssetDescriptor(action_key, AssetType::kInputAction,
    "/Game/" + action_rel_desc.generic_string(),
    action_rel_desc.generic_string(),
    std::as_bytes(std::span { &action_desc, 1U }));
  writer.WriteAssetDescriptor(context_key, AssetType::kInputMappingContext,
    "/Game/" + context_rel_desc.generic_string(),
    context_rel_desc.generic_string(), context_blob,
    oxygen::data::AssetReferences::Create({},
      {
        {
          .key = action_key,
          .kind = oxygen::data::KeyReferenceKind::kAsset,
          .expected_type = AssetType::kInputAction,
        },
      })
      .value());
  static_cast<void>(writer.Finish());
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
  const auto action_key = oxygen::data::AssetKey::FromVirtualPath(
    "/Test/loose_input_action_catalog.asset");
  const auto context_key = oxygen::data::AssetKey::FromVirtualPath(
    "/Test/loose_input_context_catalog.asset");
  const auto cooked_root = temp_dir_ / "mounted_input_contexts_loose";
  WriteLooseCookedInputAssets(cooked_root, action_key, context_key);

  asset_loader_->AddLooseCookedRoot(cooked_root);

  const auto mounted_contexts = asset_loader_->EnumerateMountedInputContexts();
  ASSERT_FALSE(mounted_contexts.empty());

  const auto found = std::ranges::find_if(mounted_contexts,
    [&](const auto& entry) -> auto { return entry.asset_key == context_key; });
  ASSERT_NE(found, mounted_contexts.end());
  EXPECT_EQ(found->name, "HydratedContext");
  EXPECT_EQ(found->default_priority, 77);
  EXPECT_EQ((found->flags
              & oxygen::data::pak::input::InputMappingContextFlags::kAutoLoad),
    oxygen::data::pak::input::InputMappingContextFlags::kAutoLoad);
  EXPECT_EQ(
    (found->flags
      & oxygen::data::pak::input::InputMappingContextFlags::kAutoActivate),
    oxygen::data::pak::input::InputMappingContextFlags::kAutoActivate);
}

NOLINT_TEST_F(AssetLoaderBasicTest,
  HydrateInputContextExpectedToRegisterActionsAndBuildMappingContext)
{
  const auto action_key = oxygen::data::AssetKey::FromVirtualPath(
    "/Test/loose_input_action_hydrate.asset");
  const auto context_key = oxygen::data::AssetKey::FromVirtualPath(
    "/Test/loose_input_context_hydrate.asset");
  const auto cooked_root = temp_dir_ / "hydrate_input_context_loose";
  WriteLooseCookedInputAssets(cooked_root, action_key, context_key);

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

      OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
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
          }

          const auto action = input_system.GetActionByName("Move");
          EXPECT_THAT(action, NotNull());
          if (action) {
            EXPECT_TRUE(action->ConsumesInput());
          }
        }

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
    "/Test/loose_scene_catalog_2.asset");
  const auto cooked_root = temp_dir_ / "mounted_scenes_loose_2";
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
  const auto cooked_root = temp_dir_ / "loose_cooked";
  const auto material_key
    = oxygen::data::AssetKey::FromVirtualPath("/Test/loose_material.asset");
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

      OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();

        loader.AddLooseCookedRoot(cooked_root);

        const auto material
          = co_await loader.LoadAssetAsync<MaterialAsset>(material_key);
        EXPECT_THAT(material, NotNull());

        if (material) {
          const auto base_color_key = material->GetBaseColorTextureKey();
          EXPECT_NE(base_color_key.get(), 0U);
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
  const auto cooked_root_a = temp_dir_ / "loose_cooked_a";
  const auto cooked_root_b = temp_dir_ / "loose_cooked_b";

  const auto material_key_a
    = oxygen::data::AssetKey::FromVirtualPath("/Test/loose_material_a.asset");
  const auto material_key_b
    = oxygen::data::AssetKey::FromVirtualPath("/Test/loose_material_b.asset");

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

      OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
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
  const auto cooked_root = temp_dir_ / "loose_cooked_invalid_tex_table";
  WriteLooseCookedIndexWithInvalidTexturesTable(cooked_root);

  // Act & Assert
  NOLINT_EXPECT_THROW(
    { asset_loader_->AddLooseCookedRoot(cooked_root); }, std::runtime_error);
}

NOLINT_TEST_F(AssetLoaderBasicTest, LoadAssetNonExistentReturnsNull)
{
  // Arrange
  const auto non_existent_key
    = oxygen::data::AssetKey::FromVirtualPath("/Test/non_existent_asset.asset");

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

      OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
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
