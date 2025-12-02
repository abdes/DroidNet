//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

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

NOLINT_TEST_F(TransientStructuredBufferTest,
  HasInvalidBindingAndNoMappedPointerAtConstruction)
{
  // Arrange
  TransientStructuredBuffer transient_buffer(GfxPtr(), Staging(), 64);

  // Act
  auto binding = transient_buffer.GetBinding();

  // Assert
  EXPECT_EQ(binding.srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(binding.stride, 64u);
  EXPECT_EQ(transient_buffer.GetMappedPtr(), nullptr);
}

// Note: earlier tests covered allocate+reset behaviour together with mapped
// pointer persistence and reset idempotency. Removing the redundant test to
// keep the suite concise while preserving coverage.

NOLINT_TEST_F(TransientStructuredBufferTest,
  ReallocateReplacesDescriptorAndProvidesMappedPointer)
{
  // Arrange
  TransientStructuredBuffer transient_buffer(GfxPtr(), Staging(), 64);

  // Act: first allocation
  auto alloc1_result = transient_buffer.Allocate(10);

  // Assert: first allocation succeeded
  ASSERT_TRUE(alloc1_result.has_value())
    << "First Allocate failed: " << alloc1_result.error().message();
  auto srv_initial = transient_buffer.GetBinding().srv;
  EXPECT_NE(srv_initial, kInvalidShaderVisibleIndex);

  // Act: reallocate
  auto alloc2_result = transient_buffer.Allocate(20);

  // Assert: reallocation succeeded and descriptor changed
  ASSERT_TRUE(alloc2_result.has_value())
    << "Reallocate failed: " << alloc2_result.error().message();
  auto srv_realloc = transient_buffer.GetBinding().srv;
  EXPECT_NE(srv_realloc, kInvalidShaderVisibleIndex);
  EXPECT_NE(srv_realloc, srv_initial);

  EXPECT_NE(transient_buffer.GetMappedPtr(), nullptr);
}

NOLINT_TEST_F(TransientStructuredBufferTest, AllocateZeroIsNoOpSuccess)
{
  // Arrange
  TransientStructuredBuffer transient_buffer(GfxPtr(), Staging(), 64);

  // Act
  auto alloc_result_zero = transient_buffer.Allocate(0);

  // Assert: Allocate(0) should be a no-op success
  ASSERT_TRUE(alloc_result_zero.has_value())
    << "Allocate(0) returned error: " << alloc_result_zero.error().message();
  EXPECT_EQ(transient_buffer.GetBinding().srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(transient_buffer.GetMappedPtr(), nullptr);
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

  // Act
  auto alloc_result = transient_buffer.Allocate(10);

  // Assert: mapping error should propagate
  ASSERT_FALSE(alloc_result.has_value());
  EXPECT_EQ(alloc_result.error(),
    oxygen::engine::upload::make_error_code(
      oxygen::engine::upload::UploadError::kStagingMapFailed));
  EXPECT_EQ(transient_buffer.GetBinding().srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(transient_buffer.GetMappedPtr(), nullptr);
}

// Confirm mapped pointer is valid for writes until Reset() is called
NOLINT_TEST_F(
  TransientStructuredBufferTest, MappedPointerWritesPersistUntilReset)
{
  // Arrange
  TransientStructuredBuffer transient_buffer(
    GfxPtr(), Staging(), 8); // 8-byte stride

  // Act
  auto alloc_result = transient_buffer.Allocate(4);

  // Assert: successful allocation and valid mapped pointer
  ASSERT_TRUE(alloc_result.has_value())
    << "Allocate failed: " << alloc_result.error().message();

  auto* mapped = static_cast<uint64_t*>(transient_buffer.GetMappedPtr());
  ASSERT_NE(mapped, nullptr);

  // Arrange/Act: write pattern to first and last element
  mapped[0] = 0xAABBCCDDEEFF0011ull;
  mapped[3] = 0x1122334455667788ull;

  // Assert: reads reflect writes while allocation is active
  EXPECT_EQ(mapped[0], 0xAABBCCDDEEFF0011ull);
  EXPECT_EQ(mapped[3], 0x1122334455667788ull);

  // Act: reset
  transient_buffer.Reset();

  // Assert: mapped pointer becomes unavailable
  EXPECT_EQ(transient_buffer.GetMappedPtr(), nullptr);
}

// Reset can be called multiple times and leaves object in cleared state
NOLINT_TEST_F(
  TransientStructuredBufferTest, ResetIsIdempotentAndLeavesObjectCleared)
{
  // Arrange
  TransientStructuredBuffer transient_buffer(GfxPtr(), Staging(), 64);

  // Act: allocate then reset twice
  ASSERT_TRUE(transient_buffer.Allocate(2).has_value());
  transient_buffer.Reset();

  // Second Reset should be a no-op and must not throw
  EXPECT_NO_THROW(transient_buffer.Reset());

  // Assert: resources released and no mapped pointer
  EXPECT_EQ(transient_buffer.GetBinding().srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(transient_buffer.GetMappedPtr(), nullptr);
}

// After Reset, Allocate must re-create binding and mapped pointer
NOLINT_TEST_F(TransientStructuredBufferTest,
  AllocateAfterResetRecreatesValidBindingAndMappedPointer)
{
  // Arrange
  TransientStructuredBuffer transient_buffer(GfxPtr(), Staging(), 64);

  // Act: initial allocation
  auto r1 = transient_buffer.Allocate(4);
  ASSERT_TRUE(r1.has_value())
    << "Initial allocate failed: " << r1.error().message();
  auto srv1 = transient_buffer.GetBinding().srv;
  EXPECT_NE(srv1, kInvalidShaderVisibleIndex);

  // Act: reset then allocate again
  transient_buffer.Reset();
  auto r2 = transient_buffer.Allocate(4);

  // Assert: second allocation succeeds and provides valid mapping
  ASSERT_TRUE(r2.has_value()) << "Re-allocate failed: " << r2.error().message();
  auto srv2 = transient_buffer.GetBinding().srv;
  EXPECT_NE(srv2, kInvalidShaderVisibleIndex);

  // It's perfectly valid for the descriptor allocator to reuse indices, so we
  // only assert that the binding is valid and mapped pointer is present.
  EXPECT_NE(transient_buffer.GetMappedPtr(), nullptr);
}

} // namespace oxygen::engine::upload
