//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::data::pak::core::AssetHeader;
using oxygen::serio::ReadOnlyMemoryStream;
using Reader = oxygen::serio::Reader<ReadOnlyMemoryStream>;

//! The defect `load` is refused with, or nothing when it does not throw.
auto DefectOf(auto&& load) -> std::optional<oxygen::data::CookedDefect>
{
  try {
    load();
  } catch (const oxygen::data::CookedFormatError& error) {
    return error.Defect();
  }
  return std::nullopt;
}

//=== LoadAssetHeader Basic Tests ===--------------------------------------//

//! Scenario: LoadAssetHeader returns correct AssetHeader for valid input
NOLINT_TEST(LoadAssetHeaderBasic, ReturnsCorrectHeader)
{
  // Arrange
  auto header = AssetHeader {};
  header.asset_type = 3;
  header.strings = { .offset = 200U, .size = 9U };
  header.name_offset = 1U;
  header.version = 42;
  header.streaming_priority = 7;
  constexpr uint64_t kExpectedHashPrefix = 0x123456789ABCDEF0ULL;
  std::memcpy(header.content_hash.data(), &kExpectedHashPrefix,
    sizeof(kExpectedHashPrefix));
  header.variant_flags = 0xAABBCCDD;
  const auto buffer
    = std::bit_cast<std::array<std::byte, sizeof(AssetHeader)>>(header);

  ReadOnlyMemoryStream stream(
    std::span<const std::byte>(buffer.data(), buffer.size()));
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
  const auto hash_prefix = oxygen::serio::CopyAt<uint64_t>(
    std::as_bytes(std::span { result.content_hash }));
  ASSERT_HAS_VALUE(hash_prefix);
  EXPECT_EQ(*hash_prefix, kExpectedHashPrefix);
  EXPECT_EQ(result.variant_flags, 0xAABBCCDDU);
}

//! Scenario: LoadAssetHeader throws on invalid asset type
NOLINT_TEST(LoadAssetHeaderError, ThrowsOnInvalidAssetType)
{
  // Arrange
  auto header = AssetHeader {};
  header.asset_type = 255; // Invalid (assuming kMaxAssetType < 255)
  const auto buffer
    = std::bit_cast<std::array<std::byte, sizeof(AssetHeader)>>(header);

  ReadOnlyMemoryStream stream(
    std::span<const std::byte>(buffer.data(), buffer.size()));
  Reader reader(stream);

  // Act & Assert
  AssetHeader result;
  const auto defect = DefectOf(
    [&] { oxygen::content::loaders::LoadAssetHeader(reader, result); });
  ASSERT_HAS_VALUE(defect);
  EXPECT_EQ(*defect, oxygen::data::CookedDefect::kInvalidValue);
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
  ReadOnlyMemoryStream stream { std::span<const std::byte>(bytes) };
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
  ReadOnlyMemoryStream stream { std::span<const std::byte>(bytes) };
  Reader reader(stream);
  auto header = AssetHeader {};
  oxygen::content::loaders::LoadAssetHeader(reader, header);
  const auto table
    = oxygen::content::loaders::ReadDescriptorStrings(reader, 0U, header);

  // Act & Assert
  const auto defect = DefectOf([&] {
    static_cast<void>(oxygen::content::loaders::ResolveDescriptorString(
      table, header.strings.size, "name"));
  });
  ASSERT_HAS_VALUE(defect);
  EXPECT_EQ(*defect, oxygen::data::CookedDefect::kOutOfBounds);
}

//! Scenario: a table that does not end with NUL is rejected.
NOLINT_TEST(DescriptorStrings, RejectsUnterminatedTable)
{
  // Arrange
  auto strings = oxygen::data::DescriptorStringsBuilder {};
  static_cast<void>(strings.Add("Name"));
  auto bytes = MakeDescriptor(strings);
  bytes.back() = std::byte { 'x' };
  ReadOnlyMemoryStream stream { std::span<const std::byte>(bytes) };
  Reader reader(stream);
  auto header = AssetHeader {};
  oxygen::content::loaders::LoadAssetHeader(reader, header);

  // Act & Assert
  const auto defect = DefectOf([&] {
    static_cast<void>(
      oxygen::content::loaders::ReadDescriptorStrings(reader, 0U, header));
  });
  ASSERT_HAS_VALUE(defect);
  EXPECT_EQ(*defect, oxygen::data::CookedDefect::kInvalidString);
}

} // anonymous namespace
