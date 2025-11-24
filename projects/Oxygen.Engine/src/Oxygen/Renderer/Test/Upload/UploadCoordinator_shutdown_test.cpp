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
#include <Oxygen/Renderer/Test/Upload/UploadCoordinatorTest.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

namespace {

using namespace std::chrono_literals;
using oxygen::engine::upload::SizeBytes;
using oxygen::engine::upload::UploadBufferDesc;
using oxygen::engine::upload::UploadDataView;
using oxygen::engine::upload::UploadError;
using oxygen::engine::upload::UploadKind;
using oxygen::engine::upload::UploadRequest;
using oxygen::engine::upload::UploadResult;
using oxygen::engine::upload::UploadTicket;
using oxygen::engine::upload::testing::UploadCoordinatorTest;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;

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
  uploader.OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
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
  q->QueueSignalCommand(0);

  // Advance the retirable slot so the tracker will erase entries created in
  // this slot (simulate frame cleanup) — entries are now erased but the
  // last_registered_fence is still set which Shutdown must honor.
  uploader.OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  // After a brief delay, signal the queue completion to allow Shutdown to
  // observe progress and finish.
  const auto fence = ticket.fence.get();
  std::thread completion_thread([q, fence]() {
    std::this_thread::sleep_for(20ms);
    q->QueueSignalCommand(fence);
  });

  // Act: call Shutdown which should wait for the fence to be observed
  auto res = uploader.Shutdown(std::chrono::milliseconds { 1000 });

  // Cleanup
  completion_thread.join();

  ASSERT_TRUE(res.has_value()) << "Shutdown did not complete successfully";
}

// Shutdown should return an error if the queue never advances and timeout
// expires.
NOLINT_TEST_F(UploadCoordinatorTest, Shutdown_TimesOutWhenQueueStalls)
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

  uploader.OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 2 });

  auto ticket_res = uploader.Submit(req, Staging());
  ASSERT_TRUE(ticket_res.has_value());

  // Force the transfer queue to remain un-signalled
  auto q
    = GfxPtr()->GetCommandQueue(oxygen::graphics::SingleQueueStrategy().KeyFor(
      oxygen::graphics::QueueRole::kTransfer));
  ASSERT_NE(q, nullptr);
  q->QueueSignalCommand(0);

  // Try shutdown with a very small timeout so it fails deterministically
  auto res = uploader.Shutdown(std::chrono::milliseconds { 5 });
  ASSERT_FALSE(res.has_value())
    << "Expected shutdown to time out but it returned success";
}

}
