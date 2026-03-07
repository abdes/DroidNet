//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <filesystem>
#include <process.h>
#include <string_view>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Pak/PakCatalogIo.h>

namespace {

namespace data = oxygen::data;
namespace pak = oxygen::content::pak;

auto MakeSourceKey(const uint8_t seed) -> data::SourceKey
{
  auto bytes = std::array<uint8_t, data::SourceKey::kSizeBytes> {};
  for (auto i = size_t { 0U }; i < bytes.size(); ++i) {
    bytes[i] = static_cast<uint8_t>(seed + static_cast<uint8_t>(i));
  }
  bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0FU) | 0x70U);
  bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3FU) | 0x80U);
  return data::SourceKey::FromBytes(bytes).value();
}

auto MakeAssetKey(const uint8_t seed) -> data::AssetKey
{
  auto bytes = std::array<uint8_t, data::AssetKey::kSizeBytes> {};
  bytes.fill(seed);
  return data::AssetKey::FromBytes(bytes);
}

auto MakeDigest(const uint8_t seed) -> std::array<uint8_t, 32>
{
  auto digest = std::array<uint8_t, 32> {};
  digest.fill(seed);
  return digest;
}

auto MakeEntry(const uint8_t asset_seed, const data::AssetType type,
  const uint8_t descriptor_seed, const uint8_t transitive_seed)
  -> data::PakCatalogEntry
{
  return data::PakCatalogEntry {
    .asset_key = MakeAssetKey(asset_seed),
    .asset_type = type,
    .descriptor_digest = MakeDigest(descriptor_seed),
    .transitive_resource_digest = MakeDigest(transitive_seed),
  };
}

auto MakeCatalog() -> data::PakCatalog
{
  return data::PakCatalog {
    .source_key = MakeSourceKey(0x41U),
    .content_version = 42U,
    .catalog_digest = MakeDigest(0x21U),
    .entries = {
      MakeEntry(0xB0U, data::AssetType::kScene, 0x31U, 0x41U),
      MakeEntry(0xA0U, data::AssetType::kMaterial, 0x32U, 0x42U),
    },
  };
}

auto TempCatalogPath() -> std::filesystem::path
{
  return std::filesystem::temp_directory_path()
    / std::filesystem::path(
      "oxygen-pak-catalog-io-" + std::to_string(_getpid()) + ".json");
}

auto ExpectCatalogEqual(
  const data::PakCatalog& lhs, const data::PakCatalog& rhs) -> void
{
  EXPECT_EQ(lhs.source_key, rhs.source_key);
  EXPECT_EQ(lhs.content_version, rhs.content_version);
  EXPECT_EQ(lhs.catalog_digest, rhs.catalog_digest);
  ASSERT_EQ(lhs.entries.size(), rhs.entries.size());
  for (size_t i = 0; i < lhs.entries.size(); ++i) {
    EXPECT_EQ(lhs.entries[i].asset_key, rhs.entries[i].asset_key);
    EXPECT_EQ(lhs.entries[i].asset_type, rhs.entries[i].asset_type);
    EXPECT_EQ(
      lhs.entries[i].descriptor_digest, rhs.entries[i].descriptor_digest);
    EXPECT_EQ(lhs.entries[i].transitive_resource_digest,
      rhs.entries[i].transitive_resource_digest);
  }
}

NOLINT_TEST(
  PakCatalogIoTest, CanonicalJsonRoundTripSortsEntriesDeterministically)
{
  const auto catalog = MakeCatalog();

  const auto text = pak::PakCatalogIo::ToCanonicalJsonString(catalog);
  const auto parsed = pak::PakCatalogIo::Parse(text);

  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed.value().entries.size(), 2U);
  EXPECT_LT(
    parsed.value().entries[0].asset_key, parsed.value().entries[1].asset_key);
  ExpectCatalogEqual(parsed.value(), parsed.value());
  EXPECT_EQ(text, pak::PakCatalogIo::ToCanonicalJsonString(parsed.value()));
}

NOLINT_TEST(PakCatalogIoTest, EquivalentCatalogsProduceIdenticalCanonicalOutput)
{
  auto catalog_a = MakeCatalog();
  auto catalog_b = MakeCatalog();
  std::ranges::reverse(catalog_b.entries);

  const auto text_a = pak::PakCatalogIo::ToCanonicalJsonString(catalog_a);
  const auto text_b = pak::PakCatalogIo::ToCanonicalJsonString(catalog_b);

  EXPECT_EQ(text_a, text_b);
}

NOLINT_TEST(PakCatalogIoTest, ParseRejectsDuplicateAssetKeys)
{
  const auto text = std::string_view { R"({
  "schema_version": 1,
  "source_key": "41424344-4546-7748-894a-4b4c4d4e4f50",
  "content_version": 7,
  "catalog_digest": "2121212121212121212121212121212121212121212121212121212121212121",
  "entries": [
    {
      "asset_key": "10101010-1010-1010-1010-101010101010",
      "asset_type": "Material",
      "descriptor_digest": "3131313131313131313131313131313131313131313131313131313131313131",
      "transitive_resource_digest": "4141414141414141414141414141414141414141414141414141414141414141"
    },
    {
      "asset_key": "10101010-1010-1010-1010-101010101010",
      "asset_type": "Scene",
      "descriptor_digest": "3232323232323232323232323232323232323232323232323232323232323232",
      "transitive_resource_digest": "4242424242424242424242424242424242424242424242424242424242424242"
    }
  ]
})" };

  const auto parsed = pak::PakCatalogIo::Parse(text);

  EXPECT_FALSE(parsed.has_value());
}

NOLINT_TEST(PakCatalogIoTest, ReadAndWriteRoundTripCatalogFile)
{
  const auto path = TempCatalogPath();
  std::error_code ec;
  std::filesystem::remove(path, ec);

  const auto catalog = MakeCatalog();
  const auto write_result = pak::PakCatalogIo::Write(path, catalog);
  ASSERT_TRUE(write_result.has_value());

  const auto read_result = pak::PakCatalogIo::Read(path);
  ASSERT_TRUE(read_result.has_value());
  ExpectCatalogEqual(read_result.value(),
    pak::PakCatalogIo::Parse(pak::PakCatalogIo::ToCanonicalJsonString(catalog))
      .value());

  std::filesystem::remove(path, ec);
}

} // namespace
