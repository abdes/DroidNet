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
#include <cstring>
#include <filesystem>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "./AssetLoader_test.h"
#include "Fixtures/LooseCookedTestLayout.h"
#include "Fixtures/LooseCookedTestWriter.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Testing/GTest.h>

using ::testing::NotNull;

using oxygen::observer_ptr;
using oxygen::co::Co;
using oxygen::co::testing::TestEventLoop;

using oxygen::content::AssetLoader;
using oxygen::content::AssetLoaderConfig;
using oxygen::content::testing::AssetLoaderBasicTest;
using oxygen::content::testing::LooseCookedLayout;

using oxygen::data::GeometryAsset;
using oxygen::data::InputMappingContextAsset;
using oxygen::data::PhysicsSceneAsset;
using oxygen::data::SceneAsset;

using oxygen::base::CheckedAt;

namespace {

auto WriteLooseCookedSceneWithSingleRootNode(
  const std::filesystem::path& cooked_root,
  const oxygen::data::AssetKey& scene_key) -> void
{
  using oxygen::data::AssetType;
  using oxygen::data::pak::world::NodeRecord;
  using oxygen::data::pak::world::SceneAssetDesc;
  using oxygen::data::pak::world::SceneEnvironmentBlockHeader;

  const LooseCookedLayout layout {};

  // Arrange: write cooked scene descriptor bytes.
  SceneAssetDesc desc {};
  desc.header.asset_type = static_cast<uint8_t>(AssetType::kScene);
  std::ranges::copy(
    std::string_view { "TestScene" }, std::begin(desc.header.name));
  desc.header.version = oxygen::data::pak::world::kSceneAssetVersion;

  desc.nodes.offset = sizeof(SceneAssetDesc);
  desc.nodes.count = 1;
  desc.nodes.entry_size = sizeof(NodeRecord);

  static constexpr auto kStrings = std::to_array("\0root\0");
  desc.scene_strings.offset = sizeof(SceneAssetDesc) + sizeof(NodeRecord);
  desc.scene_strings.size = sizeof(kStrings) - 1;

  NodeRecord node {};
  node.node_id = scene_key;
  node.scene_name_offset = 1; // "root"
  node.parent_index = 0; // root parent is self
  node.node_flags = 0;

  const size_t total_size
    = static_cast<size_t>(desc.scene_strings.offset + desc.scene_strings.size)
    + sizeof(SceneEnvironmentBlockHeader);
  std::vector<std::byte> bytes(total_size);
  std::memcpy(bytes.data(), &desc, sizeof(desc));
  std::memcpy(
    std::span(bytes).subspan(desc.nodes.offset).data(), &node, sizeof(node));
  std::memcpy(std::span(bytes).subspan(desc.scene_strings.offset).data(),
    kStrings.data(), kStrings.size() - 1);

  SceneEnvironmentBlockHeader env_header {};
  env_header.byte_size = sizeof(SceneEnvironmentBlockHeader);
  env_header.systems_count = 0;
  std::memcpy(std::span(bytes)
                .subspan(desc.scene_strings.offset + desc.scene_strings.size)
                .data(),
    &env_header, sizeof(env_header));

  const auto descriptor_relpath
    = std::string(layout.scenes_subdir) + "/TestScene.scene";

  oxygen::content::testing::LooseCookedTestWriter writer(cooked_root);
  writer.WriteAssetDescriptor(scene_key, AssetType::kScene,
    std::string(layout.virtual_mount_root) + "/" + descriptor_relpath,
    descriptor_relpath, bytes);
  static_cast<void>(writer.Finish());
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
  const auto cooked_root = temp_dir_ / "loose_cooked_scene";
  const auto scene_key
    = oxygen::data::AssetKey::FromVirtualPath("/Test/test_scene_loose.asset");
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

      OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
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
  SceneVersionMigrationTest, PackedSceneV7IsRejectedBeforeDecodingRecords)
{
  oxygen::data::pak::world::SceneAssetDesc descriptor {};
  constexpr uint8_t kRetiredSceneVersion = 7U;
  descriptor.header.version = kRetiredSceneVersion;
  auto bytes = std::vector<std::byte>(sizeof(descriptor));
  std::memcpy(bytes.data(), &descriptor, sizeof(descriptor));
  try {
    const auto scene = oxygen::data::SceneAsset { oxygen::data::AssetKey {},
      std::move(bytes) };
    FAIL() << "Retired scene version was accepted";
  } catch (const std::runtime_error& error) {
    EXPECT_THAT(
      error.what(), ::testing::HasSubstr("unsupported descriptor version"));
  }
}

NOLINT_TEST(
  SceneVersionMigrationTest, CurrentSceneRejectsRetiredSkyLightRecordSize)
{
  namespace world = oxygen::data::pak::world;
  world::SceneAssetDesc descriptor {};
  descriptor.header.version = world::kSceneAssetVersion;
  descriptor.nodes = {
    .offset = sizeof(descriptor),
    .count = 1U,
    .entry_size = sizeof(world::NodeRecord),
  };
  descriptor.scene_strings.offset
    = sizeof(descriptor) + sizeof(world::NodeRecord);
  descriptor.scene_strings.size = 1U;
  const auto environment_offset = descriptor.scene_strings.offset + 1U;
  world::SceneEnvironmentBlockHeader environment {};
  constexpr uint32_t kRetiredSkyLightRecordSize = 92U;
  environment.byte_size = sizeof(environment) + kRetiredSkyLightRecordSize;
  environment.systems_count = 1U;
  world::SkyLightEnvironmentRecord retired {};
  retired.header.record_size = kRetiredSkyLightRecordSize;
  auto bytes
    = std::vector<std::byte>(environment_offset + environment.byte_size);
  std::memcpy(bytes.data(), &descriptor, sizeof(descriptor));
  const world::NodeRecord node {};
  std::memcpy(std::span(bytes).subspan(descriptor.nodes.offset).data(), &node,
    sizeof(node));
  std::memcpy(std::span(bytes).subspan(environment_offset).data(), &environment,
    sizeof(environment));
  std::memcpy(
    std::span(bytes).subspan(environment_offset + sizeof(environment)).data(),
    &retired, sizeof(retired));
  try {
    const auto scene = oxygen::data::SceneAsset { oxygen::data::AssetKey {},
      std::move(bytes) };
    FAIL() << "Retired SkyLight record was accepted";
  } catch (const std::runtime_error& error) {
    EXPECT_THAT(error.what(), ::testing::HasSubstr("environment record"));
  }
}
