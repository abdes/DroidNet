//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <limits>
#include <type_traits>
#include <utility>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.Constants.h>
#include <Oxygen/Core/Bindless/Types.h>

// Google Test matchers for logging tests
using testing::AllOf;
using testing::HasSubstr;

namespace {

//! Validate the invalid sentinel round-trips and value semantics.
NOLINT_TEST(Handle, Invalid_RecognizesInvalidSentinel)
{
  // Arrange
  auto invalid = oxygen::kInvalidBindlessHeapIndex;

  // Act

  // Assert
  EXPECT_EQ(invalid.get(), oxygen::kInvalidBindlessIndex);
}

//! Ensure to_string returns a human-readable numeric representation.
NOLINT_TEST(Handle, ToString_ContainsNumericValue)
{
  // Arrange
  oxygen::BindlessHeapIndex h { 42u };

  // Act
  auto s = to_string(h);

  // Assert
  EXPECT_NE(s.find("42"), std::string::npos);
}

//! Pack/unpack keeps index and generation and IsValid reports correctly.
NOLINT_TEST(Versioned, PackUnpack_RetainsIndexAndGeneration)
{
  using oxygen::BindlessHeapIndex;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;

  // Arrange
  Generation gen { 3u };
  BindlessHeapIndex idx { 7u };
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
NOLINT_TEST(Versioned, Hash_EqualForEqualValues)
{
  using oxygen::BindlessHeapIndex;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;
  using Hasher = VersionedBindlessHandle::Hasher;

  // Arrange
  Generation gen { 1u };
  VersionedBindlessHandle a { BindlessHeapIndex { 5u }, gen };
  VersionedBindlessHandle b { BindlessHeapIndex { 5u }, gen };

  // Act
  Hasher hasher;

  // Assert
  EXPECT_EQ(hasher(a), hasher(b));
}

//! Invalid/uninitialized versioned handle packing should round-trip to invalid.
NOLINT_TEST(Versioned, InvalidPack_UninitializedIsInvalidAfterPack)
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
NOLINT_TEST(Versioned, Hash_DifferentGenerationsProduceDifferentHashes)
{
  using oxygen::BindlessHeapIndex;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;
  using Hasher = VersionedBindlessHandle::Hasher;

  // Arrange
  BindlessHeapIndex idx { 10u };
  VersionedBindlessHandle a { idx, Generation { 1u } };
  VersionedBindlessHandle b { idx, Generation { 2u } };

  // Act
  Hasher hasher;

  // Assert
  EXPECT_NE(hasher(a), hasher(b));
}

//! to_string edge cases: zero and max index formatting.
NOLINT_TEST(Handle, ToString_ZeroAndMaxFormatting)
{
  // Arrange
  oxygen::BindlessHeapIndex zero { 0u };
  oxygen::BindlessHeapIndex max { std::numeric_limits<uint32_t>::max() };

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
NOLINT_TEST(Versioned, ToString_IncludesIndexAndGenerationExact)
{
  using oxygen::BindlessHeapIndex;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;

  // Arrange
  VersionedBindlessHandle v { BindlessHeapIndex { 7u }, Generation { 3u } };

  // Act
  auto s = to_string(v);

  // Assert
  EXPECT_THAT(s, HasSubstr("VersionedBindlessHandle(i:7, g:3)"));
}

//! to_string for the invalid sentinel should render the invalid numeric index
//! so callers can detect sentinel values in logs.
NOLINT_TEST(Handle, ToString_InvalidSentinelProducesInvalidIndex)
{
  // Arrange
  auto s = to_string(oxygen::kInvalidBindlessHeapIndex);

  // Assert: contains numeric sentinel
  EXPECT_NE(s.find(std::to_string(
              static_cast<uint64_t>(oxygen::kInvalidBindlessIndex))),
    std::string::npos);
}

//! Verify max-value formatting for VersionedBindlessHandle prints full
//! uint32_t
NOLINT_TEST(Versioned, ToString_MaxValuesFormatting)
{
  using oxygen::BindlessHeapIndex;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;

  // Arrange
  constexpr uint32_t max = std::numeric_limits<uint32_t>::max();
  VersionedBindlessHandle v { BindlessHeapIndex { max }, Generation { max } };

  // Act
  auto s = to_string(v);

  // Assert
  const auto expected = std::string("VersionedBindlessHandle(i:")
    + std::to_string(max) + ", g:" + std::to_string(max) + ")";
  EXPECT_THAT(s, HasSubstr(expected));
}

//! Near-max generation packing and wrap-around behavior.
NOLINT_TEST(Versioned, WrapAround_NearMaxGenerationPacking)
{
  using oxygen::BindlessHeapIndex;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;

  // Arrange
  constexpr uint32_t near_max = std::numeric_limits<uint32_t>::max() - 1u;
  BindlessHeapIndex idx { 123u };
  Generation g1 { near_max };
  VersionedBindlessHandle v1 { idx, g1 };

  // Act: increment generation (simulate allocator overflow)
  auto g2 = Generation { (g1.get() + 1u) };
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
NOLINT_TEST(Versioned, Order_OrdersByGenerationWhenIndexEqual)
{
  using oxygen::BindlessHeapIndex;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;

  // Arrange
  BindlessHeapIndex idx { 50u };
  VersionedBindlessHandle low { idx, Generation { 1u } };
  VersionedBindlessHandle high { idx, Generation { 2u } };

  // Act / Assert: direct comparison uses VersionedBindlessHandle's
  // operator<=>
  EXPECT_LT(low, high);
  EXPECT_TRUE(low <= high);
  EXPECT_FALSE(high < low);
}

//! Different indices should order by index regardless of generation.
NOLINT_TEST(Versioned, Order_OrdersByIndexFirst)
{
  using oxygen::BindlessHeapIndex;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;

  // Arrange
  VersionedBindlessHandle a { BindlessHeapIndex { 10 }, Generation { 5 } };
  VersionedBindlessHandle b { BindlessHeapIndex { 11 }, Generation { 0 } };

  // Assert
  EXPECT_LT(a, b);
  EXPECT_FALSE(b < a);
}

//! Verify transitivity: if a < b and b < c then a < c
NOLINT_TEST(Versioned, Order_TransitiveOrdering)
{
  using oxygen::BindlessHeapIndex;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;

  // Arrange
  VersionedBindlessHandle a { BindlessHeapIndex { 1 }, Generation { 1 } };
  VersionedBindlessHandle b { BindlessHeapIndex { 1 }, Generation { 2 } };
  VersionedBindlessHandle c { BindlessHeapIndex { 2 }, Generation { 0 } };

  // Assert
  EXPECT_LT(a, b);
  EXPECT_LT(b, c);
  EXPECT_LT(a, c);
}

//! Equal when both index and generation match exactly.
NOLINT_TEST(Versioned, Order_EqualityWhenBothMatch)
{
  using oxygen::BindlessHeapIndex;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;

  // Arrange
  VersionedBindlessHandle x { BindlessHeapIndex { 42 }, Generation { 7 } };
  VersionedBindlessHandle y { BindlessHeapIndex { 42 }, Generation { 7 } };

  // Assert
  EXPECT_EQ(x, y);
  EXPECT_FALSE(x < y);
  EXPECT_FALSE(y < x);
}

TEST(AllTypes, CompileTimeProperties)
{
  using oxygen::BindlessHeapCapacity;
  using oxygen::BindlessHeapIndex;
  using oxygen::BindlessItemCount;

  // Ensure underlying size is 32-bit for all strongly-typed values used by
  // the bindless subsystem.
  static_assert(sizeof(BindlessHeapIndex) == sizeof(uint32_t));
  static_assert(sizeof(BindlessItemCount) == sizeof(uint32_t));
  static_assert(sizeof(BindlessHeapCapacity) == sizeof(uint32_t));

  // Ensure no implicit conversions exist between these strong types and raw
  // integers or the handle type.
  static_assert(!std::is_convertible_v<BindlessItemCount, BindlessHeapIndex>);
  static_assert(!std::is_convertible_v<BindlessItemCount, uint32_t>);
  static_assert(
    !std::is_convertible_v<BindlessHeapCapacity, BindlessHeapIndex>);
  static_assert(!std::is_convertible_v<BindlessHeapCapacity, uint32_t>);
}

//! BindlessItemCount supports arithmetic operations and formatting.
NOLINT_TEST(HandleCount, Arithmetic_IncrementAndAddition)
{
  using oxygen::BindlessItemCount;

  // Arrange
  BindlessItemCount count { 5u };
  BindlessItemCount other { 3u };

  // Act
  auto pre_inc = ++count;
  auto post_inc = count++;
  auto sum = count + other;
  auto diff = count - other;

  // Assert
  EXPECT_EQ(pre_inc.get(), 6u);
  EXPECT_EQ(post_inc.get(), 6u);
  EXPECT_EQ(count.get(), 7u);
  EXPECT_EQ(sum.get(), 10u);
  EXPECT_EQ(diff.get(), 4u);
}

//! BindlessItemCount comparison operations work correctly.
NOLINT_TEST(HandleCount, Comparison_OrderingAndEquality)
{
  using oxygen::BindlessItemCount;

  // Arrange
  BindlessItemCount small { 5u };
  BindlessItemCount large { 10u };
  BindlessItemCount equal { 5u };

  // Assert
  EXPECT_LT(small, large);
  EXPECT_LE(small, large);
  EXPECT_LE(small, equal);
  EXPECT_GT(large, small);
  EXPECT_GE(large, small);
  EXPECT_GE(equal, small);
  EXPECT_EQ(small, equal);
  EXPECT_NE(small, large);
}

//! BindlessHeapCapacity supports arithmetic and comparison operations.
NOLINT_TEST(HandleCapacity, Arithmetic_AdditionAndSubtraction)
{
  using oxygen::BindlessHeapCapacity;

  // Arrange
  BindlessHeapCapacity capacity { 100u };
  BindlessHeapCapacity delta { 25u };

  // Act
  auto increased = capacity + delta;
  auto decreased = capacity - delta;

  // Assert
  EXPECT_EQ(increased.get(), 125u);
  EXPECT_EQ(decreased.get(), 75u);
  EXPECT_LT(decreased, capacity);
  EXPECT_GT(increased, capacity);
}

//! Generation type supports increment and addition operations.
NOLINT_TEST(VersionedGeneration, Arithmetic_IncrementAndAddition)
{
  using Generation = oxygen::VersionedBindlessHandle::Generation;

  // Arrange
  Generation gen { 10u };
  Generation delta { 5u };

  // Act
  auto pre_inc = ++gen;
  auto post_inc = gen++;
  auto sum = gen + delta;

  // Assert
  EXPECT_EQ(pre_inc.get(), 11u);
  EXPECT_EQ(post_inc.get(), 11u);
  EXPECT_EQ(gen.get(), 12u);
  EXPECT_EQ(sum.get(), 17u);
}

//! Generation type comparison operations work correctly.
NOLINT_TEST(VersionedGeneration, Comparison_OrderingAndEquality)
{
  using Generation = oxygen::VersionedBindlessHandle::Generation;

  // Arrange
  Generation low { 3u };
  Generation high { 7u };
  Generation equal { 3u };

  // Assert
  EXPECT_LT(low, high);
  EXPECT_LE(low, high);
  EXPECT_LE(low, equal);
  EXPECT_GT(high, low);
  EXPECT_GE(high, low);
  EXPECT_GE(equal, low);
  EXPECT_EQ(low, equal);
  EXPECT_NE(low, high);
}

//! VersionedBindlessHandle move construction and assignment work correctly.
NOLINT_TEST(Versioned, MoveSemantics_ConstructionAndAssignment)
{
  using oxygen::BindlessHeapIndex;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;

  // Arrange
  VersionedBindlessHandle original { BindlessHeapIndex { 42u },
    Generation { 7u } };
  auto expected_index = original.ToBindlessHandle();
  auto expected_gen = original.GenerationValue();

  // Act: move construction
  VersionedBindlessHandle moved { std::move(original) };

  // Assert: moved object has correct values
  EXPECT_EQ(moved.ToBindlessHandle(), expected_index);
  EXPECT_EQ(moved.GenerationValue(), expected_gen);
  EXPECT_TRUE(moved.IsValid());

  // Act: move assignment
  VersionedBindlessHandle assigned;
  assigned = std::move(moved);

  // Assert: assigned object has correct values
  EXPECT_EQ(assigned.ToBindlessHandle(), expected_index);
  EXPECT_EQ(assigned.GenerationValue(), expected_gen);
  EXPECT_TRUE(assigned.IsValid());
}

//! VersionedBindlessHandle copy construction and assignment work correctly.
NOLINT_TEST(Versioned, CopySemantics_ConstructionAndAssignment)
{
  using oxygen::BindlessHeapIndex;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;

  // Arrange
  VersionedBindlessHandle original { BindlessHeapIndex { 33u },
    Generation { 4u } };

  // Act: copy construction
  VersionedBindlessHandle copied { original };

  // Assert: both objects are equal and valid
  EXPECT_EQ(copied, original);
  EXPECT_EQ(copied.ToBindlessHandle(), original.ToBindlessHandle());
  EXPECT_EQ(copied.GenerationValue(), original.GenerationValue());
  EXPECT_TRUE(copied.IsValid());
  EXPECT_TRUE(original.IsValid());

  // Act: copy assignment
  VersionedBindlessHandle assigned;
  assigned = original;

  // Assert: assigned object equals original
  EXPECT_EQ(assigned, original);
  EXPECT_EQ(assigned.ToBindlessHandle(), original.ToBindlessHandle());
  EXPECT_EQ(assigned.GenerationValue(), original.GenerationValue());
}

//! VersionedBindlessHandle default construction creates invalid handle.
NOLINT_TEST(Versioned, DefaultConstruction_CreatesInvalidHandle)
{
  // Arrange & Act
  oxygen::VersionedBindlessHandle default_handle {};

  // Assert
  EXPECT_FALSE(default_handle.IsValid());
  EXPECT_EQ(
    default_handle.ToBindlessHandle().get(), oxygen::kInvalidBindlessIndex);
  EXPECT_EQ(default_handle.GenerationValue().get(), 0u);
}

//! Constexpr operations work at compile time for basic operations.
NOLINT_TEST(Versioned, Constexpr_CompileTimeOperations)
{
  using oxygen::BindlessHeapIndex;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;

  // Compile-time construction and operations
  constexpr VersionedBindlessHandle h { BindlessHeapIndex { 15u },
    Generation { 2u } };
  constexpr auto packed = h.ToPacked();
  constexpr auto unpacked = VersionedBindlessHandle::FromPacked(packed);
  constexpr auto is_valid = h.IsValid();
  constexpr auto index = h.ToBindlessHandle();
  constexpr auto generation = h.GenerationValue();

  // Runtime assertions to verify compile-time computations
  EXPECT_TRUE(is_valid);
  EXPECT_EQ(index.get(), 15u);
  EXPECT_EQ(generation.get(), 2u);
  EXPECT_EQ(unpacked.ToBindlessHandle().get(), 15u);
  EXPECT_EQ(unpacked.GenerationValue().get(), 2u);

  // Verify constexpr comparison
  constexpr VersionedBindlessHandle h1 { BindlessHeapIndex { 10u },
    Generation { 1u } };
  constexpr VersionedBindlessHandle h2 { BindlessHeapIndex { 10u },
    Generation { 2u } };
  constexpr bool less_than = h1 < h2;
  constexpr bool equal = h1 == h1;

  EXPECT_TRUE(less_than);
  EXPECT_TRUE(equal);
}

//! Actual generation overflow behavior with uint32_t max values.
NOLINT_TEST(Versioned, Overflow_ActualGenerationWrapAround)
{
  using oxygen::BindlessHeapIndex;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;

  // Arrange: use actual max value
  constexpr uint32_t max_gen = std::numeric_limits<uint32_t>::max();
  BindlessHeapIndex idx { 456u };
  Generation max_generation { max_gen };

  // Act: create handle with max generation and pack/unpack
  VersionedBindlessHandle max_handle { idx, max_generation };
  auto packed_max = max_handle.ToPacked();
  auto unpacked_max = VersionedBindlessHandle::FromPacked(packed_max);

  // Assert: max value round-trips correctly
  EXPECT_EQ(unpacked_max.ToBindlessHandle(), idx);
  EXPECT_EQ(unpacked_max.GenerationValue().get(), max_gen);
  EXPECT_TRUE(unpacked_max.IsValid());

  // Act: simulate wrap-around by manual increment (in real usage,
  // this would be handled by the allocator)
  Generation wrapped_gen { 0u }; // Wrapped around from max
  VersionedBindlessHandle wrapped_handle { idx, wrapped_gen };

  // Assert: different packed values and proper generation values
  auto packed_wrapped = wrapped_handle.ToPacked();
  EXPECT_NE(packed_max.get(), packed_wrapped.get());
  EXPECT_EQ(wrapped_handle.GenerationValue().get(), 0u);
}

//! BindlessHeapIndex hashing works correctly with std::hash via Hashable skill.
NOLINT_TEST(Handle, Hashing_StdHashBehavior)
{
  using oxygen::BindlessHeapIndex;

  // Arrange
  BindlessHeapIndex h1 { 123u };
  BindlessHeapIndex h2 { 123u };
  BindlessHeapIndex h3 { 456u };

  // Act
  std::hash<BindlessHeapIndex> hasher;
  auto hash1 = hasher(h1);
  auto hash2 = hasher(h2);
  auto hash3 = hasher(h3);

  // Assert
  EXPECT_EQ(hash1, hash2); // Equal handles produce equal hashes
  EXPECT_NE(hash1, hash3); // Different handles produce different hashes
}

//! BindlessHeapIndex comparison works correctly via Comparable skill.
NOLINT_TEST(Handle, Comparison_ComparableSkillBehavior)
{
  using oxygen::BindlessHeapIndex;

  // Arrange
  BindlessHeapIndex small { 10u };
  BindlessHeapIndex large { 20u };
  BindlessHeapIndex equal { 10u };

  // Assert: all comparison operators work
  EXPECT_LT(small, large);
  EXPECT_LE(small, large);
  EXPECT_LE(small, equal);
  EXPECT_GT(large, small);
  EXPECT_GE(large, small);
  EXPECT_GE(equal, small);
  EXPECT_EQ(small, equal);
  EXPECT_NE(small, large);
}

//! Cross-type safety: different handle types cannot be mixed.
NOLINT_TEST(HandleTypes, TypeSafety_NoImplicitConversions)
{
  using oxygen::BindlessHeapCapacity;
  using oxygen::BindlessHeapIndex;
  using oxygen::BindlessItemCount;

  // Compile-time assertions to verify type safety
  static_assert(!std::is_convertible_v<BindlessHeapIndex, BindlessItemCount>);
  static_assert(
    !std::is_convertible_v<BindlessHeapIndex, BindlessHeapCapacity>);
  static_assert(!std::is_convertible_v<BindlessItemCount, BindlessHeapIndex>);
  static_assert(
    !std::is_convertible_v<BindlessHeapCapacity, BindlessHeapIndex>);
  static_assert(
    !std::is_convertible_v<BindlessItemCount, BindlessHeapCapacity>);
  static_assert(
    !std::is_convertible_v<BindlessHeapCapacity, BindlessItemCount>);

  // Verify no implicit conversion to underlying type
  static_assert(!std::is_convertible_v<BindlessHeapIndex, uint32_t>);
  static_assert(!std::is_convertible_v<uint32_t, BindlessHeapIndex>);

  // Runtime verification that types work independently
  BindlessHeapIndex handle { 42u };
  BindlessItemCount count { 42u };
  BindlessHeapCapacity capacity { 42u };

  // They can have same underlying value but are different types
  EXPECT_EQ(handle.get(), count.get());
  EXPECT_EQ(handle.get(), capacity.get());
  // Note: We cannot compare handle == count directly due to strong typing
}

//=== Logging Integration Tests ===-------------------------------------------//

#if LOGURU_USE_FMTLIB

//! Test fixture for bindless handle logging integration tests.
class LoggingTests : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    saved_verbosity_ = loguru::g_stderr_verbosity;
    // Set to INFO level so LOG_F calls produce output
    loguru::g_stderr_verbosity = loguru::Verbosity_INFO;
  }

  auto TearDown() -> void override
  {
    loguru::g_stderr_verbosity = saved_verbosity_;
  }

  // Helper to capture stderr while running a callable and return the output.
  template <typename F> auto CaptureStderr(F&& f) -> std::string
  {
    testing::internal::CaptureStderr();
    std::forward<F>(f)();
    return testing::internal::GetCapturedStderr();
  }

private:
  loguru::Verbosity saved_verbosity_ { loguru::Verbosity_OFF };
};

//! Verify that BindlessHeapIndex can be logged using LOG_F macro.
NOLINT_TEST_F(LoggingTests, BindlessHeapIndex_LoggingIntegration_ProducesOutput)
{
  using oxygen::BindlessHeapIndex;
  using testing::HasSubstr;

  // Arrange
  BindlessHeapIndex handle { 123u };

  // Act
  auto output = CaptureStderr([&] { LOG_F(INFO, "Handle: {}", handle); });

  // Assert
  EXPECT_THAT(output, HasSubstr("Handle:"));
  EXPECT_THAT(output, HasSubstr("123"));
}

//! Verify that BindlessItemCount can be logged using LOG_F macro.
NOLINT_TEST_F(LoggingTests, BindlessItemCount_LoggingIntegration_ProducesOutput)
{
  using oxygen::BindlessItemCount;
  using testing::HasSubstr;

  // Arrange
  BindlessItemCount count { 456u };

  // Act
  auto output = CaptureStderr([&] { LOG_F(INFO, "Count: {}", count); });

  // Assert
  EXPECT_THAT(output, HasSubstr("Count:"));
  EXPECT_THAT(output, HasSubstr("456"));
}

//! Verify that BindlessHeapCapacity can be logged using LOG_F macro.
NOLINT_TEST_F(
  LoggingTests, BindlessHeapCapacity_LoggingIntegration_ProducesOutput)
{
  using oxygen::BindlessHeapCapacity;
  using testing::HasSubstr;

  // Arrange
  BindlessHeapCapacity capacity { 789u };

  // Act
  auto output = CaptureStderr([&] { LOG_F(INFO, "Capacity: {}", capacity); });

  // Assert
  EXPECT_THAT(output, HasSubstr("Capacity:"));
  EXPECT_THAT(output, HasSubstr("789"));
}

//! Verify that VersionedBindlessHandle can be logged using LOG_F macro.
NOLINT_TEST_F(LoggingTests,
  VersionedBindlessHandle_LoggingIntegration_ProducesFormattedOutput)
{
  using oxygen::BindlessHeapIndex;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;
  using testing::HasSubstr;

  // Arrange
  VersionedBindlessHandle versioned { BindlessHeapIndex { 42u },
    Generation { 7u } };

  // Act
  auto output = CaptureStderr([&] { LOG_F(INFO, "Versioned: {}", versioned); });

  // Assert
  EXPECT_THAT(output, HasSubstr("Versioned:"));
  EXPECT_THAT(output, HasSubstr("VersionedBindlessHandle(i:42, g:7)"));
}

//! Verify that Generation type can be logged using LOG_F macro.
NOLINT_TEST_F(LoggingTests, Generation_LoggingIntegration_ProducesOutput)
{
  using Generation = oxygen::VersionedBindlessHandle::Generation;
  using testing::HasSubstr;

  // Arrange
  Generation generation { 13u };

  // Act
  auto output
    = CaptureStderr([&] { LOG_F(INFO, "Generation: {}", generation); });

  // Assert
  EXPECT_THAT(output, HasSubstr("Generation:"));
  EXPECT_THAT(output, HasSubstr("13"));
}

//! Verify that invalid BindlessHeapIndex can be logged and shows sentinel
//! value.
NOLINT_TEST_F(
  LoggingTests, InvalidBindlessHeapIndex_LoggingIntegration_ShowsSentinel)
{
  using testing::HasSubstr;

  // Arrange
  auto invalid = oxygen::kInvalidBindlessHeapIndex;

  // Act
  auto output = CaptureStderr([&] { LOG_F(INFO, "Invalid: {}", invalid); });

  // Assert
  EXPECT_THAT(output, HasSubstr("Invalid:"));
  EXPECT_THAT(output, HasSubstr(std::to_string(oxygen::kInvalidBindlessIndex)));
}

//! Verify that invalid VersionedBindlessHandle can be logged.
NOLINT_TEST_F(LoggingTests,
  InvalidVersionedBindlessHandle_LoggingIntegration_ShowsInvalidFormat)
{
  using oxygen::VersionedBindlessHandle;
  using testing::HasSubstr;

  // Arrange
  VersionedBindlessHandle invalid {}; // Default construction creates invalid

  // Act
  auto output
    = CaptureStderr([&] { LOG_F(INFO, "Invalid versioned: {}", invalid); });

  // Assert
  EXPECT_THAT(output, HasSubstr("Invalid versioned:"));
  EXPECT_THAT(output, HasSubstr("VersionedBindlessHandle(i:"));
  EXPECT_THAT(output, HasSubstr(std::to_string(oxygen::kInvalidBindlessIndex)));
  EXPECT_THAT(output, HasSubstr("g:0)"));
}

//! Verify that multiple bindless types can be logged together.
NOLINT_TEST_F(
  LoggingTests, MultipleTypes_LoggingIntegration_FormatsAllCorrectly)
{
  using oxygen::BindlessHeapCapacity;
  using oxygen::BindlessHeapIndex;
  using oxygen::BindlessItemCount;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;
  using testing::AllOf;
  using testing::HasSubstr;

  // Arrange
  BindlessHeapIndex handle { 10u };
  BindlessItemCount count { 20u };
  BindlessHeapCapacity capacity { 100u };
  VersionedBindlessHandle versioned { BindlessHeapIndex { 5u },
    Generation { 2u } };

  // Act
  auto output = CaptureStderr([&] {
    LOG_F(INFO, "Handle: {}, Count: {}, Capacity: {}, Versioned: {}", handle,
      count, capacity, versioned);
  });

  // Assert
  EXPECT_THAT(output,
    AllOf(HasSubstr("Handle:"), HasSubstr("10"), HasSubstr("Count:"),
      HasSubstr("20"), HasSubstr("Capacity:"), HasSubstr("100"),
      HasSubstr("Versioned:"), HasSubstr("VersionedBindlessHandle(i:5, g:2)")));
}

//! Verify that edge case values (zero, max) can be logged correctly.
NOLINT_TEST_F(LoggingTests, EdgeCaseValues_LoggingIntegration_HandlesExtremes)
{
  using oxygen::BindlessHeapIndex;
  using oxygen::VersionedBindlessHandle;
  using Generation = VersionedBindlessHandle::Generation;
  using testing::AllOf;
  using testing::HasSubstr;

  // Arrange
  BindlessHeapIndex zero { 0u };
  BindlessHeapIndex max_handle { std::numeric_limits<uint32_t>::max() };
  VersionedBindlessHandle max_versioned {
    BindlessHeapIndex { std::numeric_limits<uint32_t>::max() },
    Generation { std::numeric_limits<uint32_t>::max() }
  };

  // Act
  auto output = CaptureStderr([&] {
    LOG_F(INFO, "Zero: {}, Max: {}, MaxVersioned: {}", zero, max_handle,
      max_versioned);
  });

  // Assert
  const auto max_str = std::to_string(std::numeric_limits<uint32_t>::max());
  EXPECT_THAT(output,
    AllOf(HasSubstr("Zero:"), HasSubstr("0"), HasSubstr("Max:"),
      HasSubstr(max_str), HasSubstr("MaxVersioned:"),
      HasSubstr(
        "VersionedBindlessHandle(i:" + max_str + ", g:" + max_str + ")")));
}

//! Verify that bindless namespace aliases work with logging.
NOLINT_TEST_F(LoggingTests, NamespaceAliases_LoggingIntegration_WorkCorrectly)
{
  using testing::AllOf;
  using testing::HasSubstr;

  // Arrange
  oxygen::bindless::HeapIndex handle { 42u };
  oxygen::bindless::Count count { 15u };
  oxygen::bindless::Capacity capacity { 200u };

  // Act
  auto output = CaptureStderr([&] {
    LOG_F(INFO, "Alias Handle: {}, Count: {}, Capacity: {}", handle, count,
      capacity);
  });

  // Assert
  EXPECT_THAT(output,
    AllOf(HasSubstr("Alias Handle:"), HasSubstr("42"), HasSubstr("Count:"),
      HasSubstr("15"), HasSubstr("Capacity:"), HasSubstr("200")));
}

#else // LOGURU_USE_FMTLIB

//! Fallback test when fmt is disabled - ensures logging tests compile.
NOLINT_TEST(LoggingFallback, FmtDisabled_BindlessLoggingTestsStillCompile)
{
  // This test ensures the logging test section compiles even when
  // LOGURU_USE_FMTLIB is disabled
  NOLINT_SUCCEED();
}

#endif // LOGURU_USE_FMTLIB

} // namespace
