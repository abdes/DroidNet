//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Detail/Barriers.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Renderer/Test/Upload/UploadCoordinatorTest.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

namespace {

using oxygen::engine::upload::Bytes;
using oxygen::engine::upload::UploadBufferDesc;
using oxygen::engine::upload::UploadDataView;
using oxygen::engine::upload::UploadKind;
using oxygen::engine::upload::UploadRequest;
using oxygen::engine::upload::testing::UploadCoordinatorTest;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::QueueKey;
namespace frame = oxygen::frame;

//! Happy-path buffer upload: CopyBuffer and queue signal are recorded and
//! ticket completes after retire.
NOLINT_TEST_F(UploadCoordinatorTest, BufferUpload_MockedPath_Completes)
{
  // Arrange
  // Destination buffer (vertex usage to trigger VB state transition branch)
  BufferDesc dst_desc {
    .size_bytes = 1024,
    .usage = BufferUsage::kVertex,
    .memory = BufferMemory::kDeviceLocal,
  };
  auto dst = GfxPtr()->CreateBuffer(dst_desc);

  std::array<std::byte, 64> data {};
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] = static_cast<std::byte>(i);
  }

  UploadRequest req {
    .kind = UploadKind::kBuffer,
    .batch_policy = {},
    .priority = {},
    .debug_name = "BufUpload",
    .desc = UploadBufferDesc {
      .dst = dst,
      .size_bytes = 64,
      .dst_offset = 128,
    },
    .subresources = {},
    .data = UploadDataView { .bytes
      = std::span<const std::byte>(data.data(), data.size()) },
  };

  auto& uploader = Uploader();

  // Act
  auto ticket_result = uploader.Submit(req, Staging());
  ASSERT_TRUE(ticket_result.has_value()) << "Submit failed";
  const auto ticket = ticket_result.value();

  // Assert copy call captured
  const auto& log = GfxPtr()->buffer_log_;
  ASSERT_TRUE(log.copy_called);
  EXPECT_EQ(log.copy_dst, dst.get());
  EXPECT_EQ(log.copy_dst_offset, 128u);
  ASSERT_NE(log.copy_src, nullptr);
  EXPECT_EQ(log.copy_src_offset, 0u);
  EXPECT_EQ(log.copy_size, 64u);

  // Simulate frame advance to complete fences
  SimulateFrameStart(frame::Slot { 1 });

  // Ticket completion
  auto complete_result = uploader.IsComplete(ticket);
  ASSERT_TRUE(complete_result.has_value()) << "IsComplete failed";
  EXPECT_TRUE(complete_result.value());
  auto res = uploader.TryGetResult(ticket);
  if (!res.has_value()) {
    FAIL() << "expected a value";
  }
  ASSERT_TRUE(res->bytes_uploaded == 64U);

  // Cleanup: process deferred releases to avoid reclaimer warnings
  GfxPtr()->Flush();
}

//! Producer path: UploadRequest holds a producer instead of a byte view; the
//! producer fills the mapped staging span. CopyBuffer and ticket completion are
//! validated.
NOLINT_TEST_F(UploadCoordinatorTest, BufferUpload_WithProducer_Completes)
{
  BufferDesc dst_desc {
    .size_bytes = 512,
    .usage = BufferUsage::kVertex,
    .memory = BufferMemory::kDeviceLocal,
  };
  auto dst = GfxPtr()->CreateBuffer(dst_desc);

  constexpr size_t size = 128;
  bool producer_ran = false;
  std::move_only_function<bool(std::span<std::byte>)> producer
    = [&producer_ran](std::span<std::byte> out) -> bool {
    producer_ran = true;
    for (size_t i = 0; i < out.size(); ++i) {
      out[i] = static_cast<std::byte>(i & 0xFF);
    }
    return true;
  };

  UploadRequest req {
    .kind = UploadKind::kBuffer,
    .batch_policy = {},
    .priority = {},
    .debug_name = "BufUploadProducer",
    .desc = UploadBufferDesc {
      .dst = dst,
      .size_bytes = size,
      .dst_offset = 64,
    },
    .subresources = {},
    .data = std::move(producer),
  };

  auto& uploader = Uploader();

  // Act
  auto ticket_result = uploader.Submit(req, Staging());
  ASSERT_TRUE(ticket_result.has_value()) << "Submit failed";
  const auto ticket = ticket_result.value();

  // Assert
  EXPECT_TRUE(producer_ran);
  const auto& log = GfxPtr()->buffer_log_;
  ASSERT_TRUE(log.copy_called);
  EXPECT_EQ(log.copy_dst, dst.get());
  EXPECT_EQ(log.copy_dst_offset, 64u);
  ASSERT_NE(log.copy_src, nullptr);
  EXPECT_EQ(log.copy_src_offset % 256u, 0u); // staging base alignment
  EXPECT_EQ(log.copy_size, size);

  // Simulate frame advance to complete fences
  SimulateFrameStart(frame::Slot { 1 });

  auto complete_result = uploader.IsComplete(ticket);
  ASSERT_TRUE(complete_result.has_value()) << "IsComplete failed";
  EXPECT_TRUE(complete_result.value());
  auto res = uploader.TryGetResult(ticket);
  if (!res.has_value()) {
    FAIL() << "expected a value";
  }
  ASSERT_TRUE(res->success);
  ASSERT_TRUE(res->bytes_uploaded == size);

  GfxPtr()->Flush();
}

//! SubmitMany coalesces consecutive buffer uploads into one staging allocation
//! and records two CopyBuffer commands with aligned source offsets.
NOLINT_TEST_F(UploadCoordinatorTest, BufferSubmitMany_CoalescesAndCompletes)
{
  BufferDesc dest_desc {
    .size_bytes = 2048,
    .usage = BufferUsage::kVertex,
    .memory = BufferMemory::kDeviceLocal,
  };
  auto dst_a = GfxPtr()->CreateBuffer(dest_desc);
  auto dst_b = GfxPtr()->CreateBuffer(dest_desc);

  std::array<std::byte, 64> data_a {};
  for (size_t i = 0; i < data_a.size(); ++i) {
    data_a[i] = static_cast<std::byte>(i);
  }
  std::array<std::byte, 80> data_b {};
  for (auto& i : data_b) {
    i = static_cast<std::byte>(0xAA);
  }

  UploadRequest ra {
    .kind = UploadKind::kBuffer,
    .batch_policy = oxygen::engine::upload::BatchPolicy::kCoalesce,
    .priority = {},
    .debug_name = "A",
    .desc
    = UploadBufferDesc {
      .dst = dst_a,
      .size_bytes = 64,
      .dst_offset = 0,
    },
    .subresources = {},
    .data = UploadDataView {
      .bytes = std::span<const std::byte>(data_a.data(), data_a.size()) },
  };

  UploadRequest rb { .kind = UploadKind::kBuffer,
    .batch_policy = oxygen::engine::upload::BatchPolicy::kCoalesce,
    .priority = {},
    .debug_name = "B",
    .desc
    = UploadBufferDesc {
      .dst = dst_b,
      .size_bytes = 80,
      .dst_offset = 256,
    },
    .subresources = {},
    .data = UploadDataView {
      .bytes = std::span<const std::byte>(data_b.data(), data_b.size()) } };

  auto& uploader = Uploader();

  // Act
  std::vector<UploadRequest> upload_requests;
  upload_requests.emplace_back(std::move(ra));
  upload_requests.emplace_back(std::move(rb));
  auto tickets_result
    = uploader.SubmitMany(std::span<const UploadRequest>(
                            upload_requests.data(), upload_requests.size()),
      Staging());
  ASSERT_TRUE(tickets_result.has_value()) << "SubmitMany failed";
  const auto& tickets = tickets_result.value();

  // Simulate frame advance to complete fences
  SimulateFrameStart(frame::Slot { 1 });

  // Assert: two tickets, both complete with expected byte counts
  ASSERT_EQ(tickets.size(), 2u);
  for (const auto& t : tickets) {
    auto complete_result = uploader.IsComplete(t);
    ASSERT_TRUE(complete_result.has_value()) << "IsComplete failed";
    EXPECT_TRUE(complete_result.value());
  }
  auto res_a = uploader.TryGetResult(tickets[0]);
  auto res_b = uploader.TryGetResult(tickets[1]);
  if (!res_a.has_value()) {
    FAIL() << "expected a value";
  }
  ASSERT_TRUE(res_a->bytes_uploaded == 64u);
  if (!res_b.has_value()) {
    FAIL() << "expected a value";
  }
  ASSERT_TRUE(res_b->bytes_uploaded == 80u);

  // Assert: two copy events recorded with alignment between src offsets
  const auto& log = GfxPtr()->buffer_log_;
  ASSERT_GE(log.copies.size(), 2u);
  const auto& e0 = log.copies[0];
  const auto& e1 = log.copies[1];
  EXPECT_EQ(e0.dst, dst_a.get());
  EXPECT_EQ(e0.dst_offset, 0u);
  EXPECT_EQ(e0.size, 64u);
  EXPECT_EQ(e1.dst, dst_b.get());
  EXPECT_EQ(e1.dst_offset, 256u);
  EXPECT_EQ(e1.size, 80u);
  // Buffer copy alignment is 256; first src_offset should be 0 (or base),
  // second offset must be first offset + 256.
  EXPECT_EQ(e1.src_offset - e0.src_offset, 256u);

  // Cleanup
  GfxPtr()->Flush();
}

//! SubmitMany coalescing with producers: two producer-backed requests are
//! packed into one staging allocation; ensure both producers run and CopyBuffer
//! events reflect aligned src offsets.
NOLINT_TEST_F(
  UploadCoordinatorTest, BufferSubmitMany_Producers_CoalescesAndCompletes)
{
  BufferDesc dest_desc {
    .size_bytes = 2048,
    .usage = BufferUsage::kVertex,
    .memory = BufferMemory::kDeviceLocal,
  };
  auto dst_a = GfxPtr()->CreateBuffer(dest_desc);
  auto dst_b = GfxPtr()->CreateBuffer(dest_desc);

  bool prod_a_ran = false;
  bool prod_b_ran = false;
  constexpr size_t size_a = 96;
  constexpr size_t size_b = 128;
  std::move_only_function<bool(std::span<std::byte>)> pa
    = [&prod_a_ran](std::span<std::byte> out) -> bool {
    prod_a_ran = true;
    std::memset(out.data(), 0x11, out.size());
    return true;
  };
  std::move_only_function<bool(std::span<std::byte>)> pb
    = [&prod_b_ran](std::span<std::byte> out) -> bool {
    prod_b_ran = true;
    std::memset(out.data(), 0x22, out.size());
    return true;
  };

  UploadRequest ra {
    .kind = UploadKind::kBuffer,
    .batch_policy = oxygen::engine::upload::BatchPolicy::kCoalesce,
    .priority = {},
    .debug_name = "A-prod",
    .desc
    = UploadBufferDesc {
      .dst = dst_a,
      .size_bytes = size_a,
      .dst_offset = 0,
    },
    .subresources = {},
    .data = std::move(pa),
  };
  UploadRequest rb {
    .kind = UploadKind::kBuffer,
    .batch_policy = oxygen::engine::upload::BatchPolicy::kCoalesce,
    .priority = {},
    .debug_name = "B-prod",
    .desc = UploadBufferDesc {
      .dst = dst_b,
      .size_bytes = size_b,
      .dst_offset = 256,
    },
    .subresources = {},
    .data = std::move(pb),
  };

  auto& uploader = Uploader();

  // Act
  std::vector<UploadRequest> upload_requests;
  upload_requests.emplace_back(std::move(ra));
  upload_requests.emplace_back(std::move(rb));
  auto tickets_result
    = uploader.SubmitMany(std::span<const UploadRequest>(
                            upload_requests.data(), upload_requests.size()),
      Staging());
  ASSERT_TRUE(tickets_result.has_value()) << "SubmitMany failed";
  const auto& tickets = tickets_result.value();

  // Assert producers ran
  EXPECT_TRUE(prod_a_ran);
  EXPECT_TRUE(prod_b_ran);

  // Simulate frame advance to complete fences
  SimulateFrameStart(frame::Slot { 1 });

  // Assert tickets complete
  ASSERT_EQ(tickets.size(), 2u);
  auto complete_result_0 = uploader.IsComplete(tickets[0]);
  ASSERT_TRUE(complete_result_0.has_value()) << "IsComplete failed";
  EXPECT_TRUE(complete_result_0.value());
  auto complete_result_1 = uploader.IsComplete(tickets[1]);
  ASSERT_TRUE(complete_result_1.has_value()) << "IsComplete failed";
  EXPECT_TRUE(complete_result_1.value());
  auto res_a = uploader.TryGetResult(tickets[0]);
  auto res_b = uploader.TryGetResult(tickets[1]);
  if (!res_a.has_value()) {
    FAIL() << "expected a value";
  }
  EXPECT_TRUE(res_a->bytes_uploaded == size_a);
  if (!res_b.has_value()) {
    FAIL() << "expected a value";
  }
  ASSERT_TRUE(res_b.has_value() && res_b->bytes_uploaded == size_b);

  // Assert copy log: two events, aligned src offsets
  const auto& log = GfxPtr()->buffer_log_;
  ASSERT_GE(log.copies.size(), 2u);
  const auto& e0 = log.copies[0];
  const auto& e1 = log.copies[1];
  EXPECT_EQ(e0.dst, dst_a.get());
  EXPECT_EQ(e0.dst_offset, 0u);
  EXPECT_EQ(e0.size, size_a);
  EXPECT_EQ(e1.dst, dst_b.get());
  EXPECT_EQ(e1.dst_offset, 256u);
  EXPECT_EQ(e1.size, size_b);
  EXPECT_EQ(e0.src_offset % 256u, 0u);
  EXPECT_EQ(e1.src_offset - e0.src_offset, 256u);

  GfxPtr()->Flush();
}

//! Producer returns false: coordinator reports failure and records no copy.
NOLINT_TEST_F(UploadCoordinatorTest, BufferUpload_WithProducer_Fails_NoCopy)
{
  BufferDesc dst_desc2 {
    .size_bytes = 1024,
    .usage = BufferUsage::kVertex,
    .memory = BufferMemory::kDeviceLocal,
  };
  auto dst = GfxPtr()->CreateBuffer(dst_desc2);

  bool prod_ran = false;
  std::move_only_function<bool(std::span<std::byte>)> prod
    = [&prod_ran](std::span<std::byte>) -> bool {
    prod_ran = true;
    return false; // fail
  };

  UploadRequest req {
    .kind = UploadKind::kBuffer,
    .batch_policy = {},
    .priority = {},
    .debug_name = "FailProd",
    .desc = UploadBufferDesc {
      .dst = dst,
      .size_bytes = 64,
      .dst_offset = 0,
    },
    .subresources = {},
    .data = std::move(prod),
  };

  auto& uploader = Uploader();

  auto ticket_result = uploader.Submit(req, Staging());
  ASSERT_TRUE(ticket_result.has_value()) << "Submit failed";
  const auto ticket = ticket_result.value();

  EXPECT_TRUE(prod_ran);
  const auto& log = GfxPtr()->buffer_log_;
  EXPECT_FALSE(log.copy_called);

  // Simulate frame advance to complete fences (even for failed uploads)
  SimulateFrameStart(frame::Slot { 1 });

  auto complete_result = uploader.IsComplete(ticket);
  ASSERT_TRUE(complete_result.has_value()) << "IsComplete failed";
  ASSERT_TRUE(complete_result.value());
  auto res = uploader.TryGetResult(ticket);
  if (!res.has_value()) {
    FAIL() << "expected a value";
  }
  EXPECT_FALSE(res->success);
  EXPECT_EQ(res->error, oxygen::engine::upload::UploadError::kProducerFailed);
  EXPECT_EQ(res->bytes_uploaded, 0u);

  GfxPtr()->Flush();
}

//! Batch: first producer ok, second fails. Only first copy is recorded; both
//! tickets complete with respective statuses.
NOLINT_TEST_F(
  UploadCoordinatorTest, BufferSubmitMany_ProducerSecondFails_PartialSubmit)
{
  BufferDesc dest_desc {
    .size_bytes = 2048,
    .usage = BufferUsage::kVertex,
    .memory = BufferMemory::kDeviceLocal,
  };
  auto dst_a = GfxPtr()->CreateBuffer(dest_desc);
  auto dst_b = GfxPtr()->CreateBuffer(dest_desc);

  bool prod_a_ran = false;
  bool prod_b_ran = false;
  std::move_only_function<bool(std::span<std::byte>)> pa
    = [&prod_a_ran](std::span<std::byte> out) -> bool {
    prod_a_ran = true;
    std::memset(out.data(), 0x33, out.size());
    return true;
  };
  std::move_only_function<bool(std::span<std::byte>)> pb
    = [&prod_b_ran](std::span<std::byte>) -> bool {
    prod_b_ran = true;
    return false;
  };

  UploadRequest ra {
    .kind = UploadKind::kBuffer,
    .batch_policy = oxygen::engine::upload::BatchPolicy::kCoalesce,
    .priority = {},
    .debug_name = "A-prod-ok",
    .desc = UploadBufferDesc {
      .dst = dst_a,
      .size_bytes = 64,
      .dst_offset = 0,
    },
    .subresources = {},
    .data = std::move(pa) };
  UploadRequest rb {
    .kind = UploadKind::kBuffer,
    .batch_policy = oxygen::engine::upload::BatchPolicy::kCoalesce,
    .priority = {},
    .debug_name = "B-prod-fail",
    .desc = UploadBufferDesc {
      .dst = dst_b,
      .size_bytes = 64,
      .dst_offset = 0,
    },
    .subresources = {},
    .data = std::move(pb),
  };

  auto& uploader = Uploader();

  std::vector<UploadRequest> upload_requests;
  upload_requests.emplace_back(std::move(ra));
  upload_requests.emplace_back(std::move(rb));
  auto tickets_result
    = uploader.SubmitMany(std::span<const UploadRequest>(
                            upload_requests.data(), upload_requests.size()),
      Staging());
  ASSERT_TRUE(tickets_result.has_value()) << "SubmitMany failed";
  const auto& tickets = tickets_result.value();

  EXPECT_TRUE(prod_a_ran);
  EXPECT_TRUE(prod_b_ran);

  // Copy log should have exactly one copy for the successful one
  const auto& log = GfxPtr()->buffer_log_;
  ASSERT_EQ(log.copies.size(), 1u);
  EXPECT_EQ(log.copies[0].dst, dst_a.get());

  // Simulate frame advance to complete fences
  SimulateFrameStart(frame::Slot { 1 });

  ASSERT_EQ(tickets.size(), 2u);
  auto complete_result_0 = uploader.IsComplete(tickets[0]);
  ASSERT_TRUE(complete_result_0.has_value()) << "IsComplete failed";
  ASSERT_TRUE(complete_result_0.value());
  auto complete_result_1 = uploader.IsComplete(tickets[1]);
  ASSERT_TRUE(complete_result_1.has_value()) << "IsComplete failed";
  ASSERT_TRUE(complete_result_1.value());
  auto r0 = uploader.TryGetResult(tickets[0]);
  auto r1 = uploader.TryGetResult(tickets[1]);
  if (!r0.has_value()) {
    FAIL() << "expected a value";
  }
  ASSERT_TRUE(r0->success);
  if (!r1.has_value()) {
    FAIL() << "expected a value";
  }
  ASSERT_FALSE(r1->success);
  EXPECT_EQ(r1->error, oxygen::engine::upload::UploadError::kProducerFailed);
  GfxPtr()->Flush();
}

} // namespace
