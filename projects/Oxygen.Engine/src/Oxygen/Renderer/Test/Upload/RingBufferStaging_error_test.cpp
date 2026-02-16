//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Test/Upload/UploadCoordinatorTest.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>

using oxygen::engine::upload::SizeBytes;
using oxygen::engine::upload::UploadError;
using oxygen::frame::SlotCount;

namespace {

constexpr uint32_t kTestAlignment = 256U;
constexpr float kTestSlack = 0.5F;
constexpr SizeBytes kTestAllocationSize { 64 };
constexpr SizeBytes kSmallAllocationSize { 32 };
constexpr uint32_t kSmallTestAlignment = 64U;
constexpr SizeBytes kGrowthPadding { 128 };

// Error and death fixture
class RingBufferStagingErrorTest
  : public oxygen::engine::upload::testing::UploadCoordinatorTest { };

/*!
 CreateBuffer throwing should surface as a staging allocation failure.
*/
NOLINT_TEST_F(RingBufferStagingErrorTest, CreateBuffer_Throws_ReturnsError)
{
  // Arrange: force CreateBuffer to throw
  Gfx().SetThrowOnCreateBuffer(true);

  // Recreate uploader/provider after changing gfx behaviour
  auto& uploader = Uploader();
  auto provider = uploader.CreateRingBufferStaging(
    SlotCount { 1 }, kTestAlignment, kTestSlack);
  SetStagingProvider(provider);
  ASSERT_NE(provider, nullptr);

  // Act
  auto alloc = provider->Allocate(kTestAllocationSize, "throw-test");

  // Assert
  ASSERT_FALSE(alloc.has_value());
  EXPECT_EQ(alloc.error(), UploadError::kStagingAllocFailed);
}

/*!
 If Map returns null, Allocate should fail with kStagingAllocFailed.
*/
NOLINT_TEST_F(RingBufferStagingErrorTest, Map_ReturnsNull_ReturnsError)
{
  // Arrange: force Map to return nullptr
  Gfx().SetFailMap(true);

  // Recreate uploader/provider after changing gfx behaviour
  auto& uploader = Uploader();
  auto provider = uploader.CreateRingBufferStaging(
    SlotCount { 1 }, kTestAlignment, kTestSlack);
  SetStagingProvider(provider);
  ASSERT_NE(provider, nullptr);

  // Act
  auto alloc = provider->Allocate(kTestAllocationSize, "map-null-test");

  // Assert
  ASSERT_FALSE(alloc.has_value());
  EXPECT_EQ(alloc.error(), UploadError::kStagingMapFailed);
}

/*!
 Allocation construction is invalid with null buffer/ptr; ensure checks fire.
*/
NOLINT_TEST_F(RingBufferStagingErrorTest, Allocation_Construct_Invalid_Deaths)
{
  // This is a death-test: attempt to construct Allocation with invalid args.
  // Use EXPECT_DEATH to validate CHECK failures.

  // Note: allocation ctor is OXGN_RNDR_API and enforces CHECKs; craft a
  // minimal scenario to trigger them.
  EXPECT_DEATH(
    {
      // Create a bogus Allocation by invoking constructor with nullptr buffer
      oxygen::engine::upload::StagingProvider::Allocation(nullptr,
        oxygen::engine::upload::OffsetBytes { 0 },
        oxygen::engine::upload::SizeBytes { 1 }, nullptr);
    },
    "");
}

// Edge fixture for capacity and growth tests
class RingBufferStagingEdgeTest
  : public oxygen::engine::upload::testing::UploadCoordinatorTest { };

/*!
 Ensure capacity grows when a large allocation is requested.
*/
NOLINT_TEST_F(RingBufferStagingEdgeTest, EnsureCapacity_GrowsBuffer)
{
  auto& uploader = Uploader();
  auto provider = uploader.CreateRingBufferStaging(
    SlotCount { 1 }, kSmallTestAlignment, kTestSlack);
  ASSERT_NE(provider, nullptr);
  SetStagingProvider(provider);

  // Arrange: small allocation to initialize
  auto a1 = provider->Allocate(kSmallAllocationSize, "init");
  ASSERT_TRUE(a1.has_value());
  const SizeBytes before_size { provider->GetStats().current_buffer_size };

  // Act: allocate a larger size to force growth
  auto a2 = provider->Allocate(before_size + kGrowthPadding, "grow");

  // Assert
  ASSERT_TRUE(a2.has_value());
  EXPECT_GT(provider->GetStats().current_buffer_size, before_size.get());

  // Drain deferred buffer-release callbacks deterministically to avoid
  // teardown-time lifetime coupling between UploadCoordinator and Graphics.
  Gfx().Flush();
}

} // namespace
