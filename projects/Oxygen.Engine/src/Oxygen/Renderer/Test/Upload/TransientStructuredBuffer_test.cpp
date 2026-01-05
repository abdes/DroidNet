//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <cstddef>
#include <cstdint>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Renderer/Test/Upload/RingBufferStagingFixture.h>
#include <Oxygen/Renderer/Upload/Errors.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>

using oxygen::engine::upload::TransientStructuredBuffer;

namespace oxygen::engine::upload {

using oxygen::frame::SlotCount;

class TransientStructuredBufferTest : public testing::RingBufferStagingFixture {
protected:
  void SetUp() override
  {
    testing::RingBufferStagingFixture::SetUp();
    // Create a simple ring buffer staging provider for tests
    auto provider = MakeRingBuffer(SlotCount { 1 }, 256u);
    ASSERT_NE(provider, nullptr);
  }
};

NOLINT_TEST_F(
  TransientStructuredBufferTest, AllocateBeforeFrameStartReturnsInvalidRequest)
{
  // Arrange: create transient buffer but do not start a frame slot
  TransientStructuredBuffer transient_buffer(GfxPtr(), Staging(), 64);

  // Act
  auto r = transient_buffer.Allocate(1);

  // Assert: allocation without a valid frame slot is an invalid request
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(),
    oxygen::engine::upload::make_error_code(
      oxygen::engine::upload::UploadError::kInvalidRequest));
}

// Note: earlier tests covered allocate+reset behaviour together with mapped
// pointer persistence and reset idempotency. Removing the redundant test to
// keep the suite concise while preserving coverage.

NOLINT_TEST_F(TransientStructuredBufferTest,
  MultipleAllocationsInSameSlotReturnValidAllocations)
{
  // Arrange
  TransientStructuredBuffer transient_buffer(GfxPtr(), Staging(), 64);

  // Activate frame slot
  transient_buffer.OnFrameStart(frame::SequenceNumber { 1 }, frame::Slot { 0 });
  // Act: first allocation
  auto alloc1_result = transient_buffer.Allocate(10);

  // Assert: first allocation succeeded and provides a valid allocation
  ASSERT_TRUE(alloc1_result.has_value())
    << "First Allocate failed: " << alloc1_result.error().message();
  const auto alloc1 = *alloc1_result;
  EXPECT_NE(alloc1.srv, kInvalidShaderVisibleIndex);
  EXPECT_NE(alloc1.mapped_ptr, nullptr);

  // Act: second allocation in same slot
  auto alloc2_result = transient_buffer.Allocate(20);

  // Assert: second allocation succeeded and provides a valid allocation
  ASSERT_TRUE(alloc2_result.has_value())
    << "Second Allocate failed: " << alloc2_result.error().message();
  const auto alloc2 = *alloc2_result;
  EXPECT_NE(alloc2.srv, kInvalidShaderVisibleIndex);
  EXPECT_NE(alloc2.mapped_ptr, nullptr);
  // Both allocations should have been created within the current frame
  EXPECT_EQ(alloc1.sequence, frame::SequenceNumber { 1 });
  EXPECT_EQ(alloc2.sequence, frame::SequenceNumber { 1 });
  EXPECT_EQ(alloc1.slot, frame::Slot { 0 });
  EXPECT_EQ(alloc2.slot, frame::Slot { 0 });
}

//! Ensures mapped pointers are stride-aligned even when the staging provider
//! only guarantees a smaller power-of-two alignment.
NOLINT_TEST_F(TransientStructuredBufferTest,
  StrideAlignmentAdjustsMappedPointerWhenOffsetsMisaligned)
{
  // Arrange: small alignment to reproduce cross-stride misalignment.
  auto provider = MakeRingBuffer(SlotCount { 1 }, 16u);
  SetStagingProvider(provider);
  ASSERT_NE(provider, nullptr);

  // Two transient buffers share the same ring but use different strides.
  TransientStructuredBuffer a(GfxPtr(), Staging(), 16);
  TransientStructuredBuffer b(GfxPtr(), Staging(), 48);

  const auto seq = frame::SequenceNumber { 1 };
  const auto slot = frame::Slot { 0 };
  a.OnFrameStart(seq, slot);
  b.OnFrameStart(seq, slot);

  // Act
  auto a_alloc = a.Allocate(1);
  ASSERT_TRUE(a_alloc.has_value());
  auto b_alloc = b.Allocate(1);
  ASSERT_TRUE(b_alloc.has_value());

  // Assert
  auto* a_ptr = static_cast<std::byte*>(a_alloc->mapped_ptr);
  auto* b_ptr = static_cast<std::byte*>(b_alloc->mapped_ptr);
  ASSERT_NE(a_ptr, nullptr);
  ASSERT_NE(b_ptr, nullptr);

  // First allocation consumes 32 bytes in the ring due to over-allocation and
  // 16-byte ring alignment. The second allocation would start at byte offset
  // 32, which is not aligned to 48. The transient buffer must shift the mapped
  // pointer forward to the next 48-byte boundary.
  const auto delta = static_cast<std::uint64_t>(b_ptr - a_ptr);
  EXPECT_EQ(delta, 48u);
}

NOLINT_TEST_F(TransientStructuredBufferTest, AllocateZeroIsNoOpSuccess)
{
  // Arrange
  TransientStructuredBuffer transient_buffer(GfxPtr(), Staging(), 64);

  // Activate frame slot
  transient_buffer.OnFrameStart(frame::SequenceNumber { 1 }, frame::Slot { 0 });
  // Act
  auto alloc_result_zero = transient_buffer.Allocate(0);

  // Assert: Allocate(0) should be a no-op success and produce an empty/invalid
  // transient allocation carrying the current sequence and slot
  ASSERT_TRUE(alloc_result_zero.has_value())
    << "Allocate(0) returned error: " << alloc_result_zero.error().message();
  const auto alloc0 = *alloc_result_zero;
  EXPECT_EQ(alloc0.srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(alloc0.mapped_ptr, nullptr);
  EXPECT_EQ(alloc0.sequence, frame::SequenceNumber { 1 });
  EXPECT_EQ(alloc0.slot, frame::Slot { 0 });
}

NOLINT_TEST_F(TransientStructuredBufferTest,
  AllocateWhenCreateBufferThrowsReturnsStagingAllocFailed)
{
  // Arrange: force underlying CreateBuffer to throw so staging allocation fails
  Gfx().SetThrowOnCreateBuffer(true);

  // Recreate uploader/provider after changing gfx behaviour
  auto& uploader = Uploader();
  auto provider = uploader.CreateRingBufferStaging(SlotCount { 1 }, 256u, 0.5f);
  SetStagingProvider(provider);
  ASSERT_NE(provider, nullptr);

  // Arrange: construct transient buffer
  TransientStructuredBuffer transient_buffer(GfxPtr(), Staging(), 64);

  // Activate frame slot so allocator will attempt staging allocation
  transient_buffer.OnFrameStart(frame::SequenceNumber { 1 }, frame::Slot { 0 });

  // Act
  auto alloc_result_fail = transient_buffer.Allocate(10);

  // Assert: should propagate staging allocation failure
  ASSERT_FALSE(alloc_result_fail.has_value());
  EXPECT_EQ(alloc_result_fail.error(),
    oxygen::engine::upload::make_error_code(
      oxygen::engine::upload::UploadError::kStagingAllocFailed));
}

// Ensure that failing to map the staging buffer surface is handled correctly
NOLINT_TEST_F(
  TransientStructuredBufferTest, AllocateWhenMapFailsReturnsStagingMapFailed)
{
  // Arrange: force underlying Map() to fail
  Gfx().SetFailMap(true);

  // Recreate uploader/provider after changing gfx behaviour
  auto& uploader = Uploader();
  auto provider = uploader.CreateRingBufferStaging(SlotCount { 1 }, 256u, 0.5f);
  SetStagingProvider(provider);
  ASSERT_NE(provider, nullptr);

  TransientStructuredBuffer transient_buffer(GfxPtr(), Staging(), 64);

  // Activate frame slot
  transient_buffer.OnFrameStart(frame::SequenceNumber { 1 }, frame::Slot { 0 });
  // Act
  auto alloc_result = transient_buffer.Allocate(10);

  // Assert: mapping error should propagate
  ASSERT_FALSE(alloc_result.has_value());
  EXPECT_EQ(alloc_result.error(),
    oxygen::engine::upload::make_error_code(
      oxygen::engine::upload::UploadError::kStagingMapFailed));
}

// Confirm mapped pointer is valid for writes until Reset() is called
NOLINT_TEST_F(
  TransientStructuredBufferTest, MappedPointerWritesPersistUntilSlotReset)
{
  // Arrange
  TransientStructuredBuffer transient_buffer(
    GfxPtr(), Staging(), 8); // 8-byte stride

  // Activate frame slot
  transient_buffer.OnFrameStart(frame::SequenceNumber { 1 }, frame::Slot { 0 });
  // Act
  auto alloc_result = transient_buffer.Allocate(4);

  // Assert: successful allocation and valid mapped pointer
  ASSERT_TRUE(alloc_result.has_value())
    << "Allocate failed: " << alloc_result.error().message();

  const auto alloc = *alloc_result;
  auto* mapped = static_cast<uint64_t*>(alloc.mapped_ptr);
  ASSERT_NE(mapped, nullptr);

  // Arrange/Act: write pattern to first and last element
  mapped[0] = 0xAABBCCDDEEFF0011ull;
  mapped[3] = 0x1122334455667788ull;

  // Assert: reads reflect writes while allocation is active
  EXPECT_EQ(mapped[0], 0xAABBCCDDEEFF0011ull);
  EXPECT_EQ(mapped[3], 0x1122334455667788ull);

  // Act: starting the next frame resets the slot
  transient_buffer.OnFrameStart(frame::SequenceNumber { 2 }, frame::Slot { 0 });

  // Assert: allocation for previous sequence should now be considered invalid
  EXPECT_FALSE(alloc.IsValid(frame::SequenceNumber { 2 }));
}

// Reset can be called multiple times and leaves object in cleared state
NOLINT_TEST_F(
  TransientStructuredBufferTest, ResetIsIdempotentAndLeavesObjectCleared)
{
  // Arrange
  TransientStructuredBuffer transient_buffer(GfxPtr(), Staging(), 64);

  // Activate frame slot
  transient_buffer.OnFrameStart(frame::SequenceNumber { 1 }, frame::Slot { 0 });
  // Act: allocate then advance frame twice to emulate reset -> idempotent
  auto initial_alloc = transient_buffer.Allocate(2);
  ASSERT_TRUE(initial_alloc.has_value());

  // Idempotency: starting the next frame twice should be safe and should
  // retire previous allocations for the slot.
  transient_buffer.OnFrameStart(frame::SequenceNumber { 2 }, frame::Slot { 0 });
  EXPECT_NO_THROW(transient_buffer.OnFrameStart(
    frame::SequenceNumber { 3 }, frame::Slot { 0 }));

  // After the slot reset, the earlier allocation must no longer be valid.
  EXPECT_FALSE(initial_alloc->IsValid(frame::SequenceNumber { 3 }));

  // Ensure a new allocation for the new frame succeeds.
  auto new_alloc = transient_buffer.Allocate(1);
  ASSERT_TRUE(new_alloc.has_value());
}

// After Reset, Allocate must re-create binding and mapped pointer
NOLINT_TEST_F(
  TransientStructuredBufferTest, AllocateAfterResetRecreatesValidAllocation)
{
  // Arrange
  TransientStructuredBuffer transient_buffer(GfxPtr(), Staging(), 64);

  // Activate frame slot
  transient_buffer.OnFrameStart(frame::SequenceNumber { 1 }, frame::Slot { 0 });
  // Act: initial allocation
  auto r1 = transient_buffer.Allocate(4);
  ASSERT_TRUE(r1.has_value())
    << "Initial allocate failed: " << r1.error().message();
  const auto a1 = *r1;
  EXPECT_NE(a1.srv, kInvalidShaderVisibleIndex);

  // Act: reset then allocate again
  // Reset the slot by moving to the next frame
  transient_buffer.OnFrameStart(frame::SequenceNumber { 2 }, frame::Slot { 0 });
  // Need to re-activate slot for new frame
  transient_buffer.OnFrameStart(frame::SequenceNumber { 2 }, frame::Slot { 0 });
  auto r2 = transient_buffer.Allocate(4);

  // Assert: second allocation succeeds and provides valid mapping
  ASSERT_TRUE(r2.has_value()) << "Re-allocate failed: " << r2.error().message();
  const auto a2 = *r2;
  EXPECT_NE(a2.srv, kInvalidShaderVisibleIndex);

  // Re-allocate after starting a new frame slot must succeed and return a
  // valid mapped pointer
  ASSERT_TRUE(r2.has_value()) << "Re-allocate failed: " << r2.error().message();
  EXPECT_NE((*r2).mapped_ptr, nullptr);
}

NOLINT_TEST_F(
  TransientStructuredBufferTest, MultipleAllocationsPersistUntilFrameReset)
{
  TransientStructuredBuffer transient_buffer(GfxPtr(), Staging(), 16);

  // Activate slot for this frame
  const auto seq = frame::SequenceNumber { 1 };
  transient_buffer.OnFrameStart(seq, frame::Slot { 0 });

  auto a1 = transient_buffer.Allocate(2);
  ASSERT_TRUE(a1.has_value());
  auto a2 = transient_buffer.Allocate(3);
  ASSERT_TRUE(a2.has_value());

  // Both allocations were created in this frame and should report valid
  EXPECT_TRUE(a1->IsValid(seq));
  EXPECT_TRUE(a2->IsValid(seq));

  // After moving to next frame the slot is reset, and new allocations should
  // not match the old sequence
  transient_buffer.OnFrameStart(frame::SequenceNumber { 2 }, frame::Slot { 0 });

  EXPECT_FALSE(a1->IsValid(frame::SequenceNumber { 2 }));
  EXPECT_FALSE(a2->IsValid(frame::SequenceNumber { 2 }));
}

} // namespace oxygen::engine::upload
