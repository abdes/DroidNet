//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakCatalogIo.cpp

#include <cstdint>
#include <filesystem>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Pak/PakCatalogIo.h>
#include <Oxygen/Cooker/test/Support/TestValues.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace data = oxygen::data;
namespace pak = oxygen::content::pak;

using oxygen::cooker::test::MakeAssetKey;
using oxygen::cooker::testing::SeedBytes;
using oxygen::cooker::testing::SourceKeyOf;

//! A patch catalog with entries, a deletion and a base layer, so that the
//! file carries every kind of record.
auto MakePatchCatalog() -> data::PakCatalog
{
  const auto digest = [](const uint8_t seed) -> oxygen::base::Sha256Digest {
    return oxygen::base::ComputeSha256(SeedBytes(seed));
  };
  auto catalog = data::PakCatalog {
    .source_key = SourceKeyOf(1U),
    .content_version = 2U,
    .catalog_digest = {},
    .mount = "Game",
    .entries = {
      {
        .asset_key = MakeAssetKey(1U),
        .asset_type = data::AssetType::kScene,
        .descriptor_digest = digest(1U),
        .transitive_resource_digest = digest(2U),
      },
      {
        .asset_key = MakeAssetKey(2U),
        .asset_type = data::AssetType::kMaterial,
        .descriptor_digest = digest(3U),
        .transitive_resource_digest = digest(4U),
      },
    },
  };
  catalog.deleted = { MakeAssetKey(3U) };
  catalog.bases = {
    {
      .source_key = SourceKeyOf(2U),
      .content_version = 1U,
      .catalog_digest = digest(5U),
    },
  };
  catalog.catalog_digest = catalog.ComputeDigest().value();
  return catalog;
}

NOLINT_TEST(PakCatalogIoTest, ReadAndWriteRoundTripCatalogFile)
{
  auto files = oxygen::cooker::testing::MemoryFileSystem {};
  const auto path = std::filesystem::path("/paks/patch.catalog.json");
  const auto catalog = MakePatchCatalog();

  ASSERT_HAS_VALUE(pak::PakCatalogIo::Write(path, catalog, files));
  const auto read = pak::PakCatalogIo::Read(path, files);

  ASSERT_HAS_VALUE(read);
  EXPECT_EQ(pak::PakCatalogIo::ToCanonicalJsonString(read.value()),
    pak::PakCatalogIo::ToCanonicalJsonString(catalog));
  EXPECT_EQ(read.value().catalog_digest, catalog.catalog_digest);
}

} // namespace
