//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/Detail/FixedDescriptorHeapSegment.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

using oxygen::kInvalidBindlessHandle;
using oxygen::bindless::Capacity;
using oxygen::bindless::Count;
using oxygen::bindless::Handle;
using oxygen::graphics::DescriptorHandle;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::detail::FixedDescriptorHeapSegment;

namespace {

class TestDescriptorHeapSegment : public FixedDescriptorHeapSegment {
  using Base = FixedDescriptorHeapSegment;

public:
  TestDescriptorHeapSegment(Capacity capacity, Handle base_index,
    ResourceViewType view_type, DescriptorVisibility visibility)
    : Base(capacity, base_index, view_type, visibility)
  {
  }

  ~TestDescriptorHeapSegment() override { Base::ReleaseAll(); }

  OXYGEN_MAKE_NON_COPYABLE(TestDescriptorHeapSegment)
  OXYGEN_DEFAULT_MOVABLE(TestDescriptorHeapSegment)
};

//! Helper assertions for DescriptorHeapSegment tests.
/*!
 Provides concise checks for segment state.
*/
void ExpectEmpty(const FixedDescriptorHeapSegment& segment)
{
  EXPECT_EQ(segment.GetAvailableCount().get(), segment.GetCapacity().get());
}
void ExpectFull(FixedDescriptorHeapSegment& segment)
{
  EXPECT_EQ(segment.GetAllocatedCount().get(), segment.GetCapacity().get());
  EXPECT_EQ(segment.GetAvailableCount().get(), 0U);
  EXPECT_EQ(segment.Allocate(), kInvalidBindlessHandle);
}
void ExpectSize(const FixedDescriptorHeapSegment& segment, uint32_t used)
{
  EXPECT_EQ(segment.GetAllocatedCount().get(), used);
  EXPECT_EQ(
    segment.GetAvailableCount().get(), segment.GetCapacity().get() - used);
}

//===----------------------------------------------------------------------===//
// Construction & Properties
//===----------------------------------------------------------------------===//

//! Construction with base index 0 and CPU-only visibility.
NOLINT_TEST(FixedDescriptorHeapSegmentTest, ConstructionZeroBase)
{
  // Test default construction with base index 0 and CPU-only visibility.
  const TestDescriptorHeapSegment seg(Capacity { 8 }, Handle { 0 },
    ResourceViewType::kConstantBuffer, DescriptorVisibility::kCpuOnly);
  EXPECT_EQ(seg.GetViewType(), ResourceViewType::kConstantBuffer);
  EXPECT_EQ(seg.GetVisibility(), DescriptorVisibility::kCpuOnly);
  EXPECT_EQ(seg.GetBaseIndex().get(), 0U);
  ExpectEmpty(seg);
}

//! Construction with nonzero base index and shader-visible visibility.
NOLINT_TEST(FixedDescriptorHeapSegmentTest, ConstructionNonzeroBase)
{
  // Test construction with nonzero base index and shader-visible visibility.
  constexpr uint32_t base = 42;
  const TestDescriptorHeapSegment seg(Capacity { 16 }, Handle { base },
    ResourceViewType::kStructuredBuffer_SRV,
    DescriptorVisibility::kShaderVisible);
  EXPECT_EQ(seg.GetViewType(), ResourceViewType::kStructuredBuffer_SRV);
  EXPECT_EQ(seg.GetVisibility(), DescriptorVisibility::kShaderVisible);
  EXPECT_EQ(seg.GetBaseIndex().get(), base);
  ExpectEmpty(seg);
}

//! Construction with zero capacity.
NOLINT_TEST(FixedDescriptorHeapSegmentTest, ConstructionWithZeroCapacity)
{
  TestDescriptorHeapSegment seg(Capacity { 0 }, Handle { 0 },
    ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible);
  EXPECT_EQ(seg.GetCapacity().get(), 0U);
  ExpectEmpty(seg);
  EXPECT_EQ(seg.Allocate(), kInvalidBindlessHandle);
}

//! Construction with invalid view type or visibility.
NOLINT_TEST(
  FixedDescriptorHeapSegmentTest, ConstructionWithInvalidTypeOrVisibility)
{
  const TestDescriptorHeapSegment seg(Capacity { 4 }, Handle { 0 },
    ResourceViewType::kNone, DescriptorVisibility::kNone);
  EXPECT_EQ(seg.GetViewType(), ResourceViewType::kNone);
  EXPECT_EQ(seg.GetVisibility(), DescriptorVisibility::kNone);
  ExpectEmpty(seg);
}

//! Destruction warning if not empty.
NOLINT_TEST(FixedDescriptorHeapSegmentTest, DestructionWhenNotEmpty)
{
  // Setup log capture for destruction warnings.
  const auto old_verbosity = loguru::g_stderr_verbosity;
  loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;
  const bool old_color = loguru::g_colorlogtostderr;
  loguru::g_colorlogtostderr = false;
  testing::internal::CaptureStderr();

  {
    // Allocate a descriptor to ensure the segment is not empty at destruction.
    FixedDescriptorHeapSegment seg(Capacity { 4 }, Handle { 0 },
      ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible);
    if (seg.GetCapacity().get() == 0) {
      return;
    }
    [[maybe_unused]] auto idx = seg.Allocate();
    ExpectSize(seg, 1U);
  }

  std::string output = testing::internal::GetCapturedStderr();
  loguru::g_stderr_verbosity = old_verbosity;
  loguru::g_colorlogtostderr = old_color;
  // Check that the warning message appears in the output.
  EXPECT_NE(output.find("descriptors still allocated"), std::string::npos);
}

//===----------------------------------------------------------------------===//
// Allocation
//===----------------------------------------------------------------------===//

//! Sequential allocation returns correct indices and updates state.
NOLINT_TEST(FixedDescriptorHeapSegmentTest, SequentialAllocation)
{
  constexpr uint32_t base = 10;
  TestDescriptorHeapSegment seg(Capacity { 4 }, Handle { base },
    ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible);
  constexpr uint32_t n = 4U;
  // Allocate up to n descriptors and check their indices.
  for (uint32_t i = 0; i < (std::min)(n, seg.GetCapacity().get()); ++i) {
    [[maybe_unused]] auto idx = seg.Allocate();
    EXPECT_NE(idx, kInvalidBindlessHandle);
    EXPECT_EQ(idx, Handle { base + i });
  }
  ExpectSize(seg, (std::min)(n, seg.GetCapacity().get()));
}

//! Allocate until full, then fail.
NOLINT_TEST(FixedDescriptorHeapSegmentTest, AllocateUntilFull)
{
  TestDescriptorHeapSegment seg(Capacity { 8 }, Handle { 0 },
    ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible);
  const auto capacity = seg.GetCapacity();
  if (capacity.get() == 0) {
    EXPECT_EQ(seg.Allocate(), kInvalidBindlessHandle);
    ExpectFull(seg);
    EXPECT_EQ(seg.GetAvailableCount().get(), 0U);
    return;
  }
  // Allocate all descriptors until full.
  for (uint32_t i = 0; i < capacity.get(); ++i) {
    [[maybe_unused]] auto idx = seg.Allocate();
    EXPECT_NE(idx, kInvalidBindlessHandle);
    EXPECT_EQ(idx, Handle { i });
  }
  // Next allocation should fail.
  ExpectFull(seg);
}

//! Allocate and release all, then allocate again.
NOLINT_TEST(FixedDescriptorHeapSegmentTest, AllocateReleaseAllThenAllocateAgain)
{
  constexpr uint32_t cap = 4;
  TestDescriptorHeapSegment seg(Capacity { cap }, Handle { 0 },
    ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible);
  std::vector<Handle> indices;
  indices.reserve(cap);
  for (uint32_t i = 0; i < cap; ++i) {
    indices.push_back(seg.Allocate());
  }
  for (const Handle idx : indices) {
    EXPECT_TRUE(seg.Release(idx));
  }
  ExpectEmpty(seg);
  // Allocate again after full release
  for (uint32_t i = 0; i < cap; ++i) {
    auto idx = seg.Allocate();
    EXPECT_NE(idx, kInvalidBindlessHandle);
  }
  ExpectFull(seg);
}

//! Allocate, release, and check available/allocated counts.
NOLINT_TEST(FixedDescriptorHeapSegmentTest, AllocateReleaseCounts)
{
  TestDescriptorHeapSegment seg(Capacity { 3 }, Handle { 0 },
    ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible);
  const auto a = seg.Allocate();
  const auto b = seg.Allocate();
  EXPECT_EQ(seg.GetAllocatedCount().get(), 2U);
  EXPECT_EQ(seg.GetAvailableCount().get(), 1U);
  EXPECT_TRUE(seg.Release(a));
  EXPECT_EQ(seg.GetAllocatedCount().get(), 1U);
  EXPECT_EQ(seg.GetAvailableCount().get(), 2U);
  EXPECT_TRUE(seg.Release(b));
  EXPECT_EQ(seg.GetAllocatedCount().get(), 0U);
  EXPECT_EQ(seg.GetAvailableCount().get(), 3U);
}

//===----------------------------------------------------------------------===//
// Release & Recycling
//===----------------------------------------------------------------------===//

//! Release and immediate recycle of a single descriptor.
NOLINT_TEST(FixedDescriptorHeapSegmentTest, ReleaseAndRecycleSingle)
{
  TestDescriptorHeapSegment seg(Capacity { 4 }, Handle { 0 },
    ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible);
  const auto capacity = seg.GetCapacity();
  if (capacity.get() == 0) {
    EXPECT_EQ(seg.Allocate(), kInvalidBindlessHandle);
    return;
  }
  // Allocate and release a descriptor, then allocate again and expect the same
  // index.
  [[maybe_unused]] const auto idx = seg.Allocate();
  EXPECT_NE(idx, kInvalidBindlessHandle);
  EXPECT_TRUE(seg.Release(idx));
  ExpectSize(seg, 0U);
  const auto recycled = seg.Allocate();
  EXPECT_EQ(recycled, idx);
  ExpectSize(seg, 1U);
}

//! Release multiple descriptors, verify counts, no recycle.
NOLINT_TEST(FixedDescriptorHeapSegmentTest, ReleaseMultipleNoRecycle)
{
  TestDescriptorHeapSegment seg(Capacity { 4 }, Handle { 0 },
    ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible);
  const auto capacity = seg.GetCapacity();
  if (capacity.get() < 3) {
    return;
  }
  // Allocate three descriptors, release two, and check state.
  [[maybe_unused]] const auto idx0 = seg.Allocate();
  [[maybe_unused]] auto idx1 = seg.Allocate();
  [[maybe_unused]] const auto idx2 = seg.Allocate();
  EXPECT_TRUE(seg.Release(idx0));
  EXPECT_TRUE(seg.Release(idx2));
  // There should be one allocated descriptor left.
  EXPECT_EQ(seg.GetAllocatedCount().get(), 1U);
  ExpectSize(seg, 1U);
}

//! Release all, then allocate and check indices are reused.
NOLINT_TEST(FixedDescriptorHeapSegmentTest, ReleaseAllAndReuseIndices)
{
  constexpr uint32_t cap = 3;
  TestDescriptorHeapSegment seg(Capacity { cap }, Handle { 0 },
    ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible);
  std::vector<Handle> indices;
  indices.reserve(cap);
  for (uint32_t i = 0; i < cap; ++i) {
    indices.push_back(seg.Allocate());
  }
  for (const Handle idx : indices) {
    EXPECT_TRUE(seg.Release(idx));
  }
  ExpectEmpty(seg);
  std::vector<uint32_t> new_indices;
  new_indices.reserve(cap);
  for (uint32_t i = 0; i < cap; ++i) {
    new_indices.push_back(seg.Allocate().get());
  }
  // All indices should be valid and within the original range
  for (uint32_t idx : new_indices) {
    EXPECT_GE(idx, 0U);
    EXPECT_LT(idx, cap);
  }
}

//===----------------------------------------------------------------------===//
// Release Error/Boundary Conditions
//===----------------------------------------------------------------------===//

//! Release already released index fails.
NOLINT_TEST(FixedDescriptorHeapSegmentTest, ReleaseAlreadyReleasedFails)
{
  TestDescriptorHeapSegment seg(Capacity { 4 }, Handle { 0 },
    ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible);
  if (const auto capacity = seg.GetCapacity(); capacity.get() == 0) {
    EXPECT_FALSE(seg.Release(Handle { 0 }));
    return;
  }
  // Release the same index twice; second release should fail.
  const auto idx = seg.Allocate();
  EXPECT_TRUE(seg.Release(idx));
  EXPECT_FALSE(seg.Release(idx));
  ExpectEmpty(seg);
}

//! Release unallocated index fails.
NOLINT_TEST(FixedDescriptorHeapSegmentTest, ReleaseUnallocatedIndexFails)
{
  constexpr uint32_t base = 10;
  TestDescriptorHeapSegment seg(Capacity { 8 }, Handle { base },
    ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible);
  const auto capacity = seg.GetCapacity();
  if (capacity.get() < 6) {
    return;
  }
  // Try to release an index that was never allocated.
  [[maybe_unused]] auto idx1 = seg.Allocate();
  [[maybe_unused]] auto idx2 = seg.Allocate();
  constexpr uint32_t unallocated = base + 5;
  EXPECT_FALSE(seg.Release(Handle { unallocated }));
  if (const uint32_t next = base + seg.GetAllocatedCount().get();
    next < base + capacity.get()) {
    EXPECT_FALSE(seg.Release(Handle { next }));
  }
}

//! Release out-of-bounds indices fails.
NOLINT_TEST(FixedDescriptorHeapSegmentTest, ReleaseOutOfBoundsFails)
{
  constexpr uint32_t base = 20;
  TestDescriptorHeapSegment seg(Capacity { 8 }, Handle { base },
    ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible);
  const auto capacity = seg.GetCapacity();
  if constexpr (base == 0) {
    if (capacity.get() == 0) {
      EXPECT_FALSE(seg.Release(Handle { base + capacity.get() }));
      EXPECT_FALSE(seg.Release(kInvalidBindlessHandle));
      return;
    }
  }
  if (capacity.get() == 0) {
    if constexpr (base > 0) {
      EXPECT_FALSE(seg.Release(Handle { base - 1 }));
    }
    EXPECT_FALSE(seg.Release(Handle { base + capacity.get() }));
    EXPECT_FALSE(seg.Release(Handle { base + capacity.get() + 1 }));
    EXPECT_FALSE(seg.Release(kInvalidBindlessHandle));
    return;
  }
  // Try to release indices outside the valid range.
  [[maybe_unused]] auto idx = seg.Allocate();
  if constexpr (base > 0) {
    EXPECT_FALSE(seg.Release(Handle { base - 1 }));
  }
  EXPECT_FALSE(seg.Release(Handle { base + capacity.get() }));
  EXPECT_FALSE(seg.Release(Handle { base + capacity.get() + 1 }));
  EXPECT_FALSE(seg.Release(kInvalidBindlessHandle));
}

//! Release with invalid index (max uint32_t).
NOLINT_TEST(FixedDescriptorHeapSegmentTest, ReleaseInvalidIndex)
{
  TestDescriptorHeapSegment seg(Capacity { 4 }, Handle { 0 },
    ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible);
  EXPECT_FALSE(seg.Release(kInvalidBindlessHandle));
}

//! Release with negative index (converted to uint32_t).
NOLINT_TEST(FixedDescriptorHeapSegmentTest, ReleaseNegativeIndex)
{
  TestDescriptorHeapSegment seg(Capacity { 4 }, Handle { 0 },
    ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible);
  constexpr int32_t neg = -1;
  EXPECT_FALSE(seg.Release(Handle { static_cast<uint32_t>(neg) }));
}

//! Release after reallocation and double-release.
NOLINT_TEST(FixedDescriptorHeapSegmentTest, ReleaseAfterReallocation)
{
  TestDescriptorHeapSegment seg(Capacity { 4 }, Handle { 0 },
    ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible);
  if (const auto capacity = seg.GetCapacity(); capacity.get() == 0) {
    return;
  }

  // Allocate one descriptor
  const auto idx = seg.Allocate();
  EXPECT_NE(idx, kInvalidBindlessHandle);

  // Release it
  EXPECT_TRUE(seg.Release(idx));
  ExpectSize(seg, 0U);

  // Re-allocate (should get the same index back due to LIFO)
  const auto idx2 = seg.Allocate();
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
NOLINT_TEST(FixedDescriptorHeapSegmentTest, LIFORecycling)
{
  constexpr uint32_t base = 100;
  TestDescriptorHeapSegment seg(Capacity { 8 }, Handle { base },
    ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible);
  if (const auto capacity = seg.GetCapacity(); capacity.get() < 5) {
    return;
  }

  // Allocate a, b, c, d, e in order
  [[maybe_unused]] auto a = seg.Allocate(); // base+0
  const auto b = seg.Allocate(); // base+1
  const auto c = seg.Allocate(); // base+2
  const auto d = seg.Allocate(); // base+3
  [[maybe_unused]] auto e = seg.Allocate(); // base+4
  ExpectSize(seg, 5U);

  // Release b, d, c in that order
  EXPECT_TRUE(seg.Release(b)); // base+1
  EXPECT_TRUE(seg.Release(d)); // base+3
  EXPECT_TRUE(seg.Release(c)); // base+2
  ExpectSize(seg, 2U);

  // LIFO: should get c, d, b (base+2, base+3, base+1)
  const auto f = seg.Allocate();
  EXPECT_EQ(f, Handle { base + 2 });
  const auto g = seg.Allocate();
  EXPECT_EQ(g, Handle { base + 3 });
  const auto h = seg.Allocate();
  EXPECT_EQ(h, Handle { base + 1 });

  ExpectSize(seg, 5U);
}

//! LIFO recycling with full free list.
NOLINT_TEST(FixedDescriptorHeapSegmentTest, LIFORecycleFullFreeList)
{
  constexpr uint32_t cap = 5;
  TestDescriptorHeapSegment seg(Capacity { cap }, Handle { 0 },
    ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible);
  std::vector<Handle> indices;
  indices.reserve(cap);
  for (uint32_t i = 0; i < cap; ++i) {
    indices.push_back(seg.Allocate());
  }
  // Release all except the first
  for (uint32_t i = 1; i < cap; ++i) {
    EXPECT_TRUE(seg.Release(indices[i]));
  }
  // Allocate again and check LIFO order
  for (uint32_t i = cap; i-- > 1;) {
    auto idx = seg.Allocate();
    EXPECT_EQ(idx, indices[i]);
  }
}

//===----------------------------------------------------------------------===//
// Move Semantics
//===----------------------------------------------------------------------===//

//! Move construction and assignment.
NOLINT_TEST(FixedDescriptorHeapSegmentTest, MoveSemantics)
{
  uint32_t base = 77;
  auto visibility = DescriptorVisibility::kShaderVisible;
  TestDescriptorHeapSegment orig(Capacity { 8 }, Handle { base },
    ResourceViewType::kConstantBuffer, visibility);
  auto capacity = orig.GetCapacity();

  if (capacity.get() == 0) {
    TestDescriptorHeapSegment moved(std::move(orig));
    EXPECT_EQ(moved.GetCapacity().get(), 0U);
    TestDescriptorHeapSegment assign(Capacity { 8 }, Handle { base + 1 },
      ResourceViewType::kConstantBuffer, visibility);
    assign = std::move(moved);
    EXPECT_EQ(assign.GetCapacity().get(), 0U);
    return;
  }

  // Allocate about half the capacity in the original segment
  std::vector<Handle> allocations;
  allocations.reserve(capacity.get() / 2 + (capacity.get() % 2));
  for (uint32_t i = 0; i < capacity.get() / 2 + (capacity.get() % 2); ++i) {
    allocations.push_back(orig.Allocate());
  }
  // Optionally release the first allocation if more than one was made
  if (allocations.size() > 1) {
    EXPECT_TRUE(orig.Release(allocations[0]));
  }

  // Record the state of the original segment before moving
  uint32_t orig_size = orig.GetAllocatedCount().get();
  uint32_t orig_avail = orig.GetAvailableCount().get();
  auto orig_next = orig.Allocate();
  if (orig_next != kInvalidBindlessHandle) {
    EXPECT_TRUE(orig.Release(orig_next));
  }

  // Move-construct a new segment from the original
  TestDescriptorHeapSegment moved(std::move(orig));

  // Check that all properties and state are preserved after move construction
  EXPECT_EQ(moved.GetViewType(), ResourceViewType::kConstantBuffer);
  EXPECT_EQ(moved.GetVisibility(), visibility);
  EXPECT_EQ(moved.GetBaseIndex().get(), base);
  EXPECT_EQ(moved.GetCapacity(), capacity);
  EXPECT_EQ(moved.GetAllocatedCount().get(), orig_size);
  EXPECT_EQ(moved.GetAvailableCount().get(), orig_avail);

  // Allocate from the moved segment and verify the next index matches
  auto moved_next = moved.Allocate();
  EXPECT_EQ(moved_next, orig_next);
  if (moved_next != kInvalidBindlessHandle) {
    EXPECT_TRUE(moved.Release(moved_next));
  }

  // Create another segment and allocate from it to set up for move assignment
  TestDescriptorHeapSegment another(Capacity { 8 }, Handle { base + 100 },
    ResourceViewType::kConstantBuffer, visibility);
  if (capacity.get() > 0) {
    auto _ = another.Allocate();
    (void)_;
  }
  uint32_t another_size = another.GetAllocatedCount().get();
  uint32_t another_avail = another.GetAvailableCount().get();
  auto another_next = another.Allocate();
  if (another_next != kInvalidBindlessHandle) {
    EXPECT_TRUE(another.Release(another_next));
  }

  // Move-assign 'another' into 'moved' and verify all properties and state
  moved = std::move(another);

  EXPECT_EQ(moved.GetViewType(), ResourceViewType::kConstantBuffer);
  EXPECT_EQ(moved.GetVisibility(), visibility);
  EXPECT_EQ(moved.GetBaseIndex(), Handle { base + 100 });
  EXPECT_EQ(moved.GetCapacity(), capacity);
  EXPECT_EQ(moved.GetAllocatedCount().get(), another_size);
  EXPECT_EQ(moved.GetAvailableCount().get(), another_avail);

  // Allocate from the newly assigned segment and verify the next index
  auto assigned_next = moved.Allocate();
  EXPECT_EQ(assigned_next, another_next);
  if (assigned_next != kInvalidBindlessHandle) {
    EXPECT_TRUE(moved.Release(assigned_next));
  }
}

//! Move from empty segment.
NOLINT_TEST(FixedDescriptorHeapSegmentTest, MoveFromEmptySegment)
{
  TestDescriptorHeapSegment seg(Capacity { 4 }, Handle { 0 },
    ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible);
  const TestDescriptorHeapSegment moved(std::move(seg));
  ExpectEmpty(moved);
  EXPECT_EQ(moved.GetCapacity().get(), 4U);
}

//! Move assign to self.
NOLINT_TEST(FixedDescriptorHeapSegmentTest, MoveAssignToSelf)
{
  TestDescriptorHeapSegment seg(Capacity { 4 }, Handle { 0 },
    ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible);
  seg = std::move(seg); // NOLINT(clang-diagnostic-self-move) -- testing
  ExpectEmpty(seg);
  EXPECT_EQ(seg.GetCapacity().get(), 4U);
}

//===----------------------------------------------------------------------===//
// Polymorphic Interface
//===----------------------------------------------------------------------===//

//! Polymorphic interface usage.
NOLINT_TEST(FixedDescriptorHeapSegmentTest, PolymorphicInterfaceUsage)
{
  const auto seg = std::make_unique<TestDescriptorHeapSegment>(Capacity { 8 },
    Handle { 100 }, ResourceViewType::kConstantBuffer,
    DescriptorVisibility::kShaderVisible);

  const auto capacity = seg->GetCapacity();
  if (capacity.get() == 0) {
    return;
  }
  // Allocate and release a few descriptors through the base interface.
  ExpectSize(*seg, 0U);

  constexpr uint32_t n = 4U;
  for (uint32_t i = 0; i < (std::min)(n, capacity.get()); ++i) {
    auto idx = seg->Allocate();
    EXPECT_NE(idx, kInvalidBindlessHandle);
    EXPECT_EQ(idx, Handle { seg->GetBaseIndex().get() + i });
    EXPECT_EQ(seg->GetAllocatedCount().get(), i + 1);
  }
  for (uint32_t i = 0; i < (std::min)(n, capacity.get()); ++i) {
    EXPECT_TRUE(seg->Release(Handle { seg->GetBaseIndex().get() + i }));
  }
  ExpectSize(*seg, 0U);
  ExpectEmpty(*seg);
}

//===----------------------------------------------------------------------===//
// Parameterized Tests
//===----------------------------------------------------------------------===//

class AllocateUntilFullParamTest : public ::testing::TestWithParam<uint32_t> {
};

INSTANTIATE_TEST_SUITE_P(
  CapacityCases, AllocateUntilFullParamTest, ::testing::Values(0U, 1U, 10U));

//! Allocate until full, then fail.
NOLINT_TEST_P(AllocateUntilFullParamTest, AllocateUntilFull)
{
  const uint32_t test_capacity = GetParam();
  TestDescriptorHeapSegment seg(Capacity { test_capacity }, Handle { 0 },
    ResourceViewType::kConstantBuffer, DescriptorVisibility::kShaderVisible);
  const auto capacity = seg.GetCapacity();
  if (capacity.get() == 0) {
    EXPECT_EQ(seg.Allocate(), kInvalidBindlessHandle);
    ExpectFull(seg);
    EXPECT_EQ(seg.GetAvailableCount().get(), 0U);
    return;
  }
  // Allocate all descriptors until full.
  for (uint32_t i = 0; i < capacity.get(); ++i) {
    [[maybe_unused]] auto idx = seg.Allocate();
    EXPECT_NE(idx, kInvalidBindlessHandle);
    EXPECT_EQ(idx, Handle { i });
  }
  // Next allocation should fail.
  ExpectFull(seg);
}

} // namespace
