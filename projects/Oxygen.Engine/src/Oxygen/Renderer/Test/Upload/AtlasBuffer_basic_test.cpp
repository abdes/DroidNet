//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <system_error>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Upload/AtlasBuffer.h>
#include <Oxygen/Renderer/Upload/Types.h>

using oxygen::engine::upload::AtlasBuffer;
using oxygen::engine::upload::EnsureBufferResult;
using ElementRef = AtlasBuffer::ElementRef;
using oxygen::frame::Slot;

namespace {

//! Basic allocation / free / recycle scenario
/*!
 Verifies Allocate() success up to capacity, failure on exhaustion, Release() +
 OnFrameStart() recycling, and basic stats invariants.
*/
NOLINT_TEST(AtlasBuffer, AllocateFreeRecycle)
{
  // Arrange
  constexpr std::uint32_t kInitialCapacity = 8;
  constexpr std::uint32_t kStride = 64;
  oxygen::renderer::testing::FakeGraphics gfx; // fake backend
  oxygen::observer_ptr<oxygen::Graphics> gfx_ptr(&gfx);
  AtlasBuffer atlas(gfx_ptr, kStride, "TestAtlas");
  auto ensure = atlas.EnsureCapacity(kInitialCapacity, 0.f);
  ASSERT_TRUE(ensure.has_value());

  std::vector<ElementRef> refs;
  // Act: allocate all slots
  for (std::uint32_t i = 0; i < kInitialCapacity; ++i) {
    auto alloc = atlas.Allocate(1);
    ASSERT_TRUE(alloc.has_value());
    refs.push_back(alloc.value());
  }
  // Assert: no more slots available
  auto no_alloc = atlas.Allocate(1);
  EXPECT_FALSE(no_alloc.has_value());

  // Release all slots
  for (const auto& ref : refs) {
    atlas.Release(ref, Slot { 0 });
  }
  // Recycle retirees (simulate frame advance for slot 0)
  atlas.OnFrameStart(Slot { 0 });

  // Act: allocate again after recycle
  for (std::uint32_t i = 0; i < kInitialCapacity; ++i) {
    auto alloc = atlas.Allocate(1);
    ASSERT_TRUE(alloc.has_value());
  }

  // Basic stat sanity
  auto stats = atlas.GetStats();
  EXPECT_EQ(stats.capacity_elements, kInitialCapacity);
  EXPECT_EQ(stats.free_list_size, 0u);
}

//! EnsureCapacity growth path
/*!
 Verifies kCreated then kResized transitions, stable previously allocated
 indices, and stats ensure_calls increment.
*/
NOLINT_TEST(AtlasBuffer, EnsureCapacityGrowth)
{
  // Arrange
  constexpr std::uint32_t kInitial = 4;
  constexpr std::uint32_t kLarger = 10; // force resize (no slack)
  constexpr std::uint32_t kStride = 32;
  oxygen::renderer::testing::FakeGraphics gfx;
  oxygen::observer_ptr<oxygen::Graphics> gfx_ptr(&gfx);
  AtlasBuffer atlas(gfx_ptr, kStride, "GrowthAtlas");

  // Act + Assert: initial ensure -> created
  auto ensure_created = atlas.EnsureCapacity(kInitial, 0.f);
  ASSERT_TRUE(ensure_created.has_value());
  EXPECT_EQ(*ensure_created, EnsureBufferResult::kCreated);
  EXPECT_GE(atlas.CapacityElements(), kInitial);

  // Allocate a couple entries
  auto a0 = atlas.Allocate();
  auto a1 = atlas.Allocate();
  ASSERT_TRUE(a0.has_value());
  ASSERT_TRUE(a1.has_value());
  const auto idx1 = atlas.GetElementIndex(a1.value());

  // Second ensure with larger min -> resized
  auto ensure_resized = atlas.EnsureCapacity(kLarger, 0.f);
  ASSERT_TRUE(ensure_resized.has_value());
  EXPECT_EQ(*ensure_resized, EnsureBufferResult::kResized);
  EXPECT_GE(atlas.CapacityElements(), kLarger);

  // Allocate another and ensure previously allocated index is unchanged
  auto a2 = atlas.Allocate();
  ASSERT_TRUE(a2.has_value());
  EXPECT_EQ(atlas.GetElementIndex(a1.value()), idx1);

  // Stats sanity
  auto stats = atlas.GetStats();
  EXPECT_GE(stats.capacity_elements, kLarger);
  EXPECT_EQ(stats.ensure_calls, 2u);
}

//! Allocation exhaustion error path
/*!
 Allocates exactly capacity elements then expects std::errc::no_buffer_space on
 the next allocation attempt.
*/
NOLINT_TEST(AtlasBuffer, AllocationExhaustionError)
{
  // Arrange
  constexpr std::uint32_t kCap = 2;
  constexpr std::uint32_t kStride = 16;
  oxygen::renderer::testing::FakeGraphics gfx;
  oxygen::observer_ptr<oxygen::Graphics> gfx_ptr(&gfx);
  AtlasBuffer atlas(gfx_ptr, kStride, "ExhaustAtlas");
  ASSERT_TRUE(atlas.EnsureCapacity(kCap, 0.f).has_value());

  // Act
  auto a0 = atlas.Allocate();
  auto a1 = atlas.Allocate();
  ASSERT_TRUE(a0.has_value());
  ASSERT_TRUE(a1.has_value());
  auto a2 = atlas.Allocate();

  // Assert
  ASSERT_FALSE(a2.has_value());
  EXPECT_EQ(a2.error(), std::make_error_code(std::errc::no_buffer_space));
}

//! Free list reuse (order agnostic)
/*!
 Releases a subset out-of-order, recycles the frame slot, and verifies the freed
 indices are returned (without asserting ordering).
*/
NOLINT_TEST(AtlasBuffer, FreeListReuse)
{
  // Arrange
  constexpr std::uint32_t kCap = 4;
  constexpr std::uint32_t kStride = 24;
  oxygen::renderer::testing::FakeGraphics gfx;
  oxygen::observer_ptr<oxygen::Graphics> gfx_ptr(&gfx);
  AtlasBuffer atlas(gfx_ptr, kStride, "ReuseAtlas");
  ASSERT_TRUE(atlas.EnsureCapacity(kCap, 0.f).has_value());
  std::array<ElementRef, kCap> refs {};
  for (std::uint32_t i = 0; i < kCap; ++i) {
    auto a = atlas.Allocate();
    ASSERT_TRUE(a.has_value());
    refs[i] = a.value();
  }

  // Act: release indices 1 then 3 (out of allocation order)
  atlas.Release(refs[1], Slot { 0 });
  atlas.Release(refs[3], Slot { 0 });
  atlas.OnFrameStart(Slot { 0 }); // recycle

  // Allocate twice: collect indices and confirm they match the released set
  auto r0 = atlas.Allocate();
  auto r1 = atlas.Allocate();
  ASSERT_TRUE(r0.has_value());
  ASSERT_TRUE(r1.has_value());
  const auto got0 = atlas.GetElementIndex(r0.value());
  const auto got1 = atlas.GetElementIndex(r1.value());
  const auto rel_a = atlas.GetElementIndex(refs[1]);
  const auto rel_b = atlas.GetElementIndex(refs[3]);
  // Order agnostic check
  const bool match_direct = (got0 == rel_a && got1 == rel_b);
  const bool match_swap = (got0 == rel_b && got1 == rel_a);
  EXPECT_TRUE(match_direct || match_swap);
}

//! MakeUploadDesc validation
/*!
 Valid ElementRef produces expected offset; default-constructed invalid
 reference returns std::errc::invalid_argument.
*/
NOLINT_TEST(AtlasBuffer, MakeUploadDescValidation)
{
  // Arrange
  constexpr std::uint32_t kStride = 40;
  oxygen::renderer::testing::FakeGraphics gfx;
  oxygen::observer_ptr<oxygen::Graphics> gfx_ptr(&gfx);
  AtlasBuffer atlas(gfx_ptr, kStride, "DescAtlas");
  ASSERT_TRUE(atlas.EnsureCapacity(4, 0.f).has_value());
  auto alloc = atlas.Allocate();
  ASSERT_TRUE(alloc.has_value());
  const auto elem_index = atlas.GetElementIndex(alloc.value());

  // Act
  auto desc_ok = atlas.MakeUploadDesc(alloc.value(), kStride);
  AtlasBuffer::ElementRef invalid_ref; // default -> invalid srv
  auto desc_err = atlas.MakeUploadDesc(invalid_ref, kStride);

  // Assert
  ASSERT_TRUE(desc_ok.has_value());
  EXPECT_EQ(desc_ok->size_bytes, kStride);
  EXPECT_EQ(
    desc_ok->dst_offset, static_cast<std::uint64_t>(elem_index) * kStride);
  ASSERT_FALSE(desc_err.has_value());
  EXPECT_EQ(
    desc_err.error(), std::make_error_code(std::errc::invalid_argument));
}

//! MakeUploadDescForIndex error handling
/*!
 Covers invalid pre-EnsureCapacity(), out-of-range after ensure, and valid
 in-range descriptor creation.
*/
NOLINT_TEST(AtlasBuffer, MakeUploadDescForIndexErrors)
{
  // Arrange
  constexpr std::uint32_t kStride = 20;
  oxygen::renderer::testing::FakeGraphics gfx;
  oxygen::observer_ptr<oxygen::Graphics> gfx_ptr(&gfx);
  AtlasBuffer atlas(gfx_ptr, kStride, "IndexDescAtlas");

  // Act: invalid before ensure
  auto pre = atlas.MakeUploadDescForIndex(0, kStride);
  ASSERT_FALSE(pre.has_value());
  EXPECT_EQ(pre.error(), std::make_error_code(std::errc::invalid_argument));

  // Ensure capacity 2
  ASSERT_TRUE(atlas.EnsureCapacity(2, 0.f).has_value());
  // Out of range
  auto oor = atlas.MakeUploadDescForIndex(5, kStride);
  ASSERT_FALSE(oor.has_value());
  EXPECT_EQ(oor.error(), std::make_error_code(std::errc::result_out_of_range));
  // In range
  auto ok = atlas.MakeUploadDescForIndex(1, kStride);
  ASSERT_TRUE(ok.has_value());
  EXPECT_EQ(ok->dst_offset, static_cast<std::uint64_t>(1) * kStride);
}

//! Frame-slot retire recycling behavior
/*!
 Releases elements into distinct frame slots, recycles one slot at a time,
 validating delayed reuse until matching OnFrameStart().
*/
NOLINT_TEST(AtlasBuffer, MultiFrameRetireRecycling)
{
  // Arrange
  constexpr std::uint32_t kStride = 28;
  oxygen::renderer::testing::FakeGraphics gfx;
  oxygen::observer_ptr<oxygen::Graphics> gfx_ptr(&gfx);
  AtlasBuffer atlas(gfx_ptr, kStride, "RetireAtlas");
  ASSERT_TRUE(atlas.EnsureCapacity(5, 0.f).has_value());
  std::array<ElementRef, 5> refs {};
  for (std::uint32_t i = 0; i < 5; ++i) {
    auto a = atlas.Allocate();
    ASSERT_TRUE(a.has_value());
    refs[i] = a.value();
  }
  const auto idx_slot0 = atlas.GetElementIndex(refs[1]);
  const auto idx_slot1 = atlas.GetElementIndex(refs[2]);

  // Release into different slots
  atlas.Release(refs[1], Slot { 0 });
  atlas.Release(refs[2], Slot { 1 });

  // Act + Assert: recycle slot 1 first -> only idx_slot1 available
  atlas.OnFrameStart(Slot { 1 });
  auto r1 = atlas.Allocate();
  ASSERT_TRUE(r1.has_value());
  EXPECT_EQ(atlas.GetElementIndex(r1.value()), idx_slot1);

  // Slot 0 not yet recycled: allocating again should FAIL (no capacity and
  // slot0 still retired)
  auto fresh_fail = atlas.Allocate();
  ASSERT_FALSE(fresh_fail.has_value());

  // Now recycle slot 0 and expect idx_slot0
  atlas.OnFrameStart(Slot { 0 });
  auto r0 = atlas.Allocate();
  ASSERT_TRUE(r0.has_value());
  EXPECT_EQ(atlas.GetElementIndex(r0.value()), idx_slot0);
}

//! Multi-count allocation unsupported (Phase 1)
/*!
 Requests count > 1 and expects std::errc::invalid_argument.
*/
NOLINT_TEST(AtlasBuffer, MultiCountAllocationUnsupported)
{
  // Arrange
  oxygen::renderer::testing::FakeGraphics gfx;
  oxygen::observer_ptr<oxygen::Graphics> gfx_ptr(&gfx);
  AtlasBuffer atlas(gfx_ptr, 8u, "CountAtlas");
  ASSERT_TRUE(atlas.EnsureCapacity(4, 0.f).has_value());

  // Act
  auto alloc = atlas.Allocate(2);

  // Assert
  ASSERT_FALSE(alloc.has_value());
  EXPECT_EQ(alloc.error(), std::make_error_code(std::errc::invalid_argument));
}

//! EnsureCapacity kUnchanged path
/*!
 Multiple calls with equal/decreasing minima after creation return kUnchanged;
 capacity and ensure_calls stats verified.
*/
NOLINT_TEST(AtlasBuffer, EnsureCapacityUnchanged)
{
  // Arrange
  constexpr std::uint32_t kCap = 8;
  constexpr std::uint32_t kStride = 32;
  oxygen::renderer::testing::FakeGraphics gfx;
  oxygen::observer_ptr<oxygen::Graphics> gfx_ptr(&gfx);
  AtlasBuffer atlas(gfx_ptr, kStride, "UnchangedAtlas");

  // Act
  auto first = atlas.EnsureCapacity(kCap, 0.f);
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(*first, EnsureBufferResult::kCreated);
  auto unchanged1 = atlas.EnsureCapacity(6, 0.f);
  ASSERT_TRUE(unchanged1.has_value());
  EXPECT_EQ(*unchanged1, EnsureBufferResult::kUnchanged);
  auto unchanged2 = atlas.EnsureCapacity(kCap, 0.f);
  ASSERT_TRUE(unchanged2.has_value());
  EXPECT_EQ(*unchanged2, EnsureBufferResult::kUnchanged);

  // Assert: capacity did not shrink and stats reflect 3 ensure calls
  EXPECT_EQ(atlas.CapacityElements(), kCap);
  auto stats = atlas.GetStats();
  EXPECT_EQ(stats.ensure_calls, 3u);
}

} // namespace
