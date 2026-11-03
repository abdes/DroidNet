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
#include <string_view>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Testing/GTest.h>

namespace {
using oxygen::data::AssetKey;
using oxygen::data::AssetType;
using oxygen::data::CookedDefect;
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
    .mount = "Props",
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
      { .mount = "Engine", .requirements = {
        { .kind = KeyReferenceKind::kAsset, .key = Key(6U),
          .type = AssetType::kMaterial, },
        { .kind = KeyReferenceKind::kTexture, .key = Key(7U),
          .type = AssetType::kUnknown, },
      }, },
      { .mount = "Vehicles", .requirements = {
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
  EXPECT_EQ(decoded->mount, original.mount);
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

//! The mount is part of the catalog: renaming it changes the digest.
NOLINT_TEST(PakCatalogTest, MountIsPartOfTheDigest)
{
  auto catalog = Catalog();
  catalog.mount = "Others";
  EXPECT_NE(catalog.ComputeDigest().value(), catalog.catalog_digest);
  catalog.mount.clear();
  EXPECT_FALSE(catalog.ComputeDigest());
  catalog.mount = std::string(256U, 'A');
  EXPECT_FALSE(catalog.ComputeDigest());
}

//! Every pak names its mount, even one that lists nothing.
NOLINT_TEST(PakCatalogTest, EveryPakNamesItsMount)
{
  auto empty = PakCatalog { .source_key = Source(3U) };
  EXPECT_FALSE(empty.ComputeDigest());
  empty.mount = "Props";
  EXPECT_TRUE(empty.ComputeDigest());
}

//! A mount is required once, never the pak's own, and for something; each
//! requirement names an asset with its type or a texture, once.
NOLINT_TEST(PakCatalogTest, RejectsInvalidDependencies)
{
  const auto rejects = [](auto edit) -> bool {
    auto catalog = Catalog();
    edit(catalog);
    return !catalog.ComputeDigest();
  };
  EXPECT_TRUE(rejects([](PakCatalog& catalog) -> void {
    catalog.dependencies.at(0).mount = catalog.mount;
  }));
  EXPECT_TRUE(rejects([](PakCatalog& catalog) -> void {
    catalog.dependencies.at(1).mount = catalog.dependencies.at(0).mount;
  }));
  EXPECT_TRUE(rejects([](PakCatalog& catalog) -> void {
    catalog.dependencies.at(0).mount.clear();
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

//! The rule decoding `bytes`, which must fail, reports broken.
auto DecodeDefect(const std::span<const std::byte> bytes) -> CookedDefect
{
  const auto decoded = PakCatalog::Decode(bytes);
  if (decoded) {
    ADD_FAILURE() << "the catalog decoded";
    return {};
  }
  return decoded.error().Defect();
}

// The wire layout, from the format: the version, source identity, content
// version and four record counts, then the mount (a one-byte size and its
// characters), then the entries (key, type, two digests); the digest of the
// bytes before it ends the catalog.
constexpr auto kEntryCountOffset
  = sizeof(uint32_t) + SourceKey::kSizeBytes + sizeof(uint16_t);
constexpr auto kHeaderSize = kEntryCountOffset + (4U * sizeof(uint32_t));
constexpr auto kDigestSize = size_t { 32U };
constexpr auto kEntrySize = AssetKey::kSizeBytes + 1U + (2U * kDigestSize);

//! `bytes`, a catalog of the mount `mount`, with its first two entries
//! swapped and its digest as encoded.
auto WithSwappedEntries(std::vector<std::byte> bytes,
  const std::string_view mount) -> std::vector<std::byte>
{
  const auto first = kHeaderSize + 1U + mount.size();
  const auto entries = std::span(bytes).subspan(first, 2U * kEntrySize);
  std::ranges::swap_ranges(
    entries.first(kEntrySize), entries.subspan(kEntrySize));
  return bytes;
}

//! Signs `bytes` again: their digest becomes the SHA-256 of the bytes before
//! it, so a test reaches the checks that follow the digest's.
auto Reseal(std::vector<std::byte>& bytes) -> void
{
  const auto signed_bytes = std::span(bytes).first(bytes.size() - kDigestSize);
  const auto digest = oxygen::base::ComputeSha256(signed_bytes);
  std::ranges::copy(std::as_bytes(std::span(digest)),
    std::span(bytes).last(kDigestSize).begin());
}

//! A catalog cut short is truncated or short of what its counts declare,
//! at every length.
NOLINT_TEST(PakCatalogTest, RejectsEveryTruncation)
{
  const auto encoded = Catalog().Encode();
  ASSERT_HAS_VALUE(encoded);
  const auto& valid = *encoded;

  for (size_t size = 0; size < valid.size(); ++size) {
    const auto defect = DecodeDefect(std::span(valid).first(size));
    EXPECT_THAT(defect,
      ::testing::AnyOf(::testing::Eq(CookedDefect::kTruncated),
        ::testing::Eq(CookedDefect::kSizeMismatch)))
      << size;
  }
}

//! Trailing bytes, a damaged digest and counts the bytes cannot hold are
//! refused for their rule; another version is unsupported.
NOLINT_TEST(PakCatalogTest, RejectsCorruptionAndOversizedCounts)
{
  const auto encoded = Catalog().Encode();
  ASSERT_HAS_VALUE(encoded);
  const auto& valid = *encoded;

  auto trailing = valid;
  trailing.push_back(std::byte { 0U });
  auto damaged_digest = valid;
  damaged_digest.at(damaged_digest.size() - 1U) ^= std::byte { 1U };
  auto other_version = valid;
  other_version.at(0) = std::byte { 0U };
  auto oversized = valid;
  for (size_t index = 0; index < sizeof(uint32_t); ++index) {
    oversized.at(kEntryCountOffset + index) = std::byte { 0xFFU };
  }

  EXPECT_EQ(DecodeDefect(trailing), CookedDefect::kSizeMismatch);
  EXPECT_EQ(DecodeDefect(damaged_digest), CookedDefect::kDigestMismatch);
  EXPECT_EQ(DecodeDefect(other_version), CookedDefect::kUnsupportedVersion);
  EXPECT_EQ(DecodeDefect(oversized), CookedDefect::kSizeMismatch);
}

//! The digest authenticates the bytes received: entries swapped under the
//! original digest are refused, though the catalog they hold is the same.
NOLINT_TEST(PakCatalogTest, RejectsBytesTheirDigestDoesNotCover)
{
  const auto catalog = Catalog();
  const auto encoded = catalog.Encode();
  ASSERT_HAS_VALUE(encoded);

  const auto bytes = WithSwappedEntries(*encoded, catalog.mount);

  EXPECT_EQ(DecodeDefect(bytes), CookedDefect::kDigestMismatch);
}

//! Entries out of canonical order are refused even under the digest of the
//! bytes as they are.
NOLINT_TEST(PakCatalogTest, RejectsRecordsOutOfCanonicalOrder)
{
  const auto catalog = Catalog();
  const auto encoded = catalog.Encode();
  ASSERT_HAS_VALUE(encoded);

  auto bytes = WithSwappedEntries(*encoded, catalog.mount);
  Reseal(bytes);

  EXPECT_EQ(DecodeDefect(bytes), CookedDefect::kNotCanonical);
}

//! Names a virtual path cannot hold as its mount; each fits the field.
constexpr auto kMisspelledMounts = std::array {
  "bad/name",
  "1Props",
  "Pro ps",
  "Pro.ps",
  ".",
  ".Props",
  "Pr\xC3\xB6ps", // "Pröps": a letter beyond ASCII
};

//! A catalog names only mounts a virtual path can name, for itself and for
//! each dependency, so a dependency can always match a provider.
NOLINT_TEST(PakCatalogTest, RejectsMountsAVirtualPathCannotName)
{
  for (const auto* const mount : kMisspelledMounts) {
    auto own = Catalog();
    own.mount = mount;
    EXPECT_FALSE(own.ComputeDigest()) << mount;
    auto dependency = Catalog();
    dependency.dependencies.at(0).mount = mount;
    EXPECT_FALSE(dependency.ComputeDigest()) << mount;
  }
  auto spelled = Catalog();
  spelled.mount = "_Props-2";
  EXPECT_TRUE(spelled.ComputeDigest());
}

//! A decoded catalog whose mount a virtual path cannot name is refused, even
//! under a digest of its bytes.
NOLINT_TEST(PakCatalogTest, DecodeRefusesAMisspelledMount)
{
  const auto catalog = Catalog();
  ASSERT_EQ(catalog.mount, "Props");
  const auto encoded = catalog.Encode();
  ASSERT_HAS_VALUE(encoded);
  auto bytes = *encoded;
  // The mount's characters follow its one-byte size, after the header.
  bytes.at(kHeaderSize + 1U + 2U) = std::byte { '/' };
  Reseal(bytes);

  EXPECT_EQ(DecodeDefect(bytes), CookedDefect::kInvalidPath);
}

//! The received bytes' digest is checked before their record values.
NOLINT_TEST(PakCatalogTest, DigestMismatchPrecedesInvalidEntry)
{
  // Arrange
  auto bytes = Catalog().Encode().value();
  const auto entry_type
    = kHeaderSize + 1U + Catalog().mount.size() + AssetKey::kSizeBytes;
  bytes.at(entry_type) = std::byte { 0U };

  // Act / Assert
  EXPECT_EQ(DecodeDefect(bytes), CookedDefect::kDigestMismatch);
  Reseal(bytes);
  EXPECT_EQ(DecodeDefect(bytes), CookedDefect::kInvalidValue);
}

//! Structural validation precedes canonical-order validation.
NOLINT_TEST(PakCatalogTest, InvalidEntryPrecedesNoncanonicalOrder)
{
  // Arrange
  const auto catalog = Catalog();
  auto bytes = WithSwappedEntries(catalog.Encode().value(), catalog.mount);
  const auto entry_type
    = kHeaderSize + 1U + catalog.mount.size() + AssetKey::kSizeBytes;
  bytes.at(entry_type) = std::byte { 0U };
  Reseal(bytes);

  // Act / Assert
  EXPECT_EQ(DecodeDefect(bytes), CookedDefect::kInvalidValue);
}

//! Entries are validated before deletions, bases, and dependencies.
NOLINT_TEST(PakCatalogTest, StructuralFailuresFollowRecordOrder)
{
  // Arrange
  auto catalog = Catalog();
  catalog.entries.at(0).asset_type = AssetType::kUnknown;
  catalog.deleted.at(0) = {};
  catalog.bases.at(0).source_key = catalog.source_key;
  catalog.dependencies.at(0).mount = catalog.mount;

  // Act / Assert
  auto result = catalog.ComputeDigest();
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(), "Invalid PAK catalog entry");

  catalog.entries.at(0).asset_type = AssetType::kMaterial;
  result = catalog.ComputeDigest();
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(), "Invalid PAK deletion");

  catalog.deleted.at(0) = Key(4U);
  result = catalog.ComputeDigest();
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(), "PAK is its own base");

  catalog.bases.at(0).source_key = Source(1U);
  result = catalog.ComputeDigest();
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(), "PAK depends on its mount");
}

} // namespace
