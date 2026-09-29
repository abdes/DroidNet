//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "Fixtures/LooseCookedTestWriter.h"
#include <gtest/gtest.h>

#include <Oxygen/Content/Internal/LooseCookedSource.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>

using oxygen::content::internal::LooseCookedSource;
using oxygen::content::testing::LooseCookedTestWriter;
using oxygen::data::AssetKey;
using oxygen::data::AssetType;
using oxygen::data::loose_cooked::FileKind;

namespace {

auto MakeAssetKey(const std::uint8_t seed) -> AssetKey
{
  auto bytes = std::array<std::uint8_t, AssetKey::kSizeBytes> {};
  bytes.at(0) = seed;
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
    writer.WriteAssetDescriptor(MakeAssetKey(1U), AssetType::kMaterial,
      "/Content/test.omat", "test.omat", data);
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
    const std::vector<std::byte> data { std::byte { 0 } };
    writer.WriteAssetDescriptor(MakeAssetKey(1U), AssetType::kMaterial,
      "/Content/test.omat", "test.omat", data);
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

TEST_F(LooseCookedSourceTest, FullIntegrityChecksEveryAuxiliaryFile)
{
  const std::array bytes { std::byte { 42 } };
  LooseCookedTestWriter writer(CookedRoot());
  writer.WriteFile(FileKind::kAuxiliary, "Resources/first.obuf", bytes);
  writer.WriteFile(FileKind::kAuxiliary, "Resources/second.obuf", bytes);
  static_cast<void>(writer.Finish());
  EXPECT_NO_THROW({ LooseCookedSource source(CookedRoot(), true); });

  {
    std::ofstream changed(
      CookedRoot() / "Resources/second.obuf", std::ios::binary);
    changed.put('!');
  }
  EXPECT_NO_THROW({ LooseCookedSource source(CookedRoot(), false); });
  EXPECT_THROW(
    { LooseCookedSource source(CookedRoot(), true); }, std::runtime_error);
}

TEST_F(LooseCookedSourceTest, MetadataAdmissionRejectsExtraAndMissingMembers)
{
  const std::array bytes { std::byte { 42 } };
  LooseCookedTestWriter writer(CookedRoot());
  writer.WriteFile(FileKind::kAuxiliary, "Resources/first.obuf", bytes);
  static_cast<void>(writer.Finish());
  const auto extra = CookedRoot() / "stray.bin";
  {
    std::ofstream output(extra);
    output.put('!');
  }
  EXPECT_THROW(
    { LooseCookedSource source(CookedRoot(), false); }, std::runtime_error);
  std::filesystem::remove(extra);
  std::filesystem::remove(CookedRoot() / "Resources/first.obuf");
  EXPECT_THROW(
    { LooseCookedSource source(CookedRoot(), false); }, std::runtime_error);
}

TEST_F(LooseCookedSourceTest, NestedContainersAreNotMembershipExceptions)
{
  WriteValidEmptyIndex();
  LooseCookedTestWriter nested(CookedRoot() / "nested");
  static_cast<void>(nested.Finish());
  EXPECT_THROW(
    { LooseCookedSource source(CookedRoot(), false); }, std::runtime_error);
}

TEST_F(LooseCookedSourceTest, GenerationMarkerIsTheOnlyNonContentMember)
{
  WriteValidEmptyIndex();
  {
    std::ofstream marker(CookedRoot() / ".generation.lock");
  }
  EXPECT_NO_THROW({ LooseCookedSource source(CookedRoot(), true); });
}

TEST_F(LooseCookedSourceTest, FullIntegritySupportsLongMemberPaths)
{
  LooseCookedTestWriter writer(CookedRoot());
  const auto relative
    = std::string(120, 'a') + "/" + std::string(120, 'b') + ".obuf";
  const std::array bytes { std::byte { 42 } };
  writer.WriteFile(FileKind::kAuxiliary, relative, bytes);
  static_cast<void>(writer.Finish());
  EXPECT_GT((CookedRoot() / relative).native().size(), 260U);
  EXPECT_NO_THROW({ LooseCookedSource source(CookedRoot(), true); });
}

} // namespace
