//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <type_traits>
#include <unordered_map>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/Detail/DescriptorHeapSegment.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::detail::DescriptorHeapSegment;
using oxygen::graphics::detail::StaticDescriptorHeapSegment;

namespace {

// Helper assertion functions to avoid magic numbers and reduce verbosity
void ExpectAvailableEqualsCapacity(const auto& segment)
{
    EXPECT_EQ(segment.GetAvailableCount(), segment.GetCapacity());
}
void ExpectSizeEqualsCapacity(const auto& segment)
{
    EXPECT_EQ(segment.GetSize(), segment.GetCapacity());
}
void ExpectAvailable(const auto& segment, uint32_t used)
{
    EXPECT_EQ(segment.GetAvailableCount(), segment.GetCapacity() - used);
}
void ExpectSize(const auto& segment, const uint32_t used)
{
    EXPECT_EQ(segment.GetSize(), used);
}
void ExpectSizeFromCapacity(const auto& segment, uint32_t released)
{
    EXPECT_EQ(segment.GetSize(), segment.GetCapacity() - released);
}

// List of all types to test
using OptimalCapacityTypes = ::testing::Types<
    std::integral_constant<ResourceViewType, ResourceViewType::kConstantBuffer>,
    std::integral_constant<ResourceViewType, ResourceViewType::kTexture_SRV>,
    std::integral_constant<ResourceViewType, ResourceViewType::kTypedBuffer_SRV>,
    std::integral_constant<ResourceViewType, ResourceViewType::kStructuredBuffer_SRV>,
    std::integral_constant<ResourceViewType, ResourceViewType::kRawBuffer_SRV>,
    std::integral_constant<ResourceViewType, ResourceViewType::kTexture_UAV>,
    std::integral_constant<ResourceViewType, ResourceViewType::kTypedBuffer_UAV>,
    std::integral_constant<ResourceViewType, ResourceViewType::kStructuredBuffer_UAV>,
    std::integral_constant<ResourceViewType, ResourceViewType::kRawBuffer_UAV>,
    std::integral_constant<ResourceViewType, ResourceViewType::kSamplerFeedbackTexture_UAV>,
    std::integral_constant<ResourceViewType, ResourceViewType::kSampler>,
    std::integral_constant<ResourceViewType, ResourceViewType::kTexture_RTV>,
    std::integral_constant<ResourceViewType, ResourceViewType::kTexture_DSV>,
    std::integral_constant<ResourceViewType, ResourceViewType::kRayTracingAccelStructure>>;

// Helper template to count types
template <typename... Ts>
struct TypeCount;

template <typename... Ts>
struct TypeCount<::testing::Types<Ts...>> {
    static constexpr std::size_t value = sizeof...(Ts);
};

// Ensure that we are testing all valid types (exclude kNone and >= kMax)
static_assert(
    TypeCount<OptimalCapacityTypes>::value == static_cast<std::size_t>(ResourceViewType::kMax) - 1,
    "Mismatch in number of resource view types: "
    "update OptimalCapacityTypes if you add/remove ResourceViewType enums!");

// Typed test suite for optimal capacity

template <typename T>
class DescriptorHeapSegmentTest : public ::testing::Test { };

// Custom name generator for typed tests using nostd::to_string
struct ResourceViewTypeNameGenerator {
    template <typename T>
    static auto GetName(int) -> std::string
    {
        auto value = static_cast<ResourceViewType>(T::value);
        const char* raw = nostd::to_string(value);
        std::string name = raw ? raw : "__NotSupported__";
        // If nostd::to_string returns "Unknown" or empty, append the numeric value for uniqueness
        if (name.empty() || name == "__NotSupported__") {
            name = "NotSupported_" + std::to_string(static_cast<int>(T::value));
        }
        // Ensure valid C++ identifier for Google Test
        for (char& c : name) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
                c = '_';
        }
        return name;
    }
};

TYPED_TEST_SUITE(
    DescriptorHeapSegmentTest,
    OptimalCapacityTypes,
    ResourceViewTypeNameGenerator);

// --- Construction and Capacity ---

//! Scenario: Verify segment properties immediately after construction with various parameters.
NOLINT_TYPED_TEST(DescriptorHeapSegmentTest, TestConstructionAndInitialState)
{
    constexpr ResourceViewType type = TypeParam::value;

    // Test case 1: Zero base_index, CPU-only visibility
    {
        StaticDescriptorHeapSegment<type> segment(DescriptorVisibility::kCpuOnly, 0U);
        EXPECT_EQ(segment.GetViewType(), type);
        EXPECT_EQ(segment.GetVisibility(), DescriptorVisibility::kCpuOnly);
        EXPECT_EQ(segment.GetBaseIndex(), 0U);
        EXPECT_EQ(segment.GetSize(), 0U);
        ExpectAvailableEqualsCapacity(segment); // Checks available == capacity
    }

    // Test case 2: Non-zero base_index, ShaderVisible visibility
    {
        constexpr uint32_t base_index = 50U;
        StaticDescriptorHeapSegment<type> segment(DescriptorVisibility::kShaderVisible, base_index);
        EXPECT_EQ(segment.GetViewType(), type);
        EXPECT_EQ(segment.GetVisibility(), DescriptorVisibility::kShaderVisible);
        EXPECT_EQ(segment.GetBaseIndex(), base_index);
        EXPECT_EQ(segment.GetSize(), 0U);
        ExpectAvailableEqualsCapacity(segment);
    }
}

NOLINT_TYPED_TEST(DescriptorHeapSegmentTest, OptimalCapacity)
{
    constexpr ResourceViewType type = TypeParam::value;
    StaticDescriptorHeapSegment<type> seg(DescriptorVisibility::kShaderVisible, 0);

    const std::unordered_map<ResourceViewType, uint32_t> expected_capacities = {
        { ResourceViewType::kConstantBuffer, 64U },
        { ResourceViewType::kTexture_SRV, 256U },
        { ResourceViewType::kTypedBuffer_SRV, 64U },
        { ResourceViewType::kStructuredBuffer_SRV, 64U },
        { ResourceViewType::kRawBuffer_SRV, 64U },
        { ResourceViewType::kTexture_UAV, 64U },
        { ResourceViewType::kTypedBuffer_UAV, 64U },
        { ResourceViewType::kStructuredBuffer_UAV, 64U },
        { ResourceViewType::kRawBuffer_UAV, 64U },
        { ResourceViewType::kSamplerFeedbackTexture_UAV, 64U },
        { ResourceViewType::kSampler, 32U },
        { ResourceViewType::kTexture_RTV, 16U },
        { ResourceViewType::kTexture_DSV, 16U },
        { ResourceViewType::kRayTracingAccelStructure, 16U }
    };

    EXPECT_TRUE(expected_capacities.contains(type))
        << "Missing capacity expectation for type: " << nostd::to_string(type);

    EXPECT_EQ(seg.GetCapacity(), expected_capacities.at(type));
}

// --- Basic Allocation ---

//! Scenario: Allocate multiple descriptors and verify their indices and segment counts.
NOLINT_TYPED_TEST(DescriptorHeapSegmentTest, TestSequentialAllocation)
{
    constexpr ResourceViewType type = TypeParam::value;
    constexpr uint32_t base_index = 10U;
    StaticDescriptorHeapSegment<type> segment(DescriptorVisibility::kShaderVisible, base_index);

    constexpr auto capacity = segment.GetCapacity();
    if constexpr (capacity == 0) {
        EXPECT_EQ(segment.Allocate(), UINT32_MAX);
        ExpectSize(segment, 0U);
        ExpectAvailable(segment, 0U);
        return;
    }

    const uint32_t num_allocations = std::min(4U, capacity);
    std::vector<uint32_t> allocated_indices;
    for (uint32_t i = 0; i < num_allocations; ++i) {
        const auto idx = segment.Allocate();
        EXPECT_NE(idx, UINT32_MAX);
        EXPECT_EQ(idx, base_index + i);
        allocated_indices.push_back(idx);
    }

    ExpectSize(segment, num_allocations);
    ExpectAvailable(segment, num_allocations);
}

//! Scenario: Fill the segment completely and verify behavior.
NOLINT_TYPED_TEST(DescriptorHeapSegmentTest, TestAllocateUntilFull)
{
    constexpr ResourceViewType type = TypeParam::value;
    constexpr uint32_t base_index = 0U; // Using 0 for simplicity in index checking
    StaticDescriptorHeapSegment<type> segment(DescriptorVisibility::kShaderVisible, base_index);
    constexpr auto capacity = segment.GetCapacity();

    if constexpr (capacity == 0) {
        EXPECT_EQ(segment.Allocate(), UINT32_MAX);
        ExpectSizeEqualsCapacity(segment);
        EXPECT_EQ(segment.GetAvailableCount(), 0U);
        return;
    }

    for (uint32_t i = 0; i < capacity; ++i) {
        auto idx = segment.Allocate();
        EXPECT_NE(idx, UINT32_MAX) << "Allocation failed at index " << i << " for base_index " << base_index;
        EXPECT_EQ(idx, base_index + i);
    }

    ExpectSizeEqualsCapacity(segment);
    EXPECT_EQ(segment.GetAvailableCount(), 0U);
    EXPECT_EQ(segment.Allocate(), UINT32_MAX) << "Allocation should fail when segment is full.";
}

// --- Release and Recycle ---

//! Scenario: Release a single descriptor and immediately reallocate it.
NOLINT_TYPED_TEST(DescriptorHeapSegmentTest, TestSingleReleaseAndRecycle)
{
    constexpr ResourceViewType type = TypeParam::value;
    StaticDescriptorHeapSegment<type> segment(DescriptorVisibility::kShaderVisible, 0U);
    constexpr auto capacity = segment.GetCapacity();

    if constexpr (capacity == 0) {
        EXPECT_EQ(segment.Allocate(), UINT32_MAX);
        return;
    }

    if constexpr (capacity < 2 && capacity > 0) { // Simplified for capacity 1
        auto idx0 = segment.Allocate();
        EXPECT_NE(idx0, UINT32_MAX);
        EXPECT_TRUE(segment.Release(idx0));
        ExpectSize(segment, 0U);
        auto recycled_idx = segment.Allocate();
        EXPECT_EQ(recycled_idx, idx0);
        ExpectSize(segment, 1U);
        return;
    }

    if constexpr (capacity < 3) { // Test needs at least 2, ideally 3. Skip if less than 2.
        return;
    }

    auto idx0 = segment.Allocate();
    auto idx1 = segment.Allocate();
    auto idx2 = segment.Allocate();
    EXPECT_NE(idx0, UINT32_MAX);
    EXPECT_NE(idx1, UINT32_MAX);
    EXPECT_NE(idx2, UINT32_MAX);
    ExpectSize(segment, 3U);

    EXPECT_TRUE(segment.Release(idx1));
    ExpectSize(segment, 2U);
    ExpectAvailable(segment, 2U);

    auto recycled_idx = segment.Allocate();
    EXPECT_EQ(recycled_idx, idx1) << "Should recycle the released index.";
    ExpectSize(segment, 3U);
    ExpectAvailable(segment, 3U);
}

//! Scenario: Release multiple descriptors and verify counts without immediate recycling.
NOLINT_TYPED_TEST(DescriptorHeapSegmentTest, TestMultipleReleasesNoRecycle)
{
    constexpr ResourceViewType type = TypeParam::value;
    StaticDescriptorHeapSegment<type> segment(DescriptorVisibility::kShaderVisible, 0U);
    constexpr auto capacity = segment.GetCapacity();

    if constexpr (capacity < 3) { // Test needs at least 3 allocations to be meaningful.
        return;
    }

    auto idx0 = segment.Allocate();
    auto idx1 = segment.Allocate();
    auto idx2 = segment.Allocate();
    EXPECT_NE(idx0, UINT32_MAX);
    EXPECT_NE(idx1, UINT32_MAX);
    EXPECT_NE(idx2, UINT32_MAX);
    uint32_t current_allocated = 3;
    ExpectSize(segment, current_allocated);

    EXPECT_TRUE(segment.Release(idx0));
    current_allocated--;
    ExpectSize(segment, current_allocated);
    ExpectAvailable(segment, current_allocated);

    EXPECT_TRUE(segment.Release(idx1));
    current_allocated--;
    ExpectSize(segment, current_allocated);
    ExpectAvailable(segment, current_allocated);

    EXPECT_FALSE(segment.IsEmpty()); // idx2 should still be allocated
}

// --- Release Error Conditions ---

//! Scenario: Attempt to release an already released descriptor, expecting failure.
NOLINT_TYPED_TEST(DescriptorHeapSegmentTest, TestReleaseAlreadyReleasedFails)
{
    constexpr ResourceViewType type = TypeParam::value;
    StaticDescriptorHeapSegment<type> segment(DescriptorVisibility::kShaderVisible, 0U);
    constexpr auto capacity = segment.GetCapacity();

    if constexpr (capacity == 0) {
        EXPECT_FALSE(segment.Release(0U));
        return;
    }

    auto idx0 = segment.Allocate();
    EXPECT_NE(idx0, UINT32_MAX);
    ExpectSize(segment, 1U);

    EXPECT_TRUE(segment.Release(idx0));
    ExpectSize(segment, 0U);

    EXPECT_FALSE(segment.Release(idx0)) << "Releasing an already released index should fail.";
    ExpectSize(segment, 0U);
    ExpectAvailableEqualsCapacity(segment);
}

//! Scenario: Test releasing an index that was never allocated but is within segment capacity.
NOLINT_TYPED_TEST(DescriptorHeapSegmentTest, TestReleaseUnallocatedIndexFails)
{
    constexpr ResourceViewType type = TypeParam::value;
    constexpr uint32_t base_index = 10;
    StaticDescriptorHeapSegment<type> segment(DescriptorVisibility::kShaderVisible, base_index);
    constexpr auto capacity = segment.GetCapacity();

    if constexpr (capacity < 6) { // Test logic assumes capacity > 5 for unallocated_index check
        return;
    }

    EXPECT_NE(segment.Allocate(), UINT32_MAX); // base_index + 0
    EXPECT_NE(segment.Allocate(), UINT32_MAX); // base_index + 1
    const uint32_t current_size = segment.GetSize();
    const uint32_t current_available = segment.GetAvailableCount();

    uint32_t unallocated_index = base_index + 5;
    EXPECT_FALSE(segment.Release(unallocated_index))
        << "Releasing an unallocated index (beyond current allocations) should fail.";
    EXPECT_EQ(segment.GetSize(), current_size);
    EXPECT_EQ(segment.GetAvailableCount(), current_available);

    uint32_t next_to_be_allocated_index = base_index + segment.GetSize();
    if (next_to_be_allocated_index < base_index + capacity) {
        EXPECT_FALSE(segment.Release(next_to_be_allocated_index))
            << "Releasing the next-to-be-allocated index should fail.";
        EXPECT_EQ(segment.GetSize(), current_size);
        EXPECT_EQ(segment.GetAvailableCount(), current_available);
    }
}

//! Scenario: Test releasing indices at various out-of-bounds conditions.
NOLINT_TYPED_TEST(DescriptorHeapSegmentTest, TestReleaseOutOfBoundsIndicesFails)
{
    constexpr ResourceViewType type = TypeParam::value;
    constexpr uint32_t base_index = 20;
    StaticDescriptorHeapSegment<type> segment(DescriptorVisibility::kShaderVisible, base_index);
    constexpr uint32_t capacity = segment.GetCapacity();

    if constexpr (capacity == 0 && base_index == 0) { // Specific edge case where base_index - 1 is problematic
        EXPECT_FALSE(segment.Release(base_index + capacity));
        EXPECT_FALSE(segment.Release(UINT32_MAX));
        return;
    }
    if constexpr (capacity == 0) { // General case for zero capacity
        if constexpr (base_index > 0) { // Avoid underflow if base_index is 0
            EXPECT_FALSE(segment.Release(base_index - 1));
        }
        EXPECT_FALSE(segment.Release(base_index + capacity)); // effectively base_index
        EXPECT_FALSE(segment.Release(base_index + capacity + 1));
        EXPECT_FALSE(segment.Release(UINT32_MAX));
        return;
    }

    uint32_t allocated_idx = segment.Allocate();
    EXPECT_NE(allocated_idx, UINT32_MAX);
    uint32_t initial_size = segment.GetSize();
    uint32_t initial_available = segment.GetAvailableCount();

    if constexpr (base_index > 0) { // Avoid underflow if base_index is 0
        EXPECT_FALSE(segment.Release(base_index - 1))
            << "Should not release index below base_index.";
        EXPECT_EQ(segment.GetSize(), initial_size);
        EXPECT_EQ(segment.GetAvailableCount(), initial_available);
    }
    EXPECT_FALSE(segment.Release(base_index + capacity))
        << "Should not release index at base_index + capacity (which is out of bounds).";
    EXPECT_EQ(segment.GetSize(), initial_size);
    EXPECT_EQ(segment.GetAvailableCount(), initial_available);

    EXPECT_FALSE(segment.Release(base_index + capacity + 1))
        << "Should not release index above base_index + capacity.";
    EXPECT_EQ(segment.GetSize(), initial_size);
    EXPECT_EQ(segment.GetAvailableCount(), initial_available);

    EXPECT_FALSE(segment.Release(UINT32_MAX))
        << "Should not release UINT32_MAX.";
    EXPECT_EQ(segment.GetSize(), initial_size);
    EXPECT_EQ(segment.GetAvailableCount(), initial_available);
}

// --- LIFO / Functional Tests ---

//! Scenario: Verify LIFO behavior of the descriptor recycling.
NOLINT_TYPED_TEST(DescriptorHeapSegmentTest, LIFORecyclingBehavior)
{
    constexpr ResourceViewType type = TypeParam::value;
    constexpr uint32_t base_index = 100;
    StaticDescriptorHeapSegment<type> segment(
        DescriptorVisibility::kShaderVisible, base_index);
    constexpr auto capacity = segment.GetCapacity();

    if constexpr (capacity < 5) { // Test requires at least 5 allocations to run as written.
        return;
    }

    [[maybe_unused]] auto a = segment.Allocate(); // base_index + 0
    auto b = segment.Allocate(); // base_index + 1
    auto c = segment.Allocate(); // base_index + 2
    auto d = segment.Allocate(); // base_index + 3
    [[maybe_unused]] auto e = segment.Allocate(); // base_index + 4

    constexpr uint32_t initial_allocations = 5;
    for (uint32_t i = initial_allocations; i < capacity; ++i) {
        auto idx = segment.Allocate();
        EXPECT_NE(idx, UINT32_MAX);
    }
    EXPECT_EQ(segment.GetAvailableCount(), 0U);
    ExpectSizeEqualsCapacity(segment);

    EXPECT_TRUE(segment.Release(b));
    EXPECT_TRUE(segment.Release(d));
    EXPECT_TRUE(segment.Release(c));
    ExpectSizeFromCapacity(segment, 3U);

    auto f = segment.Allocate();
    EXPECT_EQ(f, base_index + 2);
    auto g = segment.Allocate();
    EXPECT_EQ(g, base_index + 3);
    auto h = segment.Allocate();
    EXPECT_EQ(h, base_index + 1);

    EXPECT_EQ(segment.GetAvailableCount(), 0U);
    ExpectSizeEqualsCapacity(segment);
    EXPECT_EQ(segment.Allocate(), UINT32_MAX);
}

//! Scenario: Fill segment, release all, then fill again, verifying LIFO order.
NOLINT_TYPED_TEST(DescriptorHeapSegmentTest, TestFullCycleLIFOVerification)
{
    constexpr ResourceViewType type = TypeParam::value;
    constexpr uint32_t base_index = 0;
    StaticDescriptorHeapSegment<type> segment(DescriptorVisibility::kShaderVisible, base_index);
    constexpr auto capacity = segment.GetCapacity();

    if constexpr (capacity == 0) {
        return;
    }

    std::vector<uint32_t> allocated_indices;
    allocated_indices.reserve(capacity);

    for (uint32_t i = 0; i < capacity; ++i) {
        uint32_t index = segment.Allocate();
        EXPECT_NE(index, UINT32_MAX);
        EXPECT_EQ(index, base_index + i);
        allocated_indices.push_back(index);
    }
    EXPECT_EQ(segment.Allocate(), UINT32_MAX);
    EXPECT_EQ(segment.GetAvailableCount(), 0U);
    EXPECT_EQ(segment.GetSize(), capacity);

    std::vector<uint32_t> released_order = allocated_indices;
    std::ranges::reverse(released_order);

    for (uint32_t index_to_release : released_order) {
        EXPECT_TRUE(segment.Release(index_to_release));
    }
    EXPECT_EQ(segment.GetAvailableCount(), capacity);
    EXPECT_EQ(segment.GetSize(), 0U);

    std::vector<uint32_t> reallocated_indices;
    reallocated_indices.reserve(capacity);
    for (uint32_t i = 0; i < capacity; ++i) {
        uint32_t index = segment.Allocate();
        EXPECT_NE(index, UINT32_MAX);
        reallocated_indices.push_back(index);
    }
    EXPECT_EQ(segment.Allocate(), UINT32_MAX);
    EXPECT_EQ(segment.GetAvailableCount(), 0U);
    EXPECT_EQ(segment.GetSize(), capacity);

    // Verify LIFO: reallocated should match original allocated_indices due to release order.
    for (uint32_t i = 0; i < capacity; ++i) {
        EXPECT_EQ(reallocated_indices[i], allocated_indices[i])
            << "LIFO reallocation order mismatch at index " << i;
    }
}

// --- Other ---

//! Scenario: Test move construction and move assignment for StaticDescriptorHeapSegment.
NOLINT_TYPED_TEST(DescriptorHeapSegmentTest, MoveSemantics)
{
    constexpr ResourceViewType type = TypeParam::value;
    constexpr uint32_t base_index = 77;
    constexpr DescriptorVisibility visibility = DescriptorVisibility::kShaderVisible;

    StaticDescriptorHeapSegment<type> original_segment(visibility, base_index);
    constexpr auto capacity = original_segment.GetCapacity();

    if constexpr (capacity == 0) { // Simplified checks for zero capacity
        StaticDescriptorHeapSegment<type> moved_segment_construct(std::move(original_segment));
        EXPECT_EQ(moved_segment_construct.GetCapacity(), 0);
        StaticDescriptorHeapSegment<type> moved_segment_assign(visibility, base_index + 1);
        moved_segment_assign = std::move(moved_segment_construct); // original_segment is already moved from
        EXPECT_EQ(moved_segment_assign.GetCapacity(), 0);
        return;
    }

    std::vector<uint32_t> allocated_in_original;
    if constexpr (capacity > 0) {
        for (uint32_t i = 0; i < capacity / 2 + (capacity % 2); ++i) {
            uint32_t idx = original_segment.Allocate();
            EXPECT_NE(idx, UINT32_MAX);
            allocated_in_original.push_back(idx);
        }
    }
    if (allocated_in_original.size() > 1) {
        EXPECT_TRUE(original_segment.Release(allocated_in_original[0]));
    }

    uint32_t original_size = original_segment.GetSize();
    uint32_t original_available = original_segment.GetAvailableCount();
    // Capture next_index state by allocating and releasing
    uint32_t original_next_potential_idx = original_segment.Allocate();
    if (original_next_potential_idx != UINT32_MAX) {
        EXPECT_TRUE(original_segment.Release(original_next_potential_idx));
    }

    StaticDescriptorHeapSegment<type> moved_segment_construct(std::move(original_segment));

    EXPECT_EQ(moved_segment_construct.GetViewType(), type);
    EXPECT_EQ(moved_segment_construct.GetVisibility(), visibility);
    EXPECT_EQ(moved_segment_construct.GetBaseIndex(), base_index);
    EXPECT_EQ(moved_segment_construct.GetCapacity(), capacity);
    EXPECT_EQ(moved_segment_construct.GetSize(), original_size);
    EXPECT_EQ(moved_segment_construct.GetAvailableCount(), original_available);
    uint32_t moved_next_idx = moved_segment_construct.Allocate();
    EXPECT_EQ(moved_next_idx, original_next_potential_idx);
    if (moved_next_idx != UINT32_MAX) {
        EXPECT_TRUE(moved_segment_construct.Release(moved_next_idx));
    }

    // original_segment is now in a valid but unspecified state.

    StaticDescriptorHeapSegment<type> another_original_segment(visibility, base_index + 100);
    if constexpr (capacity > 0) {
        [[maybe_unused]] auto _ = another_original_segment.Allocate();
    }
    uint32_t another_original_size = another_original_segment.GetSize();
    uint32_t another_original_available = another_original_segment.GetAvailableCount();
    uint32_t another_original_next_potential_idx = another_original_segment.Allocate();
    if (another_original_next_potential_idx != UINT32_MAX) {
        EXPECT_TRUE(another_original_segment.Release(another_original_next_potential_idx));
    }

    // Use the already moved-from segment for assignment target to ensure it handles it.
    moved_segment_construct = std::move(another_original_segment);
    EXPECT_EQ(moved_segment_construct.GetViewType(), type);
    EXPECT_EQ(moved_segment_construct.GetVisibility(), visibility);
    EXPECT_EQ(moved_segment_construct.GetBaseIndex(), base_index + 100);
    EXPECT_EQ(moved_segment_construct.GetCapacity(), capacity);
    EXPECT_EQ(moved_segment_construct.GetSize(), another_original_size);
    EXPECT_EQ(moved_segment_construct.GetAvailableCount(), another_original_available);
    uint32_t assigned_next_idx = moved_segment_construct.Allocate();
    EXPECT_EQ(assigned_next_idx, another_original_next_potential_idx);
    if (assigned_next_idx != UINT32_MAX) {
        EXPECT_TRUE(moved_segment_construct.Release(assigned_next_idx));
    }
}

NOLINT_TYPED_TEST(DescriptorHeapSegmentTest, PolymorphicInterfaceUsage)
{
    constexpr ResourceViewType type = TypeParam::value;
    const std::unique_ptr<DescriptorHeapSegment> segment
        = std::make_unique<StaticDescriptorHeapSegment<type>>(
            DescriptorVisibility::kShaderVisible, 100);

    const auto capacity = segment->GetCapacity();
    if (capacity == 0) {
        return; // Test not meaningful for zero capacity
    }

    EXPECT_GT(segment->GetCapacity(), 0U);
    ExpectSize(*segment, 0U);

    const uint32_t test_count = std::min(4U, capacity);
    EXPECT_GT(test_count, 0U);

    for (uint32_t i = 0; i < test_count; ++i) {
        uint32_t index = segment->Allocate();
        EXPECT_NE(index, UINT32_MAX);
        EXPECT_EQ(index, segment->GetBaseIndex() + i);
        EXPECT_EQ(segment->GetSize(), i + 1);
    }
    for (uint32_t i = 0; i < test_count; ++i) {
        EXPECT_TRUE(segment->Release(segment->GetBaseIndex() + i));
    }
    ExpectSize(*segment, 0U);
    ExpectAvailableEqualsCapacity(*segment);
}

} // namespace
