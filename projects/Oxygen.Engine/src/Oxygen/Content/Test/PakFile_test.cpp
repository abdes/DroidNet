//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <system_error>

#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::testing {

inline auto MakeTestAssetKey(const uint8_t id) -> oxygen::data::AssetKey
{
  oxygen::data::AssetKey key {};
  std::ranges::fill(key.guid, id);
  return key;
}

class PakFileTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    temp_dir_ = std::filesystem::temp_directory_path() / "oxygen_pakfile_test";
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
    std::filesystem::create_directories(temp_dir_, ec);
    test_pak_path_ = temp_dir_ / "test.pak";
  }

  void TearDown() override
  {
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
  }

  // Helper macro-like configuration for custom pak generation
  struct PakConfig {
    data::pak::core::PakHeader header {};
    data::pak::core::PakFooter footer {};
    std::vector<data::pak::core::AssetDirectoryEntry> directory;
    std::vector<PakFile::BrowseEntry> browse_index;

    PakConfig()
    {
      const std::span<const char> header_magic(
        data::pak::core::kPakHeaderMagic);
      std::ranges::copy(header_magic, std::ranges::begin(header.magic));
      // I'll use the default format version for the tests.
      header.version = data::pak::core::kCurrentPakFormatVersion;

      const std::span<const char> footer_magic(
        data::pak::core::kPakFooterMagic);
      std::ranges::copy(footer_magic, std::ranges::begin(footer.footer_magic));
    }
  };

  auto WritePak(const PakConfig& config) -> void
  {
    std::ofstream out(test_pak_path_, std::ios::binary);

    // Write Header
    // NOLINTBEGIN(*-reinterpret-cast)
    out.write(
      reinterpret_cast<const char*>(&config.header), sizeof(config.header));

    // Wait, directory needs to be at footer.directory_offset.
    // For simplicity, we just put directory immediately after header.
    auto current_offset = out.tellp();

    // Write Directory
    auto dir_offset = current_offset;
    for (const auto& entry : config.directory) {
      out.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
    }
    auto dir_size = out.tellp() - dir_offset;

    // Update Footer with directory info
    auto footer = config.footer;
    footer.directory_offset = static_cast<uint64_t>(dir_offset);
    footer.directory_size = static_cast<uint64_t>(dir_size);
    footer.asset_count = config.directory.size();

    // Write Browse Index if needed (mocking skipped for brevity unless tested)
    // Write Footer
    out.write(reinterpret_cast<const char*>(&footer), sizeof(footer));
    // NOLINTEND(*-reinterpret-cast)
    out.close();
  }

  // NOLINTBEGIN(*-non-private-member-variables-in-classes)
  std::filesystem::path temp_dir_;
  std::filesystem::path test_pak_path_;
  // NOLINTEND(*-non-private-member-variables-in-classes)
};

TEST_F(PakFileTest, LoadValidPakFile)
{
  PakConfig config;
  constexpr uint16_t kTestContentVersion = 42;
  config.header.content_version = kTestContentVersion;
  WritePak(config);

  PakFile pak(test_pak_path_);
  EXPECT_EQ(pak.FilePath(), test_pak_path_);
  EXPECT_EQ(
    pak.FormatVersion(), oxygen::data::pak::core::kCurrentPakFormatVersion);
  EXPECT_EQ(pak.ContentVersion(), 42);
  EXPECT_FALSE(pak.HasBrowseIndex());
  EXPECT_TRUE(pak.Directory().empty());
}

TEST_F(PakFileTest, FailsOnInvalidHeaderMagic)
{
  PakConfig config;
  std::ranges::fill(config.header.magic, '\0');
  WritePak(config);

  EXPECT_THROW(PakFile pak(test_pak_path_), std::runtime_error);
}

TEST_F(PakFileTest, FailsOnUnsupportedVersion)
{
  PakConfig config;
  constexpr uint16_t kUnsupportedVersion = 99;
  config.header.version = kUnsupportedVersion;
  WritePak(config);

  EXPECT_THROW(PakFile pak(test_pak_path_), std::runtime_error);
}

TEST_F(PakFileTest, FailsOnInvalidFooterMagic)
{
  PakConfig config;
  std::ranges::fill(config.footer.footer_magic, '\0');
  WritePak(config);

  EXPECT_THROW(PakFile pak(test_pak_path_), std::runtime_error);
}

TEST_F(PakFileTest, MissingFileThrows)
{
  EXPECT_THROW(PakFile pak(temp_dir_ / "nonexistent.pak"), std::system_error);
}

TEST_F(PakFileTest, AssetDirectoryLookup)
{
  PakConfig config;
  data::pak::core::AssetDirectoryEntry entry1;
  constexpr auto kAssetType1 = data::AssetType::kMaterial;
  constexpr uint64_t kDescOffset1 = 512;
  entry1.asset_key = MakeTestAssetKey(1);
  entry1.asset_type = kAssetType1;
  entry1.desc_offset = kDescOffset1;

  data::pak::core::AssetDirectoryEntry entry2;
  constexpr auto kAssetType2 = data::AssetType::kGeometry;
  constexpr uint64_t kDescOffset2 = 1024;
  entry2.asset_key = MakeTestAssetKey(2);
  entry2.asset_type = kAssetType2;
  entry2.desc_offset = kDescOffset2;

  config.directory.push_back(entry1);
  config.directory.push_back(entry2);
  WritePak(config);

  PakFile pak(test_pak_path_);
  EXPECT_EQ(pak.Directory().size(), 2);

  auto found1 = pak.FindEntry(entry1.asset_key);
  ASSERT_TRUE(found1.has_value());
  EXPECT_EQ(found1->desc_offset, kDescOffset1);

  auto found2 = pak.FindEntry(entry2.asset_key);
  ASSERT_TRUE(found2.has_value());
  EXPECT_EQ(found2->desc_offset, kDescOffset2);

  constexpr uint8_t kMissingKeyId = 9;
  auto not_found = pak.FindEntry(MakeTestAssetKey(kMissingKeyId));
  EXPECT_FALSE(not_found.has_value());
}

TEST_F(PakFileTest, Crc32ValidationSkippedIfZero)
{
  PakConfig config;
  config.footer.pak_crc32 = 0; // Skip validation
  WritePak(config);

  PakFile pak(test_pak_path_);
  EXPECT_NO_THROW(pak.ValidateCrc32Integrity());
}

TEST_F(PakFileTest, Crc32ValidationFailsIfMismatch)
{
  PakConfig config;
  constexpr uint32_t kBadHash = 0xDEADBEEF;
  config.footer.pak_crc32 = kBadHash; // Will obviously not match
  WritePak(config);

  PakFile pak(test_pak_path_);
  EXPECT_THROW(pak.ValidateCrc32Integrity(), std::runtime_error);
}

TEST_F(PakFileTest, ResourceTablesPresence)
{
  PakConfig config;
  constexpr uint32_t kBufferCount = 10;
  constexpr uint32_t kBufferEntrySize = 32;
  config.footer.buffer_table.count = kBufferCount;
  config.footer.buffer_table.entry_size = kBufferEntrySize;
  config.footer.texture_table.count = 0; // No textures
  WritePak(config);

  PakFile pak(test_pak_path_);

  EXPECT_TRUE(pak.HasTableOf<oxygen::data::BufferResource>());
  EXPECT_FALSE(pak.HasTableOf<oxygen::data::TextureResource>());
  EXPECT_FALSE(pak.HasTableOf<oxygen::data::ScriptResource>());
  EXPECT_FALSE(pak.HasTableOf<oxygen::data::PhysicsResource>());

  std::ignore = pak.BuffersTable();
  EXPECT_THROW(std::ignore = pak.TexturesTable(), std::runtime_error);

  EXPECT_NE(pak.GetResourceTable<oxygen::data::BufferResource>(), nullptr);
  EXPECT_EQ(pak.GetResourceTable<oxygen::data::TextureResource>(), nullptr);
}

TEST_F(PakFileTest, ScriptParamReadRequests)
{
  PakConfig config;
  WritePak(config);

  PakFile pak(test_pak_path_);
  // Ensure empty returns empty and not out of bounds
  PakFile::ScriptParamReadRequest req;
  req.absolute_offset = 0;
  req.count = 0;
  auto res = pak.ReadScriptParamRecords(req);
  EXPECT_TRUE(res.empty());

  constexpr uint64_t kOffsetOutOfBounds = 1000000;
  constexpr uint32_t kCountReq = 10;
  req.absolute_offset = kOffsetOutOfBounds;
  req.count = kCountReq;
  EXPECT_THROW(
    std::ignore = pak.ReadScriptParamRecords(req), std::runtime_error);
}

} // namespace oxygen::content::testing
