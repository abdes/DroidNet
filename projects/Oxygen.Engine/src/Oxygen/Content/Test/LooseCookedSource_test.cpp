//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include <Oxygen/Content/Internal/LooseCookedSource.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>

#include "Fixtures/LooseCookedTestWriter.h"

using oxygen::content::internal::LooseCookedSource;
using oxygen::content::testing::LooseCookedTestWriter;
using oxygen::data::AssetKey;
using oxygen::data::AssetType;
using oxygen::data::loose_cooked::FileKind;

namespace {

auto MakeAssetKey(const std::uint8_t seed) -> AssetKey
{
  auto bytes = std::array<std::uint8_t, AssetKey::kSizeBytes> {};
  bytes[0] = seed;
  return AssetKey::FromBytes(bytes);
}

class LooseCookedSourceTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    const auto* const test_info
      = ::testing::UnitTest::GetInstance()->current_test_info();
    const auto temp_dir = std::filesystem::temp_directory_path();
    cooked_root_ = temp_dir
      / ("LooseCookedSourceTest_Root_" + std::string(test_info->name()));
    std::error_code ec;
    std::filesystem::remove_all(cooked_root_, ec);
    std::filesystem::create_directories(cooked_root_, ec);
  }

  void TearDown() override
  {
    std::error_code ec;
    std::filesystem::remove_all(cooked_root_, ec);
  }

  auto WriteValidEmptyIndex() const -> void
  {
    LooseCookedTestWriter writer(CookedRoot());
    (void)writer.Finish();
  }

  [[nodiscard]] auto CookedRoot() const -> const std::filesystem::path&
  {
    return cooked_root_;
  }

private:
  std::filesystem::path cooked_root_;
};

TEST_F(LooseCookedSourceTest, ConstructorMissingIndexFileThrows)
{
  EXPECT_THROW(
    { LooseCookedSource source(CookedRoot(), false); }, std::runtime_error);
}

TEST_F(LooseCookedSourceTest, ConstructorValidEmptyIndexInitializes)
{
  WriteValidEmptyIndex();
  EXPECT_NO_THROW({ LooseCookedSource source(CookedRoot(), false); });
}

TEST_F(LooseCookedSourceTest, ConstructorFileMissingThrows)
{
  {
    LooseCookedTestWriter writer(CookedRoot());
    const std::vector<std::byte> data { std::byte { 0 } };
    writer.WriteFile(FileKind::kBuffersTable, "buffers.table", data);
    writer.WriteFile(FileKind::kBuffersData, "buffers.data", data);
    (void)writer.Finish();
  }

  // Remove the data file, but retain the index entry.
  // We can do this safely because writer releases files when it goes out of
  // scope.
  std::filesystem::remove(CookedRoot() / "buffers.data");

  EXPECT_THROW(
    { LooseCookedSource source(CookedRoot(), false); }, std::runtime_error);
}

TEST_F(LooseCookedSourceTest, ConstructorFileSizeMismatchThrows)
{
  {
    LooseCookedTestWriter writer(CookedRoot());
    const std::vector<std::byte> data { std::byte { 0 } };
    writer.WriteFile(FileKind::kBuffersTable, "buffers.table", data);
    writer.WriteFile(FileKind::kBuffersData, "buffers.data", data);
    (void)writer.Finish();
  }

  // Corrupt the size of the file on disk to be 2
  const auto table_path = CookedRoot() / "buffers.table";
  std::filesystem::resize_file(table_path, 2);

  EXPECT_THROW(
    { LooseCookedSource source(CookedRoot(), false); }, std::runtime_error);
}

TEST_F(LooseCookedSourceTest, ConstructorDescriptorMissingThrows)
{
  {
    LooseCookedTestWriter writer(CookedRoot());
    const std::vector<std::byte> data { std::byte { 0 } };
    writer.WriteAssetDescriptor(
      MakeAssetKey(1U), AssetType::kMaterial, "/Content/test.omat",
      "test.omat", data);
    (void)writer.Finish();
  }

  std::filesystem::remove(CookedRoot() / "test.omat");

  EXPECT_THROW(
    { LooseCookedSource source(CookedRoot(), false); }, std::runtime_error);
}

TEST_F(LooseCookedSourceTest, ConstructorDescriptorSha256MismatchThrows)
{
  {
    LooseCookedTestWriter writer(CookedRoot());
    writer.SetComputeSha256(true); // Tell writer to generate a real hash
    const std::vector<std::byte> data { std::byte { 0 } };
    writer.WriteAssetDescriptor(
      MakeAssetKey(1U), AssetType::kMaterial, "/Content/test.omat",
      "test.omat", data);
    (void)writer.Finish();
  }

  // Malform the file data to break its SHA256 checksum, preserving size
  const auto path = CookedRoot() / "test.omat";
  const char new_data = 1;
  {
    std::ofstream os(path, std::ios::binary);
    os.write(&new_data, 1);
  }

  // Without verification, loads fine despite invalid hash mismatch
  EXPECT_NO_THROW({ LooseCookedSource source(CookedRoot(), false); });

  // With verification enabled, throws
  EXPECT_THROW(
    { LooseCookedSource source(CookedRoot(), true); }, std::runtime_error);
}

TEST_F(LooseCookedSourceTest, ReadersReturnNullWhenFilesOmitted)
{
  WriteValidEmptyIndex();
  {
    LooseCookedSource source(CookedRoot(), false);

    EXPECT_EQ(source.CreateBufferTableReader(), nullptr);
    EXPECT_EQ(source.CreateBufferDataReader(), nullptr);
    EXPECT_EQ(source.CreateTextureTableReader(), nullptr);
    EXPECT_EQ(source.CreateTextureDataReader(), nullptr);
    EXPECT_EQ(source.CreateScriptTableReader(), nullptr);
    EXPECT_EQ(source.CreateScriptDataReader(), nullptr);
    EXPECT_EQ(source.CreatePhysicsTableReader(), nullptr);
    EXPECT_EQ(source.CreatePhysicsDataReader(), nullptr);

    EXPECT_EQ(source.CreateAssetDescriptorReader(MakeAssetKey(1U)), nullptr);
  }
}

TEST_F(LooseCookedSourceTest, ReadScriptSlotRecordsEmptyReturnsEmpty)
{
  {
    LooseCookedTestWriter writer(CookedRoot());
    std::vector<std::byte> data; // 0 entries
    writer.WriteFile(FileKind::kScriptsTable, "scripts.table", data);
    writer.WriteFile(FileKind::kScriptsData, "scripts.data", data);
    (void)writer.Finish();
  }

  {
    LooseCookedSource source(CookedRoot(), false);
    const auto records = source.ReadScriptSlotRecords(0, 0);
    EXPECT_TRUE(records.empty());
  }
}

} // namespace
