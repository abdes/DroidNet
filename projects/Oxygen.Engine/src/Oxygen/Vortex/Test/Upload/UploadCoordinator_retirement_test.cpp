//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Fixtures/UploadCoordinatorTest.h>
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
  ASSERT_LT(completed, ticket->fence.get());

  SimulateFrameStart(frame::Slot { 1U });

  const auto pending = Uploader().IsComplete(*ticket);
  ASSERT_TRUE(pending.has_value());
  EXPECT_FALSE(*pending);
  EXPECT_FALSE(Uploader().TryGetResult(*ticket).has_value());
  EXPECT_EQ(queue->GetCompletedValue(), completed);

  queue->CompleteThrough(std::numeric_limits<std::uint64_t>::max());
  EXPECT_THROW(SimulateFrameStart(frame::Slot { 2U }), std::runtime_error);
  EXPECT_FALSE(Uploader().TryGetResult(*ticket).has_value());

  queue->CompleteThrough(ticket->fence.get());
  SimulateFrameStart(frame::Slot { 2U });

  if (const auto result = Uploader().TryGetResult(*ticket);
    result.has_value()) {
    EXPECT_TRUE(result->success);
    EXPECT_EQ(result->bytes_uploaded, kPayloadBytes);
  } else {
    ADD_FAILURE() << "Completed upload has no result";
  }
}

} // namespace oxygen::vortex::upload::testing
