//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <span>
#include <stdexcept>
#include <system_error>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/SourceKey.h>
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
  struct PakConfig {
    data::pak::core::PakHeader header {};
    data::pak::core::PakFooter footer {};
    std::vector<data::pak::core::BufferResourceDesc> buffers {};

    PakConfig()
    {
      const std::span<const char> header_magic(
        data::pak::core::kPakHeaderMagic);
      std::ranges::copy(header_magic, std::ranges::begin(header.magic));
      header.version = data::pak::core::kCurrentPakFormatVersion;
      constexpr std::array<uint8_t, 16> kSourceIdentity {
        0x41U,
        0x42U,
        0x43U,
        0x44U,
        0x45U,
        0x46U,
        0x77U,
        0x48U,
        0x89U,
        0x4AU,
        0x4BU,
        0x4CU,
        0x4DU,
        0x4EU,
        0x4FU,
        0x50U,
      };
      header.source_identity = kSourceIdentity;

      const std::span<const char> footer_magic(
        data::pak::core::kPakFooterMagic);
      std::ranges::copy(footer_magic, std::ranges::begin(footer.footer_magic));
    }
  };

  auto WritePak(const PakConfig& config) -> void
  {
    auto catalog = data::PakCatalog {
      .source_key
      = data::SourceKey::FromBytes(PakConfig {}.header.source_identity).value(),
      .content_version = config.header.content_version,
      .catalog_digest = {},
      .entries = {},
      .deleted = {},
      .bases = {},
    };
    catalog.catalog_digest = catalog.ComputeDigest().value();
    const auto bytes = catalog.Encode().value();
    serio::FileStream<> stream(test_pak_path_, std::ios::out | std::ios::trunc);
    serio::Writer writer(stream);
    const auto packed = writer.ScopedAlignment(1U);
    ASSERT_TRUE(
      writer.WriteBlob(std::as_bytes(std::span { &config.header, 1U })));
    auto footer = config.footer;
    auto offset = uint64_t { sizeof(config.header) };
    if (!config.buffers.empty()) {
      footer.buffer_table = { .offset = offset,
        .count = static_cast<uint32_t>(config.buffers.size()),
        .entry_size = sizeof(data::pak::core::BufferResourceDesc) };
      const auto buffer_bytes = std::as_bytes(std::span(config.buffers));
      ASSERT_TRUE(writer.WriteBlob(buffer_bytes));
      offset += buffer_bytes.size();
    }
    ASSERT_TRUE(writer.WriteBlob(bytes));
    footer.directory_offset = offset;
    footer.directory_size = 0U;
    footer.asset_count = 0U;
    footer.catalog_offset = offset;
    footer.catalog_size = bytes.size();
    ASSERT_TRUE(writer.WriteBlob(std::as_bytes(std::span { &footer, 1U })));
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
