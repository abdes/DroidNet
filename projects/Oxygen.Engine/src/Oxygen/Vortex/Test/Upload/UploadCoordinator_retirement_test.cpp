//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <tuple>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/Test/Fixtures/UploadCoordinatorTest.h>
#include <Oxygen/Vortex/Upload/Errors.h>
#include <Oxygen/Vortex/Upload/Types.h>

namespace oxygen::vortex::upload::testing {
namespace {
  constexpr auto kPayloadBytes = std::size_t { 16U };
}

NOLINT_TEST_F(UploadCoordinatorTest, EmptyFrameStartDoesNotDrainUnrelatedWork)
{
  const auto queue = Gfx().GetFakeCommandQueue(graphics::QueueRole::kTransfer);
  ASSERT_NE(queue, nullptr);
  queue->SetAutoComplete(false);
  const auto pending = queue->Signal();
  const auto completed = queue->GetCompletedValue();
  ASSERT_LT(completed, pending);

  SimulateFrameStart(frame::Slot { 0U });

  EXPECT_EQ(queue->GetCompletedValue(), completed);
  EXPECT_EQ(queue->GetCurrentValue(), pending);
}

NOLINT_TEST_F(UploadCoordinatorTest, FrameStartRetiresOnlyCompletedUploads)
{
  const auto queue = Gfx().GetFakeCommandQueue(graphics::QueueRole::kTransfer);
  ASSERT_NE(queue, nullptr);
  queue->SetAutoComplete(false);
  auto provider
    = Uploader().CreateRingBufferStaging(frame::kFramesInFlight, 4U);
  SimulateFrameStart(frame::Slot { 0U });
  auto destination = Gfx().CreateBuffer({
    .size_bytes = kPayloadBytes,
    .memory = graphics::BufferMemory::kDeviceLocal,
  });
  const auto source = std::array<std::byte, kPayloadBytes> {};
  const auto request = UploadRequest {
    .kind = UploadKind::kBuffer,
    .priority = {},
    .debug_name = "Pending upload retirement",
    .desc
    = UploadBufferDesc { .dst = destination, .size_bytes = kPayloadBytes },
    .subresources = {},
    .data = UploadDataView { .bytes = std::span(source) },
  };
  const auto ticket = Uploader().Submit(request, *provider);
  ASSERT_TRUE(ticket.has_value());
  const auto completed = queue->GetCompletedValue();
  ASSERT_LT(completed, ticket->Fence().get());

  SimulateFrameStart(frame::Slot { 1U });
  SimulateFrameStart(frame::Slot { 2U });
  SimulateFrameStart(frame::Slot { 0U });

  EXPECT_FALSE(ticket->TryGetResult().has_value());
  EXPECT_EQ(queue->GetCompletedValue(), completed);

  queue->CompleteThrough(ticket->Fence().get());
  SimulateFrameStart(frame::Slot { 0U });

  if (const auto result = ticket->TryGetResult(); result.has_value()) {
    EXPECT_TRUE(result->success);
    EXPECT_EQ(result->bytes_uploaded, kPayloadBytes);
  } else {
    ADD_FAILURE() << "Completed upload has no result";
  }
}

namespace {
  auto DestroyAfterCompletion(UploadTicket ticket,
    std::unique_ptr<UploadCoordinator>& coordinator) -> co::Co<UploadResult>
  {
    const auto result = co_await ticket.AwaitGpuCompletionAsync();
    coordinator.reset();
    co_return result;
  }
}

NOLINT_TEST_F(UploadCoordinatorTest, ShutdownCanResumeAnOwnerDestroyingConsumer)
{
  for (const bool finish_all : { false, true }) {
    const auto queue
      = Gfx().GetFakeCommandQueue(graphics::QueueRole::kTransfer);
    ASSERT_NE(queue, nullptr);
    queue->SetAutoComplete(false);
    auto coordinator = std::make_unique<UploadCoordinator>(GfxPtr());
    auto provider
      = coordinator->CreateRingBufferStaging(frame::kFramesInFlight, 4U);
    coordinator->OnFrameStart(
      vortex::internal::RendererTagFactory::Get(), frame::Slot { 0 });
    const auto destination = Gfx().CreateBuffer({
      .size_bytes = kPayloadBytes,
      .memory = graphics::BufferMemory::kDeviceLocal,
    });
    const auto source = std::array<std::byte, kPayloadBytes> {};
    const UploadRequest request {
      .kind = UploadKind::kBuffer,
      .priority = {},
      .debug_name = "Reentrant shutdown",
      .desc = UploadBufferDesc { .dst = destination,
        .size_bytes = kPayloadBytes,
        .dst_offset = 0 },
      .subresources = {},
      .data = UploadDataView { .bytes = source },
    };
    const auto first = coordinator->Submit(request, *provider);
    const auto later = coordinator->Submit(request, *provider);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(later.has_value());
    co::testing::TestEventLoop loop;
    loop.Schedule(std::chrono::milliseconds { 1 }, [&]() {
      queue->CompleteThrough(
        finish_all ? later->Fence().get() : first->Fence().get());
      const auto outcome
        = coordinator->Shutdown(std::chrono::milliseconds { 0 });
      EXPECT_EQ(outcome.has_value(), finish_all);
    });
    const auto results = co::Run(loop,
      co::AllOf(DestroyAfterCompletion(*first, coordinator),
        later->AwaitGpuCompletionAsync()));
    EXPECT_FALSE(coordinator);
    EXPECT_TRUE(std::get<0>(results).success);
    EXPECT_EQ(std::get<1>(results).success, finish_all);
    if (!finish_all) {
      EXPECT_EQ(std::get<1>(results).error, UploadError::kTrackerShutdown);
    }
    queue->CompleteThrough(later->Fence().get());
  }
}

NOLINT_TEST_F(UploadCoordinatorTest, DeviceLossClosesPendingUploadResults)
{
  const auto queue = Gfx().GetFakeCommandQueue(graphics::QueueRole::kTransfer);
  ASSERT_NE(queue, nullptr);
  queue->SetAutoComplete(false);
  auto provider
    = Uploader().CreateRingBufferStaging(frame::kFramesInFlight, 4U);
  SimulateFrameStart(frame::Slot { 0 });
  const auto destination = Gfx().CreateBuffer({
    .size_bytes = kPayloadBytes,
    .memory = graphics::BufferMemory::kDeviceLocal,
  });
  const auto source = std::array<std::byte, kPayloadBytes> {};
  const UploadRequest request {
    .kind = UploadKind::kBuffer,
    .priority = {},
    .debug_name = "Device loss",
    .desc = UploadBufferDesc { .dst = destination,
      .size_bytes = kPayloadBytes,
      .dst_offset = 0 },
    .subresources = {},
    .data = UploadDataView { .bytes = source },
  };
  const auto ticket = Uploader().Submit(request, *provider);
  ASSERT_TRUE(ticket.has_value());
  queue->CompleteThrough(std::numeric_limits<std::uint64_t>::max());
  EXPECT_THROW(SimulateFrameStart(frame::Slot { 1 }), std::runtime_error);
  EXPECT_EQ(ticket->Await().error, UploadError::kDeviceLost);
}

} // namespace oxygen::vortex::upload::testing
