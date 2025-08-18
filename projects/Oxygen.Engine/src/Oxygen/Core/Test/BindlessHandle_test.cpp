//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Bindless/Generated.Constants.h>
#include <Oxygen/Core/Types/BindlessHandle.h>

namespace {

//! Validate the invalid sentinel round-trips and value semantics.
NOLINT_TEST(BindlessHandle, Invalid_RecognizesInvalidSentinel)
{
  // Arrange
  auto invalid = oxygen::kInvalidBindlessHandle;

  // Act

  // Assert
  EXPECT_EQ(invalid.get(), oxygen::kInvalidBindlessIndex);
}

//! Ensure to_string returns a human-readable numeric representation.
NOLINT_TEST(BindlessHandle, ToString_ContainsNumericValue)
{
  // Arrange
  oxygen::BindlessHandle h { 42u };

  // Act
  auto s = to_string(h);

  // Assert
  EXPECT_NE(s.find("42"), std::string::npos);
}

//! Pack/unpack keeps index and generation and IsValid reports correctly.
NOLINT_TEST(VersionedBindlessHandle, PackUnpack_RetainsIndexAndGeneration)
{
  using oxygen::BindlessHandle;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;

  // Arrange
  Generation gen { 3u };
  BindlessHandle idx { 7u };
  VersionedBindlessHandle v { idx, gen };

  // Act
  auto packed = v.ToPacked();
  auto unpacked = VersionedBindlessHandle::FromPacked(packed);

  // Assert
  EXPECT_TRUE(v.IsValid());
  EXPECT_EQ(v.ToBindlessHandle(), idx);
  EXPECT_EQ(v.GenerationValue().get(), gen.get());
  EXPECT_EQ(unpacked.ToBindlessHandle(), idx);
  EXPECT_EQ(unpacked.GenerationValue().get(), gen.get());
}

//! Explicit hasher should produce identical hashes for equal handles.
NOLINT_TEST(VersionedBindlessHandle, Hash_EqualForEqualValues)
{
  using oxygen::BindlessHandle;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;
  using Hasher = VersionedBindlessHandle::Hasher;

  // Arrange
  Generation gen { 1u };
  VersionedBindlessHandle a { BindlessHandle { 5u }, gen };
  VersionedBindlessHandle b { BindlessHandle { 5u }, gen };

  // Act
  Hasher hasher;

  // Assert
  EXPECT_EQ(hasher(a), hasher(b));
}

//! Invalid/uninitialized versioned handle packing should round-trip to invalid.
NOLINT_TEST(
  VersionedBindlessHandle, InvalidPack_UninitializedIsInvalidAfterPack)
{
  // Arrange
  oxygen::VersionedBindlessHandle v_default {};

  // Act
  auto packed = v_default.ToPacked();
  auto unpacked = oxygen::VersionedBindlessHandle::FromPacked(packed);

  // Assert
  EXPECT_FALSE(unpacked.IsValid());
  EXPECT_EQ(unpacked.ToBindlessHandle().get(), oxygen::kInvalidBindlessIndex);
}

//! Different generations must produce different hashes for same index.
NOLINT_TEST(
  VersionedBindlessHandle, Hash_DifferentGenerationsProduceDifferentHashes)
{
  using oxygen::BindlessHandle;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;
  using Hasher = VersionedBindlessHandle::Hasher;

  // Arrange
  BindlessHandle idx { 10u };
  VersionedBindlessHandle a { idx, Generation { 1u } };
  VersionedBindlessHandle b { idx, Generation { 2u } };

  // Act
  Hasher hasher;

  // Assert
  EXPECT_NE(hasher(a), hasher(b));
}

//! to_string edge cases: zero and max index formatting.
NOLINT_TEST(BindlessHandle, ToString_ZeroAndMaxFormatting)
{
  // Arrange
  oxygen::BindlessHandle zero { 0u };
  oxygen::BindlessHandle max { std::numeric_limits<uint32_t>::max() };

  // Act
  auto s0 = to_string(zero);
  auto smax = to_string(max);

  // Assert: both string forms contain the numeric forms
  EXPECT_NE(s0.find("0"), std::string::npos);
  EXPECT_NE(smax.find(std::to_string(
              static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))),
    std::string::npos);
}

//! Exact formatting: Versioned to_string should include the index and
//! generation using the exact format "Bindless(i:<index>, g:<generation>)".
NOLINT_TEST(VersionedBindlessHandle, ToString_IncludesIndexAndGenerationExact)
{
  using oxygen::BindlessHandle;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;

  // Arrange
  VersionedBindlessHandle v { BindlessHandle { 7u }, Generation { 3u } };

  // Act
  auto s = to_string(v);

  // Assert
  EXPECT_EQ(s, std::string("Bindless(i:7, g:3)"));
}

//! to_string for the invalid sentinel should render the invalid numeric index
//! so callers can detect sentinel values in logs.
NOLINT_TEST(BindlessHandle, ToString_InvalidSentinelProducesInvalidIndex)
{
  // Arrange
  auto s = to_string(oxygen::kInvalidBindlessHandle);

  // Assert: contains numeric sentinel
  EXPECT_NE(s.find(std::to_string(
              static_cast<uint64_t>(oxygen::kInvalidBindlessIndex))),
    std::string::npos);
}

//! Verify max-value formatting for VersionedBindlessHandle prints full uint32_t
NOLINT_TEST(VersionedBindlessHandle, ToString_MaxValuesFormatting)
{
  using oxygen::BindlessHandle;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;

  // Arrange
  const uint32_t max = std::numeric_limits<uint32_t>::max();
  VersionedBindlessHandle v { BindlessHandle { max }, Generation { max } };

  // Act
  auto s = to_string(v);

  // Assert
  const auto expected = std::string("Bindless(i:") + std::to_string(max)
    + ", g:" + std::to_string(max) + ")";
  EXPECT_EQ(s, expected);
}

//! Near-max generation packing and wrap-around behavior.
NOLINT_TEST(VersionedBindlessHandle, WrapAround_NearMaxGenerationPacking)
{
  using oxygen::BindlessHandle;
  using oxygen::VersionedBindlessHandle;
  using Generation = oxygen::VersionedBindlessHandle::Generation;

  // Arrange
  const uint32_t near_max = std::numeric_limits<uint32_t>::max() - 1u;
  BindlessHandle idx { 123u };
  Generation g1 { near_max };
  VersionedBindlessHandle v1 { idx, g1 };

  // Act: increment generation (simulate allocator overflow)
  auto g2 = Generation { static_cast<uint32_t>(g1.get() + 1u) };
  VersionedBindlessHandle v2 { idx, g2 };

  auto packed1 = v1.ToPacked();
  auto packed2 = v2.ToPacked();
  auto unpack1 = VersionedBindlessHandle::FromPacked(packed1);
  auto unpack2 = VersionedBindlessHandle::FromPacked(packed2);

  // Assert: packed values differ and generation fields preserved modulo 2^32
  // Compare the raw underlying packed numeric values explicitly.
  EXPECT_NE(packed1.get(), packed2.get());
  EXPECT_EQ(unpack1.GenerationValue().get(), g1.get());
  EXPECT_EQ(unpack2.GenerationValue().get(), g2.get());
}

//! Ordering: when indices equal, ordering follows generation.
NOLINT_TEST(VersionedBindlessHandle, Order_OrdersByGenerationWhenIndexEqual)
{
  using oxygen::BindlessHandle;
  using oxygen::VersionedBindlessHandle;
  using Generation = oxygen::VersionedBindlessHandle::Generation;

  // Arrange
  BindlessHandle idx { 50u };
  VersionedBindlessHandle low { idx, Generation { 1u } };
  VersionedBindlessHandle high { idx, Generation { 2u } };

  // Act / Assert: direct comparison uses VersionedBindlessHandle's operator<=>
  EXPECT_LT(low, high);
  EXPECT_TRUE(low <= high);
  EXPECT_FALSE(high < low);
}

//! Different indices should order by index regardless of generation.
NOLINT_TEST(VersionedBindlessHandle, Order_OrdersByIndexFirst)
{
  using oxygen::BindlessHandle;
  using oxygen::VersionedBindlessHandle;
  using Generation = oxygen::VersionedBindlessHandle::Generation;

  // Arrange
  VersionedBindlessHandle a { BindlessHandle { 10 }, Generation { 5 } };
  VersionedBindlessHandle b { BindlessHandle { 11 }, Generation { 0 } };

  // Assert
  EXPECT_LT(a, b);
  EXPECT_FALSE(b < a);
}

//! Verify transitivity: if a < b and b < c then a < c
NOLINT_TEST(VersionedBindlessHandle, Order_TransitiveOrdering)
{
  using oxygen::BindlessHandle;
  using oxygen::VersionedBindlessHandle;
  using Generation = oxygen::VersionedBindlessHandle::Generation;

  // Arrange
  VersionedBindlessHandle a { BindlessHandle { 1 }, Generation { 1 } };
  VersionedBindlessHandle b { BindlessHandle { 1 }, Generation { 2 } };
  VersionedBindlessHandle c { BindlessHandle { 2 }, Generation { 0 } };

  // Assert
  EXPECT_LT(a, b);
  EXPECT_LT(b, c);
  EXPECT_LT(a, c);
}

//! Equal when both index and generation match exactly.
NOLINT_TEST(VersionedBindlessHandle, Order_EqualityWhenBothMatch)
{
  using oxygen::BindlessHandle;
  using oxygen::VersionedBindlessHandle;
  using Generation = oxygen::VersionedBindlessHandle::Generation;

  // Arrange
  VersionedBindlessHandle x { BindlessHandle { 42 }, Generation { 7 } };
  VersionedBindlessHandle y { BindlessHandle { 42 }, Generation { 7 } };

  // Assert
  EXPECT_EQ(x, y);
  EXPECT_FALSE(x < y);
  EXPECT_FALSE(y < x);
}

} // namespace
