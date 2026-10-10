//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakCatalogIo.cpp

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

#include "PakTestSupport.h"

#include <Oxygen/Cooker/Pak/PakCatalogIo.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace data = oxygen::data;
namespace pak = oxygen::content::pak;
namespace paktest = oxygen::content::pak::test;

using paktest::MakeAssetKey;
using paktest::MakeDigest;
using paktest::MakeSourceKey;

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
  auto catalog = data::PakCatalog {
    .source_key = MakeSourceKey(0x41U),
    .content_version = 42U,
    .catalog_digest = MakeDigest(0x21U),
    .entries = {
      MakeEntry(0xB0U, data::AssetType::kScene, 0x31U, 0x41U),
      MakeEntry(0xA0U, data::AssetType::kMaterial, 0x32U, 0x42U),
    },
  };
  catalog.deleted = { MakeAssetKey(0xC0U) };
  catalog.bases = { { .source_key = MakeSourceKey(0x51U),
    .content_version = 1U,
    .catalog_digest = MakeDigest(0x61U) } };
  catalog.catalog_digest = catalog.ComputeDigest().value();
  return catalog;
}

auto ExpectCatalogEqual(
  const data::PakCatalog& lhs, const data::PakCatalog& rhs) -> void
{
  EXPECT_EQ(lhs.source_key, rhs.source_key);
  EXPECT_EQ(lhs.content_version, rhs.content_version);
  EXPECT_EQ(lhs.catalog_digest, rhs.catalog_digest);
  EXPECT_EQ(lhs.deleted, rhs.deleted);
  EXPECT_EQ(lhs.bases, rhs.bases);
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
  "schema_version": 2,
  "deleted": [],
  "bases": [],
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

} // namespace
