//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Testing/GTest.h>

namespace {
using oxygen::data::AssetKey;
using oxygen::data::AssetType;
using oxygen::data::KeyReferenceKind;
using oxygen::data::PakCatalog;
using oxygen::data::PakRequirement;
using oxygen::data::SourceKey;

auto Key(const uint8_t value) -> AssetKey
{
  AssetKey::ByteArray bytes {};
  bytes.at(0) = value;
  return AssetKey::FromBytes(bytes);
}

auto Source(const uint8_t value) -> SourceKey
{
  std::array<uint8_t, SourceKey::kSizeBytes> bytes {};
  bytes.at(0) = value;
  bytes.at(6) = 0x70U;
  bytes.at(8) = 0x80U;
  return SourceKey::FromBytes(bytes).value();
}

auto Catalog() -> PakCatalog
{
  auto catalog = PakCatalog {
    .source_key = Source(3U),
    .content_version = 2U,
    .catalog_digest = {},
    .library = "Props",
    .entries = {
      { .asset_key = Key(1U), .asset_type = AssetType::kMaterial,
        .descriptor_digest = {}, .transitive_resource_digest = {}, },
      { .asset_key = Key(2U), .asset_type = AssetType::kGeometry,
        .descriptor_digest = {}, .transitive_resource_digest = {}, },
    },
    .deleted = { Key(4U), Key(5U) },
    .bases = {
      { .source_key = Source(1U), .content_version = 0U, .catalog_digest = {} },
      { .source_key = Source(2U), .content_version = 1U, .catalog_digest = {} },
    },
    .dependencies = {
      { .library = "Engine", .requirements = {
        { .kind = KeyReferenceKind::kAsset, .key = Key(6U),
          .type = AssetType::kMaterial, },
        { .kind = KeyReferenceKind::kTexture, .key = Key(7U),
          .type = AssetType::kUnknown, },
      }, },
      { .library = "Vehicles", .requirements = {
        { .kind = KeyReferenceKind::kAsset, .key = Key(8U),
          .type = AssetType::kGeometry, },
      }, },
    },
  };
  catalog.entries.at(0).descriptor_digest.fill(0x11U);
  catalog.entries.at(0).transitive_resource_digest.fill(0x22U);
  catalog.entries.at(1).descriptor_digest.fill(0x33U);
  catalog.entries.at(1).transitive_resource_digest.fill(0x44U);
  catalog.bases.at(0).catalog_digest.fill(0x55U);
  catalog.bases.at(1).catalog_digest.fill(0x66U);
  catalog.catalog_digest = catalog.ComputeDigest().value();
  return catalog;
}

NOLINT_TEST(PakCatalogTest, RoundTripPreservesLayerMeaning)
{
  const auto original = Catalog();
  const auto bytes = original.Encode();
  ASSERT_TRUE(bytes.has_value());
  const auto decoded = PakCatalog::Decode(*bytes);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->source_key, original.source_key);
  EXPECT_EQ(decoded->content_version, original.content_version);
  EXPECT_EQ(decoded->library, original.library);
  EXPECT_EQ(decoded->catalog_digest, original.catalog_digest);
  EXPECT_EQ(decoded->deleted, original.deleted);
  EXPECT_EQ(decoded->bases, original.bases);
  EXPECT_EQ(decoded->dependencies, original.dependencies);
  ASSERT_EQ(decoded->entries.size(), original.entries.size());
  for (size_t index = 0; index < original.entries.size(); ++index) {
    const auto& entry = decoded->entries.at(index);
    const auto& expected = original.entries.at(index);
    EXPECT_EQ(entry.asset_key, expected.asset_key);
    EXPECT_EQ(entry.asset_type, expected.asset_type);
    EXPECT_EQ(entry.descriptor_digest, expected.descriptor_digest);
    EXPECT_EQ(
      entry.transitive_resource_digest, expected.transitive_resource_digest);
  }
  EXPECT_EQ(decoded->Encode().value(), *bytes);
}

NOLINT_TEST(PakCatalogTest, EntryOrderIsCanonicalButBaseOrderIsSemantic)
{
  auto catalog = Catalog();
  const auto encoded = catalog.Encode().value();
  std::ranges::reverse(catalog.entries);
  std::ranges::reverse(catalog.deleted);
  std::ranges::reverse(catalog.dependencies);
  std::ranges::reverse(catalog.dependencies.back().requirements);
  EXPECT_EQ(catalog.Encode().value(), encoded);
  std::ranges::reverse(catalog.bases);
  EXPECT_NE(catalog.ComputeDigest().value(), catalog.catalog_digest);
  EXPECT_FALSE(catalog.Validate());
  EXPECT_FALSE(catalog.Encode());
}

NOLINT_TEST(PakCatalogTest, DeletedOnlyLayerRoundTripsAndChangesDigest)
{
  auto catalog = Catalog();
  catalog.entries.clear();
  catalog.catalog_digest = catalog.ComputeDigest().value();
  const auto bytes = catalog.Encode().value();
  const auto decoded = PakCatalog::Decode(bytes);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_TRUE(decoded->entries.empty());
  EXPECT_EQ(decoded->deleted, catalog.deleted);
  catalog.deleted.clear();
  EXPECT_NE(catalog.ComputeDigest().value(), catalog.catalog_digest);
}

NOLINT_TEST(PakCatalogTest, RejectsConflictingEntriesAndInvalidBases)
{
  auto catalog = Catalog();
  catalog.deleted.push_back(catalog.entries.at(0).asset_key);
  EXPECT_FALSE(catalog.ComputeDigest());
  catalog = Catalog();
  catalog.entries.push_back(catalog.entries.at(0));
  EXPECT_FALSE(catalog.ComputeDigest());
  catalog = Catalog();
  catalog.bases.at(0).source_key = catalog.source_key;
  EXPECT_FALSE(catalog.ComputeDigest());
  catalog = Catalog();
  catalog.bases.push_back(catalog.bases.at(0));
  EXPECT_FALSE(catalog.ComputeDigest());
}

NOLINT_TEST(PakCatalogTest, BaselineMustBeImmediatelyBelowPatch)
{
  const auto patch = Catalog();
  EXPECT_TRUE(patch.ValidateBaseLayers(patch.bases));
  auto layers = patch.bases;
  const auto engine = oxygen::data::PakCatalogBase {
    .source_key = Source(9U),
    .content_version = 1U,
    .catalog_digest = {},
  };
  layers.insert(layers.begin(), engine);
  EXPECT_TRUE(patch.ValidateBaseLayers(layers));
  layers.insert(layers.end() - 1, engine);
  EXPECT_FALSE(patch.ValidateBaseLayers(layers));
  layers = patch.bases;
  std::ranges::reverse(layers);
  EXPECT_FALSE(patch.ValidateBaseLayers(layers));
  layers = patch.bases;
  layers.at(0).catalog_digest.at(0) ^= 1U;
  EXPECT_FALSE(patch.ValidateBaseLayers(layers));
  layers = patch.bases;
  ++layers.at(0).content_version;
  EXPECT_FALSE(patch.ValidateBaseLayers(layers));
  EXPECT_FALSE(patch.ValidateBaseLayers({}));
}

//! The library is part of the catalog: renaming it changes the digest.
NOLINT_TEST(PakCatalogTest, LibraryIsPartOfTheDigest)
{
  auto catalog = Catalog();
  catalog.library = "Others";
  EXPECT_NE(catalog.ComputeDigest().value(), catalog.catalog_digest);
  catalog.library.clear();
  EXPECT_FALSE(catalog.ComputeDigest());
  catalog.library = std::string(256U, 'A');
  EXPECT_FALSE(catalog.ComputeDigest());
}

//! Every pak names its library, even one that lists nothing.
NOLINT_TEST(PakCatalogTest, EveryPakNamesItsLibrary)
{
  auto empty = PakCatalog { .source_key = Source(3U) };
  EXPECT_FALSE(empty.ComputeDigest());
  empty.library = "Props";
  EXPECT_TRUE(empty.ComputeDigest());
}

//! A library is required once, never the pak's own, and for something; each
//! requirement names an asset with its type or a texture, once.
NOLINT_TEST(PakCatalogTest, RejectsInvalidDependencies)
{
  const auto rejects = [](auto edit) -> bool {
    auto catalog = Catalog();
    edit(catalog);
    return !catalog.ComputeDigest();
  };
  EXPECT_TRUE(rejects([](PakCatalog& catalog) -> void {
    catalog.dependencies.at(0).library = catalog.library;
  }));
  EXPECT_TRUE(rejects([](PakCatalog& catalog) -> void {
    catalog.dependencies.at(1).library = catalog.dependencies.at(0).library;
  }));
  EXPECT_TRUE(rejects([](PakCatalog& catalog) -> void {
    catalog.dependencies.at(0).library.clear();
  }));
  EXPECT_TRUE(rejects([](PakCatalog& catalog) -> void {
    catalog.dependencies.at(0).requirements.clear();
  }));
  EXPECT_TRUE(rejects([](PakCatalog& catalog) -> void {
    auto& requirements = catalog.dependencies.at(0).requirements;
    requirements.push_back(requirements.at(0));
  }));
  EXPECT_TRUE(rejects([](PakCatalog& catalog) -> void {
    catalog.dependencies.at(0).requirements.at(0).type = AssetType::kUnknown;
  }));
  EXPECT_TRUE(rejects([](PakCatalog& catalog) -> void {
    catalog.dependencies.at(0).requirements.at(1).type = AssetType::kMaterial;
  }));
  EXPECT_TRUE(rejects([](PakCatalog& catalog) -> void {
    catalog.dependencies.at(0).requirements.at(0).kind
      = KeyReferenceKind::kPhysicsResource;
  }));
  EXPECT_TRUE(rejects([](PakCatalog& catalog) -> void {
    catalog.dependencies.at(0).requirements.at(0).key = {};
  }));
}

//! An asset and a texture may share a key: a requirement is its kind and key.
NOLINT_TEST(PakCatalogTest, AcceptsAnAssetAndATextureOfOneKey)
{
  auto catalog = Catalog();
  catalog.dependencies.at(0).requirements.push_back(PakRequirement {
    .kind = KeyReferenceKind::kTexture,
    .key = Key(6U),
    .type = AssetType::kUnknown,
  });
  EXPECT_TRUE(catalog.ComputeDigest());
}

NOLINT_TEST(PakCatalogTest, RejectsTruncationCorruptionAndOversizedCounts)
{
  const auto valid = Catalog().Encode().value();
  for (size_t size = 0; size < valid.size(); ++size) {
    EXPECT_FALSE(PakCatalog::Decode(std::span(valid).first(size))) << size;
  }
  auto trailing = valid;
  trailing.push_back(std::byte { 0U });
  EXPECT_FALSE(PakCatalog::Decode(trailing));
  auto bytes = valid;
  bytes.at(bytes.size() - 1U) ^= std::byte { 1U };
  EXPECT_FALSE(PakCatalog::Decode(bytes));
  bytes = valid;
  bytes.at(0) = std::byte { 0U };
  EXPECT_FALSE(PakCatalog::Decode(bytes));
  bytes = valid;
  constexpr auto kEntryCountOffset
    = sizeof(uint32_t) + SourceKey::kSizeBytes + sizeof(uint16_t);
  for (size_t index = 0; index < sizeof(uint32_t); ++index) {
    bytes.at(kEntryCountOffset + index) = std::byte { 0xFFU };
  }
  EXPECT_FALSE(PakCatalog::Decode(bytes));
}

} // namespace
