//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "./AssetLoader_test.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Content/Loaders/ScriptLoader.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Cooker/testing/ScriptBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/OxCo/test/Utils/TestEventLoop.h>
#include <Oxygen/Testing/GTest.h>

using ::testing::NotNull;

using oxygen::observer_ptr;
using oxygen::co::Co;
using oxygen::co::testing::TestEventLoop;
using oxygen::content::AssetLoader;
using oxygen::content::AssetLoaderConfig;
using oxygen::content::testing::AssetLoaderBasicTest;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::data::SceneAsset;
using oxygen::data::ScriptAsset;
using oxygen::data::ScriptResource;

namespace {

auto WriteLooseCookedScriptAsset(const std::filesystem::path& cooked_root,
  const oxygen::data::AssetKey& script_key,
  const oxygen::data::ScriptParameters& parameters = {}) -> void
{
  using oxygen::data::AssetType;
  using oxygen::data::pak::scripting::ScriptResourceDesc;

  constexpr std::array<uint8_t, 8> kBytecode
    = { 0x4D, 0x4F, 0x56, 0x45, 0x42, 0x43, 0xA1, 0xA2 };

  // The root records the payload's SHA-256 in its row.
  auto root = LooseCookedRoot { "test" };
  const auto bytecode = root.AddScript(
    std::as_bytes(std::span { kBytecode }), ScriptResourceDesc {});
  auto script = oxygen::cooker::testing::ScriptBuilder { "Reload" };
  script.Bytecode(bytecode).Parameters(parameters);
  root.Add({
    .type = AssetType::kScript,
    .leaf = "Scripts/Reload.oscript",
    .descriptor = script.Bytes(),
    .references = script.References(),
    .key = script_key,
  });
  oxygen::cooker::testing::Materialize(root.Files(), cooked_root);
}

//! Key of the script an asset parameter of the scripted scene names.
constexpr int32_t kSlotExecutionOrder = 3;

auto ParamScriptKey() -> oxygen::data::AssetKey
{
  return oxygen::data::AssetKey::FromVirtualPath("/test/param-script.oscript");
}

auto WriteLooseCookedSceneWithScripting(
  const std::filesystem::path& cooked_root,
  const oxygen::data::AssetKey& scene_key, const bool with_asset_param = false)
  -> void
{
  using oxygen::cooker::testing::ResourceTable;
  using oxygen::cooker::testing::ScriptAssetValue;
  using oxygen::cooker::testing::ScriptParam;
  using oxygen::data::AssetType;

  const auto script_key
    = oxygen::data::AssetKey::FromVirtualPath("/test/scene-script.oscript");
  auto params = std::vector<ScriptParam> {};
  if (with_asset_param) {
    params.push_back({
      .key = "helper",
      .value = ScriptAssetValue {
        .key = ParamScriptKey(),
        .type = AssetType::kScript,
      },
    });
  }
  auto scene = oxygen::cooker::testing::SceneBuilder { "test" };
  const auto node = scene.Node("root");
  scene.ScriptSlot(node, script_key, std::move(params), kSlotExecutionOrder);

  auto root = LooseCookedRoot { "test" };
  root.Add({
    .type = AssetType::kScene,
    .leaf = "Scenes/ScriptScene.scene",
    .descriptor = scene.Bytes(),
    .references = scene.References(),
    .key = scene_key,
  });
  const auto bare = oxygen::cooker::testing::ScriptBuilder { "script" };
  root.Add({
    .type = AssetType::kScript,
    .leaf = "scene-script.oscript",
    .descriptor = bare.Bytes(),
    .references = bare.References(),
    .key = script_key,
  });
  if (with_asset_param) {
    root.Add({
      .type = AssetType::kScript,
      .leaf = "param-script.oscript",
      .descriptor = bare.Bytes(),
      .references = bare.References(),
      .key = ParamScriptKey(),
    });
  }
  root.AddEmptyTable(ResourceTable::kScripts);
  oxygen::cooker::testing::Materialize(root.Files(), cooked_root);
}

class AssetLoaderScriptingTest : public AssetLoaderBasicTest { };

NOLINT_TEST_F(AssetLoaderScriptingTest,
  LoadAssetLooseCookedScriptResourceGenericLoadExpectedToSucceed)
{
  const auto script_key
    = oxygen::data::AssetKey::FromVirtualPath("/test/Reload.oscript");
  const auto cooked_root = TempDir() / "loose_script";
  WriteLooseCookedScriptAsset(cooked_root, script_key);

  TestEventLoop el;
  oxygen::co::Run(el,
    [](oxygen::data::AssetKey script_key, std::filesystem::path cooked_root,
      TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
      oxygen::co::ThreadPool pool(el, 2);
      AssetLoaderConfig config {};
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
      AssetLoader loader(Tag::Get(), config);

      loader.RegisterLoader(oxygen::content::loaders::LoadScriptAsset);
      loader.RegisterLoader(oxygen::content::loaders::LoadScriptResource);

      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();
        loader.AddLooseCookedRoot(cooked_root);

        const auto script_asset
          = co_await loader.LoadAssetAsync<ScriptAsset>(script_key);
        EXPECT_THAT(script_asset, NotNull());
        if (!script_asset) {
          loader.Stop();
          co_return oxygen::co::kJoin;
        }

        const auto key = loader.MakeScriptResourceKeyForAsset(
          *script_asset, oxygen::data::ResourceReferenceIndex { 0U });
        EXPECT_TRUE(key.has_value());
        if (!key.has_value()) {
          loader.Stop();
          co_return oxygen::co::kJoin;
        }
        const auto script_resource
          = co_await loader.LoadResourceAsync<ScriptResource>(*key);

        EXPECT_THAT(script_resource, NotNull());
        if (script_resource) {
          EXPECT_THAT(script_resource->GetData(),
            ::testing::ElementsAre(
              0x4D, 0x4F, 0x56, 0x45, 0x42, 0x43, 0xA1, 0xA2));
        }

        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(script_key, cooked_root, &el));
}

NOLINT_TEST_F(AssetLoaderScriptingTest,
  LoadAssetLooseCookedSceneWithScriptingExpectedToLoad)
{
  const auto scene_key
    = oxygen::data::AssetKey::FromVirtualPath("/test/Scripting.oscene");
  const auto cooked_root = TempDir() / "loose_scene_scripting";
  WriteLooseCookedSceneWithScripting(cooked_root, scene_key);

  TestEventLoop el;
  oxygen::co::Run(el,
    [](oxygen::data::AssetKey scene_key, std::filesystem::path cooked_root,
      TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
      oxygen::co::ThreadPool pool(el, 2);
      AssetLoaderConfig config {};
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
      AssetLoader loader(Tag::Get(), config);
      loader.RegisterLoader(oxygen::content::loaders::LoadSceneAsset);

      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();
        loader.AddLooseCookedRoot(cooked_root);

        const auto scene
          = co_await loader.LoadAssetAsync<SceneAsset>(scene_key);
        EXPECT_THAT(scene, NotNull());
        EXPECT_TRUE(
          loader.HasScriptAsset(oxygen::data::AssetKey::FromVirtualPath(
            "/test/scene-script.oscript")));

        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(scene_key, cooked_root, &el));
}

//! Parameter declarations cooked after the script descriptor load into the
//! asset in declaration order with their types and defaults.
NOLINT_TEST_F(AssetLoaderScriptingTest, ScriptAssetLoadsDeclaredParameters)
{
  using oxygen::data::ScriptAssetParam;

  // Arrange
  const auto script_key
    = oxygen::data::AssetKey::FromVirtualPath("/test/Reload.oscript");
  const auto cooked_root = TempDir() / "loose_script_declarations";
  constexpr auto kSpeedDefault = 2.5F;
  const auto declarations = oxygen::data::ScriptParameters {
    { "speed", kSpeedDefault },
    {
      "orb",
      ScriptAssetParam {
        .key = {},
        .asset_type = oxygen::data::AssetType::kMaterial,
      },
    },
  };
  WriteLooseCookedScriptAsset(cooked_root, script_key, declarations);

  TestEventLoop el;
  oxygen::co::Run(el,
    [](oxygen::data::AssetKey script_key, std::filesystem::path cooked_root,
      TestEventLoop* loop, float speed_default) -> Co<> {
      auto& el = *loop;
      oxygen::co::ThreadPool pool(el, 2);
      AssetLoaderConfig config {};
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
      AssetLoader loader(Tag::Get(), config);
      loader.RegisterLoader(oxygen::content::loaders::LoadScriptAsset);
      loader.RegisterLoader(oxygen::content::loaders::LoadScriptResource);

      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();
        loader.AddLooseCookedRoot(cooked_root);

        // Act
        const auto script
          = co_await loader.LoadAssetAsync<ScriptAsset>(script_key);

        // Assert
        EXPECT_THAT(script, NotNull());
        if (script) {
          EXPECT_EQ(script->ParametersCount(), 2U);
          EXPECT_FLOAT_EQ(
            std::get<float>(script->GetParameter("speed")), speed_default);
          EXPECT_EQ(
            std::get<ScriptAssetParam>(script->GetParameter("orb")).asset_type,
            oxygen::data::AssetType::kMaterial);
          const auto first = script->Parameters().begin();
          EXPECT_EQ((*first).key, "speed");
        }
        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(script_key, cooked_root, &el, kSpeedDefault));
}

//! Hydrated slots carry the cooked execution order.
NOLINT_TEST_F(AssetLoaderScriptingTest, HydratedSlotCarriesExecutionOrder)
{
  using oxygen::data::pak::scripting::ScriptingComponentRecord;
  const auto scene_key
    = oxygen::data::AssetKey::FromVirtualPath("/test/ScriptingOrder.oscene");
  const auto cooked_root = TempDir() / "loose_scene_slot_order";
  WriteLooseCookedSceneWithScripting(cooked_root, scene_key);

  TestEventLoop el;
  oxygen::co::Run(el,
    [](oxygen::data::AssetKey scene_key, std::filesystem::path cooked_root,
      TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
      oxygen::co::ThreadPool pool(el, 2);
      AssetLoaderConfig config {};
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
      AssetLoader loader(Tag::Get(), config);
      loader.RegisterLoader(oxygen::content::loaders::LoadSceneAsset);

      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();
        loader.AddLooseCookedRoot(cooked_root);

        const auto scene
          = co_await loader.LoadAssetAsync<SceneAsset>(scene_key);
        EXPECT_THAT(scene, NotNull());
        if (scene) {
          const auto components
            = scene->GetComponents<ScriptingComponentRecord>();
          EXPECT_EQ(components.size(), 1U);
          if (!components.empty()) {
            const auto slots
              = loader.GetHydratedScriptSlots(*scene, components.front());
            EXPECT_EQ(slots.size(), 1U);
            if (!slots.empty()) {
              EXPECT_EQ(slots.front().execution_order, kSlotExecutionOrder);
            }
          }
        }

        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(scene_key, cooked_root, &el));
}

//! A slot's asset parameter is a scene reference and a load dependency.
NOLINT_TEST_F(AssetLoaderScriptingTest, SceneAssetParameterIsADependency)
{
  const auto scene_key
    = oxygen::data::AssetKey::FromVirtualPath("/test/ScriptingParams.oscene");
  const auto cooked_root = TempDir() / "loose_scene_asset_param";
  WriteLooseCookedSceneWithScripting(cooked_root, scene_key, true);

  TestEventLoop el;
  oxygen::co::Run(el,
    [](oxygen::data::AssetKey scene_key, std::filesystem::path cooked_root,
      TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
      oxygen::co::ThreadPool pool(el, 2);
      AssetLoaderConfig config {};
      config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
      AssetLoader loader(Tag::Get(), config);
      loader.RegisterLoader(oxygen::content::loaders::LoadSceneAsset);

      OXCO_WITH_NURSERY(n)
      {
        co_await n.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();
        loader.AddLooseCookedRoot(cooked_root);

        const auto scene
          = co_await loader.LoadAssetAsync<SceneAsset>(scene_key);
        EXPECT_THAT(scene, NotNull());
        EXPECT_TRUE(loader.HasScriptAsset(ParamScriptKey()));

        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(scene_key, cooked_root, &el));
}

} // namespace
