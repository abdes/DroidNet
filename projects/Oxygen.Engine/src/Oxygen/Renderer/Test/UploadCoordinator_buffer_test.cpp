//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Detail/Barriers.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>

#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

#include <Oxygen/Renderer/Test/Helpers/UploadTestFakes.h>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

namespace {
using oxygen::engine::upload::Bytes;
using oxygen::engine::upload::UploadBufferDesc;
using oxygen::engine::upload::UploadCoordinator;
using oxygen::engine::upload::UploadDataView;
using oxygen::engine::upload::UploadKind;
using oxygen::engine::upload::UploadRequest;
using oxygen::engine::upload::UploadTicket;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::CommandList;
using oxygen::graphics::CommandQueue;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::QueueKey;
using oxygen::graphics::QueueRole;
using oxygen::tests::uploadhelpers::FakeGraphics_Buffer;

// --- Minimal test fakes --------------------------------------------------//

class FakeBuffer final : public Buffer {
public:
  FakeBuffer(std::string_view name, uint64_t size, BufferUsage usage)
    : Buffer(name)
  {
    desc_.size_bytes = size;
    desc_.usage = usage;
    desc_.memory = BufferMemory::kDeviceLocal;
  }

  [[nodiscard]] auto GetDescriptor() const noexcept -> BufferDesc override
  {
    return desc_;
  }
  [[nodiscard]] auto GetNativeResource() const
    -> oxygen::graphics::NativeObject override
  {
    return oxygen::graphics::NativeObject(
      const_cast<FakeBuffer*>(this), oxygen::graphics::Buffer::ClassTypeId());
  }
  auto Map(uint64_t, uint64_t) -> void* override { return nullptr; }
  auto UnMap() -> void override { }
  auto Update(const void*, uint64_t, uint64_t) -> void override { }
  [[nodiscard]] auto GetSize() const noexcept -> uint64_t override
  {
    return desc_.size_bytes;
  }
  [[nodiscard]] auto GetUsage() const noexcept -> BufferUsage override
  {
    return desc_.usage;
  }
  [[nodiscard]] auto GetMemoryType() const noexcept -> BufferMemory override
  {
    return desc_.memory;
  }
  [[nodiscard]] auto IsMapped() const noexcept -> bool override
  {
    return false;
  }
  [[nodiscard]] auto GetGPUVirtualAddress() const -> uint64_t override
  {
    return 0;
  }

protected:
  [[nodiscard]] auto CreateConstantBufferView(
    const oxygen::graphics::DescriptorHandle&,
    const oxygen::graphics::BufferRange&) const
    -> oxygen::graphics::NativeObject override
  {
    return {};
  }
  [[nodiscard]] auto CreateShaderResourceView(
    const oxygen::graphics::DescriptorHandle&, oxygen::Format,
    oxygen::graphics::BufferRange, uint32_t) const
    -> oxygen::graphics::NativeObject override
  {
    return {};
  }
  [[nodiscard]] auto CreateUnorderedAccessView(
    const oxygen::graphics::DescriptorHandle&, oxygen::Format,
    oxygen::graphics::BufferRange, uint32_t) const
    -> oxygen::graphics::NativeObject override
  {
    return {};
  }

private:
  BufferDesc desc_ {};
};

//! Happy-path buffer upload: CopyBuffer and queue signal are recorded and
//! ticket completes after retire.
NOLINT_TEST(UploadCoordinator, BufferUpload_MockedPath_Completes)
{
  // Arrange
  auto gfx = std::make_shared<FakeGraphics_Buffer>();
  gfx->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

  // Destination buffer (vertex usage to trigger VB state transition branch)
  auto dst
    = std::make_shared<FakeBuffer>("Dst", /*size*/ 1024, BufferUsage::kVertex);

  std::array<std::byte, 64> data {};
  for (size_t i = 0; i < data.size(); ++i)
    data[i] = static_cast<std::byte>(i);

  UploadRequest req { .kind = UploadKind::kBuffer,
    .batch_policy = {},
    .priority = {},
    .debug_name = "BufUpload",
    .desc
    = UploadBufferDesc { .dst = dst, .size_bytes = 64, .dst_offset = 128 },
    .subresources = {},
    .data = UploadDataView {
      .bytes = std::span<const std::byte>(data.data(), data.size()) } };

  UploadCoordinator coord(gfx);

  // Act
  auto ticket = coord.Submit(req);
  coord.Flush();
  coord.RetireCompleted();

  // Assert copy call captured
  const auto& log = gfx->buffer_log_;
  ASSERT_TRUE(log.copy_called);
  EXPECT_EQ(log.copy_dst, dst.get());
  EXPECT_EQ(log.copy_dst_offset, 128u);
  ASSERT_NE(log.copy_src, nullptr);
  EXPECT_EQ(log.copy_src_offset, 0u);
  EXPECT_EQ(log.copy_size, 64u);

  // Ticket completion
  EXPECT_TRUE(coord.IsComplete(ticket));
  auto res = coord.TryGetResult(ticket);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->bytes_uploaded, 64u);

  // Cleanup: process deferred releases to avoid reclaimer warnings
  gfx->Shutdown();
}

//! Producer path: UploadRequest holds a producer instead of a byte view; the
//! producer fills the mapped staging span. CopyBuffer and ticket completion are
//! validated.
NOLINT_TEST(UploadCoordinator, BufferUpload_WithProducer_Completes)
{
  // Arrange
  auto gfx = std::make_shared<FakeGraphics_Buffer>();
  gfx->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

  auto dst
    = std::make_shared<FakeBuffer>("Dst", /*size*/ 512, BufferUsage::kVertex);

  constexpr size_t kSize = 128;
  bool producer_ran = false;
  std::move_only_function<bool(std::span<std::byte>)> producer
    = [&producer_ran](std::span<std::byte> out) -> bool {
    producer_ran = true;
    for (size_t i = 0; i < out.size(); ++i) {
      out[i] = static_cast<std::byte>(i & 0xFF);
    }
    return true;
  };

  UploadRequest req { .kind = UploadKind::kBuffer,
    .batch_policy = {},
    .priority = {},
    .debug_name = "BufUploadProducer",
    .desc
    = UploadBufferDesc { .dst = dst, .size_bytes = kSize, .dst_offset = 64 },
    .subresources = {},
    .data = std::move(producer) };

  UploadCoordinator coord(gfx);

  // Act
  auto ticket = coord.Submit(req);
  coord.Flush();
  coord.RetireCompleted();

  // Assert
  EXPECT_TRUE(producer_ran);
  const auto& log = gfx->buffer_log_;
  ASSERT_TRUE(log.copy_called);
  EXPECT_EQ(log.copy_dst, dst.get());
  EXPECT_EQ(log.copy_dst_offset, 64u);
  ASSERT_NE(log.copy_src, nullptr);
  EXPECT_EQ(log.copy_src_offset % 256u, 0u); // staging base alignment
  EXPECT_EQ(log.copy_size, kSize);

  EXPECT_TRUE(coord.IsComplete(ticket));
  auto res = coord.TryGetResult(ticket);
  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(res->success);
  EXPECT_EQ(res->bytes_uploaded, kSize);

  gfx->Shutdown();
}

//! SubmitMany coalesces consecutive buffer uploads into one staging allocation
//! and records two CopyBuffer commands with aligned source offsets.
NOLINT_TEST(UploadCoordinator, BufferSubmitMany_CoalescesAndCompletes)
{
  // Arrange
  auto gfx = std::make_shared<FakeGraphics_Buffer>();
  gfx->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

  auto dst_a = std::make_shared<FakeBuffer>("DstA", 2048, BufferUsage::kVertex);
  auto dst_b = std::make_shared<FakeBuffer>("DstB", 2048, BufferUsage::kVertex);

  std::array<std::byte, 64> data_a {};
  for (size_t i = 0; i < data_a.size(); ++i)
    data_a[i] = static_cast<std::byte>(i);
  std::array<std::byte, 80> data_b {};
  for (size_t i = 0; i < data_b.size(); ++i)
    data_b[i] = static_cast<std::byte>(0xAA);

  UploadRequest ra { .kind = UploadKind::kBuffer,
    .batch_policy = oxygen::engine::upload::BatchPolicy::kCoalesce,
    .priority = {},
    .debug_name = "A",
    .desc
    = UploadBufferDesc { .dst = dst_a, .size_bytes = 64, .dst_offset = 0 },
    .subresources = {},
    .data = UploadDataView {
      .bytes = std::span<const std::byte>(data_a.data(), data_a.size()) } };

  UploadRequest rb { .kind = UploadKind::kBuffer,
    .batch_policy = oxygen::engine::upload::BatchPolicy::kCoalesce,
    .priority = {},
    .debug_name = "B",
    .desc
    = UploadBufferDesc { .dst = dst_b, .size_bytes = 80, .dst_offset = 256 },
    .subresources = {},
    .data = UploadDataView {
      .bytes = std::span<const std::byte>(data_b.data(), data_b.size()) } };

  UploadCoordinator coord(gfx);

  // Act
  std::vector<UploadRequest> reqs;
  reqs.emplace_back(std::move(ra));
  reqs.emplace_back(std::move(rb));
  auto tickets = coord.SubmitMany(
    std::span<const UploadRequest>(reqs.data(), reqs.size()));
  coord.Flush();
  coord.RetireCompleted();

  // Assert: two tickets, both complete with expected byte counts
  ASSERT_EQ(tickets.size(), 2u);
  for (const auto& t : tickets) {
    EXPECT_TRUE(coord.IsComplete(t));
  }
  auto res_a = coord.TryGetResult(tickets[0]);
  auto res_b = coord.TryGetResult(tickets[1]);
  ASSERT_TRUE(res_a.has_value());
  ASSERT_TRUE(res_b.has_value());
  EXPECT_EQ(res_a->bytes_uploaded, 64u);
  EXPECT_EQ(res_b->bytes_uploaded, 80u);

  // Assert: two copy events recorded with alignment between src offsets
  const auto& log = gfx->buffer_log_;
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
  gfx->Shutdown();
}

//! SubmitMany coalescing with producers: two producer-backed requests are
//! packed into one staging allocation; ensure both producers run and CopyBuffer
//! events reflect aligned src offsets.
NOLINT_TEST(UploadCoordinator, BufferSubmitMany_Producers_CoalescesAndCompletes)
{
  // Arrange
  auto gfx = std::make_shared<FakeGraphics_Buffer>();
  gfx->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

  auto dst_a = std::make_shared<FakeBuffer>("DstA", 2048, BufferUsage::kVertex);
  auto dst_b = std::make_shared<FakeBuffer>("DstB", 2048, BufferUsage::kVertex);

  bool prod_a_ran = false;
  bool prod_b_ran = false;
  constexpr size_t kSizeA = 96;
  constexpr size_t kSizeB = 128;
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

  UploadRequest ra { .kind = UploadKind::kBuffer,
    .batch_policy = oxygen::engine::upload::BatchPolicy::kCoalesce,
    .priority = {},
    .debug_name = "A-prod",
    .desc
    = UploadBufferDesc { .dst = dst_a, .size_bytes = kSizeA, .dst_offset = 0 },
    .subresources = {},
    .data = std::move(pa) };
  UploadRequest rb { .kind = UploadKind::kBuffer,
    .batch_policy = oxygen::engine::upload::BatchPolicy::kCoalesce,
    .priority = {},
    .debug_name = "B-prod",
    .desc = UploadBufferDesc { .dst = dst_b,
      .size_bytes = kSizeB,
      .dst_offset = 256 },
    .subresources = {},
    .data = std::move(pb) };

  UploadCoordinator coord(gfx);

  // Act
  std::vector<UploadRequest> reqs;
  reqs.emplace_back(std::move(ra));
  reqs.emplace_back(std::move(rb));
  auto tickets = coord.SubmitMany(
    std::span<const UploadRequest>(reqs.data(), reqs.size()));
  coord.Flush();
  coord.RetireCompleted();

  // Assert producers ran
  EXPECT_TRUE(prod_a_ran);
  EXPECT_TRUE(prod_b_ran);

  // Assert tickets complete
  ASSERT_EQ(tickets.size(), 2u);
  EXPECT_TRUE(coord.IsComplete(tickets[0]));
  EXPECT_TRUE(coord.IsComplete(tickets[1]));
  auto res_a = coord.TryGetResult(tickets[0]);
  auto res_b = coord.TryGetResult(tickets[1]);
  ASSERT_TRUE(res_a.has_value());
  ASSERT_TRUE(res_b.has_value());
  EXPECT_EQ(res_a->bytes_uploaded, kSizeA);
  EXPECT_EQ(res_b->bytes_uploaded, kSizeB);

  // Assert copy log: two events, aligned src offsets
  const auto& log = gfx->buffer_log_;
  ASSERT_GE(log.copies.size(), 2u);
  const auto& e0 = log.copies[0];
  const auto& e1 = log.copies[1];
  EXPECT_EQ(e0.dst, dst_a.get());
  EXPECT_EQ(e0.dst_offset, 0u);
  EXPECT_EQ(e0.size, kSizeA);
  EXPECT_EQ(e1.dst, dst_b.get());
  EXPECT_EQ(e1.dst_offset, 256u);
  EXPECT_EQ(e1.size, kSizeB);
  EXPECT_EQ(e0.src_offset % 256u, 0u);
  EXPECT_EQ(e1.src_offset - e0.src_offset, 256u);

  gfx->Shutdown();
}

//! Producer returns false: coordinator reports failure and records no copy.
NOLINT_TEST(UploadCoordinator, BufferUpload_WithProducer_Fails_NoCopy)
{
  auto gfx = std::make_shared<FakeGraphics_Buffer>();
  gfx->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

  auto dst = std::make_shared<FakeBuffer>("Dst", 1024, BufferUsage::kVertex);

  bool prod_ran = false;
  std::move_only_function<bool(std::span<std::byte>)> prod
    = [&prod_ran](std::span<std::byte>) -> bool {
    prod_ran = true;
    return false; // fail
  };

  UploadRequest req { .kind = UploadKind::kBuffer,
    .batch_policy = {},
    .priority = {},
    .debug_name = "FailProd",
    .desc = UploadBufferDesc { .dst = dst, .size_bytes = 64, .dst_offset = 0 },
    .subresources = {},
    .data = std::move(prod) };

  UploadCoordinator coord(gfx);
  auto ticket = coord.Submit(req);
  coord.Flush();
  coord.RetireCompleted();

  EXPECT_TRUE(prod_ran);
  const auto& log = gfx->buffer_log_;
  EXPECT_FALSE(log.copy_called);

  ASSERT_TRUE(coord.IsComplete(ticket));
  auto res = coord.TryGetResult(ticket);
  ASSERT_TRUE(res.has_value());
  EXPECT_FALSE(res->success);
  EXPECT_EQ(res->error, oxygen::engine::upload::UploadError::kProducerFailed);
  EXPECT_EQ(res->bytes_uploaded, 0u);

  gfx->Shutdown();
}

//! Batch: first producer ok, second fails. Only first copy is recorded; both
//! tickets complete with respective statuses.
NOLINT_TEST(
  UploadCoordinator, BufferSubmitMany_ProducerSecondFails_PartialSubmit)
{
  auto gfx = std::make_shared<FakeGraphics_Buffer>();
  gfx->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

  auto dst_a = std::make_shared<FakeBuffer>("DstA", 2048, BufferUsage::kVertex);
  auto dst_b = std::make_shared<FakeBuffer>("DstB", 2048, BufferUsage::kVertex);

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

  UploadRequest ra { .kind = UploadKind::kBuffer,
    .batch_policy = oxygen::engine::upload::BatchPolicy::kCoalesce,
    .priority = {},
    .debug_name = "A-prod-ok",
    .desc
    = UploadBufferDesc { .dst = dst_a, .size_bytes = 64, .dst_offset = 0 },
    .subresources = {},
    .data = std::move(pa) };
  UploadRequest rb { .kind = UploadKind::kBuffer,
    .batch_policy = oxygen::engine::upload::BatchPolicy::kCoalesce,
    .priority = {},
    .debug_name = "B-prod-fail",
    .desc
    = UploadBufferDesc { .dst = dst_b, .size_bytes = 64, .dst_offset = 0 },
    .subresources = {},
    .data = std::move(pb) };

  UploadCoordinator coord(gfx);
  std::vector<UploadRequest> reqs;
  reqs.emplace_back(std::move(ra));
  reqs.emplace_back(std::move(rb));
  auto tickets = coord.SubmitMany(
    std::span<const UploadRequest>(reqs.data(), reqs.size()));
  coord.Flush();
  coord.RetireCompleted();

  EXPECT_TRUE(prod_a_ran);
  EXPECT_TRUE(prod_b_ran);

  // Copy log should have exactly one copy for the successful one
  const auto& log = gfx->buffer_log_;
  ASSERT_EQ(log.copies.size(), 1u);
  EXPECT_EQ(log.copies[0].dst, dst_a.get());

  ASSERT_EQ(tickets.size(), 2u);
  ASSERT_TRUE(coord.IsComplete(tickets[0]));
  ASSERT_TRUE(coord.IsComplete(tickets[1]));
  auto r0 = coord.TryGetResult(tickets[0]);
  auto r1 = coord.TryGetResult(tickets[1]);
  ASSERT_TRUE(r0.has_value());
  ASSERT_TRUE(r1.has_value());
  EXPECT_TRUE(r0->success);
  EXPECT_FALSE(r1->success);
  EXPECT_EQ(r1->error, oxygen::engine::upload::UploadError::kProducerFailed);

  gfx->Shutdown();
}

} // namespace
