//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Test/Fixtures/UploadCoordinatorTest.h>
#include <Oxygen/Vortex/Upload/Errors.h>
#include <Oxygen/Vortex/Upload/Types.h>

namespace oxygen::vortex::upload::testing {
namespace {
  constexpr std::size_t kPayloadBytes = 16U;
  constexpr auto kCompletionDelay = std::chrono::milliseconds { 1 };

  class AsyncUploadLifetimeTest : public UploadCoordinatorTest {
  protected:
    auto SetUp() -> void override
    {
      UploadCoordinatorTest::SetUp();
      queue_ = Gfx().GetFakeCommandQueue(graphics::QueueRole::kTransfer);
      ASSERT_NE(queue_, nullptr);
      queue_->SetAutoComplete(false);
    }

    auto VerifyRetirement(const std::weak_ptr<graphics::Buffer>& destination,
      const std::weak_ptr<graphics::Buffer>& staging) -> void
    {
      ASSERT_FALSE(destination.expired());
      ASSERT_FALSE(staging.expired());
      // Completing only the public ticket fence does not retire native uses.
      queue_->CompleteThrough(queue_->GetCurrentValue());
      Gfx().PollCompletedUses();
      EXPECT_FALSE(destination.expired());
      EXPECT_FALSE(staging.expired());
      queue_->CompletePrivateThrough(queue_->submitted_batches);
      Gfx().PollCompletedUses();
      EXPECT_TRUE(destination.expired());
      EXPECT_TRUE(staging.expired());
    }

    std::shared_ptr<vortex::testing::FakeCommandQueue> queue_;
  };
} // namespace

NOLINT_TEST_F(UploadCoordinatorTest, AsyncSubmissionOwnsProducerAndProvider)
{
  auto destination = Gfx().CreateBuffer({
    .size_bytes = kPayloadBytes,
    .memory = graphics::BufferMemory::kDeviceLocal,
  });
  auto source = std::make_unique<std::array<std::byte, kPayloadBytes>>();
  source->fill(std::byte { 42 });
  auto request = UploadRequest {
    .kind = UploadKind::kBuffer,
    .priority = {},
    .debug_name = "Owned async upload",
    .desc
    = UploadBufferDesc { .dst = destination, .size_bytes = kPayloadBytes },
    .subresources = {},
    .data = UploadProducer(
      [storage = std::move(source)](const std::span<std::byte> output) -> bool {
        std::ranges::copy(*storage, output.begin());
        return true;
      }),
  };
  auto provider
    = Uploader().CreateRingBufferStaging(frame::SlotCount { 1U }, 4U);
  const auto provider_lifetime = std::weak_ptr(provider);
  {
    auto operation
      = Uploader().SubmitAsync(std::move(request), std::move(provider));
    EXPECT_FALSE(provider_lifetime.expired());
    EXPECT_FALSE(Gfx().buffer_log_.copy_called);
    auto loop = co::testing::TestEventLoop {};
    loop.Schedule(kCompletionDelay,
      [this] -> void { SimulateFrameStart(frame::Slot { 1U }); });
    const auto result = co::Run(loop, std::move(operation));
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.bytes_uploaded, kPayloadBytes);
  }
  EXPECT_TRUE(provider_lifetime.expired());
  EXPECT_TRUE(Gfx().buffer_log_.copy_called);
}

NOLINT_TEST_F(UploadCoordinatorTest, AsyncBatchOwnsRequestsUntilExecution)
{
  auto destination = Gfx().CreateBuffer({
    .size_bytes = kPayloadBytes,
    .memory = graphics::BufferMemory::kDeviceLocal,
  });
  const auto pixels = std::array<std::byte, kPayloadBytes> {};
  auto requests = std::vector<UploadRequest> {};
  requests.push_back(UploadRequest {
    .kind = UploadKind::kBuffer,
    .priority = {},
    .debug_name = "Owned async batch",
    .desc
    = UploadBufferDesc { .dst = destination, .size_bytes = kPayloadBytes },
    .subresources = {},
    .data = UploadDataView { .bytes = pixels },
  });
  auto provider
    = Uploader().CreateRingBufferStaging(frame::SlotCount { 1U }, 4U);
  auto operation
    = Uploader().SubmitManyAsync(std::move(requests), std::move(provider));
  EXPECT_FALSE(Gfx().buffer_log_.copy_called);
  auto loop = co::testing::TestEventLoop {};
  loop.Schedule(kCompletionDelay,
    [this] -> void { SimulateFrameStart(frame::Slot { 1U }); });
  const auto result = co::Run(loop, std::move(operation));
  ASSERT_EQ(result.size(), 1U);
  EXPECT_TRUE(result.front().success);
  EXPECT_EQ(result.front().bytes_uploaded, kPayloadBytes);
}

NOLINT_TEST_F(
  AsyncUploadLifetimeTest, CancellationRetainsIssuedCopyUntilPrivateCompletion)
{
  auto destination = Gfx().CreateBuffer({
    .size_bytes = kPayloadBytes,
    .memory = graphics::BufferMemory::kDeviceLocal,
  });
  const auto destination_lifetime = std::weak_ptr(destination);
  const auto pixels = std::array<std::byte, kPayloadBytes> {};
  auto request = UploadRequest {
    .kind = UploadKind::kBuffer,
    .priority = {},
    .debug_name = "Canceled owned upload",
    .desc = UploadBufferDesc { .dst = std::move(destination),
      .size_bytes = kPayloadBytes, },
    .subresources = {},
    .data = UploadDataView { .bytes = pixels },
  };
  auto provider
    = Uploader().CreateRingBufferStaging(frame::SlotCount { 1U }, 4U);
  const auto provider_lifetime = std::weak_ptr(provider);
  {
    auto operation
      = Uploader().SubmitAsync(std::move(request), std::move(provider));
    auto loop = co::testing::TestEventLoop {};
    const auto result = co::Run(
      loop, co::AnyOf(std::move(operation), loop.Sleep(kCompletionDelay)));
    EXPECT_FALSE(std::get<0>(result).has_value());
    EXPECT_EQ(loop.Now(), kCompletionDelay);
  }
  EXPECT_TRUE(provider_lifetime.expired());
  EXPECT_EQ(queue_->submitted_batches, 1U);
  VerifyRetirement(destination_lifetime, Gfx().latest_created_buffer_);
}

NOLINT_TEST_F(
  AsyncUploadLifetimeTest, PartialBatchFailureRetainsAlreadyIssuedCopy)
{
  auto destination = Gfx().CreateBuffer({
    .size_bytes = kPayloadBytes,
    .memory = graphics::BufferMemory::kDeviceLocal,
  });
  const auto destination_lifetime = std::weak_ptr(destination);
  const auto pixels = std::array<std::byte, kPayloadBytes> {};
  auto requests = std::vector<UploadRequest> {};
  requests.push_back(UploadRequest {
    .kind = UploadKind::kBuffer,
    .priority = {},
    .debug_name = "Submitted batch prefix",
    .desc = UploadBufferDesc { .dst = std::move(destination),
      .size_bytes = kPayloadBytes, },
    .subresources = {},
    .data = UploadDataView { .bytes = pixels },
  });
  requests.push_back(UploadRequest {
    .kind = UploadKind::kTextureCube,
    .priority = {},
    .debug_name = "Invalid batch tail",
    .desc = UploadTextureDesc {},
    .subresources = {},
    .data = UploadDataView {},
  });
  auto provider
    = Uploader().CreateRingBufferStaging(frame::SlotCount { 1U }, 4U);
  const auto provider_lifetime = std::weak_ptr(provider);
  {
    auto loop = co::testing::TestEventLoop {};
    const auto results = co::Run(loop,
      Uploader().SubmitManyAsync(std::move(requests), std::move(provider)));
    ASSERT_EQ(results.size(), 1U);
    EXPECT_FALSE(results.front().success);
    EXPECT_EQ(results.front().error, UploadError::kInvalidRequest);
  }
  EXPECT_TRUE(provider_lifetime.expired());
  EXPECT_EQ(queue_->submitted_batches, 1U);
  VerifyRetirement(destination_lifetime, Gfx().latest_created_buffer_);
}
} // namespace oxygen::vortex::upload::testing
