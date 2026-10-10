//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "AssetLoader_test.h"
#include "Fixtures/LooseCookedTestWriter.h"
#include "Fixtures/PakTestWriter.h"

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/Loaders/PhysicsResourceLoader.h>
#include <Oxygen/Content/Loaders/PhysicsSceneLoader.h>
#include <Oxygen/Content/ResidencyPolicy.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Serio/FileLock.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {
namespace {
  namespace physics = data::pak::physics;
  using co::testing::TestEventLoop;

  const auto kScene = data::AssetKey::FromVirtualPath("/Test/scene.oscene");
  const auto kSidecar = data::AssetKey::FromVirtualPath("/Test/scene.opscene");
  const auto kShape = data::AssetKey::FromVirtualPath("/Test/shape.oshape");
  const auto kMaterial
    = data::AssetKey::FromVirtualPath("/Test/material.opmat");
  const auto kPayload = data::AssetKey::FromVirtualPath("/Test/shape.payload");

  auto WriteSidecar(const std::filesystem::path& root) -> void
  {
    physics::PhysicsSceneAssetDesc descriptor {};
    descriptor.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kPhysicsScene);
    descriptor.header.version = physics::kPhysicsSceneAssetVersion;
    descriptor.target_scene_key = kScene;
    descriptor.target_node_count = 1U;
    descriptor.component_table_count = 1U;
    descriptor.component_table_directory_offset = sizeof(descriptor);
    physics::PhysicsComponentTableDesc table {};
    table.binding_type = physics::PhysicsBindingType::kRigidBody;
    table.table.offset = sizeof(descriptor) + sizeof(table);
    table.table.count = 1U;
    table.table.entry_size = sizeof(physics::RigidBodyBindingRecord);
    physics::RigidBodyBindingRecord body {};
    body.shape_asset_key = kShape;
    body.material_asset_key = kMaterial;
    std::vector<std::byte> bytes;
    const auto append = [&bytes](const auto& record) -> auto {
      const auto chunk = std::as_bytes(std::span(&record, 1U));
      bytes.insert(bytes.end(), chunk.begin(), chunk.end());
    };
    append(descriptor);
    append(table);
    append(body);
    LooseCookedTestWriter writer(root);
    writer.SetSourceKey(data::SourceKey { Uuid::Generate() });
    writer.WriteAssetDescriptor(kSidecar, data::AssetType::kPhysicsScene,
      "/Test/scene.opscene", "scene.opscene", bytes,
      data::AssetReferences::Create({},
        {
          {
            .key = kScene,
            .kind = data::KeyReferenceKind::kLogical,
            .expected_type = data::AssetType::kScene,
          },
          {
            .key = kShape,
            .kind = data::KeyReferenceKind::kAsset,
            .expected_type = data::AssetType::kCollisionShape,
          },
          {
            .key = kMaterial,
            .kind = data::KeyReferenceKind::kAsset,
            .expected_type = data::AssetType::kPhysicsMaterial,
          },
        })
        .value());
    static_cast<void>(writer.Finish());
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
    LooseCookedTestWriter writer(root);
    writer.SetSourceKey(data::SourceKey { Uuid::Generate() });
    physics::CollisionShapeAssetDesc shape {};
    shape.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kCollisionShape);
    shape.header.version = scenario == DependencyCase::kWrongShapeVersion
      ? 0U
      : physics::kCollisionShapeAssetVersion;
    shape.shape_type = physics::ShapeType::kConvexHull;
    shape.material_asset_key = kMaterial;
    shape.cooked_shape_ref = {
      .payload_asset_key = kPayload,
      .payload_type = physics::ShapePayloadType::kConvex,
    };
    writer.WriteAssetDescriptor(kShape, data::AssetType::kCollisionShape,
      "/Test/shape.oshape", "shape.oshape",
      std::as_bytes(std::span(&shape, 1U)),
      data::AssetReferences::Create({},
        {
          {
            .key = kMaterial,
            .kind = data::KeyReferenceKind::kAsset,
            .expected_type = data::AssetType::kPhysicsMaterial,
          },
          {
            .key = kPayload,
            .kind = data::KeyReferenceKind::kPhysicsResource,
            .expected_type = data::AssetType::kUnknown,
          },
        })
        .value());
    if (scenario != DependencyCase::kMissingMaterial) {
      physics::PhysicsMaterialAssetDesc material {};
      material.header.asset_type
        = static_cast<uint8_t>(data::AssetType::kPhysicsMaterial);
      material.header.version = physics::kPhysicsMaterialAssetVersion;
      material.restitution = static_cast<float>(payload) / 10.0F;
      writer.WriteAssetDescriptor(kMaterial, data::AssetType::kPhysicsMaterial,
        "/Test/material.opmat", "material.opmat",
        std::as_bytes(std::span(&material, 1U)));
    }
    const std::array data { static_cast<std::byte>(payload) };
    physics::PhysicsResourceDesc resource {};
    resource.resource_asset_key = kPayload;
    resource.size_bytes
      = static_cast<data::pak::core::DataBlobSizeT>(data.size());
    std::ranges::copy(
      base::ComputeSha256(data), std::begin(resource.content_hash));
    const std::array table { physics::PhysicsResourceDesc {}, resource };
    writer.WriteFile(data::loose_cooked::FileKind::kPhysicsTable,
      "physics.table", std::as_bytes(std::span(table)));
    writer.WriteFile(
      data::loose_cooked::FileKind::kPhysicsData, "physics.data", data);
    static_cast<void>(writer.Finish());
    const auto lease = serio::FileLock::TryAcquire(
      root / data::loose_cooked::kGenerationLeaseFileName,
      serio::FileLockMode::kExclusive, serio::FileLockOpenMode::kOpenOrCreate);
    if (!lease) {
      throw std::system_error(lease.error());
    }
  }

  auto ExercisePhysicsBindings(TestEventLoop* loop,
    const std::filesystem::path root, const DependencyCase scenario) -> co::Co<>
  {
    const auto owner_root = root / "owner";
    const auto first_root = root / "first";
    const auto second_root = root / "second";
    WriteSidecar(owner_root);
    WriteDependencies(first_root, 1U, scenario);
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
      std::shared_ptr<data::PhysicsSceneAsset> first;
      std::string failure;
      try {
        first = co_await loader.LoadAssetAsync<data::PhysicsSceneAsset>(
          kSidecar, LoadRequest { .scope = original });
      } catch (const std::runtime_error& error) {
        failure = error.what();
      }
      if (scenario != DependencyCase::kValid) {
        const auto expected = scenario == DependencyCase::kMissingMaterial
          ? "Required physics descriptor is missing or has the wrong type: "
            + data::to_string(kMaterial)
          : std::string("Collision shape descriptor type/version is invalid");
        EXPECT_EQ(failure, expected);
        EXPECT_FALSE(first);
        EXPECT_FALSE(loader.GetPhysicsSceneAsset(kSidecar));
        loader.Stop();
        co_return co::kJoin;
      }
      EXPECT_TRUE(failure.empty()) << failure;
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
    co::Run(
      loop, ExercisePhysicsBindings(&loop, temp_dir_, DependencyCase::kValid));
  }
  NOLINT_TEST_F(
    AssetLoaderBasicTest, MissingPhysicsMaterialPreventsSidecarPublication)
  {
    TestEventLoop loop;
    co::Run(loop,
      ExercisePhysicsBindings(
        &loop, temp_dir_, DependencyCase::kMissingMaterial));
  }
  NOLINT_TEST_F(
    AssetLoaderBasicTest, InvalidShapeVersionPreventsSidecarPublication)
  {
    TestEventLoop loop;
    co::Run(loop,
      ExercisePhysicsBindings(
        &loop, temp_dir_, DependencyCase::kWrongShapeVersion));
  }
  //! Mounts a pak whose physics table holds the reserved entry 0 and a 4-byte
  //! payload at entry 1, then loads entry 1 through the pak's source key.
  auto LoadPhysicsPayloadFromPak(
    TestEventLoop* loop, const std::filesystem::path pak_path) -> co::Co<>
  {
    const auto payload = std::vector<std::byte>(4U, std::byte { 2 });
    auto config = PakTestWriter::Config {};
    auto entry = PakTestWriter::Config::PhysicsEntry {};
    entry.desc.resource_asset_key = kPayload;
    const auto hash = base::ComputeSha256(payload);
    std::ranges::copy(hash, std::begin(entry.desc.content_hash));
    entry.payload = payload;
    config.physics_resources
      = { PakTestWriter::Config::PhysicsEntry {}, std::move(entry) };
    PakTestWriter(pak_path).Write(config);
    const auto source_key
      = data::SourceKey::FromBytes(config.header.source_identity).value();

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
    co::Run(loop, LoadPhysicsPayloadFromPak(&loop, temp_dir_ / "physics.pak"));
  }
} // namespace
} // namespace oxygen::content::testing
