//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/Async/Emitters/AssetEmitter.h>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/Import/Async/IAsyncFileWriter.h>
#include <Oxygen/Content/Import/Async/ImportEventLoop.h>
#include <Oxygen/Content/Import/Async/WindowsFileWriter.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Testing/GTest.h>

#include <cstring>
#include <filesystem>
#include <fstream>

using namespace oxygen::content::import;
using namespace oxygen::co;
using oxygen::data::AssetKey;
using oxygen::data::AssetType;
namespace co = oxygen::co;

namespace {

//=== Test Helpers
//===---------------------------------------------------------//

//! Create a test AssetKey with a sequential ID.
auto MakeAssetKey(uint32_t id) -> AssetKey
{
  std::array<uint8_t, 16> guid {};
  // Put the ID in the first 4 bytes for easy identification
  guid[0] = static_cast<uint8_t>((id >> 24) & 0xFF);
  guid[1] = static_cast<uint8_t>((id >> 16) & 0xFF);
  guid[2] = static_cast<uint8_t>((id >> 8) & 0xFF);
  guid[3] = static_cast<uint8_t>(id & 0xFF);
  return AssetKey { guid };
}

//! Create test descriptor bytes with recognizable content.
auto MakeDescriptorBytes(std::string_view content) -> std::vector<std::byte>
{
  std::vector<std::byte> bytes(content.size());
  std::memcpy(bytes.data(), content.data(), content.size());
  return bytes;
}

//! Read binary file content.
auto ReadBinaryFile(const std::filesystem::path& path) -> std::vector<std::byte>
{
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return {};
  }
  const auto size = file.tellg();
  file.seekg(0);
  std::vector<std::byte> data(static_cast<size_t>(size));
  file.read(reinterpret_cast<char*>(data.data()), size);
  return data;
}

//! Read file as string.
auto ReadFileAsString(const std::filesystem::path& path) -> std::string
{
  const auto bytes = ReadBinaryFile(path);
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

//=== Test Fixture ===--------------------------------------------------------//

//! Test fixture for AssetEmitter tests.
class AssetEmitterTest : public ::testing::Test {
protected:
  auto SetUp() -> void override
  {
    loop_ = std::make_unique<ImportEventLoop>();
    writer_ = std::make_unique<WindowsFileWriter>(*loop_);
    test_dir_
      = std::filesystem::temp_directory_path() / "oxygen_asset_emitter_test";
    std::filesystem::create_directories(test_dir_);
  }

  auto TearDown() -> void override
  {
    writer_.reset();
    loop_.reset();
    std::error_code ec;
    std::filesystem::remove_all(test_dir_, ec);
  }

  auto Layout() const -> const LooseCookedLayout& { return layout_; }

  std::unique_ptr<ImportEventLoop> loop_;
  std::unique_ptr<WindowsFileWriter> writer_;
  std::filesystem::path test_dir_;
  LooseCookedLayout layout_ {}; // Uses default paths
};

//=== Basic Emission Tests
//===-------------------------------------------------//

//! Verify emitting a single material descriptor creates file.
NOLINT_TEST_F(AssetEmitterTest, Emit_SingleMaterial_CreatesFile)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);
  const auto key = MakeAssetKey(1);
  const auto bytes = MakeDescriptorBytes("material-descriptor-content");

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    emitter.Emit(key, AssetType::kMaterial, "/.cooked/Materials/Wood",
      "Materials/Wood.omat", bytes);
    co_await emitter.Finalize();
  });

  // Assert: File exists with correct content
  const auto file_path = test_dir_ / "Materials" / "Wood.omat";
  EXPECT_TRUE(std::filesystem::exists(file_path));
  EXPECT_EQ(ReadFileAsString(file_path), "material-descriptor-content");
}

//! Verify emitting multiple assets creates all files.
NOLINT_TEST_F(AssetEmitterTest, Emit_MultipleAssets_CreatesAllFiles)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    emitter.Emit(MakeAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/Wood", "Materials/Wood.omat",
      MakeDescriptorBytes("wood-material"));

    emitter.Emit(MakeAssetKey(2), AssetType::kGeometry,
      "/.cooked/Geometry/Cube", "Geometry/Cube.ogeo",
      MakeDescriptorBytes("cube-geometry"));

    emitter.Emit(MakeAssetKey(3), AssetType::kScene, "/.cooked/Scenes/Level1",
      "Scenes/Level1.oscene", MakeDescriptorBytes("level1-scene"));

    co_await emitter.Finalize();
  });

  // Assert: All files exist with correct content
  EXPECT_EQ(
    ReadFileAsString(test_dir_ / "Materials" / "Wood.omat"), "wood-material");
  EXPECT_EQ(
    ReadFileAsString(test_dir_ / "Geometry" / "Cube.ogeo"), "cube-geometry");
  EXPECT_EQ(
    ReadFileAsString(test_dir_ / "Scenes" / "Level1.oscene"), "level1-scene");
}

//! Verify Count tracks emitted assets.
NOLINT_TEST_F(AssetEmitterTest, Count_TracksEmittedAssets)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);

  // Assert initial state
  EXPECT_EQ(emitter.Count(), 0);

  // Act & Assert
  co::Run(*loop_, [&]() -> Co<> {
    for (uint32_t i = 1; i <= 5; ++i) {
      emitter.Emit(MakeAssetKey(i), AssetType::kMaterial,
        "/.cooked/Materials/Mat" + std::to_string(i),
        "Materials/Mat" + std::to_string(i) + ".omat",
        MakeDescriptorBytes("mat-" + std::to_string(i)));
      EXPECT_EQ(emitter.Count(), i);
    }

    const auto success = co_await emitter.Finalize();
    EXPECT_TRUE(success);
  });

  EXPECT_EQ(emitter.Count(), 5);
}

//=== Record Tracking Tests
//===------------------------------------------------//

//! Verify Records() returns correct metadata.
NOLINT_TEST_F(AssetEmitterTest, Records_ContainsCorrectMetadata)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);
  const auto key = MakeAssetKey(42);
  const auto bytes = MakeDescriptorBytes("test-content");

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    emitter.Emit(key, AssetType::kGeometry, "/.cooked/Geometry/MyMesh",
      "Geometry/MyMesh.ogeo", bytes);
    co_await emitter.Finalize();
  });

  // Assert
  const auto& records = emitter.Records();
  ASSERT_EQ(records.size(), 1);

  EXPECT_EQ(records[0].key.guid, key.guid);
  EXPECT_EQ(records[0].asset_type, AssetType::kGeometry);
  EXPECT_EQ(records[0].virtual_path, "/.cooked/Geometry/MyMesh");
  EXPECT_EQ(records[0].descriptor_relpath, "Geometry/MyMesh.ogeo");
  EXPECT_EQ(records[0].descriptor_size, static_cast<uint64_t>(bytes.size()));
}

//! Verify emitting the same key twice updates the record and overwrites file.
NOLINT_TEST_F(AssetEmitterTest, Emit_SameKeyTwice_UpdatesRecordAndOverwrites)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);
  const auto key = MakeAssetKey(7);

  const auto bytes_v1 = MakeDescriptorBytes("v1-content");
  const auto bytes_v2 = MakeDescriptorBytes("v2-content-longer");

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    emitter.Emit(key, AssetType::kMaterial, "/.cooked/Materials/Wood",
      "Materials/Wood.omat", bytes_v1);
    emitter.Emit(key, AssetType::kMaterial, "/.cooked/Materials/Wood",
      "Materials/Wood.omat", bytes_v2);
    co_await emitter.Finalize();
  });

  // Assert
  EXPECT_EQ(emitter.Count(), 1);
  ASSERT_EQ(emitter.Records().size(), 1);
  EXPECT_EQ(emitter.Records()[0].key.guid, key.guid);
  EXPECT_EQ(emitter.Records()[0].descriptor_relpath, "Materials/Wood.omat");
  EXPECT_EQ(emitter.Records()[0].descriptor_size,
    static_cast<uint64_t>(bytes_v2.size()));

  const auto file_path = test_dir_ / "Materials" / "Wood.omat";
  EXPECT_TRUE(std::filesystem::exists(file_path));
  EXPECT_EQ(ReadFileAsString(file_path), "v2-content-longer");
}

//! Verify conflicting virtual path mappings are rejected.
NOLINT_TEST_F(AssetEmitterTest, Emit_VirtualPathConflictBetweenKeys_Throws)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);
  const auto bytes = MakeDescriptorBytes("content");

  // Act
  emitter.Emit(MakeAssetKey(1), AssetType::kMaterial,
    "/.cooked/Materials/Shared", "Materials/SharedA.omat", bytes);

  // Assert
  EXPECT_THROW(emitter.Emit(MakeAssetKey(2), AssetType::kMaterial,
                 "/.cooked/Materials/Shared", "Materials/SharedB.omat", bytes),
    std::runtime_error);

  bool success = false;
  co::Run(*loop_, [&]() -> Co<> { success = co_await emitter.Finalize(); });
  EXPECT_TRUE(success);
}

//! Verify Records() preserves order of emission.
NOLINT_TEST_F(AssetEmitterTest, Records_PreservesEmissionOrder)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    emitter.Emit(MakeAssetKey(1), AssetType::kMaterial, "/.cooked/Materials/A",
      "Materials/A.omat", MakeDescriptorBytes("a"));
    emitter.Emit(MakeAssetKey(2), AssetType::kGeometry, "/.cooked/Geometry/B",
      "Geometry/B.ogeo", MakeDescriptorBytes("b"));
    emitter.Emit(MakeAssetKey(3), AssetType::kScene, "/.cooked/Scenes/C",
      "Scenes/C.oscene", MakeDescriptorBytes("c"));
    co_await emitter.Finalize();
  });

  // Assert: Order preserved
  const auto& records = emitter.Records();
  ASSERT_EQ(records.size(), 3);
  EXPECT_EQ(records[0].asset_type, AssetType::kMaterial);
  EXPECT_EQ(records[1].asset_type, AssetType::kGeometry);
  EXPECT_EQ(records[2].asset_type, AssetType::kScene);
}

//=== Finalization Tests
//===---------------------------------------------------//

//! Verify finalization waits for pending I/O.
NOLINT_TEST_F(AssetEmitterTest, Finalize_WaitsForPendingIO)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);

  co::Run(*loop_, [&]() -> Co<> {
    emitter.Emit(MakeAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/Mat1", "Materials/Mat1.omat",
      MakeDescriptorBytes("content-1"));
    emitter.Emit(MakeAssetKey(2), AssetType::kMaterial,
      "/.cooked/Materials/Mat2", "Materials/Mat2.omat",
      MakeDescriptorBytes("content-2"));

    // Act
    const auto success = co_await emitter.Finalize();

    // Assert
    EXPECT_TRUE(success);
    EXPECT_EQ(emitter.PendingCount(), 0);
    EXPECT_EQ(emitter.ErrorCount(), 0);
  });
}

//! Verify finalization with no assets succeeds.
NOLINT_TEST_F(AssetEmitterTest, Finalize_NoAssets_Succeeds)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);

  // Act
  bool success = false;
  co::Run(*loop_, [&]() -> Co<> { success = co_await emitter.Finalize(); });

  // Assert
  EXPECT_TRUE(success);
  EXPECT_EQ(emitter.Count(), 0);
}

//! Verify emitting after Finalize() is rejected.
NOLINT_TEST_F(AssetEmitterTest, Emit_AfterFinalize_Throws)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);

  // Act & Assert
  co::Run(*loop_, [&]() -> Co<> {
    const auto success = co_await emitter.Finalize();
    EXPECT_TRUE(success);

    EXPECT_THROW(emitter.Emit(MakeAssetKey(1), AssetType::kMaterial,
                   "/.cooked/Materials/After", "Materials/After.omat",
                   MakeDescriptorBytes("content")),
      std::runtime_error);
  });
}

//=== File Content Verification
//===--------------------------------------------//

//! Verify file content matches emitted bytes exactly.
NOLINT_TEST_F(AssetEmitterTest, Finalize_FileContentMatchesEmittedBytes)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);

  // Create bytes with specific binary pattern
  std::vector<std::byte> expected_bytes(256);
  for (size_t i = 0; i < expected_bytes.size(); ++i) {
    expected_bytes[i] = static_cast<std::byte>(i & 0xFF);
  }

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    emitter.Emit(MakeAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/Binary", "Materials/Binary.omat", expected_bytes);
    co_await emitter.Finalize();
  });

  // Assert: File content matches exactly
  const auto file_path = test_dir_ / "Materials" / "Binary.omat";
  const auto actual_bytes = ReadBinaryFile(file_path);

  ASSERT_EQ(actual_bytes.size(), expected_bytes.size());
  EXPECT_TRUE(std::memcmp(actual_bytes.data(), expected_bytes.data(),
                expected_bytes.size())
    == 0);
}

//! Verify directory structure is created as needed.
NOLINT_TEST_F(AssetEmitterTest, Emit_CreatesNestedDirectories)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    emitter.Emit(MakeAssetKey(1), AssetType::kGeometry,
      "/.cooked/Deep/Nested/Path/Mesh", "Deep/Nested/Path/Mesh.ogeo",
      MakeDescriptorBytes("nested-mesh"));
    co_await emitter.Finalize();
  });

  // Assert: Nested file exists
  const auto file_path = test_dir_ / "Deep" / "Nested" / "Path" / "Mesh.ogeo";
  EXPECT_TRUE(std::filesystem::exists(file_path));
  EXPECT_EQ(ReadFileAsString(file_path), "nested-mesh");
}

//=== State Query Tests
//===----------------------------------------------------//

//! Verify PendingCount reflects queued writes.
NOLINT_TEST_F(AssetEmitterTest, PendingCount_ReflectsQueuedWrites)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);

  // Act
  bool had_pending = false;
  bool success = false;
  co::Run(*loop_, [&]() -> Co<> {
    emitter.Emit(MakeAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/Mat", "Materials/Mat.omat",
      MakeDescriptorBytes("content"));
    had_pending = emitter.PendingCount() > 0;

    success = co_await emitter.Finalize();
  });

  // Assert
  EXPECT_TRUE(had_pending);
  EXPECT_TRUE(success);
}

//! Verify ErrorCount is zero after successful writes.
NOLINT_TEST_F(AssetEmitterTest, ErrorCount_ZeroAfterSuccessfulWrites)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    for (uint32_t i = 1; i <= 10; ++i) {
      emitter.Emit(MakeAssetKey(i), AssetType::kMaterial,
        "/.cooked/Materials/Mat" + std::to_string(i),
        "Materials/Mat" + std::to_string(i) + ".omat",
        MakeDescriptorBytes("content-" + std::to_string(i)));
    }
    co_await emitter.Finalize();
  });

  // Assert
  EXPECT_EQ(emitter.ErrorCount(), 0);
}

//=== Edge Cases ===----------------------------------------------------------//

//! Verify empty descriptor bytes are handled correctly.
NOLINT_TEST_F(AssetEmitterTest, Emit_EmptyBytes_CreatesEmptyFile)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);
  std::vector<std::byte> empty_bytes;

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    emitter.Emit(MakeAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/Empty", "Materials/Empty.omat", empty_bytes);
    co_await emitter.Finalize();
  });

  // Assert: File exists but is empty
  const auto file_path = test_dir_ / "Materials" / "Empty.omat";
  EXPECT_TRUE(std::filesystem::exists(file_path));
  EXPECT_EQ(std::filesystem::file_size(file_path), 0);
}

//! Verify large descriptor is written correctly.
NOLINT_TEST_F(AssetEmitterTest, Emit_LargeDescriptor_WrittenCorrectly)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);

  // 100KB descriptor
  constexpr size_t kLargeSize = 100 * 1024;
  std::vector<std::byte> large_bytes(kLargeSize);
  for (size_t i = 0; i < kLargeSize; ++i) {
    large_bytes[i] = static_cast<std::byte>(i & 0xFF);
  }

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    emitter.Emit(MakeAssetKey(1), AssetType::kScene, "/.cooked/Scenes/Large",
      "Scenes/Large.oscene", large_bytes);
    co_await emitter.Finalize();
  });

  // Assert: File has correct size and content
  const auto file_path = test_dir_ / "Scenes" / "Large.oscene";
  EXPECT_EQ(std::filesystem::file_size(file_path), kLargeSize);

  const auto actual_bytes = ReadBinaryFile(file_path);
  EXPECT_TRUE(
    std::memcmp(actual_bytes.data(), large_bytes.data(), kLargeSize) == 0);
}

//=== Path Validation Tests
//===------------------------------------------------//

//! Verify relative path with backslash is rejected.
NOLINT_TEST_F(AssetEmitterTest, Emit_RelativePathWithBackslash_Throws)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);
  const auto bytes = MakeDescriptorBytes("test");

  // Act & Assert
  EXPECT_THROW(emitter.Emit(MakeAssetKey(1), AssetType::kMaterial,
                 "/.cooked/Materials/Wood", "Materials\\Wood.omat", bytes),
    std::runtime_error);
}

//! Verify relative path with leading slash is rejected.
NOLINT_TEST_F(AssetEmitterTest, Emit_RelativePathWithLeadingSlash_Throws)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);
  const auto bytes = MakeDescriptorBytes("test");

  // Act & Assert
  EXPECT_THROW(emitter.Emit(MakeAssetKey(1), AssetType::kMaterial,
                 "/.cooked/Materials/Wood", "/Materials/Wood.omat", bytes),
    std::runtime_error);
}

//! Verify relative path with colon is rejected.
NOLINT_TEST_F(AssetEmitterTest, Emit_RelativePathWithColon_Throws)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);
  const auto bytes = MakeDescriptorBytes("test");

  // Act & Assert
  EXPECT_THROW(emitter.Emit(MakeAssetKey(1), AssetType::kMaterial,
                 "/.cooked/Materials/Wood", "C:Materials/Wood.omat", bytes),
    std::runtime_error);
}

//! Verify relative path with double slash is rejected.
NOLINT_TEST_F(AssetEmitterTest, Emit_RelativePathWithDoubleSlash_Throws)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);
  const auto bytes = MakeDescriptorBytes("test");

  // Act & Assert
  EXPECT_THROW(emitter.Emit(MakeAssetKey(1), AssetType::kMaterial,
                 "/.cooked/Materials/Wood", "Materials//Wood.omat", bytes),
    std::runtime_error);
}

//! Verify relative path with dot segment is rejected.
NOLINT_TEST_F(AssetEmitterTest, Emit_RelativePathWithDotSegment_Throws)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);
  const auto bytes = MakeDescriptorBytes("test");

  // Act & Assert
  EXPECT_THROW(emitter.Emit(MakeAssetKey(1), AssetType::kMaterial,
                 "/.cooked/Materials/Wood", "Materials/./Wood.omat", bytes),
    std::runtime_error);
}

//! Verify relative path with dotdot segment is rejected.
NOLINT_TEST_F(AssetEmitterTest, Emit_RelativePathWithDotDotSegment_Throws)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);
  const auto bytes = MakeDescriptorBytes("test");

  // Act & Assert
  EXPECT_THROW(emitter.Emit(MakeAssetKey(1), AssetType::kMaterial,
                 "/.cooked/Materials/Wood", "Materials/../Wood.omat", bytes),
    std::runtime_error);
}

//! Verify virtual path without leading slash is rejected.
NOLINT_TEST_F(AssetEmitterTest, Emit_VirtualPathWithoutLeadingSlash_Throws)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);
  const auto bytes = MakeDescriptorBytes("test");

  // Act & Assert
  EXPECT_THROW(emitter.Emit(MakeAssetKey(1), AssetType::kMaterial,
                 ".cooked/Materials/Wood", "Materials/Wood.omat", bytes),
    std::runtime_error);
}

//! Verify virtual path with backslash is rejected.
NOLINT_TEST_F(AssetEmitterTest, Emit_VirtualPathWithBackslash_Throws)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);
  const auto bytes = MakeDescriptorBytes("test");

  // Act & Assert
  EXPECT_THROW(emitter.Emit(MakeAssetKey(1), AssetType::kMaterial,
                 "/.cooked\\Materials\\Wood", "Materials/Wood.omat", bytes),
    std::runtime_error);
}

//! Verify virtual path with double slash is rejected.
NOLINT_TEST_F(AssetEmitterTest, Emit_VirtualPathWithDoubleSlash_Throws)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);
  const auto bytes = MakeDescriptorBytes("test");

  // Act & Assert
  EXPECT_THROW(emitter.Emit(MakeAssetKey(1), AssetType::kMaterial,
                 "/.cooked//Materials/Wood", "Materials/Wood.omat", bytes),
    std::runtime_error);
}

//! Verify empty relative path is rejected.
NOLINT_TEST_F(AssetEmitterTest, Emit_EmptyRelativePath_Throws)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);
  const auto bytes = MakeDescriptorBytes("test");

  // Act & Assert
  EXPECT_THROW(emitter.Emit(MakeAssetKey(1), AssetType::kMaterial,
                 "/.cooked/Materials/Wood", "", bytes),
    std::runtime_error);
}

//! Verify empty virtual path is rejected.
NOLINT_TEST_F(AssetEmitterTest, Emit_EmptyVirtualPath_Throws)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);
  const auto bytes = MakeDescriptorBytes("test");

  // Act & Assert
  EXPECT_THROW(emitter.Emit(MakeAssetKey(1), AssetType::kMaterial, "",
                 "Materials/Wood.omat", bytes),
    std::runtime_error);
}

//=== SHA-256 Tests
//===--------------------------------------------------------//

//! Verify Records contain SHA-256 hash.
NOLINT_TEST_F(AssetEmitterTest, Records_ContainsSha256Hash)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);
  const auto bytes = MakeDescriptorBytes("test-content-for-hashing");
  const auto expected_hash = oxygen::base::ComputeSha256(
    std::span<const std::byte>(bytes.data(), bytes.size()));

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    emitter.Emit(MakeAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/Hashed", "Materials/Hashed.omat", bytes);
    co_await emitter.Finalize();
  });

  // Assert
  const auto& records = emitter.Records();
  ASSERT_EQ(records.size(), 1);
  ASSERT_TRUE(records[0].descriptor_sha256.has_value());
  EXPECT_EQ(records[0].descriptor_sha256.value(), expected_hash);
}

//! Verify SHA-256 is omitted when disabled.
NOLINT_TEST_F(AssetEmitterTest, Records_Sha256Disabled_LeavesHashEmpty)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_, false);
  const auto bytes = MakeDescriptorBytes("test-content");

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    emitter.Emit(MakeAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/NoHash", "Materials/NoHash.omat", bytes);
    co_await emitter.Finalize();
  });

  // Assert
  const auto& records = emitter.Records();
  ASSERT_EQ(records.size(), 1);
  EXPECT_FALSE(records[0].descriptor_sha256.has_value());
}

//! Verify each record has unique SHA-256 for different content.
NOLINT_TEST_F(AssetEmitterTest, Records_DifferentContentHasDifferentHash)
{
  // Arrange
  AssetEmitter emitter(*writer_, Layout(), test_dir_);
  const auto bytes1 = MakeDescriptorBytes("content-one");
  const auto bytes2 = MakeDescriptorBytes("content-two");

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    emitter.Emit(MakeAssetKey(1), AssetType::kMaterial,
      "/.cooked/Materials/One", "Materials/One.omat", bytes1);
    emitter.Emit(MakeAssetKey(2), AssetType::kMaterial,
      "/.cooked/Materials/Two", "Materials/Two.omat", bytes2);
    co_await emitter.Finalize();
  });

  // Assert
  const auto& records = emitter.Records();
  ASSERT_EQ(records.size(), 2);
  ASSERT_TRUE(records[0].descriptor_sha256.has_value());
  ASSERT_TRUE(records[1].descriptor_sha256.has_value());
  EXPECT_NE(
    records[0].descriptor_sha256.value(), records[1].descriptor_sha256.value());
}

} // namespace
