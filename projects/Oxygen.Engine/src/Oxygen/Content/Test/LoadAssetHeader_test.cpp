//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/MemoryStream.h>
#include <array>
#include <cstring>
#include <span>
#include <vector>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Reader.h>
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::SizeIs;

using AssetHeader = oxygen::data::pak::AssetHeader;
using MemoryStream = oxygen::serio::MemoryStream;
using Reader = oxygen::serio::Reader<MemoryStream>;

//=== LoadAssetHeader Basic Tests ===--------------------------------------//

//! Scenario: LoadAssetHeader returns correct AssetHeader for valid input
TEST(LoadAssetHeader_basic, ReturnsCorrectHeader)
{
  // Arrange
  std::array<std::byte, sizeof(AssetHeader)> buffer {};
  auto* header = reinterpret_cast<AssetHeader*>(buffer.data());
  header->asset_type = 3;
  {
    constexpr char kTestName[] = "TestAsset";
    std::fill(std::begin(header->name), std::end(header->name), '\0');
    std::copy_n(kTestName, std::min(sizeof(header->name), sizeof(kTestName)),
      header->name);
  }
  header->version = 42;
  header->streaming_priority = 7;
  header->content_hash = 0x123456789ABCDEF0ULL;
  header->variant_flags = 0xAABBCCDD;

  MemoryStream stream(std::span<std::byte>(buffer.data(), buffer.size()));
  Reader reader(stream);

  // Act
  auto result = oxygen::content::loaders::LoadAssetHeader(reader);

  // Assert
  EXPECT_EQ(result.asset_type, 3);
  EXPECT_TRUE(std::string_view(result.name).starts_with("TestAsset"));
  EXPECT_EQ(result.version, 42);
  EXPECT_EQ(result.streaming_priority, 7);
  EXPECT_EQ(result.content_hash, 0x123456789ABCDEF0ULL);
  EXPECT_EQ(result.variant_flags, 0xAABBCCDDU);
}

//! Scenario: LoadAssetHeader throws on invalid asset type
TEST(LoadAssetHeader_error, ThrowsOnInvalidAssetType)
{
  // Arrange
  std::array<std::byte, sizeof(AssetHeader)> buffer {};
  auto* header = reinterpret_cast<AssetHeader*>(buffer.data());
  header->asset_type = 255; // Invalid (assuming kMaxAssetType < 255)
  {
    constexpr char kInvalidName[] = "InvalidType";
    std::fill(std::begin(header->name), std::end(header->name), '\0');
    std::copy_n(kInvalidName,
      std::min(sizeof(header->name), sizeof(kInvalidName)), header->name);
  }

  MemoryStream stream(std::span<std::byte>(buffer.data(), buffer.size()));
  Reader reader(stream);

  // Act & Assert
  EXPECT_THROW(
    oxygen::content::loaders::LoadAssetHeader(reader), std::runtime_error);
}

//! Scenario: LoadAssetHeader logs warning if name is not null-terminated
TEST(LoadAssetHeader_edge, WarnsIfNameNotNullTerminated)
{
  // Arrange
  std::array<std::byte, sizeof(AssetHeader)> buffer {};
  auto* header = reinterpret_cast<AssetHeader*>(buffer.data());
  // Fill name with non-null chars
  for (size_t i = 0; i < oxygen::data::pak::kMaxNameSize; ++i) {
    header->name[i] = 'A';
  }
  header->asset_type = 1;

  MemoryStream stream(std::span<std::byte>(buffer.data(), buffer.size()));
  Reader reader(stream);

  // Act
  // Should not throw, but should log a warning (not checked here)
  auto result = oxygen::content::loaders::LoadAssetHeader(reader);

  // Assert
  EXPECT_EQ(result.asset_type, 1);
  // Name will not be null-terminated, so string_view will be full buffer
  std::string_view name_view(result.name, oxygen::data::pak::kMaxNameSize);
  EXPECT_EQ(name_view.find('\0'), std::string_view::npos);
}

} // anonymous namespace
