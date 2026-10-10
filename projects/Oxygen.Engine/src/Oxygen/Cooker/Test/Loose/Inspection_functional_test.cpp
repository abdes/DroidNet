//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Loose/Inspection.cpp

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/Test/Fixtures/LooseCookedTestWriter.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::content::lc::Inspection;

namespace {

class InspectionTest : public oxygen::cooker::test::TempDirTest { };

//! Writes a container with one asset and two file records under `root`.
auto WriteDummyIndex(const std::filesystem::path& root,
  const uint16_t version = oxygen::data::loose_cooked::kIndexVersion) -> void
{
  using oxygen::content::testing::DescriptorRecordOverride;
  using oxygen::content::testing::LooseCookedTestWriter;
  using oxygen::data::loose_cooked::FileKind;

  constexpr uint8_t kKeyFirst = 0xAA;
  constexpr uint8_t kAssetType = 12;
  constexpr uint64_t kDescriptorSize = 42;
  constexpr uint8_t kShaFirst = 0x01;
  constexpr uint8_t kShaLast = 0x11;
  constexpr size_t kShaLastIndex = 31;
  constexpr size_t kRecordTableSize = 100;
  constexpr size_t kRecordDataSize = 200;

  auto key_bytes = std::array<uint8_t, oxygen::data::AssetKey::kSizeBytes> {};
  key_bytes.at(0) = kKeyFirst;
  auto sha = std::array<uint8_t, oxygen::data::loose_cooked::kSha256Size> {};
  sha.at(0) = kShaFirst;
  sha.at(kShaLastIndex) = kShaLast;

  LooseCookedTestWriter writer(root);
  writer.SetIndexVersion(version);
  writer.WriteFile(FileKind::kBuffersTable, "Resources/buffers.table",
    std::vector<std::byte>(kRecordTableSize));
  writer.WriteFile(FileKind::kBuffersData, "Resources/buffers.data",
    std::vector<std::byte>(kRecordDataSize));
  writer.WriteAssetDescriptor(oxygen::data::AssetKey::FromBytes(key_bytes),
    static_cast<oxygen::data::AssetType>(kAssetType), "/.cooked/MyAsset.bin",
    "MyAsset.bin", {}, {},
    DescriptorRecordOverride { .size = kDescriptorSize, .sha256 = sha });
  static_cast<void>(writer.Finish());
}

NOLINT_TEST_F(InspectionTest, LoadFromRootExposesAssetsFilesAndGuidFromIndex)
{
  WriteDummyIndex(TempDir());

  Inspection inspection;
  ASSERT_NO_THROW({ inspection.LoadFromRoot(TempDir()); });

  const auto assets = inspection.Assets();
  ASSERT_EQ(assets.size(), 1);
  const auto key_bytes = nostd::as_bytes(assets[0].key);
  EXPECT_EQ(std::to_integer<uint8_t>(key_bytes[0]), 0xAA);
  EXPECT_EQ(assets[0].virtual_path, "/.cooked/MyAsset.bin");
  EXPECT_EQ(assets[0].descriptor_relpath, "MyAsset.bin");
  EXPECT_EQ(assets[0].descriptor_size, 42);
  EXPECT_EQ(assets[0].asset_type, 12);
  ASSERT_TRUE(assets[0].descriptor_sha256.has_value());
  EXPECT_EQ(assets[0].descriptor_sha256->at(0), 0x01);
  EXPECT_EQ(
    assets[0].descriptor_sha256->at(31), 0x11); // 31 is sha256 last byte

  const auto files = inspection.Files();
  ASSERT_EQ(files.size(), 2);
  EXPECT_EQ(static_cast<uint32_t>(files[0].kind),
    static_cast<uint32_t>(oxygen::data::loose_cooked::FileKind::kBuffersTable));
  EXPECT_EQ(files[0].relpath, "Resources/buffers.table");
  EXPECT_EQ(files[0].size, 100);

  const auto source_identity = inspection.Guid();
  const auto guid_bytes = nostd::as_bytes(source_identity);
  EXPECT_EQ(std::to_integer<uint8_t>(guid_bytes[0]), 1);
  EXPECT_EQ(std::to_integer<uint8_t>(guid_bytes[15]), 16);
}

NOLINT_TEST_F(InspectionTest, LoadFromFileExposesAssetsAndFilesFromIndex)
{
  const auto custom_root = TempDir() / "custom";
  const auto index_path = custom_root / "container.index.bin";
  WriteDummyIndex(custom_root);

  Inspection inspection;
  ASSERT_NO_THROW({ inspection.LoadFromFile(index_path); });

  EXPECT_EQ(inspection.Assets().size(), 1);
  EXPECT_EQ(inspection.Files().size(), 2);
}

NOLINT_TEST_F(InspectionTest, ValidationFailureThrowsRuntimeError)
{
  constexpr uint16_t kInvalidVersion = 999;
  // Create an index with a bad version to trigger failure
  WriteDummyIndex(TempDir(), kInvalidVersion);

  Inspection inspection;
  EXPECT_THROW({ inspection.LoadFromRoot(TempDir()); }, std::runtime_error);
}

NOLINT_TEST_F(InspectionTest, MoveConstructionAndAssignmentPreserveLoadedData)
{
  WriteDummyIndex(TempDir());

  Inspection inspection_orig;
  inspection_orig.LoadFromRoot(TempDir());

  Inspection inspection_moved = std::move(inspection_orig);

  EXPECT_EQ(inspection_moved.Assets().size(), 1);
  EXPECT_EQ(inspection_moved.Files().size(), 2);

  Inspection inspection_assigned;
  // NOLINTNEXTLINE(performance-move-const-arg)
  inspection_assigned = std::move(inspection_moved);

  EXPECT_EQ(inspection_assigned.Assets().size(), 1);
  EXPECT_EQ(inspection_assigned.Files().size(), 2);
}

} // namespace
