//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using testing::AllOf;
using testing::Eq;
using testing::SizeIs;

using AssetHeader = oxygen::data::pak::core::AssetHeader;
using MemoryStream = oxygen::serio::MemoryStream;
using Reader = oxygen::serio::Reader<MemoryStream>;

//=== LoadAssetHeader Basic Tests ===--------------------------------------//

//! Scenario: LoadAssetHeader returns correct AssetHeader for valid input
NOLINT_TEST(LoadAssetHeaderBasic, ReturnsCorrectHeader)
{
  // Arrange
  std::array<std::byte, sizeof(AssetHeader)> buffer {};
  auto* header = reinterpret_cast<AssetHeader*>(buffer.data());
  header->asset_type = 3;
  header->strings = { .offset = 200U, .size = 9U };
  header->name_offset = 1U;
  header->version = 42;
  header->streaming_priority = 7;
  constexpr uint64_t kExpectedHashPrefix = 0x123456789ABCDEF0ULL;
  std::memset(header->content_hash.data(), 0, header->content_hash.size());
  std::memcpy(header->content_hash.data(), &kExpectedHashPrefix,
    sizeof(kExpectedHashPrefix));
  header->variant_flags = 0xAABBCCDD;

  MemoryStream stream(std::span<std::byte>(buffer.data(), buffer.size()));
  Reader reader(stream);

  // Act
  AssetHeader result;
  oxygen::content::loaders::LoadAssetHeader(reader, result);

  // Assert
  EXPECT_EQ(result.asset_type, 3);
  EXPECT_EQ(result.strings.offset, 200U);
  EXPECT_EQ(result.strings.size, 9U);
  EXPECT_EQ(result.name_offset, 1U);
  EXPECT_EQ(result.version, 42);
  EXPECT_EQ(result.streaming_priority, 7);
  uint64_t hash_prefix = 0;
  std::memcpy(&hash_prefix, result.content_hash.data(), sizeof(hash_prefix));
  EXPECT_EQ(hash_prefix, kExpectedHashPrefix);
  EXPECT_EQ(result.variant_flags, 0xAABBCCDDU);
}

//! Scenario: LoadAssetHeader throws on invalid asset type
NOLINT_TEST(LoadAssetHeaderError, ThrowsOnInvalidAssetType)
{
  // Arrange
  std::array<std::byte, sizeof(AssetHeader)> buffer {};
  auto* header = reinterpret_cast<AssetHeader*>(buffer.data());
  header->asset_type = 255; // Invalid (assuming kMaxAssetType < 255)

  MemoryStream stream(std::span<std::byte>(buffer.data(), buffer.size()));
  Reader reader(stream);

  // Act & Assert
  AssetHeader result;
  NOLINT_EXPECT_THROW(oxygen::content::loaders::LoadAssetHeader(reader, result),
    std::runtime_error);
}

//=== Descriptor String Table Tests ===------------------------------------//

//! A header followed by `strings`, as a descriptor with no other records.
auto MakeDescriptor(const oxygen::data::DescriptorStringsBuilder& strings)
  -> std::vector<std::byte>
{
  auto header = AssetHeader {};
  header.asset_type = 1;
  return oxygen::data::MakeDescriptorBytes(header, strings);
}

//! Scenario: names longer than 63 bytes and non-ASCII names resolve whole.
NOLINT_TEST(DescriptorStrings, ResolvesLongAndNonAsciiNames)
{
  // Arrange
  const auto long_name = std::string(200, 'n');
  const auto unicode_name = std::string("Caf\xC3\xA9");
  auto strings = oxygen::data::DescriptorStringsBuilder {};
  const auto long_offset = strings.Add(long_name);
  const auto unicode_offset = strings.Add(unicode_name);
  auto bytes = MakeDescriptor(strings);
  MemoryStream stream { std::span<std::byte>(bytes) };
  Reader reader(stream);
  auto header = AssetHeader {};
  oxygen::content::loaders::LoadAssetHeader(reader, header);

  // Act
  const auto table
    = oxygen::content::loaders::ReadDescriptorStrings(reader, 0U, header);

  // Assert
  EXPECT_EQ(oxygen::content::loaders::ResolveDescriptorString(
              table, long_offset, "name"),
    long_name);
  EXPECT_EQ(oxygen::content::loaders::ResolveDescriptorString(
              table, unicode_offset, "name"),
    unicode_name);
  EXPECT_EQ(
    oxygen::content::loaders::ResolveDescriptorString(table, 0U, "name"),
    std::string {});
}

//! Scenario: an offset outside the table is rejected, not truncated.
NOLINT_TEST(DescriptorStrings, RejectsOffsetOutsideTable)
{
  // Arrange
  auto strings = oxygen::data::DescriptorStringsBuilder {};
  static_cast<void>(strings.Add("Name"));
  auto bytes = MakeDescriptor(strings);
  MemoryStream stream { std::span<std::byte>(bytes) };
  Reader reader(stream);
  auto header = AssetHeader {};
  oxygen::content::loaders::LoadAssetHeader(reader, header);
  const auto table
    = oxygen::content::loaders::ReadDescriptorStrings(reader, 0U, header);

  // Act & Assert
  NOLINT_EXPECT_THROW(
    static_cast<void>(oxygen::content::loaders::ResolveDescriptorString(
      table, header.strings.size, "name")),
    std::runtime_error);
}

//! Scenario: a table that does not end with NUL is rejected.
NOLINT_TEST(DescriptorStrings, RejectsUnterminatedTable)
{
  // Arrange
  auto strings = oxygen::data::DescriptorStringsBuilder {};
  static_cast<void>(strings.Add("Name"));
  auto bytes = MakeDescriptor(strings);
  bytes.back() = std::byte { 'x' };
  MemoryStream stream { std::span<std::byte>(bytes) };
  Reader reader(stream);
  auto header = AssetHeader {};
  oxygen::content::loaders::LoadAssetHeader(reader, header);

  // Act & Assert
  NOLINT_EXPECT_THROW(
    static_cast<void>(
      oxygen::content::loaders::ReadDescriptorStrings(reader, 0U, header)),
    std::runtime_error);
}

} // anonymous namespace
