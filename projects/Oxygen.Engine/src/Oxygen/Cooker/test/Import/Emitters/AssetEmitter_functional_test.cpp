//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Emitters/AssetEmitter.cpp

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/testing/GeometryBuilder.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

using oxygen::co::Co;
using oxygen::content::import::AssetEmitter;
using oxygen::content::import::ImportEventLoop;
using oxygen::data::AssetKey;
using oxygen::data::AssetType;
namespace co = oxygen::co;
namespace fixtures = oxygen::cooker::testing;

namespace {

//=== Test Helpers
//===---------------------------------------------------------//

//! Create a test AssetKey with a sequential ID.
auto MakeSequentialAssetKey(uint32_t id) -> AssetKey
{
  std::array<uint8_t, 16> key_bytes {};
  // Put the ID in the first 4 bytes for easy identification
  key_bytes.at(0) = static_cast<uint8_t>((id >> 24U) & 0xFFU);
  key_bytes.at(1) = static_cast<uint8_t>((id >> 16U) & 0xFFU);
  key_bytes.at(2) = static_cast<uint8_t>((id >> 8U) & 0xFFU);
  key_bytes.at(3) = static_cast<uint8_t>(id & 0xFFU);
  return AssetKey::FromBytes(key_bytes);
}

//=== Test Fixture ===--------------------------------------------------------//

//! Test fixture for AssetEmitter tests: the emitters publish to a file system
//! held in memory, through an inline driver.
class AssetEmitterTest : public ::testing::Test {
protected:
  auto SetUp() -> void override
  {
    loop_ = std::make_unique<ImportEventLoop>();
    driver_ = std::make_unique<oxygen::serio::IoDriver>(
      oxygen::serio::IoDriver::CreateInline().value());
  }

  auto TearDown() -> void override
  {
    emitter_.reset();
    driver_.reset();
    loop_.reset();
  }

  //! The storage the emitters publish to.
  [[nodiscard]] auto Storage() -> fixtures::MemoryFileSystem& { return files_; }

  [[nodiscard]] auto Driver() -> oxygen::serio::IoDriver& { return *driver_; }

  //! The cooked root the emitters write under.
  [[nodiscard]] static auto Root() -> std::filesystem::path
  {
    return "/cooked";
  }

  //! Whether the file `relpath` below the cooked root was written.
  [[nodiscard]] auto Exists(const std::string_view relpath) const -> bool
  {
    return files_.KindOf(Root() / relpath)
      == oxygen::content::cooked::PathKind::kFile;
  }

  //! The size of the file `relpath` below the cooked root.
  [[nodiscard]] auto SizeOf(const std::string_view relpath) const -> size_t
  {
    return files_.Bytes(Root() / relpath).size();
  }

  //! The bytes of the file `relpath` below the cooked root.
  [[nodiscard]] auto ReadBytes(const std::string_view relpath) const
    -> std::vector<std::byte>
  {
    const auto bytes = files_.Bytes(Root() / relpath);
    return { bytes.begin(), bytes.end() };
  }

  //! Runs `body` in a coroutine whose nursery receives the writes of the
  //! emitter it makes with `emitter_.emplace`; the nursery joins when `body`
  //! ends. The emitter stays for inspection afterwards.
  template <typename Body> auto RunInNursery(Body body) -> void
  {
    co::Run(*loop_, [&] -> Co<> {
      OXCO_WITH_NURSERY(writes)
      {
        co_await body(writes);
        co_return co::kJoin;
      };
    });
  }

  fixtures::MemoryFileSystem files_;
  std::unique_ptr<ImportEventLoop> loop_;
  std::unique_ptr<oxygen::serio::IoDriver> driver_;
  std::optional<AssetEmitter> emitter_;
};

//! Fixture for the one test that publishes real files under a temporary
//! directory.
class AssetEmitterRealFileTest : public oxygen::testing::TempDirTest {
protected:
  auto SetUp() -> void override { loop_ = std::make_unique<ImportEventLoop>(); }

  auto TearDown() -> void override
  {
    emitter_.reset();
    loop_.reset();
  }

  std::unique_ptr<ImportEventLoop> loop_;
  std::optional<AssetEmitter> emitter_;
};

//=== Basic Emission Tests
//===-------------------------------------------------//

NOLINT_TEST_F(AssetEmitterTest, EmitSingleMaterialCreatesFile)
{
  const auto key = MakeSequentialAssetKey(1);
  const auto bytes
    = fixtures::MaterialBuilder { "material-descriptor-content" }.Bytes();

  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());
    emitter.Emit(key, AssetType::kMaterial, "/.cooked/Materials/Wood.omat",
      "Materials/Wood.omat", bytes, {});
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  // File exists with correct content
  const auto* const file_path = "Materials/Wood.omat";
  EXPECT_TRUE(Exists(file_path));
  EXPECT_EQ(ReadBytes(file_path),
    fixtures::MaterialBuilder { "material-descriptor-content" }.Bytes());
}

NOLINT_TEST_F(AssetEmitterTest, EmitMultipleAssetsCreatesAllFiles)
{

  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());
    emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/Wood.omat", "Materials/Wood.omat",
      fixtures::MaterialBuilder { "wood-material" }.Bytes(), {});

    emitter.Emit(MakeSequentialAssetKey(2), AssetType::kGeometry,
      "/.cooked/Geometry/Cube.ogeo", "Geometry/Cube.ogeo",
      fixtures::GeometryBuilder { "cube-geometry" }.Bytes(), {});

    emitter.Emit(MakeSequentialAssetKey(3), AssetType::kScene,
      "/.cooked/Scenes/Level1.oscene", "Scenes/Level1.oscene",
      fixtures::SceneBuilder { "level1-scene" }.Bytes(), {});

    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  // All files exist with correct content
  EXPECT_EQ(ReadBytes("Materials/Wood.omat"),
    fixtures::MaterialBuilder { "wood-material" }.Bytes());
  EXPECT_EQ(ReadBytes("Geometry/Cube.ogeo"),
    fixtures::GeometryBuilder { "cube-geometry" }.Bytes());
  EXPECT_EQ(ReadBytes("Scenes/Level1.oscene"),
    fixtures::SceneBuilder { "level1-scene" }.Bytes());
}

NOLINT_TEST_F(AssetEmitterTest, CountTracksEmittedAssets)
{
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());

    // Assert initial state
    EXPECT_EQ(emitter.Count(), 0);

    for (uint32_t i = 1; i <= 5; ++i) {
      emitter.Emit(MakeSequentialAssetKey(i), AssetType::kMaterial,
        "/.cooked/Materials/Mat" + std::to_string(i) + ".omat",
        "Materials/Mat" + std::to_string(i) + ".omat",
        fixtures::MaterialBuilder { "mat-" + std::to_string(i) }.Bytes(), {});
      EXPECT_EQ(emitter.Count(), i);
    }

    const auto success = co_await emitter.Finalize();
    EXPECT_TRUE(success);
  });

  ASSERT_HAS_VALUE(emitter_);
  EXPECT_EQ(emitter_->Count(), 5);
}

//=== Record Tracking Tests
//===------------------------------------------------//

NOLINT_TEST_F(AssetEmitterTest, RecordsContainsCorrectMetadata)
{
  const auto key = MakeSequentialAssetKey(42);
  const auto bytes = fixtures::GeometryBuilder { "test-content" }.Bytes();

  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());
    emitter.Emit(key, AssetType::kGeometry, "/.cooked/Geometry/MyMesh.ogeo",
      "Geometry/MyMesh.ogeo", bytes, {});
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  ASSERT_HAS_VALUE(emitter_);
  const auto& records = emitter_->Records();
  ASSERT_EQ(records.size(), 1);

  EXPECT_EQ(records.at(0).key, key);
  EXPECT_EQ(records.at(0).asset_type, AssetType::kGeometry);
  EXPECT_EQ(records.at(0).virtual_path, "/.cooked/Geometry/MyMesh.ogeo");
  EXPECT_EQ(records.at(0).descriptor_relpath, "Geometry/MyMesh.ogeo");
  EXPECT_EQ(records.at(0).descriptor_size, static_cast<uint64_t>(bytes.size()));
}

NOLINT_TEST_F(AssetEmitterTest, EmitSameKeyTwiceUpdatesRecordAndOverwrites)
{
  const auto key = MakeSequentialAssetKey(7);

  auto first = fixtures::MaterialBuilder { "white" };
  first.Texture(
    &oxygen::data::pak::render::MaterialAssetDesc::base_color_texture,
    oxygen::data::pak::core::kFallbackResourceIndex);
  auto second = fixtures::MaterialBuilder { "error" };
  second.Texture(
    &oxygen::data::pak::render::MaterialAssetDesc::base_color_texture,
    oxygen::data::pak::core::kErrorTextureResourceIndex);
  second.PixelShader();
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());
    emitter.Emit(key, AssetType::kMaterial, "/.cooked/Materials/Wood.omat",
      "Materials/Wood.omat", first.Bytes(), first.References());
    emitter.Emit(key, AssetType::kMaterial, "/.cooked/Materials/Wood.omat",
      "Materials/Wood.omat", second.Bytes(), second.References());
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  ASSERT_HAS_VALUE(emitter_);
  EXPECT_EQ(emitter_->Count(), 1);
  ASSERT_EQ(emitter_->Records().size(), 1);
  EXPECT_EQ(emitter_->Records().at(0).key, key);
  EXPECT_EQ(
    emitter_->Records().at(0).descriptor_relpath, "Materials/Wood.omat");
  EXPECT_EQ(emitter_->Records().at(0).descriptor_size,
    static_cast<uint64_t>(second.Bytes().size()));

  const auto* const file_path = "Materials/Wood.omat";
  EXPECT_TRUE(Exists(file_path));
  EXPECT_EQ(ReadBytes(file_path), second.Bytes());
  EXPECT_EQ(emitter_->Records().at(0).references, second.References());
}

NOLINT_TEST_F(AssetEmitterTest, EmitVirtualPathConflictBetweenKeysThrows)
{
  const auto bytes = fixtures::MaterialBuilder { "content" }.Bytes();

  bool success = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());
    emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/Shared.omat", "Materials/SharedA.omat", bytes, {});

    EXPECT_THROW(
      emitter.Emit(MakeSequentialAssetKey(2), AssetType::kMaterial,
        "/.cooked/Materials/Shared.omat", "Materials/SharedB.omat", bytes, {}),
      std::runtime_error);

    success = (co_await emitter.Finalize()).has_value();
  });
  EXPECT_TRUE(success);
}

NOLINT_TEST_F(AssetEmitterTest, EmitDescriptorPathConflictBetweenKeysThrows)
{
  const auto bytes_a = fixtures::MaterialBuilder { "a" }.Bytes();
  const auto bytes_b = fixtures::MaterialBuilder { "b" }.Bytes();

  bool success = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());
    emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/A.omat", "Materials/Shared.omat", bytes_a, {});

    EXPECT_THROW(
      emitter.Emit(MakeSequentialAssetKey(2), AssetType::kMaterial,
        "/.cooked/Materials/B.omat", "Materials/Shared.omat", bytes_b, {}),
      std::runtime_error);

    success = (co_await emitter.Finalize()).has_value();
  });
  EXPECT_TRUE(success);
  EXPECT_EQ(ReadBytes("Materials/Shared.omat"),
    fixtures::MaterialBuilder { "a" }.Bytes());
}

NOLINT_TEST_F(
  AssetEmitterTest, EmitSameKeyRetargetDescriptorPathAllowsPathReuse)
{
  const auto shared_key = MakeSequentialAssetKey(7);

  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());
    emitter.Emit(shared_key, AssetType::kMaterial, "/.cooked/Materials/A.omat",
      "Materials/A.omat", fixtures::MaterialBuilder { "a-v1" }.Bytes(), {});
    emitter.Emit(shared_key, AssetType::kMaterial, "/.cooked/Materials/B.omat",
      "Materials/B.omat", fixtures::MaterialBuilder { "b-v2" }.Bytes(), {});
    emitter.Emit(MakeSequentialAssetKey(8), AssetType::kMaterial,
      "/.cooked/Materials/C.omat", "Materials/A.omat",
      fixtures::MaterialBuilder { "a-v3" }.Bytes(), {});
    const auto success = co_await emitter.Finalize();
    EXPECT_TRUE(success);
  });

  ASSERT_HAS_VALUE(emitter_);
  EXPECT_EQ(emitter_->Count(), 2);
  ASSERT_EQ(emitter_->Records().size(), 2);
  EXPECT_EQ(emitter_->Records().at(0).descriptor_relpath, "Materials/B.omat");
  EXPECT_EQ(emitter_->Records().at(1).descriptor_relpath, "Materials/A.omat");
  EXPECT_EQ(ReadBytes("Materials/B.omat"),
    fixtures::MaterialBuilder { "b-v2" }.Bytes());
  EXPECT_EQ(ReadBytes("Materials/A.omat"),
    fixtures::MaterialBuilder { "a-v3" }.Bytes());
}

NOLINT_TEST_F(AssetEmitterTest, RecordsPreservesEmissionOrder)
{

  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());
    emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/A.omat", "Materials/A.omat",
      fixtures::MaterialBuilder { "a" }.Bytes(), {});
    emitter.Emit(MakeSequentialAssetKey(2), AssetType::kGeometry,
      "/.cooked/Geometry/B.ogeo", "Geometry/B.ogeo",
      fixtures::GeometryBuilder { "b" }.Bytes(), {});
    emitter.Emit(MakeSequentialAssetKey(3), AssetType::kScene,
      "/.cooked/Scenes/C.oscene", "Scenes/C.oscene",
      fixtures::SceneBuilder { "c" }.Bytes(), {});
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  ASSERT_HAS_VALUE(emitter_);
  // Order preserved
  const auto& records = emitter_->Records();
  ASSERT_EQ(records.size(), 3);
  EXPECT_EQ(records.at(0).asset_type, AssetType::kMaterial);
  EXPECT_EQ(records.at(1).asset_type, AssetType::kGeometry);
  EXPECT_EQ(records.at(2).asset_type, AssetType::kScene);
}

//=== Finalization Tests
//===---------------------------------------------------//

NOLINT_TEST_F(AssetEmitterTest, FinalizeWaitsForPendingIO)
{

  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());
    emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/Mat1.omat", "Materials/Mat1.omat",
      fixtures::MaterialBuilder { "content-1" }.Bytes(), {});
    emitter.Emit(MakeSequentialAssetKey(2), AssetType::kMaterial,
      "/.cooked/Materials/Mat2.omat", "Materials/Mat2.omat",
      fixtures::MaterialBuilder { "content-2" }.Bytes(), {});

    const auto success = co_await emitter.Finalize();

    EXPECT_TRUE(success);
    EXPECT_EQ(emitter.ErrorCount(), 0);
  });
}

NOLINT_TEST_F(AssetEmitterTest, FinalizeNoAssetsSucceeds)
{

  bool success = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());
    success = (co_await emitter.Finalize()).has_value();
  });

  EXPECT_TRUE(success);
  ASSERT_HAS_VALUE(emitter_);
  EXPECT_EQ(emitter_->Count(), 0);
}

NOLINT_TEST_F(AssetEmitterTest, EmitAfterFinalizeThrows)
{

  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());
    const auto success = co_await emitter.Finalize();
    EXPECT_TRUE(success);

    EXPECT_THROW(emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
                   "/.cooked/Materials/After.omat", "Materials/After.omat",
                   fixtures::MaterialBuilder { "content" }.Bytes(), {}),
      std::runtime_error);
  });
}

//=== File Content Verification
//===--------------------------------------------//

NOLINT_TEST_F(AssetEmitterTest, FinalizeFileContentMatchesEmittedBytes)
{

  auto binary = fixtures::MaterialBuilder { "Binary" };
  binary.PixelShader();
  const auto expected_bytes = binary.Bytes();

  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());
    emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/Binary.omat", "Materials/Binary.omat", expected_bytes,
      {});
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  // File content matches exactly
  const auto* const file_path = "Materials/Binary.omat";
  const auto actual_bytes = ReadBytes(file_path);

  ASSERT_EQ(actual_bytes.size(), expected_bytes.size());
  EXPECT_EQ(std::memcmp(actual_bytes.data(), expected_bytes.data(),
              expected_bytes.size()),
    0);
}

//! On the real file system: the storage creates the nested folders the
//! descriptor path names.
NOLINT_TEST_F(AssetEmitterRealFileTest, EmitCreatesNestedDirectories)
{
  co::Run(*loop_, [&] -> Co<> {
    OXCO_WITH_NURSERY(writes)
    {
      auto& emitter
        = emitter_.emplace(oxygen::content::cooked::RealCookerStorage(),
          loop_->Driver(), writes, TempDir());
      emitter.Emit(MakeSequentialAssetKey(1), AssetType::kGeometry,
        "/.cooked/Deep/Nested/Path/Mesh.ogeo", "Deep/Nested/Path/Mesh.ogeo",
        fixtures::GeometryBuilder { "nested-mesh" }.Bytes(), {});
      EXPECT_TRUE((co_await emitter.Finalize()).has_value());
      co_return co::kJoin;
    };
  });

  // Nested file exists
  const auto file_path = TempDir() / "Deep" / "Nested" / "Path" / "Mesh.ogeo";
  EXPECT_TRUE(std::filesystem::exists(file_path));
  EXPECT_EQ(oxygen::cooker::test::ReadBytes(file_path),
    fixtures::GeometryBuilder { "nested-mesh" }.Bytes());
}

//=== State Query Tests
//===----------------------------------------------------//

//! `Emit` starts the write in the nursery; `Finalize` joins it, so the file is
//! there once it returns.
NOLINT_TEST_F(AssetEmitterTest, FinalizeJoinsTheWritesEmitStarted)
{
  bool success = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());
    emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/Mat.omat", "Materials/Mat.omat",
      fixtures::MaterialBuilder { "content" }.Bytes(), {});

    success = (co_await emitter.Finalize()).has_value();
  });

  EXPECT_TRUE(success);
  EXPECT_EQ(ReadBytes("Materials/Mat.omat"),
    fixtures::MaterialBuilder { "content" }.Bytes());
}

NOLINT_TEST_F(AssetEmitterTest, ErrorCountZeroAfterSuccessfulWrites)
{

  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());
    for (uint32_t i = 1; i <= 10; ++i) {
      emitter.Emit(MakeSequentialAssetKey(i), AssetType::kMaterial,
        "/.cooked/Materials/Mat" + std::to_string(i) + ".omat",
        "Materials/Mat" + std::to_string(i) + ".omat",
        fixtures::MaterialBuilder { "content-" + std::to_string(i) }.Bytes(),
        {});
    }
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  ASSERT_HAS_VALUE(emitter_);
  EXPECT_EQ(emitter_->ErrorCount(), 0);
}

//=== Edge Cases ===----------------------------------------------------------//

NOLINT_TEST_F(AssetEmitterTest, EmptyDescriptorIsRejectedBeforePublication)
{
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());
    EXPECT_THROW(
      emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
        "/.cooked/Materials/Empty.omat", "Materials/Empty.omat", {}, {}),
      std::runtime_error);
    EXPECT_EQ(emitter.Count(), 0U);
    EXPECT_TRUE(emitter.Records().empty());
    EXPECT_FALSE(Exists("Materials/Empty.omat"));
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });
  EXPECT_FALSE(Exists("Materials/Empty.omat"));
}

NOLINT_TEST_F(
  AssetEmitterTest, EmitSyncRejectsMalformedDescriptorBeforePublication)
{
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());
    bool rejected = false;
    try {
      co_await emitter.EmitSync(MakeSequentialAssetKey(1), AssetType::kMaterial,
        "/.cooked/Materials/Invalid.omat", "Materials/Invalid.omat", {}, {});
    } catch (const std::runtime_error&) {
      rejected = true;
    }
    EXPECT_TRUE(rejected);
    EXPECT_EQ(emitter.Count(), 0U);
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });
  EXPECT_FALSE(Exists("Materials/Invalid.omat"));
}

NOLINT_TEST_F(AssetEmitterTest, EmitLargeDescriptorWrittenCorrectly)
{
  constexpr size_t kLargeBytes = size_t { 100U } * 1024U;
  constexpr uint32_t kNodeCount = 2048U;
  auto large = fixtures::SceneBuilder { "Large" };
  for (uint32_t i = 0U; i < kNodeCount; ++i) {
    large.Node("node-" + std::to_string(i));
  }
  const auto large_bytes = large.Bytes();
  ASSERT_GE(large_bytes.size(), kLargeBytes);
  const auto expected_size = large_bytes.size();

  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());
    emitter.Emit(MakeSequentialAssetKey(1), AssetType::kScene,
      "/.cooked/Scenes/Large.oscene", "Scenes/Large.oscene", large_bytes, {});
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  // File has correct size and content
  const auto* const file_path = "Scenes/Large.oscene";
  EXPECT_EQ(SizeOf(file_path), expected_size);

  const auto actual_bytes = ReadBytes(file_path);
  EXPECT_EQ(
    std::memcmp(actual_bytes.data(), large_bytes.data(), expected_size), 0);
}

//=== Path Validation Tests
//===------------------------------------------------//

//! One invalid `(virtual_path, descriptor_relpath)` pair.
struct PathRejectionCase final {
  const char* name;
  std::string virtual_path;
  std::string relative_path;
};

//! Parameterized fixture for paths `AssetEmitter::Emit` must reject.
class AssetEmitterPathRejectionTest
  : public AssetEmitterTest,
    public ::testing::WithParamInterface<PathRejectionCase> { };

NOLINT_TEST_P(AssetEmitterPathRejectionTest, EmitThrows)
{
  const auto bytes = fixtures::MaterialBuilder { "test" }.Bytes();

  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());
    EXPECT_THROW(
      emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
        GetParam().virtual_path, GetParam().relative_path, bytes, {}),
      std::runtime_error);
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });
}

INSTANTIATE_TEST_SUITE_P(InvalidPaths, AssetEmitterPathRejectionTest,
  ::testing::Values(PathRejectionCase { "RelativePathWithBackslash",
                      "/.cooked/Materials/Wood.omat", "Materials\\Wood.omat" },
    PathRejectionCase { "RelativePathWithLeadingSlash",
      "/.cooked/Materials/Wood.omat", "/Materials/Wood.omat" },
    PathRejectionCase { "RelativePathWithColon", "/.cooked/Materials/Wood.omat",
      "C:Materials/Wood.omat" },
    PathRejectionCase { "RelativePathWithDoubleSlash",
      "/.cooked/Materials/Wood.omat", "Materials//Wood.omat" },
    PathRejectionCase { "RelativePathWithDotSegment",
      "/.cooked/Materials/Wood.omat", "Materials/./Wood.omat" },
    PathRejectionCase { "RelativePathWithDotDotSegment",
      "/.cooked/Materials/Wood.omat", "Materials/../Wood.omat" },
    PathRejectionCase { "VirtualPathWithoutLeadingSlash",
      ".cooked/Materials/Wood.omat", "Materials/Wood.omat" },
    PathRejectionCase { "VirtualPathWithBackslash",
      "/.cooked\\Materials\\Wood.omat", "Materials/Wood.omat" },
    PathRejectionCase { "VirtualPathWithDoubleSlash",
      "/.cooked//Materials/Wood.omat", "Materials/Wood.omat" },
    PathRejectionCase {
      "EmptyRelativePath", "/.cooked/Materials/Wood.omat", "" },
    PathRejectionCase { "EmptyVirtualPath", "", "Materials/Wood.omat" }),
  [](const ::testing::TestParamInfo<PathRejectionCase>& info) -> std::string {
    return std::string(info.param.name);
  });

NOLINT_TEST_F(AssetEmitterTest, EmitVirtualPathWithCustomMountRootAccepted)
{
  const auto bytes = fixtures::MaterialBuilder { "test" }.Bytes();

  bool success = false;
  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());
    EXPECT_NO_THROW(
      emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
        "/Custom/Materials/Wood.omat", "Materials/Wood.omat", bytes, {}));

    success = (co_await emitter.Finalize()).has_value();
  });

  EXPECT_TRUE(success);
  EXPECT_EQ(ReadBytes("Materials/Wood.omat"),
    fixtures::MaterialBuilder { "test" }.Bytes());
}

//=== SHA-256 Tests
//===--------------------------------------------------------//

NOLINT_TEST_F(AssetEmitterTest, RecordsContainsSha256Hash)
{
  const auto bytes
    = fixtures::MaterialBuilder { "test-content-for-hashing" }.Bytes();
  const auto expected_hash = oxygen::base::ComputeSha256(
    std::span<const std::byte>(bytes.data(), bytes.size()));

  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());
    emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/Hashed.omat", "Materials/Hashed.omat", bytes, {});
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  ASSERT_HAS_VALUE(emitter_);
  const auto& records = emitter_->Records();
  ASSERT_EQ(records.size(), 1);
  EXPECT_EQ(records.at(0).descriptor_sha256, expected_hash);
}

NOLINT_TEST_F(AssetEmitterTest, RecordsDifferentContentHasDifferentHash)
{
  const auto bytes1 = fixtures::MaterialBuilder { "content-one" }.Bytes();
  const auto bytes2 = fixtures::MaterialBuilder { "content-two" }.Bytes();

  RunInNursery([&](co::Nursery& writes) -> Co<> {
    auto& emitter = emitter_.emplace(Storage(), Driver(), writes, Root());
    emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/One.omat", "Materials/One.omat", bytes1, {});
    emitter.Emit(MakeSequentialAssetKey(2), AssetType::kMaterial,
      "/.cooked/Materials/Two.omat", "Materials/Two.omat", bytes2, {});
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  ASSERT_HAS_VALUE(emitter_);
  const auto& records = emitter_->Records();
  ASSERT_EQ(records.size(), 2);
  EXPECT_NE(records.at(0).descriptor_sha256, records.at(1).descriptor_sha256);
}

} // namespace
