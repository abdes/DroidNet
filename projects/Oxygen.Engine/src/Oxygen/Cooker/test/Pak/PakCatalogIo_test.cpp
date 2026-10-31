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

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Pak/PakCatalogIo.h>
#include <Oxygen/Cooker/test/Support/TestValues.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace data = oxygen::data;
namespace pak = oxygen::content::pak;

using oxygen::cooker::test::MakeAssetKey;
using oxygen::cooker::testing::SourceKeyOf;

auto MakeDigest(const uint8_t seed) -> oxygen::base::Sha256Digest
{
  auto digest = oxygen::base::Sha256Digest {};
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
  auto catalog = data::PakCatalog {
    .source_key = SourceKeyOf(0x41U),
    .content_version = 42U,
    .catalog_digest = MakeDigest(0x21U),
    .mount = "Content",
    .entries = {
      MakeEntry(0xB0U, data::AssetType::kScene, 0x31U, 0x41U),
      MakeEntry(0xA0U, data::AssetType::kMaterial, 0x32U, 0x42U),
    },
  };
  catalog.deleted = { MakeAssetKey(0xC0U) };
  catalog.bases = {
    {
      .source_key = SourceKeyOf(0x51U),
      .content_version = 1U,
      .catalog_digest = MakeDigest(0x61U),
    },
  };
  catalog.dependencies = {
    {
      .mount = "Engine",
      .requirements = {
        {
          .kind = data::KeyReferenceKind::kTexture,
          .key = MakeAssetKey(0xD1U),
          .type = data::AssetType::kUnknown,
        },
        {
          .kind = data::KeyReferenceKind::kAsset,
          .key = MakeAssetKey(0xD0U),
          .type = data::AssetType::kMaterial,
        },
      },
    },
  };
  catalog.catalog_digest = catalog.ComputeDigest().value();
  return catalog;
}

NOLINT_TEST(
  PakCatalogIoTest, CanonicalJsonRoundTripSortsEntriesDeterministically)
{
  const auto catalog = MakeCatalog();

  const auto text = pak::PakCatalogIo::ToCanonicalJsonString(catalog);
  const auto parsed = pak::PakCatalogIo::Parse(text);

  ASSERT_HAS_VALUE(parsed);
  EXPECT_EQ(parsed.value().entries.size(), 2U);
  EXPECT_LT(parsed.value().entries.at(0).asset_key,
    parsed.value().entries.at(1).asset_key);
  EXPECT_EQ(parsed.value().mount, catalog.mount);
  ASSERT_EQ(parsed.value().dependencies.size(), 1U);
  EXPECT_EQ(parsed.value().dependencies.front().mount, "Engine");
  EXPECT_THAT(parsed.value().dependencies.front().requirements,
    ::testing::UnorderedElementsAreArray(
      catalog.dependencies.front().requirements));
  EXPECT_EQ(text, pak::PakCatalogIo::ToCanonicalJsonString(parsed.value()));
}

NOLINT_TEST(PakCatalogIoTest, EquivalentCatalogsProduceIdenticalCanonicalOutput)
{
  auto catalog_a = MakeCatalog();
  auto catalog_b = MakeCatalog();
  std::ranges::reverse(catalog_b.entries);
  std::ranges::reverse(catalog_b.dependencies.front().requirements);

  const auto text_a = pak::PakCatalogIo::ToCanonicalJsonString(catalog_a);
  const auto text_b = pak::PakCatalogIo::ToCanonicalJsonString(catalog_b);

  EXPECT_EQ(text_a, text_b);
}

NOLINT_TEST(PakCatalogIoTest, ParseRejectsDuplicateAssetKeys)
{
  const auto text = std::string_view { R"({
  "schema_version": 5,
  "mount": "Content",
  "deleted": [],
  "bases": [],
  "dependencies": [],
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
