//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedLogCapture.h>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/ReadbackErrors.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Direct3D12/Test/Fixtures/ReadbackTestFixture.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>

namespace {

using oxygen::SizeBytes;
using oxygen::co::Co;
using oxygen::co::Run;
using oxygen::co::testing::TestEventLoop;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferRange;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::GpuBufferReadback;
using oxygen::graphics::MappedBufferReadback;
using oxygen::graphics::ReadbackError;
using oxygen::graphics::ReadbackState;
using oxygen::graphics::ReadbackTicket;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::d3d12::testing::ReadbackTestFixture;
using oxygen::graphics::d3d12::testing::TransferQueueReadbackTestFixture;
using oxygen::testing::ScopedLogCapture;

auto MakePatternBytes(const size_t size, const uint8_t seed = 0x10)
  -> std::vector<std::byte>
{
  std::vector<std::byte> bytes(size);
  for (size_t index = 0; index < size; ++index) {
    bytes[index] = static_cast<std::byte>(
      static_cast<uint8_t>(seed + static_cast<uint8_t>(index * 3U + 1U)));
  }
  return bytes;
}

auto SliceBytes(const std::vector<std::byte>& bytes, const size_t offset,
  const size_t size) -> std::vector<std::byte>
{
  return {
    bytes.begin() + static_cast<std::ptrdiff_t>(offset),
    bytes.begin() + static_cast<std::ptrdiff_t>(offset + size),
  };
}

class BufferReadbackTestBase : public ReadbackTestFixture {
protected:
  auto CreateInitializedDeviceBuffer(const std::vector<std::byte>& bytes,
    std::string_view debug_name,
    const oxygen::graphics::QueueRole role
    = oxygen::graphics::QueueRole::kGraphics) -> std::shared_ptr<Buffer>
  {
    auto upload = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = bytes.size(),
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kUpload,
      .debug_name = std::string(debug_name) + "Upload",
    });
    auto device = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = bytes.size(),
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kDeviceLocal,
      .debug_name = std::string(debug_name),
    });

    upload->Update(bytes.data(), bytes.size(), 0);

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + "Init", role);
      CHECK_NOTNULL_F(recorder.get());
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, device, ResourceStates::kCommon);
      recorder->RequireResourceState(*upload, ResourceStates::kCopySource);
      recorder->RequireResourceState(*device, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(*device, 0, *upload, 0, bytes.size());
    }

    WaitForQueueIdle(role);
    return device;
  }

  auto CreateBufferReadback(std::string_view debug_name = "buffer-readback")
    -> std::shared_ptr<GpuBufferReadback>
  {
    auto readback = GetReadbackManager()->CreateBufferReadback(debug_name);
    CHECK_NOTNULL_F(readback.get());
    return readback;
  }

  auto EnqueueReadback(std::shared_ptr<GpuBufferReadback> readback,
    const std::shared_ptr<Buffer>& source, const BufferRange range,
    std::string_view command_list_name,
    const oxygen::graphics::QueueRole role
    = oxygen::graphics::QueueRole::kGraphics,
    const bool immediate_submission = true) -> ReadbackTicket
  {
    auto recorder
      = AcquireRecorder(command_list_name, role, immediate_submission);
    CHECK_NOTNULL_F(recorder.get());
    EnsureTracked(*recorder, source, ResourceStates::kCopyDest);

    const auto ticket = readback->EnqueueCopy(*recorder, *source, range);
    CHECK_F(ticket.has_value(), "Buffer readback enqueue failed");
    return *ticket;
  }

  static auto CopyMappedBytes(const MappedBufferReadback& mapped)
    -> std::vector<std::byte>
  {
    const auto bytes = mapped.Bytes();
    return { bytes.begin(), bytes.end() };
  }
};

class BufferReadbackCreationTest : public BufferReadbackTestBase { };
class BufferReadbackSubmissionTest : public BufferReadbackTestBase { };
class BufferReadbackMappingTest : public BufferReadbackTestBase { };
class BufferReadbackLifecycleTest : public BufferReadbackTestBase { };
class BufferReadbackManagerTest : public BufferReadbackTestBase { };
class BufferReadbackFrameLifecycleTest : public BufferReadbackTestBase { };
class BufferReadbackCoroutineTest : public BufferReadbackTestBase { };
class BufferReadbackShutdownTest : public BufferReadbackTestBase { };

class TransferQueueBufferReadbackTest
  : public TransferQueueReadbackTestFixture {
protected:
  auto CreateInitializedDeviceBuffer(const std::vector<std::byte>& bytes,
    std::string_view debug_name) -> std::shared_ptr<Buffer>
  {
    auto upload = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = bytes.size(),
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kUpload,
      .debug_name = std::string(debug_name) + "Upload",
    });
    auto device = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = bytes.size(),
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kDeviceLocal,
      .debug_name = std::string(debug_name),
    });

    upload->Update(bytes.data(), bytes.size(), 0);

    {
      auto recorder = AcquireRecorder(std::string(debug_name) + "Init",
        oxygen::graphics::QueueRole::kGraphics);
      CHECK_NOTNULL_F(recorder.get());
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, device, ResourceStates::kCommon);
      recorder->RequireResourceState(*upload, ResourceStates::kCopySource);
      recorder->RequireResourceState(*device, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(*device, 0, *upload, 0, bytes.size());
    }

    WaitForQueueIdle(oxygen::graphics::QueueRole::kGraphics);
    return device;
  }

  auto CreateBufferReadback(std::string_view debug_name = "buffer-readback")
    -> std::shared_ptr<GpuBufferReadback>
  {
    auto readback = GetReadbackManager()->CreateBufferReadback(debug_name);
    CHECK_NOTNULL_F(readback.get());
    return readback;
  }
};

NOLINT_TEST_F(BufferReadbackCreationTest, NewBufferReadbackStartsIdle)
{
  auto readback = CreateBufferReadback();

  EXPECT_EQ(readback->GetState(), ReadbackState::kIdle);
  EXPECT_FALSE(readback->Ticket().has_value());

  const auto ready = readback->IsReady();
  ASSERT_TRUE(ready.has_value());
  EXPECT_FALSE(*ready);
}

NOLINT_TEST_F(BufferReadbackCreationTest, MapNowBeforeEnqueueReturnsNotReady)
{
  auto readback = CreateBufferReadback();

  const auto mapped = readback->MapNow();
  ASSERT_FALSE(mapped.has_value());
  EXPECT_EQ(mapped.error(), ReadbackError::kNotReady);
}

NOLINT_TEST_F(BufferReadbackSubmissionTest, EnqueueCopyReturnsPendingTicket)
{
  const auto source_bytes = MakePatternBytes(64);
  auto source = CreateInitializedDeviceBuffer(source_bytes, "pending-source");
  auto readback = CreateBufferReadback("pending-readback");

  const auto ticket = EnqueueReadback(readback, source, BufferRange { 8, 16 },
    "buffer-readback-pending", oxygen::graphics::QueueRole::kGraphics, false);

  EXPECT_EQ(readback->GetState(), ReadbackState::kPending);
  ASSERT_TRUE(readback->Ticket().has_value());
  EXPECT_EQ(readback->Ticket()->id.get(), ticket.id.get());
  EXPECT_EQ(readback->Ticket()->fence.get(), ticket.fence.get());
  EXPECT_GT(ticket.fence.get(), 0U);
}

NOLINT_TEST_F(
  BufferReadbackSubmissionTest, EnqueueCopyRejectsInvalidBufferRange)
{
  const auto source_bytes = MakePatternBytes(32);
  auto source
    = CreateInitializedDeviceBuffer(source_bytes, "invalid-range-source");
  auto readback = CreateBufferReadback("invalid-range-readback");

  auto recorder = AcquireRecorder("buffer-readback-invalid-range",
    oxygen::graphics::QueueRole::kGraphics, false);
  CHECK_NOTNULL_F(recorder.get());
  EnsureTracked(*recorder, source, ResourceStates::kCopyDest);

  const auto ticket
    = readback->EnqueueCopy(*recorder, *source, BufferRange { 32, 4 });
  ASSERT_FALSE(ticket.has_value());
  EXPECT_EQ(ticket.error(), ReadbackError::kInvalidArgument);
  EXPECT_EQ(readback->GetState(), ReadbackState::kIdle);
  EXPECT_FALSE(readback->Ticket().has_value());
}

NOLINT_TEST_F(BufferReadbackSubmissionTest, IsReadyIsFalseBeforeFenceCompletion)
{
  const auto source_bytes = MakePatternBytes(48);
  auto source = CreateInitializedDeviceBuffer(source_bytes, "not-ready-source");
  auto readback = CreateBufferReadback("not-ready-readback");

  EnqueueReadback(readback, source, BufferRange { 4, 20 },
    "buffer-readback-not-ready", oxygen::graphics::QueueRole::kGraphics, false);

  const auto ready = readback->IsReady();
  ASSERT_TRUE(ready.has_value());
  EXPECT_FALSE(*ready);
  EXPECT_EQ(readback->GetState(), ReadbackState::kPending);
}

NOLINT_TEST_F(BufferReadbackSubmissionTest, SecondEnqueueWhilePendingFails)
{
  const auto source_bytes = MakePatternBytes(40);
  auto source
    = CreateInitializedDeviceBuffer(source_bytes, "double-enqueue-source");
  auto readback = CreateBufferReadback("double-enqueue-readback");

  EnqueueReadback(readback, source, BufferRange { 0, 16 },
    "buffer-readback-first-enqueue", oxygen::graphics::QueueRole::kGraphics,
    false);

  auto recorder = AcquireRecorder("buffer-readback-second-enqueue",
    oxygen::graphics::QueueRole::kGraphics, false);
  CHECK_NOTNULL_F(recorder.get());
  EnsureTracked(*recorder, source, ResourceStates::kCopyDest);

  const auto second
    = readback->EnqueueCopy(*recorder, *source, BufferRange { 8, 8 });
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), ReadbackError::kAlreadyPending);
}

NOLINT_TEST_F(BufferReadbackMappingTest, IsReadyBecomesTrueAfterFenceCompletion)
{
  const auto source_bytes = MakePatternBytes(56);
  auto source = CreateInitializedDeviceBuffer(source_bytes, "ready-source");
  auto readback = CreateBufferReadback("ready-readback");

  EnqueueReadback(
    readback, source, BufferRange { 12, 24 }, "buffer-readback-ready");
  WaitForQueueIdle();

  const auto ready = readback->IsReady();
  ASSERT_TRUE(ready.has_value());
  EXPECT_TRUE(*ready);
  EXPECT_EQ(readback->GetState(), ReadbackState::kReady);
}

NOLINT_TEST_F(BufferReadbackMappingTest, TryMapFailsWhilePending)
{
  const auto source_bytes = MakePatternBytes(56);
  auto source
    = CreateInitializedDeviceBuffer(source_bytes, "pending-map-source");
  auto readback = CreateBufferReadback("pending-map-readback");

  EnqueueReadback(readback, source, BufferRange { 6, 18 },
    "buffer-readback-pending-map", oxygen::graphics::QueueRole::kGraphics,
    false);

  const auto mapped = readback->TryMap();
  ASSERT_FALSE(mapped.has_value());
  EXPECT_EQ(mapped.error(), ReadbackError::kNotReady);
}

NOLINT_TEST_F(
  BufferReadbackMappingTest, TryMapReturnsExpectedBytesAfterCompletion)
{
  const auto source_bytes = MakePatternBytes(64);
  auto source = CreateInitializedDeviceBuffer(source_bytes, "try-map-source");
  auto readback = CreateBufferReadback("try-map-readback");

  constexpr auto kOffset = 11ULL;
  constexpr auto kSize = 21ULL;
  EnqueueReadback(readback, source, BufferRange { kOffset, kSize },
    "buffer-readback-try-map");
  WaitForQueueIdle();

  const auto mapped = readback->TryMap();
  ASSERT_TRUE(mapped.has_value());
  EXPECT_EQ(CopyMappedBytes(*mapped),
    SliceBytes(
      source_bytes, static_cast<size_t>(kOffset), static_cast<size_t>(kSize)));
}

NOLINT_TEST_F(BufferReadbackMappingTest, MapNowReturnsExpectedBytes)
{
  const auto source_bytes = MakePatternBytes(72);
  auto source = CreateInitializedDeviceBuffer(source_bytes, "map-now-source");
  auto readback = CreateBufferReadback("map-now-readback");

  constexpr auto kOffset = 9ULL;
  constexpr auto kSize = 28ULL;
  EnqueueReadback(readback, source, BufferRange { kOffset, kSize },
    "buffer-readback-map-now");

  const auto mapped = readback->MapNow();
  ASSERT_TRUE(mapped.has_value());
  EXPECT_EQ(CopyMappedBytes(*mapped),
    SliceBytes(
      source_bytes, static_cast<size_t>(kOffset), static_cast<size_t>(kSize)));
  EXPECT_EQ(readback->GetState(), ReadbackState::kMapped);
}

NOLINT_TEST_F(BufferReadbackMappingTest, TryMapRejectsSecondActiveMap)
{
  const auto source_bytes = MakePatternBytes(36);
  auto source
    = CreateInitializedDeviceBuffer(source_bytes, "active-map-source");
  auto readback = CreateBufferReadback("active-map-readback");

  EnqueueReadback(
    readback, source, BufferRange { 4, 12 }, "buffer-readback-active-map");
  WaitForQueueIdle();

  auto first = readback->TryMap();
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(readback->GetState(), ReadbackState::kMapped);

  const auto second = readback->TryMap();
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), ReadbackError::kAlreadyMapped);

  first = MappedBufferReadback {};
  EXPECT_EQ(readback->GetState(), ReadbackState::kReady);

  const auto third = readback->TryMap();
  ASSERT_TRUE(third.has_value());
}

NOLINT_TEST_F(BufferReadbackMappingTest,
  MappedViewMoveKeepsReadbackMappedUntilFinalOwnerReleases)
{
  const auto source_bytes = MakePatternBytes(48, 0x44);
  auto source = CreateInitializedDeviceBuffer(source_bytes, "moved-map-source");
  auto readback = CreateBufferReadback("moved-map-readback");

  EnqueueReadback(
    readback, source, BufferRange { 10, 18 }, "buffer-readback-moved-map");
  WaitForQueueIdle();

  auto mapped = readback->TryMap();
  ASSERT_TRUE(mapped.has_value());

  auto moved = std::move(*mapped);
  EXPECT_EQ(readback->GetState(), ReadbackState::kMapped);
  EXPECT_EQ(CopyMappedBytes(moved), SliceBytes(source_bytes, 10, 18));

  const auto second = readback->TryMap();
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), ReadbackError::kAlreadyMapped);

  moved = MappedBufferReadback {};
  EXPECT_EQ(readback->GetState(), ReadbackState::kReady);
}

NOLINT_TEST_F(
  BufferReadbackLifecycleTest, CancelPendingReadbackTransitionsToCancelled)
{
  const auto source_bytes = MakePatternBytes(40);
  auto source
    = CreateInitializedDeviceBuffer(source_bytes, "cancel-pending-source");
  auto readback = CreateBufferReadback("cancel-pending-readback");

  const auto ticket = EnqueueReadback(readback, source, BufferRange { 0, 24 },
    "buffer-readback-cancel-pending", oxygen::graphics::QueueRole::kGraphics,
    false);

  const auto cancelled = readback->Cancel();
  ASSERT_TRUE(cancelled.has_value());
  EXPECT_TRUE(*cancelled);
  EXPECT_EQ(readback->GetState(), ReadbackState::kCancelled);
  ASSERT_TRUE(readback->Ticket().has_value());
  EXPECT_EQ(readback->Ticket()->id.get(), ticket.id.get());

  const auto ready = readback->IsReady();
  ASSERT_FALSE(ready.has_value());
  EXPECT_EQ(ready.error(), ReadbackError::kCancelled);

  const auto mapped = readback->TryMap();
  ASSERT_FALSE(mapped.has_value());
  EXPECT_EQ(mapped.error(), ReadbackError::kCancelled);
}

NOLINT_TEST_F(BufferReadbackLifecycleTest, CancelCompletedReadbackReturnsFalse)
{
  const auto source_bytes = MakePatternBytes(44);
  auto source
    = CreateInitializedDeviceBuffer(source_bytes, "cancel-complete-source");
  auto readback = CreateBufferReadback("cancel-complete-readback");

  EnqueueReadback(
    readback, source, BufferRange { 5, 16 }, "buffer-readback-cancel-complete");
  WaitForQueueIdle();
  const auto ready = readback->IsReady();
  ASSERT_TRUE(ready.has_value());
  EXPECT_TRUE(*ready);

  const auto cancelled = readback->Cancel();
  ASSERT_TRUE(cancelled.has_value());
  EXPECT_FALSE(*cancelled);
  EXPECT_EQ(readback->GetState(), ReadbackState::kReady);
}

NOLINT_TEST_F(
  BufferReadbackLifecycleTest, ResetAfterCompletionReturnsObjectToIdle)
{
  const auto source_bytes = MakePatternBytes(52);
  auto source
    = CreateInitializedDeviceBuffer(source_bytes, "reset-complete-source");
  auto readback = CreateBufferReadback("reset-complete-readback");

  EnqueueReadback(
    readback, source, BufferRange { 7, 19 }, "buffer-readback-reset-complete");
  WaitForQueueIdle();

  {
    const auto mapped = readback->TryMap();
    ASSERT_TRUE(mapped.has_value());
  }
  EXPECT_EQ(readback->GetState(), ReadbackState::kReady);

  readback->Reset();
  EXPECT_EQ(readback->GetState(), ReadbackState::kIdle);
  EXPECT_FALSE(readback->Ticket().has_value());

  const auto ready = readback->IsReady();
  ASSERT_TRUE(ready.has_value());
  EXPECT_FALSE(*ready);
}

NOLINT_TEST_F(
  BufferReadbackLifecycleTest, ResetAfterCancellationReturnsObjectToIdle)
{
  const auto source_bytes = MakePatternBytes(60);
  auto source
    = CreateInitializedDeviceBuffer(source_bytes, "reset-cancel-source");
  auto readback = CreateBufferReadback("reset-cancel-readback");

  EnqueueReadback(readback, source, BufferRange { 3, 17 },
    "buffer-readback-reset-cancel", oxygen::graphics::QueueRole::kGraphics,
    false);
  const auto cancelled = readback->Cancel();
  ASSERT_TRUE(cancelled.has_value());
  EXPECT_TRUE(*cancelled);

  readback->Reset();
  EXPECT_EQ(readback->GetState(), ReadbackState::kIdle);
  EXPECT_FALSE(readback->Ticket().has_value());
}

NOLINT_TEST_F(
  BufferReadbackLifecycleTest, ReusableReadbackSupportsSequentialCycles)
{
  const auto first_bytes = MakePatternBytes(48, 0x20);
  const auto second_bytes = MakePatternBytes(48, 0x70);
  auto first_source
    = CreateInitializedDeviceBuffer(first_bytes, "reuse-first-source");
  auto second_source
    = CreateInitializedDeviceBuffer(second_bytes, "reuse-second-source");
  auto readback = CreateBufferReadback("reuse-readback");

  EnqueueReadback(readback, first_source, BufferRange { 8, 16 },
    "buffer-readback-reuse-first");
  WaitForQueueIdle();
  {
    const auto mapped = readback->TryMap();
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(CopyMappedBytes(*mapped), SliceBytes(first_bytes, 8, 16));
  }
  readback->Reset();

  EnqueueReadback(readback, second_source, BufferRange { 4, 20 },
    "buffer-readback-reuse-second");
  WaitForQueueIdle();
  {
    const auto mapped = readback->TryMap();
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(CopyMappedBytes(*mapped), SliceBytes(second_bytes, 4, 20));
  }
}

NOLINT_TEST_F(BufferReadbackLifecycleTest,
  ResetAfterCompletionReleasesReadbackOwnedStagingRegistration)
{
  const auto source_bytes = MakePatternBytes(96, 0x5A);
  auto source
    = CreateInitializedDeviceBuffer(source_bytes, "registry-cleanup-source");
  auto readback = CreateBufferReadback("registry-cleanup-readback");

  auto& registry = Backend().GetResourceRegistry();
  const auto baseline_count = registry.GetRegisteredResourceCount();

  EnqueueReadback(readback, source, BufferRange { 8, 40 },
    "buffer-readback-registry-cleanup");
  const auto mapped = readback->MapNow();
  ASSERT_TRUE(mapped.has_value());

  const auto during_readback_count = registry.GetRegisteredResourceCount();
  EXPECT_GT(during_readback_count, baseline_count);

  readback->Reset();

  EXPECT_EQ(readback->GetState(), ReadbackState::kIdle);
  EXPECT_EQ(registry.GetRegisteredResourceCount(), baseline_count);
}

NOLINT_TEST_F(BufferReadbackManagerTest,
  ManagerAwaitCompletesTicketProducedByBufferReadback)
{
  const auto source_bytes = MakePatternBytes(68);
  auto source = CreateInitializedDeviceBuffer(source_bytes, "await-source");
  auto readback = CreateBufferReadback("await-readback");

  const auto ticket = EnqueueReadback(
    readback, source, BufferRange { 10, 18 }, "buffer-readback-await");

  const auto result = AwaitReadback(ticket);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->ticket.id.get(), ticket.id.get());
  EXPECT_EQ(result->ticket.fence.get(), ticket.fence.get());
  EXPECT_EQ(result->bytes_copied.get(), 18U);
  EXPECT_FALSE(result->error.has_value());

  const auto ready = readback->IsReady();
  ASSERT_TRUE(ready.has_value());
  EXPECT_TRUE(*ready);
}

NOLINT_TEST_F(BufferReadbackManagerTest,
  ManagerAwaitWarnsAndReturnsWouldDeadlockWhenTicketSignalWasNeverSubmitted)
{
  const auto source_bytes = MakePatternBytes(52, 0x91);
  auto source
    = CreateInitializedDeviceBuffer(source_bytes, "await-deadlock-source");
  auto readback = CreateBufferReadback("await-deadlock-readback");

  const auto ticket = EnqueueReadback(readback, source, BufferRange { 8, 20 },
    "buffer-readback-await-deadlock", oxygen::graphics::QueueRole::kGraphics,
    false);

  ScopedLogCapture capture { "D3D12ReadbackAwaitWouldDeadlock",
    loguru::Verbosity_WARNING };
  const auto awaited = AwaitReadback(ticket);

  ASSERT_FALSE(awaited.has_value());
  EXPECT_EQ(awaited.error(), ReadbackError::kWouldDeadlock);
  EXPECT_TRUE(capture.Contains("would deadlock"));
  EXPECT_TRUE(
    capture.Contains("Submit the command recorder before awaiting the ticket"));

  const auto cancelled = CancelReadback(ticket);
  ASSERT_TRUE(cancelled.has_value());
  EXPECT_TRUE(*cancelled);
}

NOLINT_TEST_F(BufferReadbackManagerTest,
  ManagerAwaitWarnsAndReturnsShutdownWhenManagerWasAlreadyShutDown)
{
  GetReadbackManager()->OnFrameStart(oxygen::frame::Slot { 0 });

  const auto source_bytes = MakePatternBytes(48, 0xA3);
  auto source
    = CreateInitializedDeviceBuffer(source_bytes, "await-shutdown-source");
  auto readback = CreateBufferReadback("await-shutdown-readback");

  const auto ticket = EnqueueReadback(readback, source, BufferRange { 6, 18 },
    "buffer-readback-await-shutdown", oxygen::graphics::QueueRole::kGraphics,
    false);

  const auto shutdown_result
    = GetReadbackManager()->Shutdown(std::chrono::milliseconds { 0 });
  ASSERT_FALSE(shutdown_result.has_value());
  EXPECT_EQ(shutdown_result.error(), ReadbackError::kBackendFailure);

  ScopedLogCapture capture { "D3D12ReadbackAwaitShutdown",
    loguru::Verbosity_WARNING };
  const auto awaited = AwaitReadback(ticket);

  ASSERT_FALSE(awaited.has_value());
  EXPECT_EQ(awaited.error(), ReadbackError::kShutdown);
  EXPECT_TRUE(capture.Contains("shutting down"));

  const auto cancelled = CancelReadback(ticket);
  ASSERT_TRUE(cancelled.has_value());
  EXPECT_TRUE(*cancelled);
}

NOLINT_TEST_F(BufferReadbackFrameLifecycleTest,
  OnFrameStartRetiresCompletedTicketWhileMappedBytesStayValid)
{
  GetReadbackManager()->OnFrameStart(oxygen::frame::Slot { 0 });

  const auto source_bytes = MakePatternBytes(64, 0x29);
  auto source
    = CreateInitializedDeviceBuffer(source_bytes, "retire-ticket-source");
  auto readback = CreateBufferReadback("retire-ticket-readback");

  const auto ticket = EnqueueReadback(
    readback, source, BufferRange { 12, 20 }, "buffer-readback-retire-ticket");
  WaitForQueueIdle();

  auto mapped = readback->TryMap();
  ASSERT_TRUE(mapped.has_value());
  EXPECT_EQ(CopyMappedBytes(*mapped), SliceBytes(source_bytes, 12, 20));

  GetReadbackManager()->OnFrameStart(oxygen::frame::Slot { 0 });

  EXPECT_EQ(CopyMappedBytes(*mapped), SliceBytes(source_bytes, 12, 20));

  const auto awaited = AwaitReadback(ticket);
  ASSERT_FALSE(awaited.has_value());
  EXPECT_EQ(awaited.error(), ReadbackError::kTicketNotFound);

  mapped = MappedBufferReadback {};
  EXPECT_EQ(readback->GetState(), ReadbackState::kReady);
}

NOLINT_TEST_F(BufferReadbackCoroutineTest,
  AwaitAsyncCompletesWhenFramePumpMarksTicketComplete)
{
  const auto source_bytes = MakePatternBytes(72, 0x51);
  auto source
    = CreateInitializedDeviceBuffer(source_bytes, "await-async-source");
  auto readback = CreateBufferReadback("await-async-readback");

  const auto ticket = EnqueueReadback(
    readback, source, BufferRange { 14, 24 }, "buffer-readback-await-async");

  TestEventLoop loop;
  bool resumed = false;
  std::jthread completion_pump([this, readback] {
    WaitForQueueIdle();
    const auto ready = readback->IsReady();
    CHECK_F(ready.has_value() && *ready, "Readback should become ready");
  });

  oxygen::co::Run(loop, [&]() -> Co<> {
    co_await GetReadbackManager()->AwaitAsync(ticket);
    resumed = true;
  });

  completion_pump.join();
  EXPECT_TRUE(resumed);
  const auto ready = readback->IsReady();
  ASSERT_TRUE(ready.has_value());
  EXPECT_TRUE(*ready);
}

NOLINT_TEST_F(BufferReadbackCoroutineTest,
  AwaitAsyncReturnsImmediatelyAfterCompletionWasPumped)
{
  GetReadbackManager()->OnFrameStart(oxygen::frame::Slot { 0 });

  const auto source_bytes = MakePatternBytes(56, 0x66);
  auto source = CreateInitializedDeviceBuffer(
    source_bytes, "await-async-complete-source");
  auto readback = CreateBufferReadback("await-async-complete-readback");

  const auto ticket = EnqueueReadback(readback, source, BufferRange { 8, 22 },
    "buffer-readback-await-async-complete");
  WaitForQueueIdle();
  GetReadbackManager()->OnFrameStart(oxygen::frame::Slot { 1 });

  TestEventLoop loop;
  bool resumed = false;
  oxygen::co::Run(loop, [&]() -> Co<> {
    co_await GetReadbackManager()->AwaitAsync(ticket);
    resumed = true;
  });

  EXPECT_TRUE(resumed);
}

NOLINT_TEST_F(BufferReadbackShutdownTest,
  ShutdownReturnsBackendFailureWhileDeferredSubmissionNeverSignals)
{
  GetReadbackManager()->OnFrameStart(oxygen::frame::Slot { 0 });

  const auto source_bytes = MakePatternBytes(40, 0x77);
  auto source
    = CreateInitializedDeviceBuffer(source_bytes, "shutdown-pending-source");
  auto readback = CreateBufferReadback("shutdown-pending-readback");

  EnqueueReadback(readback, source, BufferRange { 4, 20 },
    "buffer-readback-shutdown-pending", oxygen::graphics::QueueRole::kGraphics,
    false);

  const auto shutdown_result
    = GetReadbackManager()->Shutdown(std::chrono::milliseconds { 0 });
  ASSERT_FALSE(shutdown_result.has_value());
  EXPECT_EQ(shutdown_result.error(), ReadbackError::kBackendFailure);
  EXPECT_EQ(readback->GetState(), ReadbackState::kPending);
}

NOLINT_TEST_F(
  TransferQueueBufferReadbackTest, TransferQueueReadbackCompletesAndMapsBytes)
{
  const auto source_bytes = MakePatternBytes(40, 0x33);
  auto source = CreateInitializedDeviceBuffer(source_bytes, "transfer-source");
  auto readback = CreateBufferReadback("transfer-readback");

  std::expected<ReadbackTicket, ReadbackError> ticket;
  {
    auto recorder = AcquireRecorder(
      "buffer-readback-transfer", oxygen::graphics::QueueRole::kTransfer, true);
    CHECK_NOTNULL_F(recorder.get());
    EnsureTracked(*recorder, source, ResourceStates::kCopyDest);

    ticket = readback->EnqueueCopy(*recorder, *source, BufferRange { 6, 14 });
    ASSERT_TRUE(ticket.has_value());
  }

  WaitForQueueIdle(oxygen::graphics::QueueRole::kTransfer);

  const auto ready = readback->IsReady();
  ASSERT_TRUE(ready.has_value());
  EXPECT_TRUE(*ready);

  const auto mapped = readback->TryMap();
  ASSERT_TRUE(mapped.has_value());
  EXPECT_EQ(
    std::vector<std::byte>(mapped->Bytes().begin(), mapped->Bytes().end()),
    SliceBytes(source_bytes, 6, 14));
}

} // namespace
