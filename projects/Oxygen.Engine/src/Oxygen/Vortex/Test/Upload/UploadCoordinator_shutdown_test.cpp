//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <thread>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Vortex/Test/Fixtures/UploadCoordinatorTest.h>
#include <Oxygen/Vortex/Upload/Types.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>

namespace {

using namespace std::chrono_literals;
using oxygen::SizeBytes;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::vortex::upload::UploadBufferDesc;
using oxygen::vortex::upload::UploadDataView;
using oxygen::vortex::upload::UploadError;
using oxygen::vortex::upload::UploadKind;
using oxygen::vortex::upload::UploadRequest;
using oxygen::vortex::upload::UploadResult;
using oxygen::vortex::upload::UploadTicket;
using oxygen::vortex::upload::testing::UploadCoordinatorTest;

// Ensure that Shutdown succeeds when there are no outstanding uploads.
NOLINT_TEST_F(UploadCoordinatorTest, Shutdown_NoUploads_ReturnsImmediately)
{
  auto& uploader = Uploader();
  auto res = uploader.Shutdown(std::chrono::milliseconds { 100 });
  ASSERT_TRUE(res.has_value()) << "Shutdown failed unexpectedly";
}

// Shutdown should wait for outstanding recorded uploads to complete.
NOLINT_TEST_F(UploadCoordinatorTest, Shutdown_WaitsForOutstandingUploads)
{
  // Arrange: create destination buffer and upload one small blob
  BufferDesc dst_desc { .size_bytes = 256,
    .usage = BufferUsage::kVertex,
    .memory = BufferMemory::kDeviceLocal };
  auto dst = GfxPtr()->CreateBuffer(dst_desc);

  std::array<std::byte, 64> data {};
  UploadRequest req { .kind = UploadKind::kBuffer,
    .priority = {},
    .debug_name = "shutdown-test",
    .desc = UploadBufferDesc { .dst = dst, .size_bytes = 64, .dst_offset = 0 },
    .data = UploadDataView { .bytes = std::span<const std::byte>(data) } };

  auto& uploader = Uploader();

  // Ensure frame slot set so ticket has a creation slot
  uploader.OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  auto ticket_res = uploader.Submit(req, Staging());
  ASSERT_TRUE(ticket_res.has_value());
  const auto ticket = *ticket_res;

  // Simulate that the transfer queue did not complete yet by clearing the
  // completed value. Then arrange for it to complete shortly from another
  // thread so Shutdown can observe progress and return.
  auto q
    = GfxPtr()->GetCommandQueue(oxygen::graphics::SingleQueueStrategy().KeyFor(
      oxygen::graphics::QueueRole::kTransfer));
  ASSERT_NE(q, nullptr);

  // Clear completion to simulate work in-flight
  q->Signal(0);

  // Advance the retirable slot so the tracker will erase entries created in
  // this slot (simulate frame cleanup) — entries are now erased but the
  // last_registered_fence is still set which Shutdown must honor.
  uploader.OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  // After a brief delay, signal the queue completion to allow Shutdown to
  // observe progress and finish.
  const auto fence = ticket.fence.get();
  std::thread completion_thread([q, fence]() {
    std::this_thread::sleep_for(20ms);
    q->Signal(fence);
  });

  // Act: call Shutdown which should wait for the fence to be observed
  auto res = uploader.Shutdown(std::chrono::milliseconds { 1000 });

  // Cleanup
  completion_thread.join();

  ASSERT_TRUE(res.has_value()) << "Shutdown did not complete successfully";
}

// With the current tracker cleanup semantics, shutdown may still complete
// after frame-start retirement even if the fake queue is manually reset.
NOLINT_TEST_F(UploadCoordinatorTest, ShutdownSucceedsAfterFrameCleanup)
{
  BufferDesc dst_desc { .size_bytes = 256,
    .usage = BufferUsage::kVertex,
    .memory = BufferMemory::kDeviceLocal };
  auto dst = GfxPtr()->CreateBuffer(dst_desc);

  std::array<std::byte, 64> data {};
  UploadRequest req { .kind = UploadKind::kBuffer,
    .priority = {},
    .debug_name = "timeout-test",
    .desc = UploadBufferDesc { .dst = dst, .size_bytes = 64, .dst_offset = 0 },
    .data = UploadDataView { .bytes = std::span<const std::byte>(data) } };

  auto& uploader = Uploader();

  uploader.OnFrameStart(oxygen::vortex::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });

  auto ticket_res = uploader.Submit(req, Staging());
  ASSERT_TRUE(ticket_res.has_value());

  // Force the transfer queue to appear stale in the fake backend.
  auto q
    = GfxPtr()->GetCommandQueue(oxygen::graphics::SingleQueueStrategy().KeyFor(
      oxygen::graphics::QueueRole::kTransfer));
  ASSERT_NE(q, nullptr);
  q->Signal(0);

  // Current Vortex semantics retire any tracked work that was already
  // frame-cleaned, so shutdown still succeeds in this fake-backend scenario.
  auto res = uploader.Shutdown(std::chrono::milliseconds { 5 });
  ASSERT_TRUE(res.has_value())
    << "Expected shutdown to succeed after frame cleanup";
}

}
