//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakCatalogIo.cpp

#include <array>
#include <cstdint>
#include <filesystem>

#include "PakTestSupport.h"

#include <Oxygen/Cooker/Pak/PakCatalogIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
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

NOLINT_TEST(PakCatalogIoTest, ReadAndWriteRoundTripCatalogFile)
{
  const oxygen::cooker::test::ScopedTempDir temp;
  const auto path = temp.Path() / "catalog.json";

  const auto catalog = MakeCatalog();
  const auto write_result = pak::PakCatalogIo::Write(path, catalog);
  ASSERT_TRUE(write_result.has_value());

  const auto read_result = pak::PakCatalogIo::Read(path);
  ASSERT_TRUE(read_result.has_value());
  ExpectCatalogEqual(read_result.value(),
    pak::PakCatalogIo::Parse(pak::PakCatalogIo::ToCanonicalJsonString(catalog))
      .value());
}

} // namespace
