//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "./AssetLoader_test.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>

#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>

#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>

using ::testing::NotNull;

using oxygen::observer_ptr;
using oxygen::co::Co;
using oxygen::co::testing::TestEventLoop;

using oxygen::content::AssetLoader;
using oxygen::content::AssetLoaderConfig;
using oxygen::content::testing::AssetLoaderLoadingTest;

using oxygen::data::GeometryAsset;
using oxygen::data::SceneAsset;

namespace {

auto WriteLooseCookedSceneWithSingleRootNode(
  const std::filesystem::path& cooked_root,
  const oxygen::data::AssetKey& scene_key) -> void
{
  using oxygen::data::AssetType;
  using oxygen::data::loose_cooked::v1::AssetEntry;
  using oxygen::data::loose_cooked::v1::IndexHeader;
  using oxygen::data::pak::NodeRecord;
  using oxygen::data::pak::SceneAssetDesc;

  std::filesystem::create_directories(cooked_root / "assets");

  // Arrange: write cooked scene descriptor bytes.
  SceneAssetDesc desc {};
  desc.header.asset_type = static_cast<uint8_t>(AssetType::kScene);
  std::snprintf(desc.header.name, sizeof(desc.header.name), "%s", "TestScene");
  desc.header.version = 1;

  desc.nodes.offset = sizeof(SceneAssetDesc);
  desc.nodes.count = 1;
  desc.nodes.entry_size = sizeof(NodeRecord);

  static constexpr char kStrings[] = "\0root\0";
  desc.scene_strings.offset = sizeof(SceneAssetDesc) + sizeof(NodeRecord);
  desc.scene_strings.size = sizeof(kStrings) - 1;

  NodeRecord node {};
  node.node_id = scene_key;
  node.scene_name_offset = 1; // "root"
  node.parent_index = 0; // root parent is self
  node.node_flags = 0;

  const size_t total_size
    = static_cast<size_t>(desc.scene_strings.offset + desc.scene_strings.size);
  std::vector<std::byte> bytes(total_size);
  std::memcpy(bytes.data(), &desc, sizeof(desc));
  std::memcpy(bytes.data() + desc.nodes.offset, &node, sizeof(node));
  std::memcpy(
    bytes.data() + desc.scene_strings.offset, kStrings, sizeof(kStrings) - 1);

  const auto descriptor_relpath = std::string("assets/TestScene.scene");
  {
    std::ofstream out(cooked_root / descriptor_relpath, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
  }

  // Arrange: build index string table.
  std::string strings;
  strings.push_back('\0');
  const auto off_desc = static_cast<uint32_t>(strings.size());
  strings += descriptor_relpath;
  strings.push_back('\0');
  const auto off_vpath = static_cast<uint32_t>(strings.size());
  strings += "/Content/TestScene.scene";
  strings.push_back('\0');

  IndexHeader header {};
  header.version = 1;
  header.content_version = 0;
  header.flags = oxygen::data::loose_cooked::v1::kHasVirtualPaths;
  header.string_table_offset = sizeof(IndexHeader);
  header.string_table_size = static_cast<uint64_t>(strings.size());
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.asset_count = 1;
  header.asset_entry_size = sizeof(AssetEntry);
  header.file_records_offset
    = header.asset_entries_offset + sizeof(AssetEntry) * header.asset_count;
  header.file_record_count = 0;
  header.file_record_size = sizeof(oxygen::data::loose_cooked::v1::FileRecord);

  AssetEntry asset_entry {};
  asset_entry.asset_key = scene_key;
  asset_entry.descriptor_relpath_offset = off_desc;
  asset_entry.virtual_path_offset = off_vpath;
  asset_entry.asset_type = static_cast<uint8_t>(AssetType::kScene);
  asset_entry.descriptor_size = static_cast<uint64_t>(bytes.size());

  const auto index_path = cooked_root / "container.index.bin";
  std::ofstream index_out(index_path, std::ios::binary);
  index_out.write(reinterpret_cast<const char*>(&header),
    static_cast<std::streamsize>(sizeof(header)));
  index_out.write(strings.data(), static_cast<std::streamsize>(strings.size()));
  index_out.write(reinterpret_cast<const char*>(&asset_entry),
    static_cast<std::streamsize>(sizeof(asset_entry)));
}

//! Fixture for AssetLoader dependency tests
class AssetLoaderSceneTest : public AssetLoaderLoadingTest { };

//=== AssetLoader Scene Loading Tests ===-----------------------------------//

//! Test: Scene with no renderables registers no geometry dependencies.
/*!
 Scenario: Build a PAK from a YAML spec containing a scene with nodes but no
 renderables, plus a geometry asset present in the container.

 Verify that:
 - `LoadAssetAsync<SceneAsset>` returns a valid scene.
 - The scene has zero renderable records.
 - The geometry asset is not registered as a dependency of the scene.
*/
NOLINT_TEST_F(AssetLoaderSceneTest,
  LoadAsset_SceneWithoutRenderables_RegistersNoGeometryDependencies)
{
  // Arrange
  const auto pak_path = GeneratePakFile("scene_no_renderables");
  const auto scene_key = CreateTestAssetKey("test_scene_no_renderables");
  const auto geometry_key = CreateTestAssetKey("test_geometry");

  TestEventLoop el;

  // Act + Assert
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadGeometryAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadSceneAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddPakFile(pak_path);

      const auto scene = co_await loader.LoadAssetAsync<SceneAsset>(scene_key);
      EXPECT_THAT(scene, NotNull());
      if (!scene) {
        loader.Stop();
        co_return oxygen::co::kJoin;
      }

      const auto node_count = scene->GetNodes().size();
      EXPECT_EQ(node_count, 2U);
      if (node_count != 2U) {
        loader.Stop();
        co_return oxygen::co::kJoin;
      }
      EXPECT_EQ(scene->GetNodeName(scene->GetRootNode()), "root");
      EXPECT_EQ(scene->GetNodeName(scene->GetNode(1)), "empty_node");

      const auto renderables
        = scene->GetComponents<oxygen::data::pak::RenderableRecord>();
      EXPECT_TRUE(renderables.empty());

#if !defined(NDEBUG)
      size_t dependents = 0;
      loader.ForEachDependent(
        geometry_key, [&](const oxygen::data::AssetKey&) { ++dependents; });
      EXPECT_EQ(dependents, 0U);
#endif

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: AssetLoader loads a scene and registers only renderable geometry deps.
/*!
 Scenario: Build a PAK from a YAML spec containing a scene with one renderable
 that references geometry A, plus an additional geometry B that is not
 referenced.

 Verify that:
 - `LoadAssetAsync<SceneAsset>` returns a valid scene.
 - The scene exposes expected nodes and renderable component records.
 - Only geometry A becomes a dependent edge of the scene.
*/
NOLINT_TEST_F(AssetLoaderSceneTest,
  LoadAsset_SceneWithRenderable_RegistersOnlyRenderableGeometryDependency)
{
  // Arrange
  const auto pak_path = GeneratePakFile("scene_with_renderable");
  const auto scene_key = CreateTestAssetKey("test_scene");
  const auto referenced_geometry_key = CreateTestAssetKey("test_geometry");
  const auto unused_geometry_key = CreateTestAssetKey("buffered_geometry");

  TestEventLoop el;

  // Act + Assert
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadGeometryAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadSceneAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddPakFile(pak_path);

      const auto scene = co_await loader.LoadAssetAsync<SceneAsset>(scene_key);
      EXPECT_THAT(scene, NotNull());
      if (!scene) {
        loader.Stop();
        co_return oxygen::co::kJoin;
      }

      // Assert: nodes
      const auto node_count = scene->GetNodes().size();
      EXPECT_EQ(node_count, 2U);
      if (node_count != 2U) {
        loader.Stop();
        co_return oxygen::co::kJoin;
      }
      EXPECT_EQ(scene->GetNodeName(scene->GetRootNode()), "root");
      EXPECT_EQ(scene->GetNodeName(scene->GetNode(1)), "mesh_node");

      // Assert: renderables
      const auto renderables
        = scene->GetComponents<oxygen::data::pak::RenderableRecord>();
      const auto renderable_count = renderables.size();
      EXPECT_EQ(renderable_count, 1U);
      if (renderable_count != 1U) {
        loader.Stop();
        co_return oxygen::co::kJoin;
      }
      EXPECT_EQ(renderables[0].node_index, 1U);
      EXPECT_EQ(renderables[0].geometry_key, referenced_geometry_key);

#if !defined(NDEBUG)
      // Assert: only referenced geometry becomes a dependent edge.
      bool has_scene_as_dependent = false;
      loader.ForEachDependent(
        referenced_geometry_key, [&](const oxygen::data::AssetKey& dependent) {
          if (dependent == scene_key) {
            has_scene_as_dependent = true;
          }
        });
      EXPECT_TRUE(has_scene_as_dependent);

      size_t unused_dependents = 0;
      loader.ForEachDependent(unused_geometry_key,
        [&](const oxygen::data::AssetKey&) { ++unused_dependents; });
      EXPECT_EQ(unused_dependents, 0U);
#endif

      // Sanity: referenced geometry is loadable (should already be loaded via
      // scene publish).
      const auto geometry = co_await loader.LoadAssetAsync<GeometryAsset>(
        referenced_geometry_key);
      EXPECT_THAT(geometry, NotNull());

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: Duplicate renderables do not create extra dependency edges.
/*!
 Scenario: Build a PAK with a scene containing two renderable records that both
 reference the same geometry.

 Verify that:
 - The scene contains two renderable records.
 - The referenced geometry is registered as a dependency of the scene.
*/
NOLINT_TEST_F(AssetLoaderSceneTest,
  LoadAsset_SceneWithDuplicateRenderables_RegistersSingleDependency)
{
  // Arrange
  const auto pak_path = GeneratePakFile("scene_duplicate_renderables");
  const auto scene_key = CreateTestAssetKey("test_scene_duplicate_renderables");
  const auto geometry_key = CreateTestAssetKey("test_geometry");

  TestEventLoop el;

  // Act + Assert
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadGeometryAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadSceneAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddPakFile(pak_path);

      const auto scene = co_await loader.LoadAssetAsync<SceneAsset>(scene_key);
      EXPECT_THAT(scene, NotNull());
      if (!scene) {
        loader.Stop();
        co_return oxygen::co::kJoin;
      }

      const auto renderables
        = scene->GetComponents<oxygen::data::pak::RenderableRecord>();
      const auto renderable_count = renderables.size();
      EXPECT_EQ(renderable_count, 2U);
      if (renderable_count != 2U) {
        loader.Stop();
        co_return oxygen::co::kJoin;
      }
      EXPECT_EQ(renderables[0].geometry_key, geometry_key);
      EXPECT_EQ(renderables[1].geometry_key, geometry_key);

#if !defined(NDEBUG)
      bool has_scene_as_dependent = false;
      loader.ForEachDependent(
        geometry_key, [&](const oxygen::data::AssetKey& dependent) {
          if (dependent == scene_key) {
            has_scene_as_dependent = true;
          }
        });
      EXPECT_TRUE(has_scene_as_dependent);
#endif

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: Scene referencing two geometries registers both dependencies.
/*!
 Scenario: Build a PAK with a scene containing renderables that reference two
 different geometry assets.

 Verify that both referenced geometries are registered as dependencies.
*/
NOLINT_TEST_F(AssetLoaderSceneTest,
  LoadAsset_SceneWithTwoGeometries_RegistersBothDependencies)
{
  // Arrange
  const auto pak_path = GeneratePakFile("scene_two_geometries");
  const auto scene_key = CreateTestAssetKey("test_scene_two_geometries");
  const auto geometry_a = CreateTestAssetKey("test_geometry");
  const auto geometry_b = CreateTestAssetKey("buffered_geometry");

  TestEventLoop el;

  // Act + Assert
  (oxygen::co::Run)(el, [&]() -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadBufferResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
    loader.RegisterLoader(oxygen::content::loaders::LoadMaterialAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadGeometryAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadSceneAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddPakFile(pak_path);

      const auto scene = co_await loader.LoadAssetAsync<SceneAsset>(scene_key);
      EXPECT_THAT(scene, NotNull());
      if (!scene) {
        loader.Stop();
        co_return oxygen::co::kJoin;
      }

      const auto renderables
        = scene->GetComponents<oxygen::data::pak::RenderableRecord>();
      const auto renderable_count = renderables.size();
      EXPECT_EQ(renderable_count, 2U);
      if (renderable_count != 2U) {
        loader.Stop();
        co_return oxygen::co::kJoin;
      }
      EXPECT_EQ(renderables[0].geometry_key, geometry_a);
      EXPECT_EQ(renderables[1].geometry_key, geometry_b);

#if !defined(NDEBUG)
      bool has_a = false;
      loader.ForEachDependent(
        geometry_a, [&](const oxygen::data::AssetKey& dependent) {
          if (dependent == scene_key) {
            has_a = true;
          }
        });
      EXPECT_TRUE(has_a);

      bool has_b = false;
      loader.ForEachDependent(
        geometry_b, [&](const oxygen::data::AssetKey& dependent) {
          if (dependent == scene_key) {
            has_b = true;
          }
        });
      EXPECT_TRUE(has_b);
#endif

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: AssetLoader can load a cooked scene descriptor from loose cooked root
/*!
 Scenario: Writes a minimal loose cooked root containing a single scene asset
 descriptor, mounts it, and verifies that the scene loads and exposes the root
 node name.
*/
NOLINT_TEST_F(AssetLoaderSceneTest, LoadAsset_LooseCookedScene_Loads)
{
  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_scene";
  const auto scene_key = CreateTestAssetKey("test_scene_loose");
  WriteLooseCookedSceneWithSingleRootNode(cooked_root, scene_key);

  TestEventLoop el;

  // Act + Assert
  (oxygen::co::Run)(el, [&]() -> Co<> {
    using oxygen::content::AssetLoader;
    using oxygen::content::AssetLoaderConfig;
    using oxygen::data::SceneAsset;

    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(
      oxygen::content::internal::EngineTagFactory::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadSceneAsset);

    OXCO_WITH_NURSERY(n) // NOLINT(*-avoid-reference-coroutine-parameters)
    {
      co_await n.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();

      loader.AddLooseCookedRoot(cooked_root);

      const auto scene = co_await loader.LoadAssetAsync<SceneAsset>(scene_key);
      EXPECT_THAT(scene, NotNull());

      if (scene) {
        EXPECT_EQ(scene->GetNodes().size(), 1);
        EXPECT_EQ(scene->GetNodeName(scene->GetRootNode()), "root");
        EXPECT_TRUE(
          scene->GetComponents<oxygen::data::pak::RenderableRecord>().empty());
      }

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

} // namespace
