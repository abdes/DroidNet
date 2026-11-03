//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include "AssetLoader_test.h"

#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/ContentLoadError.h>
#include <Oxygen/Content/Loaders/PhysicsResourceLoader.h>
#include <Oxygen/Content/Loaders/PhysicsSceneLoader.h>
#include <Oxygen/Content/ResidencyPolicy.h>
#include <Oxygen/Cooker/testing/AssetDescriptors.h>
#include <Oxygen/Cooker/testing/CookedPak.h>
#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/PhysicsSidecarBuilder.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/OxCo/test/Utils/TestEventLoop.h>
#include <Oxygen/Serio/FileLock.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {
namespace {
  namespace physics = data::pak::physics;
  using co::testing::TestEventLoop;
  using cooker::testing::CookedPak;
  using cooker::testing::LooseCookedRoot;
  using cooker::testing::SourceKeyOf;

  //! The identity of the root that owns the sidecar; the dependency roots take
  //! their payload byte as theirs.
  constexpr auto kOwnerSeed = uint8_t { 10U };

  const auto kScene = data::AssetKey::FromVirtualPath("/test/scene.oscene");
  const auto kSidecar = data::AssetKey::FromVirtualPath("/test/scene.opscene");
  const auto kShape = data::AssetKey::FromVirtualPath("/test/shape.oshape");
  const auto kMaterial
    = data::AssetKey::FromVirtualPath("/test/material.opmat");
  const auto kPayload = data::AssetKey::FromVirtualPath("/test/shape.payload");

  auto WriteSidecar(const std::filesystem::path& root) -> void
  {
    auto scene = cooker::testing::SceneBuilder { "scene" };
    const auto body = scene.Node("body");
    auto bindings = cooker::testing::PhysicsSidecarBuilder {};
    bindings.RigidBody(body, kShape, kMaterial);
    auto owner = LooseCookedRoot { "test", kOwnerSeed };
    owner.Add({
      .type = data::AssetType::kPhysicsScene,
      .leaf = "scene.opscene",
      .descriptor = bindings.Bytes(kScene, scene.Bytes(), "PhysicsScene"),
      .references = bindings.References(kScene),
      .key = kSidecar,
    });
    cooker::testing::Materialize(owner.Files(), root);
    const auto lease = serio::FileLock::TryAcquire(
      root / data::loose_cooked::kGenerationLeaseFileName,
      serio::FileLockMode::kExclusive, serio::FileLockOpenMode::kOpenOrCreate);
    if (!lease) {
      throw std::system_error(lease.error());
    }
  }

  enum class DependencyCase : uint8_t {
    kValid,
    kMissingMaterial,
    kWrongShapeVersion,
  };

  auto WriteDependencies(const std::filesystem::path& root,
    const uint8_t payload, const DependencyCase scenario) -> void
  {
    auto dependencies = LooseCookedRoot { "test", payload };
    physics::CollisionShapeAssetDesc shape {};
    shape.shape_type = physics::ShapeType::kConvexHull;
    shape.material_asset_key = kMaterial;
    shape.cooked_shape_ref = {
      .payload_asset_key = kPayload,
      .payload_type = physics::ShapePayloadType::kConvex,
    };
    auto shape_fixture
      = cooker::testing::CollisionShapeDescriptor("shape", shape);
    if (scenario == DependencyCase::kWrongShapeVersion) {
      cooker::testing::DescriptorDefects::SetVersion(shape_fixture.bytes, 0U);
    }
    dependencies.Add({
      .type = data::AssetType::kCollisionShape,
      .leaf = "shape.oshape",
      .descriptor = std::move(shape_fixture.bytes),
      .references = std::move(shape_fixture.references),
      .key = kShape,
    });
    if (scenario != DependencyCase::kMissingMaterial) {
      physics::PhysicsMaterialAssetDesc material {};
      material.restitution = static_cast<float>(payload) / 10.0F;
      auto material_fixture
        = cooker::testing::PhysicsMaterialDescriptor("material", material);
      dependencies.Add({
        .type = data::AssetType::kPhysicsMaterial,
        .leaf = "material.opmat",
        .descriptor = std::move(material_fixture.bytes),
        .references = std::move(material_fixture.references),
        .key = kMaterial,
      });
    }
    const std::array data { static_cast<std::byte>(payload) };
    physics::PhysicsResourceDesc resource {};
    resource.resource_asset_key = kPayload;
    dependencies.AddPhysics(data, resource);
    cooker::testing::Materialize(dependencies.Files(), root);
    const auto lease = serio::FileLock::TryAcquire(
      root / data::loose_cooked::kGenerationLeaseFileName,
      serio::FileLockMode::kExclusive, serio::FileLockOpenMode::kOpenOrCreate);
    if (!lease) {
      throw std::system_error(lease.error());
    }
  }

  auto ExercisePhysicsBindings(
    TestEventLoop* loop, const std::filesystem::path root) -> co::Co<>
  {
    const auto owner_root = root / "owner";
    const auto first_root = root / "first";
    const auto second_root = root / "second";
    WriteSidecar(owner_root);
    WriteDependencies(first_root, 1U, DependencyCase::kValid);
    WriteDependencies(second_root, 2U, DependencyCase::kValid);
    co::ThreadPool pool(*loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr(&pool);
    AssetLoader loader(engine::internal::EngineTagFactory::Get(), config);
    loader.RegisterLoader(loaders::LoadPhysicsSceneAsset);
    loader.RegisterLoader(loaders::LoadPhysicsResource);
    OXCO_WITH_NURSERY(nursery)
    {
      co_await nursery.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      const auto first_source = loader.MountLooseCookedGeneration(first_root);
      static_cast<void>(loader.MountLooseCookedGeneration(owner_root));
      auto original = loader.BeginLoadScope();
      auto first = co_await loader.LoadAssetAsync<data::PhysicsSceneAsset>(
        kSidecar, LoadRequest { .scope = original });
      if (!first) {
        ADD_FAILURE() << "Valid physics graph did not publish";
        loader.Stop();
        co_return co::kJoin;
      }
      const auto first_shape
        = loader.ReadCollisionShapeAssetDescForAsset(*first, kShape);
      if (!first_shape) {
        ADD_FAILURE() << "Missing first_shape";
        loader.Stop();
        co_return co::kJoin;
      }
      EXPECT_EQ(first_shape->cooked_shape_ref.payload_asset_key, kPayload);
      const auto first_key
        = loader.MakePhysicsResourceKeyForAsset(*first, kPayload);
      if (!first_key) {
        ADD_FAILURE() << "Missing first_key";
        loader.Stop();
        co_return co::kJoin;
      }
      static_cast<void>(
        loader.MountLooseCookedGeneration(second_root, first_source));
      const auto second
        = co_await loader.LoadAssetAsync<data::PhysicsSceneAsset>(kSidecar);
      if (!second) {
        ADD_FAILURE() << "Missing second";
        loader.Stop();
        co_return co::kJoin;
      }
      EXPECT_NE(first, second);
      const auto first_material
        = loader.ReadPhysicsMaterialAssetDescForAsset(*first, kMaterial);
      const auto second_material
        = loader.ReadPhysicsMaterialAssetDescForAsset(*second, kMaterial);
      if (!first_material) {
        ADD_FAILURE() << "Missing first_material";
        loader.Stop();
        co_return co::kJoin;
      }
      if (!second_material) {
        ADD_FAILURE() << "Missing second_material";
        loader.Stop();
        co_return co::kJoin;
      }
      EXPECT_FLOAT_EQ(first_material->restitution, 0.1F);
      EXPECT_FLOAT_EQ(second_material->restitution, 0.2F);
      const auto second_key
        = loader.MakePhysicsResourceKeyForAsset(*second, kPayload);
      if (!second_key) {
        ADD_FAILURE() << "Missing second_key";
        loader.Stop();
        co_return co::kJoin;
      }
      EXPECT_NE(first_key, second_key);
      auto repeated = co_await loader.LoadAssetAsync<data::PhysicsSceneAsset>(
        kSidecar, LoadRequest { .scope = original });
      EXPECT_EQ(repeated, first);
      repeated.reset();
      original = {};
      const auto lease_path
        = first_root / data::loose_cooked::kGenerationLeaseFileName;
      EXPECT_FALSE(serio::FileLock::TryAcquire(
        lease_path, serio::FileLockMode::kExclusive));
      auto old_payload = co_await loader.LoadPhysicsResourceAsync(*first_key);
      const auto new_payload
        = co_await loader.LoadPhysicsResourceAsync(*second_key);
      if (!old_payload) {
        ADD_FAILURE() << "Missing old_payload";
        loader.Stop();
        co_return co::kJoin;
      }
      if (!new_payload) {
        ADD_FAILURE() << "Missing new_payload";
        loader.Stop();
        co_return co::kJoin;
      }
      EXPECT_THAT(old_payload->GetData(), ::testing::ElementsAre(1U));
      EXPECT_THAT(new_payload->GetData(), ::testing::ElementsAre(2U));
      EXPECT_FALSE(loader.ReadPhysicsMaterialAssetDescForAsset(*first, kShape));
      old_payload.reset();
      first.reset();
      loader.TrimCache();
      EXPECT_TRUE(serio::FileLock::TryAcquire(
        lease_path, serio::FileLockMode::kExclusive));
      loader.Stop();
      co_return co::kJoin;
    };
  }

  NOLINT_TEST_F(AssetLoaderBasicTest,
    PhysicsBindingsRetainCapturedCrossRootValuesAndPayloads)
  {
    TestEventLoop loop;
    co::Run(loop, ExercisePhysicsBindings(&loop, TempDir()));
  }

  //! What a sidecar load over a failing dependency root left.
  struct SidecarLoad final {
    std::exception_ptr error;
    bool published = false;
  };

  //! Mounts the sidecar's root over a dependency root written for `scenario`
  //! and loads the sidecar.
  auto LoadSidecarOver(AssetLoader& loader, const std::filesystem::path& root,
    const DependencyCase scenario) -> co::Co<SidecarLoad>
  {
    WriteDependencies(root / "dependencies", 1U, scenario);
    WriteSidecar(root / "owner");
    static_cast<void>(loader.MountLooseCookedGeneration(root / "dependencies"));
    static_cast<void>(loader.MountLooseCookedGeneration(root / "owner"));
    auto error = co_await ThrownBy(
      loader.LoadAssetAsync<data::PhysicsSceneAsset>(kSidecar));
    co_return SidecarLoad {
      .error = error,
      .published = loader.GetPhysicsSceneAsset(kSidecar) != nullptr,
    };
  }

  //! A physics material the mounts do not hold fails the sidecar's load as a
  //! missing dependency, and nothing is published.
  NOLINT_TEST_F(AssetLoaderPoolTest, MissingPhysicsMaterialFailsTheSidecar)
  {
    const auto load_run = WhileActive(
      LoadSidecarOver(loader_, TempDir(), DependencyCase::kMissingMaterial));
    ASSERT_HAS_VALUE(load_run);
    const auto& load = *load_run;

    EXPECT_EQ(
      LoadFailureOf(load.error), ContentLoadFailure::kDependencyMissing);
    EXPECT_FALSE(load.published);
  }

  //! A collision shape whose descriptor is refused fails the sidecar's load as
  //! a failed dependency, with the shape's refusal nested.
  NOLINT_TEST_F(AssetLoaderPoolTest, RefusedShapeDescriptorFailsTheSidecar)
  {
    const auto load_run = WhileActive(
      LoadSidecarOver(loader_, TempDir(), DependencyCase::kWrongShapeVersion));
    ASSERT_HAS_VALUE(load_run);
    const auto& load = *load_run;

    EXPECT_EQ(LoadFailureOf(load.error), ContentLoadFailure::kDependencyFailed);
    EXPECT_EQ(
      CookedDefectIn(load.error), data::CookedDefect::kUnsupportedVersion);
    EXPECT_FALSE(load.published);
  }

  //! Mounts a pak whose physics table holds the reserved entry 0 and a 4-byte
  //! payload at entry 1, then loads entry 1 through the pak's source key.
  auto LoadPhysicsPayloadFromPak(
    TestEventLoop* loop, const std::filesystem::path pak_path) -> co::Co<>
  {
    const auto payload = std::vector<std::byte>(4U, std::byte { 2 });
    auto root = LooseCookedRoot { "test" };
    auto resource = physics::PhysicsResourceDesc {};
    resource.resource_asset_key = kPayload;
    static_cast<void>(root.AddPhysics(payload, resource));
    static_cast<void>(root.Material("Anchor.omat"));
    const auto source_key = SourceKeyOf(kOwnerSeed);
    cooker::testing::Materialize(
      CookedPak { std::move(root) }.Identity(source_key).Encode(), pak_path);

    co::ThreadPool pool(*loop, 2);
    AssetLoaderConfig loader_config {};
    loader_config.thread_pool = observer_ptr(&pool);
    loader_config.verify_content_hashes = true;
    AssetLoader loader(
      engine::internal::EngineTagFactory::Get(), loader_config);
    loader.RegisterLoader(loaders::LoadPhysicsResource);
    OXCO_WITH_NURSERY(nursery)
    {
      co_await nursery.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      loader.AddPakFile(pak_path);
      const auto key = loader.MakePhysicsResourceKey(
        source_key, data::pak::core::ResourceIndexT { 1U });
      if (!key) {
        ADD_FAILURE() << "The pak's physics resource 1 has no locator";
        loader.Stop();
        co_return co::kJoin;
      }
      const auto loaded = co_await loader.LoadPhysicsResourceAsync(*key);
      if (!loaded) {
        ADD_FAILURE() << "The pak's physics resource 1 did not load";
      } else {
        EXPECT_THAT(loaded->GetData(), ::testing::ElementsAre(2U, 2U, 2U, 2U));
      }
      loader.Stop();
      co_return co::kJoin;
    };
  }

  NOLINT_TEST_F(AssetLoaderBasicTest, LoadsPhysicsResourcePayloadFromPak)
  {
    TestEventLoop loop;
    co::Run(loop, LoadPhysicsPayloadFromPak(&loop, TempDir() / "physics.pak"));
  }
} // namespace
} // namespace oxygen::content::testing
