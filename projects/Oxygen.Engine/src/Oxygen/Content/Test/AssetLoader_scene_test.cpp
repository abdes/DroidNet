//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "./AssetLoader_test.h"
#include "Fixtures/LooseCookedTestLayout.h"
#include "Fixtures/LooseCookedTestWriter.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/PhysicsSceneLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/TextureResource.h>
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
using oxygen::content::testing::AssetLoaderLoadingTest;
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
  std::snprintf(desc.header.name, sizeof(desc.header.name), "%s", "TestScene");
  desc.header.version = oxygen::data::pak::world::kSceneAssetVersion;

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
    = static_cast<size_t>(desc.scene_strings.offset + desc.scene_strings.size)
    + sizeof(SceneEnvironmentBlockHeader);
  std::vector<std::byte> bytes(total_size);
  std::memcpy(bytes.data(), &desc, sizeof(desc));
  std::memcpy(bytes.data() + desc.nodes.offset, &node, sizeof(node));
  std::memcpy(
    bytes.data() + desc.scene_strings.offset, kStrings, sizeof(kStrings) - 1);

  SceneEnvironmentBlockHeader env_header {};
  env_header.byte_size = sizeof(SceneEnvironmentBlockHeader);
  env_header.systems_count = 0;
  std::memcpy(
    bytes.data() + desc.scene_strings.offset + desc.scene_strings.size,
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
  LoadAssetSceneWithoutRenderablesRegistersNoGeometryDependencies)
{
  // Arrange
  const auto pak_path = GeneratePakFile("scene_no_renderables");
  const auto scene_key = CreateTestAssetKey("test_scene_no_renderables");
  const auto geometry_key = CreateTestAssetKey("test_geometry");

  TestEventLoop el;

  // Act + Assert
  oxygen::co::Run(el, [&] -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(Tag::Get(), config);

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
        = scene->GetComponents<oxygen::data::pak::world::RenderableRecord>();
      EXPECT_TRUE(renderables.empty());

#ifndef NDEBUG
      size_t dependents = 0;
      loader.ForEachDependent(geometry_key,
        [&](const oxygen::data::AssetKey&) -> void { ++dependents; });
      EXPECT_EQ(dependents, 0U);
#endif

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: AssetLoader loads a scene and registers renderable asset deps.
/*!
 Scenario: Build a PAK from a YAML spec containing a scene with one renderable
 that references geometry A and a scene-authored material override, plus an
 additional geometry B that is not referenced.

 Verify that:
 - `LoadAssetAsync<SceneAsset>` returns a valid scene.
 - The scene exposes expected nodes and renderable component records.
 - Geometry A and the material override become dependent edges of the scene.
 - The unreferenced geometry does not become a scene dependency.
*/
NOLINT_TEST_F(AssetLoaderSceneTest,
  LoadAssetSceneWithRenderableRegistersRenderableDependencies)
{
  // Arrange
  const auto pak_path = GeneratePakFile("scene_with_renderable");
  const auto scene_key = CreateTestAssetKey("test_scene");
  const auto referenced_geometry_key = CreateTestAssetKey("test_geometry");
  const auto material_key = CreateTestAssetKey("simple_material");
  const auto unused_geometry_key = CreateTestAssetKey("buffered_geometry");

  TestEventLoop el;

  // Act + Assert
  oxygen::co::Run(el, [&] -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(Tag::Get(), config);

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
        = scene->GetComponents<oxygen::data::pak::world::RenderableRecord>();
      const auto renderable_count = renderables.size();
      EXPECT_EQ(renderable_count, 1U);
      if (renderable_count != 1U) {
        loader.Stop();
        co_return oxygen::co::kJoin;
      }
      EXPECT_EQ(CheckedAt(renderables, 0).node_index, 1U);
      EXPECT_EQ(
        CheckedAt(renderables, 0).geometry_key, referenced_geometry_key);
      const auto overrides
        = scene
            ->GetComponents<oxygen::data::pak::world::MaterialOverrideRecord>();
      EXPECT_EQ(overrides.size(), 1U);
      if (overrides.size() != 1U) {
        loader.Stop();
        co_return oxygen::co::kJoin;
      }
      EXPECT_EQ(overrides.front().node_index, 1U);
      EXPECT_EQ(overrides.front().material_key, material_key);

#ifndef NDEBUG
      // Assert: referenced renderable assets become dependent edges.
      bool has_scene_as_dependent = false;
      loader.ForEachDependent(referenced_geometry_key,
        [&](const oxygen::data::AssetKey& dependent) -> void {
          if (dependent == scene_key) {
            has_scene_as_dependent = true;
          }
        });
      EXPECT_TRUE(has_scene_as_dependent);

      bool has_scene_as_material_dependent = false;
      loader.ForEachDependent(
        material_key, [&](const oxygen::data::AssetKey& dependent) -> void {
          if (dependent == scene_key) {
            has_scene_as_material_dependent = true;
          }
        });
      EXPECT_TRUE(has_scene_as_material_dependent);

      size_t unused_dependents = 0;
      loader.ForEachDependent(unused_geometry_key,
        [&](const oxygen::data::AssetKey&) -> void { ++unused_dependents; });
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
  LoadAssetSceneWithDuplicateRenderablesRegistersSingleDependency)
{
  // Arrange
  const auto pak_path = GeneratePakFile("scene_duplicate_renderables");
  const auto scene_key = CreateTestAssetKey("test_scene_duplicate_renderables");
  const auto geometry_key = CreateTestAssetKey("test_geometry");

  TestEventLoop el;

  // Act + Assert
  oxygen::co::Run(el, [&] -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(Tag::Get(), config);

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
        = scene->GetComponents<oxygen::data::pak::world::RenderableRecord>();
      const auto renderable_count = renderables.size();
      EXPECT_EQ(renderable_count, 2U);
      if (renderable_count != 2U) {
        loader.Stop();
        co_return oxygen::co::kJoin;
      }
      EXPECT_EQ(CheckedAt(renderables, 0).geometry_key, geometry_key);
      EXPECT_EQ(CheckedAt(renderables, 1).geometry_key, geometry_key);

#ifndef NDEBUG
      bool has_scene_as_dependent = false;
      loader.ForEachDependent(
        geometry_key, [&](const oxygen::data::AssetKey& dependent) -> void {
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
  LoadAssetSceneWithTwoGeometriesRegistersBothDependencies)
{
  // Arrange
  const auto pak_path = GeneratePakFile("scene_two_geometries");
  const auto scene_key = CreateTestAssetKey("test_scene_two_geometries");
  const auto geometry_a = CreateTestAssetKey("test_geometry");
  const auto geometry_b = CreateTestAssetKey("buffered_geometry");

  TestEventLoop el;

  // Act + Assert
  oxygen::co::Run(el, [&] -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(Tag::Get(), config);

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
        = scene->GetComponents<oxygen::data::pak::world::RenderableRecord>();
      const auto renderable_count = renderables.size();
      EXPECT_EQ(renderable_count, 2U);
      if (renderable_count != 2U) {
        loader.Stop();
        co_return oxygen::co::kJoin;
      }
      EXPECT_EQ(CheckedAt(renderables, 0).geometry_key, geometry_a);
      EXPECT_EQ(CheckedAt(renderables, 1).geometry_key, geometry_b);

#ifndef NDEBUG
      bool has_a = false;
      loader.ForEachDependent(
        geometry_a, [&](const oxygen::data::AssetKey& dependent) -> void {
          if (dependent == scene_key) {
            has_a = true;
          }
        });
      EXPECT_TRUE(has_a);

      bool has_b = false;
      loader.ForEachDependent(
        geometry_b, [&](const oxygen::data::AssetKey& dependent) -> void {
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

//! Test: AssetLoader loads a scene with lights + environment block.
/*!
 Scenario: Build a PAK with a scene containing one directional light, one
 point light, and a trailing SceneEnvironment block.

 Verify that:
 - The scene loads successfully.
 - Light component tables are available via `GetComponents<T>()`.
 - The environment block header and records are exposed.
*/
NOLINT_TEST_F(AssetLoaderSceneTest,
  LoadAssetSceneWithLightsAndEnvironmentParsesComponentsAndEnvironment)
{
  const auto pak_path = GeneratePakFile("scene_with_lights_and_environment");
  const auto scene_key = CreateTestAssetKey("test_scene_lights_env");

  TestEventLoop el;

  oxygen::co::Run(el, [&] -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(Tag::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadSceneAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);

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

      const auto directional
        = scene
            ->GetComponents<oxygen::data::pak::world::DirectionalLightRecord>();
      EXPECT_EQ(directional.size(), 1U);
      if (directional.size() == 1U) {
        EXPECT_EQ(CheckedAt(directional, 0).node_index, 1U);
        EXPECT_EQ(CheckedAt(directional, 0).atmosphere_light_slot, 1U);
        EXPECT_EQ(CheckedAt(directional, 0).split_mode, 1U);
        EXPECT_FLOAT_EQ(CheckedAt(directional, 0).max_shadow_distance, 200.0F);
        EXPECT_FLOAT_EQ(CheckedAt(directional, 0).transition_fraction, 0.1F);
        EXPECT_FLOAT_EQ(
          CheckedAt(directional, 0).distance_fadeout_fraction, 0.1F);
      }

      const auto points
        = scene->GetComponents<oxygen::data::pak::world::PointLightRecord>();
      EXPECT_EQ(points.size(), 1U);
      if (points.size() == 1U) {
        EXPECT_EQ(CheckedAt(points, 0).node_index, 2U);
      }

      const auto spots
        = scene->GetComponents<oxygen::data::pak::world::SpotLightRecord>();
      EXPECT_TRUE(spots.empty());

      EXPECT_TRUE(scene->HasEnvironmentBlock());
      const auto* env_header = scene->GetEnvironmentBlockHeader();
      EXPECT_NE(env_header, nullptr);
      EXPECT_EQ(env_header->systems_count, 3U);

      const auto env_records = scene->GetEnvironmentSystemRecords();
      EXPECT_EQ(env_records.size(), 3U);

      uint32_t expected_byte_size = static_cast<uint32_t>(
        sizeof(oxygen::data::pak::world::SceneEnvironmentBlockHeader));
      for (const auto& record : env_records) {
        EXPECT_GE(record.header.record_size,
          sizeof(oxygen::data::pak::world::SceneEnvironmentSystemRecordHeader));
        EXPECT_EQ(record.bytes.size(), record.header.record_size);
        expected_byte_size += record.header.record_size;
      }
      EXPECT_EQ(env_header->byte_size, expected_byte_size);

      const auto sky = scene->TryGetSkyAtmosphereEnvironment();
      EXPECT_TRUE(sky.has_value());
      if (sky) {
        EXPECT_EQ(sky->header.system_type,
          static_cast<uint32_t>(oxygen::data::pak::world::
              EnvironmentComponentType::kSkyAtmosphere));
        EXPECT_EQ(sky->header.record_size,
          sizeof(oxygen::data::pak::world::SkyAtmosphereEnvironmentRecord));
      }

      const auto sky_light = scene->TryGetSkyLightEnvironment();
      EXPECT_TRUE(sky_light.has_value());
      if (sky_light) {
        EXPECT_EQ(sky_light->header.record_size, 88U);
        EXPECT_EQ(sky_light->source, 0U);
        EXPECT_FLOAT_EQ(sky_light->intensity, 3.25F);
        EXPECT_FLOAT_EQ(sky_light->diffuse_intensity, 0.5F);
        EXPECT_FLOAT_EQ(sky_light->specular_intensity, 1.5F);
        EXPECT_FLOAT_EQ(sky_light->lower_hemisphere_color[0], 0.125F);
        EXPECT_FLOAT_EQ(sky_light->volumetric_scattering_intensity, 0.75F);
        EXPECT_EQ(sky_light->affect_reflections, 0U);
        EXPECT_FLOAT_EQ(sky_light->source_cubemap_angle_radians, 1.25F);
        EXPECT_EQ(sky_light->lower_hemisphere_is_solid_color, 0U);
        EXPECT_FLOAT_EQ(sky_light->lower_hemisphere_blend_alpha, 0.375F);
      }

      const auto ppv = scene->TryGetPostProcessVolumeEnvironment();
      EXPECT_TRUE(ppv.has_value());
      if (ppv) {
        EXPECT_EQ(ppv->header.system_type,
          static_cast<uint32_t>(oxygen::data::pak::world::
              EnvironmentComponentType::kPostProcessVolume));
        EXPECT_EQ(ppv->header.record_size,
          sizeof(oxygen::data::pak::world::PostProcessVolumeEnvironmentRecord)
            + (2U
              * sizeof(
                oxygen::data::pak::world::ExposureCompensationKeyRecord)));
        EXPECT_FLOAT_EQ(ppv->auto_exposure_black_influence, 0.25F);
        EXPECT_FLOAT_EQ(ppv->auto_exposure_transition_distance_ev, 2.5F);
        EXPECT_EQ(scene->GetPostProcessCompensationCurve().size(), 2U);
        const auto mask = loader.MakeTextureResourceKeyForAsset(
          *scene, ppv->auto_exposure_metering_mask);
        EXPECT_TRUE(mask.has_value());
        if (mask) {
          const auto texture
            = co_await loader.LoadResourceAsync<oxygen::data::TextureResource>(
              *mask);
          EXPECT_THAT(texture, NotNull());
        }
        EXPECT_FALSE(loader
            .MakeTextureResourceKeyForAsset(
              *scene, oxygen::data::pak::core::kNoResourceIndex)
            .has_value());
        EXPECT_FALSE(loader
            .MakeTextureResourceKeyForAsset(
              *scene, oxygen::data::pak::core::ResourceIndexT { 999U })
            .has_value());
        EXPECT_FALSE(loader
            .MakeTextureResourceKeyForAsset(
              *oxygen::data::MaterialAsset::CreateDefault(),
              ppv->auto_exposure_metering_mask)
            .has_value());
      }

      loader.Stop();
      co_return oxygen::co::kJoin;
    };
  });
}

//! Test: Scene and physics sidecar authored in v7 load together.
/*!
 Scenario: Build a v7 PAK containing a Scene asset and its physics
 * sidecar.

 Verify that:
 - Scene and PhysicsScene sidecar assets both load.

 * - Sidecar target scene identity fields match authored values.
*/
NOLINT_TEST_F(AssetLoaderSceneTest, LoadAssetSceneWithPhysicsSidecarLoadsV7)
{
  const auto pak_path = GeneratePakFile("scene_with_physics_sidecar");
  const auto scene_key = CreateTestAssetKey("test_scene_with_physics");
  const auto sidecar_key
    = CreateTestAssetKey("test_scene_with_physics_sidecar");

  TestEventLoop el;
  oxygen::co::Run(el, [&] -> Co<> {
    oxygen::co::ThreadPool pool(el, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr<oxygen::co::ThreadPool> { &pool };
    AssetLoader loader(Tag::Get(), config);

    loader.RegisterLoader(oxygen::content::loaders::LoadSceneAsset);
    loader.RegisterLoader(oxygen::content::loaders::LoadPhysicsSceneAsset);

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

      const auto sidecar
        = co_await loader.LoadAssetAsync<PhysicsSceneAsset>(sidecar_key);
      EXPECT_THAT(sidecar, NotNull());
      if (!sidecar) {
        loader.Stop();
        co_return oxygen::co::kJoin;
      }

      EXPECT_EQ(sidecar->GetTargetSceneKey(), scene_key);
      EXPECT_EQ(sidecar->GetTargetNodeCount(), 2U);

      const auto rigid
        = sidecar
            ->GetBindings<oxygen::data::pak::physics::RigidBodyBindingRecord>();
      const auto colliders
        = sidecar
            ->GetBindings<oxygen::data::pak::physics::ColliderBindingRecord>();
      const auto characters
        = sidecar
            ->GetBindings<oxygen::data::pak::physics::CharacterBindingRecord>();
      const auto soft_bodies
        = sidecar
            ->GetBindings<oxygen::data::pak::physics::SoftBodyBindingRecord>();
      const auto joints
        = sidecar
            ->GetBindings<oxygen::data::pak::physics::JointBindingRecord>();
      const auto vehicles
        = sidecar
            ->GetBindings<oxygen::data::pak::physics::VehicleBindingRecord>();
      const auto aggregates
        = sidecar
            ->GetBindings<oxygen::data::pak::physics::AggregateBindingRecord>();

      EXPECT_EQ(rigid.size(), 1U);
      EXPECT_EQ(colliders.size(), 1U);
      EXPECT_EQ(characters.size(), 1U);
      EXPECT_EQ(soft_bodies.size(), 1U);
      EXPECT_EQ(joints.size(), 1U);
      EXPECT_EQ(vehicles.size(), 1U);
      EXPECT_EQ(aggregates.size(), 1U);

      if (!rigid.empty()) {
        EXPECT_EQ(CheckedAt(rigid, 0).node_index, 1U);
        EXPECT_FALSE(CheckedAt(rigid, 0).shape_asset_key.IsNil());
        EXPECT_FALSE(CheckedAt(rigid, 0).material_asset_key.IsNil());
      }
      if (!joints.empty()) {
        EXPECT_FALSE(CheckedAt(joints, 0).constraint_asset_key.IsNil());
      }
      if (!soft_bodies.empty()) {
        EXPECT_GT(CheckedAt(soft_bodies, 0).solver_iteration_count, 0U);
        EXPECT_NE(
          static_cast<uint32_t>(CheckedAt(soft_bodies, 0).topology_format), 0U);
      }
      if (!vehicles.empty()) {
        EXPECT_FALSE(CheckedAt(vehicles, 0).constraint_asset_key.IsNil());
      }

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
NOLINT_TEST_F(AssetLoaderSceneTest, LoadAssetLooseCookedSceneLoads)
{
  // Arrange
  const auto cooked_root = temp_dir_ / "loose_cooked_scene";
  const auto scene_key = CreateTestAssetKey("test_scene_loose");
  WriteLooseCookedSceneWithSingleRootNode(cooked_root, scene_key);

  TestEventLoop el;

  // Act + Assert
  oxygen::co::Run(el, [&] -> Co<> {
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

      const auto scene = co_await loader.LoadAssetAsync<SceneAsset>(scene_key);
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
  });
}

} // namespace

NOLINT_TEST(
  SceneVersionMigrationTest, PackedSceneV7IsRejectedBeforeDecodingRecords)
{
  oxygen::data::pak::world::SceneAssetDesc descriptor {};
  descriptor.header.version = 7U;
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
  descriptor.nodes = { sizeof(descriptor), 1U, sizeof(world::NodeRecord) };
  descriptor.scene_strings.offset
    = sizeof(descriptor) + sizeof(world::NodeRecord);
  descriptor.scene_strings.size = 1U;
  const auto environment_offset = descriptor.scene_strings.offset + 1U;
  world::SceneEnvironmentBlockHeader environment {};
  environment.byte_size = sizeof(environment) + 92U;
  environment.systems_count = 1U;
  world::SkyLightEnvironmentRecord retired {};
  retired.header.record_size = 92U;
  auto bytes
    = std::vector<std::byte>(environment_offset + environment.byte_size);
  std::memcpy(bytes.data(), &descriptor, sizeof(descriptor));
  const world::NodeRecord node {};
  std::memcpy(bytes.data() + descriptor.nodes.offset, &node, sizeof(node));
  std::memcpy(
    bytes.data() + environment_offset, &environment, sizeof(environment));
  std::memcpy(bytes.data() + environment_offset + sizeof(environment), &retired,
    sizeof(retired));
  try {
    const auto scene = oxygen::data::SceneAsset { oxygen::data::AssetKey {},
      std::move(bytes) };
    FAIL() << "Retired SkyLight record was accepted";
  } catch (const std::runtime_error& error) {
    EXPECT_THAT(error.what(), ::testing::HasSubstr("environment record"));
  }
}
