//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::ResourceIndexT;
using oxygen::data::AssetKey;
using oxygen::data::AssetReferences;
using oxygen::data::AssetType;
using oxygen::data::KeyReference;
using oxygen::data::KeyReferenceKind;
using oxygen::data::kNoResourceReference;
using oxygen::data::ResourceBinding;
using oxygen::data::ResourceKind;
using oxygen::data::ResourceReferenceIndex;

static_assert(!std::is_convertible_v<ResourceReferenceIndex, ResourceIndexT>);
static_assert(sizeof(ResourceReferenceIndex) == sizeof(uint32_t));

auto TestKey() -> AssetKey
{
  auto bytes = AssetKey::ByteArray {};
  bytes.back() = 1U;
  return AssetKey::FromBytes(bytes);
}

NOLINT_TEST(AssetReferencesTest, AbsenceAndFirstBindingFallbackRemainDistinct)
{
  const auto references = AssetReferences::Create(
    { { .kind = ResourceKind::kTexture, .index = ResourceIndexT { 0U } } }, {});
  ASSERT_TRUE(references.has_value());
  const auto absent
    = references->ResolveResource(kNoResourceReference, ResourceKind::kTexture);
  ASSERT_TRUE(absent.has_value());
  EXPECT_FALSE(absent->has_value());
  const auto fallback = references->ResolveResource(
    ResourceReferenceIndex { 0U }, ResourceKind::kTexture);
  ASSERT_TRUE(fallback.has_value());
  ASSERT_TRUE(fallback->has_value());
  EXPECT_EQ(*fallback, std::optional { ResourceIndexT { 0U } });
}

NOLINT_TEST(AssetReferencesTest, RejectsOutOfRangeAndWrongKindReferences)
{
  const auto references = AssetReferences::Create(
    { { .kind = ResourceKind::kBuffer, .index = ResourceIndexT { 7U } } }, {});
  ASSERT_TRUE(references.has_value());
  EXPECT_FALSE(references
      ->ResolveResource(ResourceReferenceIndex { 1U }, ResourceKind::kBuffer)
      .has_value());
  EXPECT_FALSE(references
      ->ResolveResource(ResourceReferenceIndex { 0U }, ResourceKind::kTexture)
      .has_value());
  EXPECT_FALSE(references
      ->ResolveResource(kNoResourceReference, static_cast<ResourceKind>(0U))
      .has_value());
}

NOLINT_TEST(
  AssetReferencesTest, ErrorTextureSurvivesRoundTripAsAnExplicitBinding)
{
  using oxygen::data::pak::core::kErrorTextureResourceIndex;
  const auto references = AssetReferences::Create(
    { { .kind = ResourceKind::kTexture, .index = kErrorTextureResourceIndex } },
    {});
  ASSERT_TRUE(references.has_value());
  const auto encoded = references->Encode();
  ASSERT_TRUE(encoded.has_value());
  const auto decoded = AssetReferences::Decode(*encoded, 1U, 0U);
  ASSERT_TRUE(decoded.has_value());
  const auto resolved = decoded->ResolveResource(
    ResourceReferenceIndex { 0U }, ResourceKind::kTexture);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, std::optional { kErrorTextureResourceIndex });
  const auto absent
    = decoded->ResolveResource(kNoResourceReference, ResourceKind::kTexture);
  ASSERT_TRUE(absent.has_value());
  EXPECT_FALSE(absent->has_value());
  for (const auto kind :
    { ResourceKind::kBuffer, ResourceKind::kScript, ResourceKind::kPhysics }) {
    EXPECT_FALSE(AssetReferences::Create(
      { { .kind = kind, .index = kErrorTextureResourceIndex } }, {})
        .has_value());
  }
}

NOLINT_TEST(AssetReferencesTest, RejectsUnknownKindsAndInvalidNullResources)
{
  for (const auto kind : {
         ResourceKind::kBuffer,
         ResourceKind::kScript,
         ResourceKind::kPhysics,
         static_cast<ResourceKind>(0U),
       }) {
    EXPECT_FALSE(AssetReferences::Create(
      { { .kind = kind, .index = ResourceIndexT { 0U } } }, {})
        .has_value());
  }
  const ResourceBinding binding {
    .kind = ResourceKind::kTexture,
    .index = ResourceIndexT { 5U },
  };
  const auto aliases = AssetReferences::Create({ binding, binding }, {});
  ASSERT_TRUE(aliases.has_value());
  ASSERT_EQ(aliases->Resources().size(), 2U);
  const auto first = aliases->ResolveResource(
    ResourceReferenceIndex { 0U }, ResourceKind::kTexture);
  const auto second = aliases->ResolveResource(
    ResourceReferenceIndex { 1U }, ResourceKind::kTexture);
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*first, *second);
  EXPECT_TRUE(AssetReferences::Create(
    { binding, { .kind = ResourceKind::kBuffer, .index = binding.index } }, {})
      .has_value());
}

NOLINT_TEST(AssetReferencesTest, RejectsInvalidAndConflictingKeyTargets)
{
  const std::array invalid {
    KeyReference {
      .key = {},
      .kind = KeyReferenceKind::kAsset,
      .expected_type = AssetType::kMaterial,
    },
    KeyReference {
      .key = TestKey(),
      .kind = KeyReferenceKind::kAsset,
      .expected_type = AssetType::kUnknown,
    },
    KeyReference {
      .key = TestKey(),
      .kind = KeyReferenceKind::kPhysicsResource,
      .expected_type = AssetType::kMaterial,
    },
    KeyReference {
      .key = TestKey(),
      .kind = static_cast<KeyReferenceKind>(0U),
      .expected_type = AssetType::kMaterial,
    },
    KeyReference {
      .key = TestKey(),
      .kind = KeyReferenceKind::kLogical,
      .expected_type = static_cast<AssetType>(255U),
    },
  };
  for (const auto& reference : invalid) {
    EXPECT_FALSE(AssetReferences::Create({}, { reference }).has_value());
  }
  const KeyReference material {
    .key = TestKey(),
    .kind = KeyReferenceKind::kAsset,
    .expected_type = AssetType::kMaterial,
  };
  const KeyReference geometry {
    .key = TestKey(),
    .kind = KeyReferenceKind::kAsset,
    .expected_type = AssetType::kGeometry,
  };
  EXPECT_FALSE(AssetReferences::Create({}, { material, material }).has_value());
  EXPECT_FALSE(AssetReferences::Create({}, { material, geometry }).has_value());
}

NOLINT_TEST(AssetReferencesTest, EncodesIndependentLittleEndianWireVector)
{
  const auto references = AssetReferences::Create(
    {
      {
        .kind = ResourceKind::kBuffer,
        .index = ResourceIndexT { 0x04030201U },
      },
    },
    {
      {
        .key = TestKey(),
        .kind = KeyReferenceKind::kAsset,
        .expected_type = AssetType::kMaterial,
      },
    });
  ASSERT_TRUE(references.has_value());
  const auto encoded = references->Encode();
  ASSERT_TRUE(encoded.has_value());
  // Buffer kind/index, 16-byte key, asset target kind and material type.
  const std::array<uint8_t, 23> expected {
    1,
    1,
    2,
    3,
    4,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    1,
    1,
    1,
  };
  EXPECT_TRUE(std::ranges::equal(*encoded, std::as_bytes(std::span(expected))));
  const auto decoded = AssetReferences::Decode(*encoded, 1U, 1U);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_TRUE(
    std::ranges::equal(decoded->Resources(), references->Resources()));
  EXPECT_TRUE(std::ranges::equal(decoded->Keys(), references->Keys()));
}

NOLINT_TEST(
  AssetReferencesTest, RejectsTruncationTrailingBytesAndOversizedCounts)
{
  constexpr auto kMaxCount = std::numeric_limits<uint32_t>::max();
  const std::array<std::byte, 4> truncated {};
  const std::array<std::byte, 6> trailing {};
  EXPECT_FALSE(AssetReferences::Decode(truncated, 1U, 0U).has_value());
  EXPECT_FALSE(AssetReferences::Decode(trailing, 1U, 0U).has_value());
  EXPECT_FALSE(AssetReferences::Decode({}, kMaxCount, kMaxCount).has_value());
}

NOLINT_TEST(AssetReferencesTest, EmptyInventoryRoundTripsWithoutSentinelRecords)
{
  const AssetReferences references;
  const auto encoded = references.Encode();
  ASSERT_TRUE(encoded.has_value());
  EXPECT_TRUE(encoded->empty());
  const auto decoded = AssetReferences::Decode(*encoded, 0U, 0U);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_TRUE(decoded->Resources().empty());
  EXPECT_TRUE(decoded->Keys().empty());
}

NOLINT_TEST(
  AssetReferencesTest, CanonicalizesKeysWithoutReorderingLocalBindings)
{
  const std::vector resources {
    ResourceBinding {
      .kind = ResourceKind::kScript,
      .index = ResourceIndexT { 9U },
    },
    ResourceBinding {
      .kind = ResourceKind::kBuffer,
      .index = ResourceIndexT { 2U },
    },
  };
  const KeyReference logical {
    .key = TestKey(),
    .kind = KeyReferenceKind::kLogical,
    .expected_type = AssetType::kScene,
  };
  const KeyReference physics {
    .key = TestKey(),
    .kind = KeyReferenceKind::kPhysicsResource,
    .expected_type = AssetType::kUnknown,
  };
  const auto references
    = AssetReferences::Create(resources, { logical, physics });
  ASSERT_TRUE(references.has_value());
  EXPECT_TRUE(std::ranges::equal(references->Resources(), resources));
  const auto ordered_keys = std::array { physics, logical };
  EXPECT_TRUE(std::ranges::equal(references->Keys(), ordered_keys));
}

NOLINT_TEST(
  AssetReferencesTest, UsagePreservesRepeatedFieldsAndRelocatedAliases)
{
  const ResourceBinding fallback {
    .kind = ResourceKind::kTexture,
    .index = ResourceIndexT { 0U },
  };
  const ResourceBinding error {
    .kind = ResourceKind::kTexture,
    .index = oxygen::data::pak::core::kErrorTextureResourceIndex,
  };
  const auto references
    = AssetReferences::Create({ fallback, fallback, error }, {});
  ASSERT_TRUE(references.has_value());
  using oxygen::data::ResourceReferenceUse;
  const auto uses = std::array {
    ResourceReferenceUse {
      .reference = ResourceReferenceIndex { 0U },
      .kind = ResourceKind::kTexture,
    },
    ResourceReferenceUse {
      .reference = ResourceReferenceIndex { 0U },
      .kind = ResourceKind::kTexture,
    },
    ResourceReferenceUse {
      .reference = ResourceReferenceIndex { 1U },
      .kind = ResourceKind::kTexture,
    },
    ResourceReferenceUse {
      .reference = ResourceReferenceIndex { 2U },
      .kind = ResourceKind::kTexture,
    },
    ResourceReferenceUse {
      .reference = kNoResourceReference,
      .kind = ResourceKind::kTexture,
    },
  };
  EXPECT_TRUE(references->ValidateUsage(uses, {}).has_value());
  EXPECT_FALSE(
    references->ValidateUsage(std::span(uses).first(2U), {}).has_value());
  auto wrong_kind = uses;
  wrong_kind.at(2).kind = ResourceKind::kBuffer;
  EXPECT_FALSE(references->ValidateUsage(wrong_kind, {}).has_value());
  auto outside = uses;
  outside.at(2).reference = ResourceReferenceIndex { 3U };
  EXPECT_FALSE(references->ValidateUsage(outside, {}).has_value());
}

NOLINT_TEST(AssetReferencesTest, UsageRequiresExactKeyKindsAndTypes)
{
  const KeyReference expected {
    .key = TestKey(),
    .kind = KeyReferenceKind::kAsset,
    .expected_type = AssetType::kMaterial,
  };
  const auto references = AssetReferences::Create({}, { expected });
  ASSERT_TRUE(references.has_value());
  const auto duplicate_uses
    = std::array { expected, expected, KeyReference {} };
  EXPECT_TRUE(references->ValidateUsage({}, duplicate_uses).has_value());
  EXPECT_FALSE(references->ValidateUsage({}, {}).has_value());
  auto wrong = expected;
  wrong.expected_type = AssetType::kGeometry;
  EXPECT_FALSE(
    references->ValidateUsage({}, std::span(&wrong, 1U)).has_value());
  wrong = expected;
  wrong.kind = KeyReferenceKind::kLogical;
  EXPECT_FALSE(
    references->ValidateUsage({}, std::span(&wrong, 1U)).has_value());
  const AssetReferences empty;
  EXPECT_FALSE(empty.ValidateUsage({}, std::span(&expected, 1U)).has_value());
  EXPECT_TRUE(empty.ValidateUsage({}, {}).has_value());
}

} // namespace
