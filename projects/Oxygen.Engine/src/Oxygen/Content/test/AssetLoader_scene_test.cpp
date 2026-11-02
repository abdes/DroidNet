//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <filesystem>
#include <string>

#include "./AssetLoader_test.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/ProductRefusal.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
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

using oxygen::data::SceneAsset;

namespace {

auto WriteLooseCookedSceneWithSingleRootNode(
  const std::filesystem::path& cooked_root,
  const oxygen::data::AssetKey& scene_key) -> void
{
  using oxygen::data::AssetType;

  auto scene = oxygen::cooker::testing::SceneBuilder { "test" };
  scene.Node("root");

  auto root = oxygen::cooker::testing::LooseCookedRoot { "test" };
  root.Add({
    .type = AssetType::kScene,
    .leaf = "TestScene.scene",
    .descriptor = scene.Bytes(),
    .references = scene.References(),
    .key = scene_key,
  });
  oxygen::cooker::testing::Materialize(root.Files(), cooked_root);
}

//! Fixture for AssetLoader dependency tests
class AssetLoaderSceneTest : public AssetLoaderBasicTest { };

//=== AssetLoader Scene Loading Tests ===-----------------------------------//

//! Test: AssetLoader can load a cooked scene descriptor from loose cooked root
/*!
 Scenario: Writes a minimal loose cooked root containing a single scene asset
 descriptor, mounts it, and verifies that the scene loads and exposes the root
 node name.
*/
NOLINT_TEST_F(AssetLoaderSceneTest, LoadAssetLooseCookedSceneLoads)
{
  // Arrange
  const auto cooked_root = TempDir() / "loose_cooked_scene";
  const auto scene_key
    = oxygen::data::AssetKey::FromVirtualPath("/test/test_scene_loose.asset");
  WriteLooseCookedSceneWithSingleRootNode(cooked_root, scene_key);

  TestEventLoop el;

  // Act + Assert
  oxygen::co::Run(el,
    [](std::filesystem::path cooked_root, oxygen::data::AssetKey scene_key,
      TestEventLoop* loop) -> Co<> {
      auto& el = *loop;
      using oxygen::content::AssetLoader;
      using oxygen::content::AssetLoaderConfig;
      using oxygen::data::SceneAsset;

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
          EXPECT_EQ(scene->GetNodes().size(), 1);
          EXPECT_EQ(scene->GetNodeName(scene->GetRootNode()), "root");
          EXPECT_TRUE(
            scene->GetComponents<oxygen::data::pak::world::RenderableRecord>()
              .empty());
        }

        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(cooked_root, scene_key, &el));
}

} // namespace

NOLINT_TEST(
  SceneVersionMigrationTest, CurrentSceneRejectsRetiredSkyLightRecordSize)
{
  namespace world = oxygen::data::pak::world;
  constexpr uint32_t kRetiredSkyLightRecordSize = 92U;
  auto scene = oxygen::cooker::testing::SceneBuilder { "Retired" };
  scene.Node("root");
  scene.Environment(world::SkyLightEnvironmentRecord {});
  auto bytes = scene.Bytes();
  oxygen::cooker::testing::DescriptorDefects::SetEnvironmentRecordSize(bytes,
    world::EnvironmentComponentType::kSkyLight, kRetiredSkyLightRecordSize);
  EXPECT_EQ(oxygen::cooker::testing::SceneRefusal(bytes),
    oxygen::data::CookedDefect::kSizeMismatch);
}
