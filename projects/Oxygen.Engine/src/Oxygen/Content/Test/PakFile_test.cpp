//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <ios>
#include <span>
#include <stdexcept>
#include <system_error>
#include <tuple>
#include <vector>

#include "Fixtures/PakTestWriter.h"
#include <gtest/gtest.h>

#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::testing {

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

  // Minimal native envelope for header/footer validation.
  using PakConfig = PakTestWriter::Config;

  auto WritePak(const PakConfig& config) -> void
  {
    PakTestWriter(test_pak_path_).Write(config);
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

TEST_F(PakFileTest, FailsOnMissingEmbeddedCatalog)
{
  PakConfig config;
  WritePak(config);
  const auto file_size = std::filesystem::file_size(test_pak_path_);
  ASSERT_GE(file_size, sizeof(config.footer));
  {
    serio::FileStream<> stream(test_pak_path_, std::ios::in | std::ios::out);
    ASSERT_TRUE(stream.Seek(file_size - sizeof(config.footer)));
    serio::Writer writer(stream);
    config.footer.directory_offset = sizeof(config.header);
    ASSERT_TRUE(
      writer.WriteBlob(std::as_bytes(std::span { &config.footer, 1U })));
  }
  EXPECT_THROW(PakFile pak(test_pak_path_), std::runtime_error);
}

TEST_F(PakFileTest, FailsWhenCatalogIdentityDisagreesWithHeader)
{
  PakConfig config;
  // The fixture catalog has its own valid default source identity.
  config.header.source_identity.at(0) ^= 1U;
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
  config.buffers.emplace_back();
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

} // namespace oxygen::content::testing
