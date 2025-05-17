//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <type_traits>
#include <unordered_map>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/Detail/DescriptorHeapSegment.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

using oxygen::graphics::DescriptorHandle;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::detail::DescriptorHeapSegment;
using oxygen::graphics::detail::StaticDescriptorHeapSegment;

namespace {

template <ResourceViewType ViewType>
class TestDescriptorHeapSegment : public StaticDescriptorHeapSegment<ViewType> {
    using Base = StaticDescriptorHeapSegment<ViewType>;

public:
    TestDescriptorHeapSegment(
        const DescriptorVisibility visibility,
        const uint32_t base_index) noexcept
        : Base(visibility, base_index)
    {
    }

    ~TestDescriptorHeapSegment() override
    {
        Base::ReleaseAll();
    }

    OXYGEN_MAKE_NON_COPYABLE(TestDescriptorHeapSegment)
    OXYGEN_DEFAULT_MOVABLE(TestDescriptorHeapSegment)
};

//! Helper assertions for DescriptorHeapSegment tests.
/*!
 Provides concise checks for segment state.
*/
void ExpectEmpty(const auto& segment)
{
    EXPECT_EQ(segment.GetAvailableCount(), segment.GetCapacity());
}
void ExpectFull(auto& segment)
{
    EXPECT_EQ(segment.GetSize(), segment.GetCapacity());
    EXPECT_EQ(segment.GetAvailableCount(), 0UL);
    EXPECT_EQ(segment.Allocate(), DescriptorHandle::kInvalidIndex);
}
void ExpectSize(const auto& segment, uint32_t used)
{
    EXPECT_EQ(segment.GetSize(), used);
    EXPECT_EQ(segment.GetAvailableCount(), segment.GetCapacity() - used);
}

//! List of all valid ResourceViewTypes for testing.
using AllResourceViewTypes = ::testing::Types<
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

//! Compile-time check: all valid types are covered.
template <typename... Ts>
struct TypeCount;
template <typename... Ts>
struct TypeCount<::testing::Types<Ts...>> {
    static constexpr std::size_t value = sizeof...(Ts);
};
static_assert(
    TypeCount<AllResourceViewTypes>::value == static_cast<std::size_t>(ResourceViewType::kMaxResourceViewType) - 1,
    "Update AllResourceViewTypes if you add/remove ResourceViewType enums.");

//! Custom name generator for typed tests.
struct ResourceViewTypeNameGenerator {
    template <typename T>
    static auto GetName(int) -> std::string
    {
        auto value = static_cast<ResourceViewType>(T::value);
        const char* raw = nostd::to_string(value);
        std::string name = raw ? raw : "__NotSupported__";
        if (name.empty() || name == "__NotSupported__") {
            name = "NotSupported_" + std::to_string(static_cast<int>(T::value));
        }
        for (char& c : name) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
                c = '_';
        }
        return name;
    }
};

//! Test fixture for TestDescriptorHeapSegment.
template <typename T>
class TestDescriptorHeapSegmentTest : public ::testing::Test { };
TYPED_TEST_SUITE(
    TestDescriptorHeapSegmentTest,
    AllResourceViewTypes,
    ResourceViewTypeNameGenerator);

//===----------------------------------------------------------------------===//
// Construction & Properties
//===----------------------------------------------------------------------===//

//! Construction and initial state.
TYPED_TEST(TestDescriptorHeapSegmentTest, ConstructionAndProperties)
{
    constexpr ResourceViewType type = TypeParam::value;
    {
        TestDescriptorHeapSegment<type> seg(DescriptorVisibility::kCpuOnly, 0);
        EXPECT_EQ(seg.GetViewType(), type);
        EXPECT_EQ(seg.GetVisibility(), DescriptorVisibility::kCpuOnly);
        EXPECT_EQ(seg.GetBaseIndex(), 0U);
        ExpectEmpty(seg);
    }
    {
        constexpr uint32_t base = 42;
        TestDescriptorHeapSegment<type> seg(DescriptorVisibility::kShaderVisible, base);
        EXPECT_EQ(seg.GetViewType(), type);
        EXPECT_EQ(seg.GetVisibility(), DescriptorVisibility::kShaderVisible);
        EXPECT_EQ(seg.GetBaseIndex(), base);
        ExpectEmpty(seg);
    }
}

//! Construction and initial state.
TYPED_TEST(TestDescriptorHeapSegmentTest, DestructionWhenNotEmpty)
{
    // Setup log capture for destruction warnings.
    // Save the current verbosity so we can restore it after the test
    const auto old_verbosity = loguru::g_stderr_verbosity;
    loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;

    // Ensure color output is off for easier matching (optional)
    const bool old_color = loguru::g_colorlogtostderr;
    loguru::g_colorlogtostderr = false;

    // Start capturing stderr
    testing::internal::CaptureStderr();

    {
        constexpr ResourceViewType type = TypeParam::value;
        StaticDescriptorHeapSegment<type> seg(DescriptorVisibility::kShaderVisible, 0);
        if constexpr (seg.GetCapacity() == 0) {
            return;
        }

        // Allocate something to ensure the segment is not empty.
        [[maybe_unused]] auto _ = seg.Allocate();
        ExpectSize(seg, 1U);
    } // Destructor is called here

    // Stop capturing and get the output
    std::string output = testing::internal::GetCapturedStderr();

    // Restore previous log settings
    loguru::g_stderr_verbosity = old_verbosity;
    loguru::g_colorlogtostderr = old_color;

    // Check that the warning message appears in the output
    EXPECT_NE(output.find("segment with allocated descriptors"), std::string::npos);
}

//! Capacity matches expected per type.
TYPED_TEST(TestDescriptorHeapSegmentTest, CapacityMatchesContract)
{
    constexpr ResourceViewType type = TypeParam::value;
    TestDescriptorHeapSegment<type> seg(DescriptorVisibility::kShaderVisible, 0);
    const std::unordered_map<ResourceViewType, uint32_t> expected = {
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
    EXPECT_TRUE(expected.contains(type));
    EXPECT_EQ(seg.GetCapacity(), expected.at(type));
}

//===----------------------------------------------------------------------===//
// Allocation
//===----------------------------------------------------------------------===//

//! Sequential allocation returns correct indices and updates state.
TYPED_TEST(TestDescriptorHeapSegmentTest, SequentialAllocation)
{
    constexpr ResourceViewType type = TypeParam::value;
    constexpr uint32_t base = 10;
    TestDescriptorHeapSegment<type> seg(DescriptorVisibility::kShaderVisible, base);
    const uint32_t n = std::min(4U, seg.GetCapacity());
    for (uint32_t i = 0; i < n; ++i) {
        auto idx = seg.Allocate();
        EXPECT_NE(idx, DescriptorHandle::kInvalidIndex);
        EXPECT_EQ(idx, base + i);
    }
    ExpectSize(seg, n);
}

//! Allocate until full, then fail.
TYPED_TEST(TestDescriptorHeapSegmentTest, AllocateUntilFull)
{
    constexpr ResourceViewType type = TypeParam::value;
    TestDescriptorHeapSegment<type> seg(DescriptorVisibility::kShaderVisible, 0);
    constexpr auto cap = seg.GetCapacity();
    if constexpr (cap == 0) {
        EXPECT_EQ(seg.Allocate(), DescriptorHandle::kInvalidIndex);
        ExpectSizeEqualsCapacity(seg);
        EXPECT_EQ(seg.GetAvailableCount(), 0U);
        return;
    }
    for (uint32_t i = 0; i < cap; ++i) {
        auto idx = seg.Allocate();
        EXPECT_NE(idx, DescriptorHandle::kInvalidIndex);
        EXPECT_EQ(idx, i);
    }
    ExpectFull(seg);
}

//===----------------------------------------------------------------------===//
// Release & Recycling
//===----------------------------------------------------------------------===//

//! Release and immediate recycle of a single descriptor.
TYPED_TEST(TestDescriptorHeapSegmentTest, ReleaseAndRecycleSingle)
{
    constexpr ResourceViewType type = TypeParam::value;
    TestDescriptorHeapSegment<type> seg(DescriptorVisibility::kShaderVisible, 0);
    constexpr auto cap = seg.GetCapacity();
    if constexpr (cap == 0) {
        EXPECT_EQ(seg.Allocate(), DescriptorHandle::kInvalidIndex);
        return;
    }
    auto idx = seg.Allocate();
    EXPECT_NE(idx, DescriptorHandle::kInvalidIndex);
    EXPECT_TRUE(seg.Release(idx));
    ExpectSize(seg, 0U);
    auto recycled = seg.Allocate();
    EXPECT_EQ(recycled, idx);
    ExpectSize(seg, 1U);
}

//! Release multiple descriptors, verify counts, no recycle.
TYPED_TEST(TestDescriptorHeapSegmentTest, ReleaseMultipleNoRecycle)
{
    constexpr ResourceViewType type = TypeParam::value;
    TestDescriptorHeapSegment<type> seg(DescriptorVisibility::kShaderVisible, 0);
    constexpr auto cap = seg.GetCapacity();
    if constexpr (cap < 3) {
        return;
    }
    auto idx0 = seg.Allocate();
    [[maybe_unused]] auto idx1 = seg.Allocate();
    auto idx2 = seg.Allocate();
    EXPECT_TRUE(seg.Release(idx0));
    EXPECT_TRUE(seg.Release(idx2));
    EXPECT_FALSE(seg.IsEmpty());
    ExpectSize(seg, 1U);
}

//===----------------------------------------------------------------------===//
// Release Error/Boundary Conditions
//===----------------------------------------------------------------------===//

//! Release already released index fails.
TYPED_TEST(TestDescriptorHeapSegmentTest, ReleaseAlreadyReleasedFails)
{
    constexpr ResourceViewType type = TypeParam::value;
    TestDescriptorHeapSegment<type> seg(DescriptorVisibility::kShaderVisible, 0);
    constexpr auto cap = seg.GetCapacity();
    if constexpr (cap == 0) {
        EXPECT_FALSE(seg.Release(0U));
        return;
    }
    auto idx = seg.Allocate();
    EXPECT_TRUE(seg.Release(idx));
    EXPECT_FALSE(seg.Release(idx));

    ExpectEmpty(seg);
}

//! Release unallocated index fails.
TYPED_TEST(TestDescriptorHeapSegmentTest, ReleaseUnallocatedIndexFails)
{
    constexpr ResourceViewType type = TypeParam::value;
    constexpr uint32_t base = 10;
    TestDescriptorHeapSegment<type> seg(DescriptorVisibility::kShaderVisible, base);
    constexpr auto cap = seg.GetCapacity();
    if constexpr (cap < 6)
        return;
    [[maybe_unused]] auto _1 = seg.Allocate();
    [[maybe_unused]] auto _2 = seg.Allocate();
    uint32_t unallocated = base + 5;
    EXPECT_FALSE(seg.Release(unallocated));
    uint32_t next = base + seg.GetSize();
    if (next < base + cap) {
        EXPECT_FALSE(seg.Release(next));
    }
}

//! Release out-of-bounds indices fails.
TYPED_TEST(TestDescriptorHeapSegmentTest, ReleaseOutOfBoundsFails)
{
    constexpr ResourceViewType type = TypeParam::value;
    constexpr uint32_t base = 20;
    TestDescriptorHeapSegment<type> seg(DescriptorVisibility::kShaderVisible, base);
    constexpr auto cap = seg.GetCapacity();
    if constexpr (cap == 0 && base == 0) {
        EXPECT_FALSE(seg.Release(base + cap));
        EXPECT_FALSE(seg.Release(DescriptorHandle::kInvalidIndex));
        return;
    }
    if constexpr (cap == 0) {
        if constexpr (base > 0)
            EXPECT_FALSE(seg.Release(base - 1));
        EXPECT_FALSE(seg.Release(base + cap));
        EXPECT_FALSE(seg.Release(base + cap + 1));
        EXPECT_FALSE(seg.Release(DescriptorHandle::kInvalidIndex));
        return;
    }
    [[maybe_unused]] auto _ = seg.Allocate();
    if constexpr (base > 0) {
        EXPECT_FALSE(seg.Release(base - 1));
    }
    EXPECT_FALSE(seg.Release(base + cap));
    EXPECT_FALSE(seg.Release(base + cap + 1));
    EXPECT_FALSE(seg.Release(DescriptorHandle::kInvalidIndex));
}

TYPED_TEST(TestDescriptorHeapSegmentTest, ReleaseAfterReallocation)
{
    constexpr ResourceViewType type = TypeParam::value;
    TestDescriptorHeapSegment<type> seg(DescriptorVisibility::kShaderVisible, 0);
    constexpr auto cap = seg.GetCapacity();
    if constexpr (cap == 0) {
        // No allocation possible, nothing to test
        return;
    }

    // Allocate one descriptor
    auto idx = seg.Allocate();
    EXPECT_NE(idx, DescriptorHandle::kInvalidIndex);

    // Release it
    EXPECT_TRUE(seg.Release(idx));
    ExpectSize(seg, 0U);

    // Re-allocate (should get the same index back due to LIFO)
    auto idx2 = seg.Allocate();
    EXPECT_EQ(idx2, idx);
    ExpectSize(seg, 1U);

    // Release again (should succeed)
    EXPECT_TRUE(seg.Release(idx2));
    ExpectSize(seg, 0U);

    // Double-release (should fail)
    EXPECT_FALSE(seg.Release(idx2));
    ExpectSize(seg, 0U);
}

//===----------------------------------------------------------------------===//
// LIFO Recycling
//===----------------------------------------------------------------------===//

//! LIFO recycling behavior.
TYPED_TEST(TestDescriptorHeapSegmentTest, LIFORecycling)
{
    constexpr ResourceViewType type = TypeParam::value;
    constexpr uint32_t base = 100;
    TestDescriptorHeapSegment<type> seg(DescriptorVisibility::kShaderVisible, base);
    constexpr auto cap = seg.GetCapacity();
    if constexpr (cap < 5) {
        return;
    }

    // Allocate a, b, c, d, e in order
    [[maybe_unused]] auto a = seg.Allocate(); // base+0
    auto b = seg.Allocate(); // base+1
    auto c = seg.Allocate(); // base+2
    auto d = seg.Allocate(); // base+3
    [[maybe_unused]] auto e = seg.Allocate(); // base+4
    ExpectSize(seg, 5U);

    // Release b, d, c in that order
    EXPECT_TRUE(seg.Release(b)); // base+1
    EXPECT_TRUE(seg.Release(d)); // base+3
    EXPECT_TRUE(seg.Release(c)); // base+2
    ExpectSize(seg, 2U);

    // LIFO: should get c, d, b (base+2, base+3, base+1)
    auto f = seg.Allocate();
    EXPECT_EQ(f, base + 2);
    auto g = seg.Allocate();
    EXPECT_EQ(g, base + 3);
    auto h = seg.Allocate();
    EXPECT_EQ(h, base + 1);

    ExpectSize(seg, 5U);
}

//! Full cycle LIFO verification.
TYPED_TEST(TestDescriptorHeapSegmentTest, FullCycleLIFOVerification)
{
    constexpr ResourceViewType type = TypeParam::value;
    constexpr uint32_t base = 0;
    TestDescriptorHeapSegment<type> seg(DescriptorVisibility::kShaderVisible, base);
    constexpr auto cap = seg.GetCapacity();
    if constexpr (cap == 0) {
        return;
    }

    // Allocate until full
    std::vector<uint32_t> allocated;
    allocated.reserve(cap);
    for (uint32_t i = 0; i < cap; ++i) {
        uint32_t idx = seg.Allocate();
        allocated.push_back(idx);
    }
    ExpectFull(seg);

    // Release all in reverse order
    std::vector<uint32_t> released = allocated;
    std::ranges::reverse(released);
    for (auto idx : released) {
        EXPECT_TRUE(seg.Release(idx));
    }
    ExpectEmpty(seg);

    std::vector<uint32_t> reallocated;
    reallocated.reserve(cap);
    for (uint32_t i = 0; i < cap; ++i) {
        reallocated.push_back(seg.Allocate());
    }
    for (uint32_t i = 0; i < cap; ++i) {
        EXPECT_EQ(reallocated[i], allocated[i]);
    }
    ExpectFull(seg);
}

//===----------------------------------------------------------------------===//
// Move Semantics
//===----------------------------------------------------------------------===//

//! Move construction and assignment.
TYPED_TEST(TestDescriptorHeapSegmentTest, MoveSemantics)
{
    constexpr ResourceViewType type = TypeParam::value;
    constexpr uint32_t base = 77;
    constexpr auto vis = DescriptorVisibility::kShaderVisible;
    TestDescriptorHeapSegment<type> orig(vis, base);
    constexpr auto cap = orig.GetCapacity();

    // Handle the edge case where the segment has zero capacity
    if constexpr (cap == 0) {
        TestDescriptorHeapSegment<type> moved(std::move(orig));
        EXPECT_EQ(moved.GetCapacity(), 0);
        TestDescriptorHeapSegment<type> assign(vis, base + 1);
        assign = std::move(moved);
        EXPECT_EQ(assign.GetCapacity(), 0);
        return;
    }

    // Allocate about half the capacity in the original segment
    std::vector<uint32_t> allocations;
    for (uint32_t i = 0; i < cap / 2 + (cap % 2); ++i) {
        allocations.push_back(orig.Allocate());
    }
    // Optionally release the first allocation if more than one was made
    if (allocations.size() > 1) {
        EXPECT_TRUE(orig.Release(allocations[0]));
    }

    // Record the state of the original segment before moving
    uint32_t orig_size = orig.GetSize();
    uint32_t orig_avail = orig.GetAvailableCount();
    uint32_t orig_next = orig.Allocate();
    if (orig_next != DescriptorHandle::kInvalidIndex) {
        EXPECT_TRUE(orig.Release(orig_next));
    }

    // Move-construct a new segment from the original
    TestDescriptorHeapSegment<type> moved(std::move(orig));

    // Check that all properties and state are preserved after move construction
    EXPECT_EQ(moved.GetViewType(), type);
    EXPECT_EQ(moved.GetVisibility(), vis);
    EXPECT_EQ(moved.GetBaseIndex(), base);
    EXPECT_EQ(moved.GetCapacity(), cap);
    EXPECT_EQ(moved.GetSize(), orig_size);
    EXPECT_EQ(moved.GetAvailableCount(), orig_avail);

    // Allocate from the moved segment and verify the next index matches
    uint32_t moved_next = moved.Allocate();
    EXPECT_EQ(moved_next, orig_next);
    if (moved_next != DescriptorHandle::kInvalidIndex) {
        EXPECT_TRUE(moved.Release(moved_next));
    }

    // Create another segment and allocate from it to set up for move assignment
    TestDescriptorHeapSegment<type> another(vis, base + 100);
    if constexpr (cap > 0) {
        [[maybe_unused]] auto _ = another.Allocate();
    }
    uint32_t another_size = another.GetSize();
    uint32_t another_avail = another.GetAvailableCount();
    uint32_t another_next = another.Allocate();
    if (another_next != DescriptorHandle::kInvalidIndex) {
        EXPECT_TRUE(another.Release(another_next));
    }

    // Move-assign 'another' into 'moved' and verify all properties and state
    moved = std::move(another);

    EXPECT_EQ(moved.GetViewType(), type);
    EXPECT_EQ(moved.GetVisibility(), vis);
    EXPECT_EQ(moved.GetBaseIndex(), base + 100);
    EXPECT_EQ(moved.GetCapacity(), cap);
    EXPECT_EQ(moved.GetSize(), another_size);
    EXPECT_EQ(moved.GetAvailableCount(), another_avail);

    // Allocate from the newly assigned segment and verify the next index
    uint32_t assigned_next = moved.Allocate();
    EXPECT_EQ(assigned_next, another_next);
    if (assigned_next != DescriptorHandle::kInvalidIndex) {
        EXPECT_TRUE(moved.Release(assigned_next));
    }
}

//===----------------------------------------------------------------------===//
// Polymorphic Interface
//===----------------------------------------------------------------------===//

//! Polymorphic interface usage.
TYPED_TEST(TestDescriptorHeapSegmentTest, PolymorphicInterfaceUsage)
{
    constexpr ResourceViewType type = TypeParam::value;
    const std::unique_ptr<DescriptorHeapSegment> seg
        = std::make_unique<TestDescriptorHeapSegment<type>>(
            DescriptorVisibility::kShaderVisible, 100);

    const auto cap = seg->GetCapacity();
    if (cap == 0) {
        return;
    }
    ExpectSize(*seg, 0U);

    const uint32_t n = std::min(4U, cap);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t idx = seg->Allocate();
        EXPECT_NE(idx, DescriptorHandle::kInvalidIndex);
        EXPECT_EQ(idx, seg->GetBaseIndex() + i);
        EXPECT_EQ(seg->GetSize(), i + 1);
    }
    for (uint32_t i = 0; i < n; ++i) {
        EXPECT_TRUE(seg->Release(seg->GetBaseIndex() + i));
    }
    ExpectSize(*seg, 0U);
    ExpectEmpty(*seg);
}

} // namespace
