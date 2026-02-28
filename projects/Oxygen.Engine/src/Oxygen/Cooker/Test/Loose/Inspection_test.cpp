//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <stdexcept>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>

#include "DummyIndex.h"

using oxygen::content::lc::Inspection;

namespace {

class InspectionTest : public testing::Test {
protected:
  void SetUp() override
  {
    temp_dir_
      = std::filesystem::temp_directory_path() / "oxygen_inspection_test";
    std::filesystem::create_directories(temp_dir_);
  }

  void TearDown() override { std::filesystem::remove_all(temp_dir_); }

  auto TempDir() const -> const std::filesystem::path& { return temp_dir_; }

private:
  std::filesystem::path
    temp_dir_; // NOLINT(misc-non-private-member-variables-in-classes)
};

NOLINT_TEST_F(InspectionTest, LoadFromRootValidPopulatesCorrectly)
{
  oxygen::content::lc::testing::CreateDummyIndex(
    TempDir() / "container.index.bin");

  Inspection inspection;
  ASSERT_NO_THROW({ inspection.LoadFromRoot(TempDir()); });

  const auto assets = inspection.Assets();
  ASSERT_EQ(assets.size(), 1);
  EXPECT_EQ(assets[0].key.guid[0], 0xAA);
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

  const auto guid = inspection.Guid();
  EXPECT_EQ(guid.get()[0], 1);
  EXPECT_EQ(guid.get()[15], 16);
}

NOLINT_TEST_F(InspectionTest, LoadFromFileValidPopulatesCorrectly)
{
  const auto index_path = TempDir() / "custom.index.bin";
  oxygen::content::lc::testing::CreateDummyIndex(index_path);

  Inspection inspection;
  ASSERT_NO_THROW({ inspection.LoadFromFile(index_path); });

  EXPECT_EQ(inspection.Assets().size(), 1);
  EXPECT_EQ(inspection.Files().size(), 2);
}

NOLINT_TEST_F(InspectionTest, ValidationFailureThrowsRuntimeError)
{
  constexpr uint16_t kInvalidVersion = 999;
  // Create an index with a bad version to trigger failure
  oxygen::content::lc::testing::CreateDummyIndex(
    TempDir() / "container.index.bin", kInvalidVersion);

  Inspection inspection;
  EXPECT_THROW({ inspection.LoadFromRoot(TempDir()); }, std::runtime_error);
}

NOLINT_TEST_F(InspectionTest, MoveSemanticsRetainDataSuccessfully)
{
  oxygen::content::lc::testing::CreateDummyIndex(
    TempDir() / "container.index.bin");

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
