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
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/IAsyncFileWriter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/WindowsFileWriter.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Cooker/Test/Support/DescriptorFixtures.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::content::import;
using namespace oxygen::co;
using oxygen::data::AssetKey;
using oxygen::data::AssetType;
namespace co = oxygen::co;
namespace fixtures = oxygen::content::test;

namespace {

using oxygen::cooker::test::ReadBytes;

//=== Test Helpers
//===---------------------------------------------------------//

//! Create a test AssetKey with a sequential ID.
auto MakeSequentialAssetKey(uint32_t id) -> AssetKey
{
  std::array<uint8_t, 16> key_bytes {};
  // Put the ID in the first 4 bytes for easy identification
  key_bytes.at(0) = static_cast<uint8_t>((id >> 24) & 0xFF);
  key_bytes.at(1) = static_cast<uint8_t>((id >> 16) & 0xFF);
  key_bytes.at(2) = static_cast<uint8_t>((id >> 8) & 0xFF);
  key_bytes.at(3) = static_cast<uint8_t>(id & 0xFF);
  return AssetKey::FromBytes(key_bytes);
}

//=== Test Fixture ===--------------------------------------------------------//

//! Test fixture for AssetEmitter tests.
class AssetEmitterTest : public oxygen::cooker::test::TempDirTest {
protected:
  auto SetUp() -> void override
  {
    loop_ = std::make_unique<ImportEventLoop>();
    writer_ = std::make_unique<WindowsFileWriter>(*loop_);
  }

  auto TearDown() -> void override
  {
    writer_.reset();
    loop_.reset();
  }

  auto Layout() const -> const LooseCookedLayout& { return layout_; }

  std::unique_ptr<ImportEventLoop> loop_;
  std::unique_ptr<WindowsFileWriter> writer_;
  LooseCookedLayout layout_ {}; // Uses default paths
};

//=== Basic Emission Tests
//===-------------------------------------------------//

NOLINT_TEST_F(AssetEmitterTest, EmitSingleMaterialCreatesFile)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());
  const auto key = MakeSequentialAssetKey(1);
  const auto bytes
    = fixtures::MaterialDescriptor("material-descriptor-content").bytes;

  co::Run(*loop_, [&] -> Co<> {
    emitter.Emit(key, AssetType::kMaterial, "/.cooked/Materials/Wood.omat",
      "Materials/Wood.omat", bytes, {});
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  // File exists with correct content
  const auto file_path = TempDir() / "Materials" / "Wood.omat";
  EXPECT_TRUE(std::filesystem::exists(file_path));
  EXPECT_EQ(ReadBytes(file_path),
    fixtures::MaterialDescriptor("material-descriptor-content").bytes);
}

NOLINT_TEST_F(AssetEmitterTest, EmitMultipleAssetsCreatesAllFiles)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());

  co::Run(*loop_, [&] -> Co<> {
    emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/Wood.omat", "Materials/Wood.omat",
      fixtures::MaterialDescriptor("wood-material").bytes, {});

    emitter.Emit(MakeSequentialAssetKey(2), AssetType::kGeometry,
      "/.cooked/Geometry/Cube.ogeo", "Geometry/Cube.ogeo",
      fixtures::GeometryDescriptor("cube-geometry").bytes, {});

    emitter.Emit(MakeSequentialAssetKey(3), AssetType::kScene,
      "/.cooked/Scenes/Level1.oscene", "Scenes/Level1.oscene",
      fixtures::SceneDescriptor("level1-scene").bytes, {});

    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  // All files exist with correct content
  EXPECT_EQ(ReadBytes(TempDir() / "Materials" / "Wood.omat"),
    fixtures::MaterialDescriptor("wood-material").bytes);
  EXPECT_EQ(ReadBytes(TempDir() / "Geometry" / "Cube.ogeo"),
    fixtures::GeometryDescriptor("cube-geometry").bytes);
  EXPECT_EQ(ReadBytes(TempDir() / "Scenes" / "Level1.oscene"),
    fixtures::SceneDescriptor("level1-scene").bytes);
}

NOLINT_TEST_F(AssetEmitterTest, CountTracksEmittedAssets)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());

  // Assert initial state
  EXPECT_EQ(emitter.Count(), 0);

  co::Run(*loop_, [&] -> Co<> {
    for (uint32_t i = 1; i <= 5; ++i) {
      emitter.Emit(MakeSequentialAssetKey(i), AssetType::kMaterial,
        "/.cooked/Materials/Mat" + std::to_string(i) + ".omat",
        "Materials/Mat" + std::to_string(i) + ".omat",
        fixtures::MaterialDescriptor("mat-" + std::to_string(i)).bytes, {});
      EXPECT_EQ(emitter.Count(), i);
    }

    const auto success = co_await emitter.Finalize();
    EXPECT_TRUE(success);
  });

  EXPECT_EQ(emitter.Count(), 5);
}

//=== Record Tracking Tests
//===------------------------------------------------//

NOLINT_TEST_F(AssetEmitterTest, RecordsContainsCorrectMetadata)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());
  const auto key = MakeSequentialAssetKey(42);
  const auto bytes = fixtures::GeometryDescriptor("test-content").bytes;

  co::Run(*loop_, [&] -> Co<> {
    emitter.Emit(key, AssetType::kGeometry, "/.cooked/Geometry/MyMesh.ogeo",
      "Geometry/MyMesh.ogeo", bytes, {});
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  const auto& records = emitter.Records();
  ASSERT_EQ(records.size(), 1);

  EXPECT_EQ(records.at(0).key, key);
  EXPECT_EQ(records.at(0).asset_type, AssetType::kGeometry);
  EXPECT_EQ(records.at(0).virtual_path, "/.cooked/Geometry/MyMesh.ogeo");
  EXPECT_EQ(records.at(0).descriptor_relpath, "Geometry/MyMesh.ogeo");
  EXPECT_EQ(records.at(0).descriptor_size, static_cast<uint64_t>(bytes.size()));
}

NOLINT_TEST_F(AssetEmitterTest, EmitSameKeyTwiceUpdatesRecordAndOverwrites)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());
  const auto key = MakeSequentialAssetKey(7);

  const auto first = fixtures::TexturedMaterialDescriptor(
    "white", oxygen::data::pak::core::kFallbackResourceIndex);
  const auto second = fixtures::TexturedMaterialDescriptor("error",
    oxygen::data::pak::core::kErrorTextureResourceIndex,
    fixtures::MaterialVariant::kWithShader);
  co::Run(*loop_, [&] -> Co<> {
    emitter.Emit(key, AssetType::kMaterial, "/.cooked/Materials/Wood.omat",
      "Materials/Wood.omat", first.bytes, first.references);
    emitter.Emit(key, AssetType::kMaterial, "/.cooked/Materials/Wood.omat",
      "Materials/Wood.omat", second.bytes, second.references);
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  EXPECT_EQ(emitter.Count(), 1);
  ASSERT_EQ(emitter.Records().size(), 1);
  EXPECT_EQ(emitter.Records().at(0).key, key);
  EXPECT_EQ(emitter.Records().at(0).descriptor_relpath, "Materials/Wood.omat");
  EXPECT_EQ(emitter.Records().at(0).descriptor_size,
    static_cast<uint64_t>(second.bytes.size()));

  const auto file_path = TempDir() / "Materials" / "Wood.omat";
  EXPECT_TRUE(std::filesystem::exists(file_path));
  EXPECT_EQ(ReadBytes(file_path), second.bytes);
  EXPECT_EQ(emitter.Records().at(0).references, second.references);
}

NOLINT_TEST_F(AssetEmitterTest, EmitVirtualPathConflictBetweenKeysThrows)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());
  const auto bytes = fixtures::MaterialDescriptor("content").bytes;

  emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
    "/.cooked/Materials/Shared.omat", "Materials/SharedA.omat", bytes, {});

  EXPECT_THROW(
    emitter.Emit(MakeSequentialAssetKey(2), AssetType::kMaterial,
      "/.cooked/Materials/Shared.omat", "Materials/SharedB.omat", bytes, {}),
    std::runtime_error);

  bool success = false;
  co::Run(*loop_,
    [&] -> Co<> { success = (co_await emitter.Finalize()).has_value(); });
  EXPECT_TRUE(success);
}

NOLINT_TEST_F(AssetEmitterTest, EmitDescriptorPathConflictBetweenKeysThrows)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());
  const auto bytes_a = fixtures::MaterialDescriptor("a").bytes;
  const auto bytes_b = fixtures::MaterialDescriptor("b").bytes;

  emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
    "/.cooked/Materials/A.omat", "Materials/Shared.omat", bytes_a, {});

  EXPECT_THROW(
    emitter.Emit(MakeSequentialAssetKey(2), AssetType::kMaterial,
      "/.cooked/Materials/B.omat", "Materials/Shared.omat", bytes_b, {}),
    std::runtime_error);

  bool success = false;
  co::Run(*loop_,
    [&] -> Co<> { success = (co_await emitter.Finalize()).has_value(); });
  EXPECT_TRUE(success);
  EXPECT_EQ(ReadBytes(TempDir() / "Materials" / "Shared.omat"),
    fixtures::MaterialDescriptor("a").bytes);
}

NOLINT_TEST_F(
  AssetEmitterTest, EmitSameKeyRetargetDescriptorPathAllowsPathReuse)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());
  const auto shared_key = MakeSequentialAssetKey(7);

  co::Run(*loop_, [&] -> Co<> {
    emitter.Emit(shared_key, AssetType::kMaterial, "/.cooked/Materials/A.omat",
      "Materials/A.omat", fixtures::MaterialDescriptor("a-v1").bytes, {});
    emitter.Emit(shared_key, AssetType::kMaterial, "/.cooked/Materials/B.omat",
      "Materials/B.omat", fixtures::MaterialDescriptor("b-v2").bytes, {});
    emitter.Emit(MakeSequentialAssetKey(8), AssetType::kMaterial,
      "/.cooked/Materials/C.omat", "Materials/A.omat",
      fixtures::MaterialDescriptor("a-v3").bytes, {});
    const auto success = co_await emitter.Finalize();
    EXPECT_TRUE(success);
  });

  EXPECT_EQ(emitter.Count(), 2);
  ASSERT_EQ(emitter.Records().size(), 2);
  EXPECT_EQ(emitter.Records().at(0).descriptor_relpath, "Materials/B.omat");
  EXPECT_EQ(emitter.Records().at(1).descriptor_relpath, "Materials/A.omat");
  EXPECT_EQ(ReadBytes(TempDir() / "Materials" / "B.omat"),
    fixtures::MaterialDescriptor("b-v2").bytes);
  EXPECT_EQ(ReadBytes(TempDir() / "Materials" / "A.omat"),
    fixtures::MaterialDescriptor("a-v3").bytes);
}

NOLINT_TEST_F(AssetEmitterTest, RecordsPreservesEmissionOrder)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());

  co::Run(*loop_, [&] -> Co<> {
    emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/A.omat", "Materials/A.omat",
      fixtures::MaterialDescriptor("a").bytes, {});
    emitter.Emit(MakeSequentialAssetKey(2), AssetType::kGeometry,
      "/.cooked/Geometry/B.ogeo", "Geometry/B.ogeo",
      fixtures::GeometryDescriptor("b").bytes, {});
    emitter.Emit(MakeSequentialAssetKey(3), AssetType::kScene,
      "/.cooked/Scenes/C.oscene", "Scenes/C.oscene",
      fixtures::SceneDescriptor("c").bytes, {});
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  // Order preserved
  const auto& records = emitter.Records();
  ASSERT_EQ(records.size(), 3);
  EXPECT_EQ(records.at(0).asset_type, AssetType::kMaterial);
  EXPECT_EQ(records.at(1).asset_type, AssetType::kGeometry);
  EXPECT_EQ(records.at(2).asset_type, AssetType::kScene);
}

//=== Finalization Tests
//===---------------------------------------------------//

NOLINT_TEST_F(AssetEmitterTest, FinalizeWaitsForPendingIO)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());

  co::Run(*loop_, [&] -> Co<> {
    emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/Mat1.omat", "Materials/Mat1.omat",
      fixtures::MaterialDescriptor("content-1").bytes, {});
    emitter.Emit(MakeSequentialAssetKey(2), AssetType::kMaterial,
      "/.cooked/Materials/Mat2.omat", "Materials/Mat2.omat",
      fixtures::MaterialDescriptor("content-2").bytes, {});

    const auto success = co_await emitter.Finalize();

    EXPECT_TRUE(success);
    EXPECT_EQ(emitter.PendingCount(), 0);
    EXPECT_EQ(emitter.ErrorCount(), 0);
  });
}

NOLINT_TEST_F(AssetEmitterTest, FinalizeNoAssetsSucceeds)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());

  bool success = false;
  co::Run(*loop_,
    [&] -> Co<> { success = (co_await emitter.Finalize()).has_value(); });

  EXPECT_TRUE(success);
  EXPECT_EQ(emitter.Count(), 0);
}

NOLINT_TEST_F(AssetEmitterTest, EmitAfterFinalizeThrows)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());

  co::Run(*loop_, [&] -> Co<> {
    const auto success = co_await emitter.Finalize();
    EXPECT_TRUE(success);

    EXPECT_THROW(emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
                   "/.cooked/Materials/After.omat", "Materials/After.omat",
                   fixtures::MaterialDescriptor("content").bytes, {}),
      std::runtime_error);
  });
}

//=== File Content Verification
//===--------------------------------------------//

NOLINT_TEST_F(AssetEmitterTest, FinalizeFileContentMatchesEmittedBytes)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());

  const auto expected_bytes = fixtures::MaterialDescriptor(
    "Binary", fixtures::MaterialVariant::kWithShader)
                                .bytes;

  co::Run(*loop_, [&] -> Co<> {
    emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/Binary.omat", "Materials/Binary.omat", expected_bytes,
      {});
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  // File content matches exactly
  const auto file_path = TempDir() / "Materials" / "Binary.omat";
  const auto actual_bytes = ReadBytes(file_path);

  ASSERT_EQ(actual_bytes.size(), expected_bytes.size());
  EXPECT_EQ(std::memcmp(actual_bytes.data(), expected_bytes.data(),
              expected_bytes.size()),
    0);
}

NOLINT_TEST_F(AssetEmitterTest, EmitCreatesNestedDirectories)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());

  co::Run(*loop_, [&] -> Co<> {
    emitter.Emit(MakeSequentialAssetKey(1), AssetType::kGeometry,
      "/.cooked/Deep/Nested/Path/Mesh.ogeo", "Deep/Nested/Path/Mesh.ogeo",
      fixtures::GeometryDescriptor("nested-mesh").bytes, {});
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  // Nested file exists
  const auto file_path = TempDir() / "Deep" / "Nested" / "Path" / "Mesh.ogeo";
  EXPECT_TRUE(std::filesystem::exists(file_path));
  EXPECT_EQ(
    ReadBytes(file_path), fixtures::GeometryDescriptor("nested-mesh").bytes);
}

//=== State Query Tests
//===----------------------------------------------------//

NOLINT_TEST_F(AssetEmitterTest, PendingCountReflectsQueuedWrites)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());

  bool had_pending = false;
  bool success = false;
  co::Run(*loop_, [&] -> Co<> {
    emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/Mat.omat", "Materials/Mat.omat",
      fixtures::MaterialDescriptor("content").bytes, {});
    had_pending = emitter.PendingCount() > 0;

    success = (co_await emitter.Finalize()).has_value();
  });

  EXPECT_TRUE(had_pending);
  EXPECT_TRUE(success);
}

NOLINT_TEST_F(AssetEmitterTest, ErrorCountZeroAfterSuccessfulWrites)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());

  co::Run(*loop_, [&] -> Co<> {
    for (uint32_t i = 1; i <= 10; ++i) {
      emitter.Emit(MakeSequentialAssetKey(i), AssetType::kMaterial,
        "/.cooked/Materials/Mat" + std::to_string(i) + ".omat",
        "Materials/Mat" + std::to_string(i) + ".omat",
        fixtures::MaterialDescriptor("content-" + std::to_string(i)).bytes, {});
    }
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  EXPECT_EQ(emitter.ErrorCount(), 0);
}

//=== Edge Cases ===----------------------------------------------------------//

NOLINT_TEST_F(AssetEmitterTest, EmptyDescriptorIsRejectedBeforePublication)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());
  EXPECT_THROW(
    emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/Empty.omat", "Materials/Empty.omat", {}, {}),
    std::runtime_error);
  EXPECT_EQ(emitter.Count(), 0U);
  EXPECT_TRUE(emitter.Records().empty());
  EXPECT_FALSE(std::filesystem::exists(TempDir() / "Materials/Empty.omat"));
  co::Run(*loop_,
    [&] -> Co<> { EXPECT_TRUE((co_await emitter.Finalize()).has_value()); });
  EXPECT_FALSE(std::filesystem::exists(TempDir() / "Materials/Empty.omat"));
}

NOLINT_TEST_F(
  AssetEmitterTest, EmitSyncRejectsMalformedDescriptorBeforePublication)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());
  co::Run(*loop_, [&] -> Co<> {
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
  EXPECT_FALSE(std::filesystem::exists(TempDir() / "Materials/Invalid.omat"));
}

NOLINT_TEST_F(AssetEmitterTest, EmitLargeDescriptorWrittenCorrectly)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());

  constexpr uint32_t kStringTableBytes = 100U * 1024U;
  const auto large_bytes
    = fixtures::SceneDescriptor("Large", kStringTableBytes).bytes;
  const auto expected_size = large_bytes.size();

  co::Run(*loop_, [&] -> Co<> {
    emitter.Emit(MakeSequentialAssetKey(1), AssetType::kScene,
      "/.cooked/Scenes/Large.oscene", "Scenes/Large.oscene", large_bytes, {});
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  // File has correct size and content
  const auto file_path = TempDir() / "Scenes" / "Large.oscene";
  EXPECT_EQ(std::filesystem::file_size(file_path), expected_size);

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
  AssetEmitter emitter(*writer_, Layout(), TempDir());
  const auto bytes = fixtures::MaterialDescriptor("test").bytes;

  EXPECT_THROW(emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
                 GetParam().virtual_path, GetParam().relative_path, bytes, {}),
    std::runtime_error);
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
  [](const ::testing::TestParamInfo<PathRejectionCase>& info) {
    return std::string(info.param.name);
  });

NOLINT_TEST_F(AssetEmitterTest, EmitVirtualPathWithCustomMountRootAccepted)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());
  const auto bytes = fixtures::MaterialDescriptor("test").bytes;

  EXPECT_NO_THROW(emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
    "/Custom/Materials/Wood.omat", "Materials/Wood.omat", bytes, {}));

  bool success = false;
  co::Run(*loop_,
    [&] -> Co<> { success = (co_await emitter.Finalize()).has_value(); });

  EXPECT_TRUE(success);
  EXPECT_EQ(ReadBytes(TempDir() / "Materials" / "Wood.omat"),
    fixtures::MaterialDescriptor("test").bytes);
}

//=== SHA-256 Tests
//===--------------------------------------------------------//

NOLINT_TEST_F(AssetEmitterTest, RecordsContainsSha256Hash)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());
  const auto bytes
    = fixtures::MaterialDescriptor("test-content-for-hashing").bytes;
  const auto expected_hash = oxygen::base::ComputeSha256(
    std::span<const std::byte>(bytes.data(), bytes.size()));

  co::Run(*loop_, [&] -> Co<> {
    emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/Hashed.omat", "Materials/Hashed.omat", bytes, {});
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  const auto& records = emitter.Records();
  ASSERT_EQ(records.size(), 1);
  const auto& hash = records.at(0).descriptor_sha256;
  if (!hash) {
    FAIL() << "Expected descriptor hash";
  }
  EXPECT_EQ(*hash, expected_hash);
}

NOLINT_TEST_F(AssetEmitterTest, RecordsSha256DisabledLeavesHashEmpty)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir(), false);
  const auto bytes = fixtures::MaterialDescriptor("test-content").bytes;

  co::Run(*loop_, [&] -> Co<> {
    emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/NoHash.omat", "Materials/NoHash.omat", bytes, {});
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  const auto& records = emitter.Records();
  ASSERT_EQ(records.size(), 1);
  EXPECT_FALSE(records.at(0).descriptor_sha256.has_value());
}

NOLINT_TEST_F(AssetEmitterTest, RecordsDifferentContentHasDifferentHash)
{
  AssetEmitter emitter(*writer_, Layout(), TempDir());
  const auto bytes1 = fixtures::MaterialDescriptor("content-one").bytes;
  const auto bytes2 = fixtures::MaterialDescriptor("content-two").bytes;

  co::Run(*loop_, [&] -> Co<> {
    emitter.Emit(MakeSequentialAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/One.omat", "Materials/One.omat", bytes1, {});
    emitter.Emit(MakeSequentialAssetKey(2), AssetType::kMaterial,
      "/.cooked/Materials/Two.omat", "Materials/Two.omat", bytes2, {});
    EXPECT_TRUE((co_await emitter.Finalize()).has_value());
  });

  const auto& records = emitter.Records();
  ASSERT_EQ(records.size(), 2);
  const auto& first_hash = records.at(0).descriptor_sha256;
  const auto& second_hash = records.at(1).descriptor_sha256;
  if (!first_hash || !second_hash) {
    FAIL() << "Expected both descriptor hashes";
  }
  EXPECT_NE(*first_hash, *second_hash);
}

} // namespace
