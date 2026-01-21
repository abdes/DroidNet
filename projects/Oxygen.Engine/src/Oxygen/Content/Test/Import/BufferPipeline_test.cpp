//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <span>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Content/Import/Internal/Pipelines/BufferPipeline.h>
#include <Oxygen/Content/Import/Internal/Utils/ContentHashUtils.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/OxCo/asio.h>

using namespace oxygen::content::import;
using namespace oxygen::co;
namespace co = oxygen::co;

namespace {

//=== Test Helpers
//===---------------------------------------------------------//

auto MakePayload(std::vector<std::byte> data, uint64_t content_hash = 0)
  -> CookedBufferPayload
{
  CookedBufferPayload cooked;
  cooked.data = std::move(data);
  cooked.alignment = 16;
  cooked.usage_flags = 0x01;
  cooked.element_stride = 32;
  cooked.element_format = 0;
  cooked.content_hash = content_hash;
  return cooked;
}

auto MakeWorkItem(std::string source_id, CookedBufferPayload cooked,
  std::stop_token stop_token = {}) -> BufferPipeline::WorkItem
{
  return BufferPipeline::WorkItem {
    .source_id = std::move(source_id),
    .cooked = std::move(cooked),
    .stop_token = stop_token,
  };
}

//=== Basic Behavior Tests
//===-----------------------------------------------------//

class BufferPipelineTest : public testing::Test {
protected:
  ImportEventLoop loop_;
};

//! Verify hashing stage fills content_hash when enabled.
NOLINT_TEST_F(BufferPipelineTest, Collect_WithHashingEnabled_ComputesHash)
{
  // Arrange
  std::vector<std::byte> bytes { std::byte { 0x10 }, std::byte { 0x20 },
    std::byte { 0x30 }, std::byte { 0x40 } };
  const std::span<const std::byte> span(bytes.data(), bytes.size());
  const auto expected_hash = util::ComputeContentHash(span);

  BufferPipeline::WorkResult result;
  ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> Co<> {
    BufferPipeline pipeline(pool,
      BufferPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);

      co_await pipeline.Submit(MakeWorkItem(
        "buffer0", MakePayload(std::move(bytes), 0 /*content_hash*/)));

      result = co_await pipeline.Collect();
      pipeline.Close();

      co_return kJoin;
    };
  });

  // Assert
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.diagnostics.empty());
  EXPECT_EQ(result.source_id, "buffer0");
  EXPECT_EQ(result.cooked.content_hash, expected_hash);
}

//! Verify hashing stage does nothing when disabled.
NOLINT_TEST_F(BufferPipelineTest, Collect_WithHashingDisabled_LeavesHashZero)
{
  // Arrange
  std::vector<std::byte> bytes(64, std::byte { 0xAB });
  BufferPipeline::WorkResult result;
  ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> Co<> {
    BufferPipeline pipeline(pool,
      BufferPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = false,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);

      co_await pipeline.Submit(MakeWorkItem(
        "buffer0", MakePayload(std::move(bytes), 0 /*content_hash*/)));

      result = co_await pipeline.Collect();
      pipeline.Close();

      co_return kJoin;
    };
  });

  // Assert
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.diagnostics.empty());
  EXPECT_EQ(result.source_id, "buffer0");
  EXPECT_EQ(result.cooked.content_hash, 0ULL);
}

//! Verify hashing stage does not overwrite an existing content_hash.
NOLINT_TEST_F(BufferPipelineTest, Collect_WithExistingHash_DoesNotOverwrite)
{
  // Arrange
  constexpr uint64_t kExistingHash = 0x12345678ABCDEF00ULL;
  std::vector<std::byte> bytes(8, std::byte { 0x01 });
  BufferPipeline::WorkResult result;
  ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> Co<> {
    BufferPipeline pipeline(pool,
      BufferPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);

      co_await pipeline.Submit(
        MakeWorkItem("buffer0", MakePayload(std::move(bytes), kExistingHash)));

      result = co_await pipeline.Collect();
      pipeline.Close();

      co_return kJoin;
    };
  });

  // Assert
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.diagnostics.empty());
  EXPECT_EQ(result.source_id, "buffer0");
  EXPECT_EQ(result.cooked.content_hash, kExistingHash);
}

//! Verify canceled work returns a failed result.
NOLINT_TEST_F(BufferPipelineTest, Collect_WhenCancelled_ReturnsFailedResult)
{
  // Arrange
  std::stop_source stop_source;
  stop_source.request_stop();

  std::vector<std::byte> bytes(16, std::byte { 0x42 });
  BufferPipeline::WorkResult result;
  ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> Co<> {
    BufferPipeline pipeline(pool,
      BufferPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);

      co_await pipeline.Submit(MakeWorkItem("buffer0",
        MakePayload(std::move(bytes), 0 /*content_hash*/),
        stop_source.get_token()));

      result = co_await pipeline.Collect();
      pipeline.Close();

      co_return kJoin;
    };
  });

  // Assert
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.diagnostics.empty());
  EXPECT_EQ(result.source_id, "buffer0");
}

//! Verify cancellation after submission returns a failed result.
NOLINT_TEST_F(BufferPipelineTest, Collect_WhenCancelledAfterSubmit_Fails)
{
  // Arrange
  std::stop_source stop_source;

  std::vector<std::byte> bytes(2 * 1024 * 1024, std::byte { 0x77 });
  BufferPipeline::WorkResult result;
  ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> Co<> {
    BufferPipeline pipeline(pool,
      BufferPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);

      co_await pipeline.Submit(MakeWorkItem("buffer0",
        MakePayload(std::move(bytes), 0 /*content_hash*/),
        stop_source.get_token()));

      stop_source.request_stop();

      result = co_await pipeline.Collect();
      pipeline.Close();

      co_return kJoin;
    };
  });

  // Assert
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.diagnostics.empty());
  EXPECT_EQ(result.source_id, "buffer0");
  EXPECT_EQ(result.cooked.content_hash, 0ULL);
}

//! Verify mixed cancellation yields mixed success states.
NOLINT_TEST_F(BufferPipelineTest, Collect_MixedCancellation_ReturnsMixedResults)
{
  // Arrange
  std::stop_source stop_source;
  BufferPipeline::WorkResult canceled_result;
  BufferPipeline::WorkResult ok_result;
  ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> Co<> {
    BufferPipeline pipeline(pool,
      BufferPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);

      co_await pipeline.Submit(MakeWorkItem("canceled",
        MakePayload(
          std::vector<std::byte>(128, std::byte { 0x11 }), 0 /*content_hash*/),
        stop_source.get_token()));

      co_await pipeline.Submit(MakeWorkItem("ok",
        MakePayload(std::vector<std::byte>(128, std::byte { 0x22 }),
          0 /*content_hash*/)));

      stop_source.request_stop();

      auto first = co_await pipeline.Collect();
      auto second = co_await pipeline.Collect();

      if (first.source_id == "canceled") {
        canceled_result = std::move(first);
        ok_result = std::move(second);
      } else {
        ok_result = std::move(first);
        canceled_result = std::move(second);
      }

      pipeline.Close();

      co_return kJoin;
    };
  });

  // Assert
  EXPECT_FALSE(canceled_result.success);
  EXPECT_TRUE(canceled_result.diagnostics.empty());
  EXPECT_EQ(canceled_result.source_id, "canceled");
  EXPECT_EQ(canceled_result.cooked.content_hash, 0ULL);

  EXPECT_TRUE(ok_result.success);
  EXPECT_TRUE(ok_result.diagnostics.empty());
  EXPECT_EQ(ok_result.source_id, "ok");
  EXPECT_NE(ok_result.cooked.content_hash, 0ULL);
}

//! Verify multiple submissions can be collected successfully.
NOLINT_TEST_F(BufferPipelineTest, Collect_MultipleSubmissions_CollectsAll)
{
  // Arrange
  constexpr int kCount = 8;
  std::unordered_map<std::string, uint64_t> expected_hash_by_id;
  expected_hash_by_id.reserve(kCount);

  for (int i = 0; i < kCount; ++i) {
    const std::string id = "buffer" + std::to_string(i);
    std::vector<std::byte> bytes(128 + i, static_cast<std::byte>(0x10 + i));
    const std::span<const std::byte> span(bytes.data(), bytes.size());
    expected_hash_by_id.emplace(id, util::ComputeContentHash(span));
  }

  std::vector<BufferPipeline::WorkResult> results;
  results.reserve(kCount);
  ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> Co<> {
    BufferPipeline pipeline(pool,
      BufferPipeline::Config {
        .queue_capacity = 16,
        .worker_count = 2,
        .with_content_hashing = true,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);

      for (int i = 0; i < kCount; ++i) {
        const std::string id = "buffer" + std::to_string(i);
        std::vector<std::byte> bytes(128 + i, static_cast<std::byte>(0x10 + i));
        co_await pipeline.Submit(
          MakeWorkItem(id, MakePayload(std::move(bytes), 0 /*content_hash*/)));
      }

      for (int i = 0; i < kCount; ++i) {
        results.push_back(co_await pipeline.Collect());
      }

      pipeline.Close();
      co_return kJoin;
    };
  });

  // Assert
  ASSERT_EQ(results.size(), static_cast<size_t>(kCount));
  for (const auto& r : results) {
    EXPECT_TRUE(r.success);
    EXPECT_TRUE(r.diagnostics.empty());
    ASSERT_TRUE(expected_hash_by_id.contains(r.source_id));
    EXPECT_EQ(r.cooked.content_hash, expected_hash_by_id.at(r.source_id));
  }
}

//! Verify hashing work does not block the import event loop.
/*! This is a proxy check that hashing is dispatched off-thread via ThreadPool.
 */
NOLINT_TEST_F(
  BufferPipelineTest, Submit_WithHashingEnabled_EventLoopStaysResponsive)
{
  // Arrange
  std::atomic<bool> posted_ran { false };
  BufferPipeline::WorkResult result;
  ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> Co<> {
    BufferPipeline pipeline(pool,
      BufferPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
        .with_content_hashing = true,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);

      std::vector<std::byte> bytes(2 * 1024 * 1024, std::byte { 0xAB });
      co_await pipeline.Submit(
        MakeWorkItem("buffer0", MakePayload(std::move(bytes), 0)));

      loop_.Post([&posted_ran]() { posted_ran.store(true); });

      EXPECT_TRUE(pipeline.HasPending());
      co_await SleepFor(loop_.IoContext(), std::chrono::milliseconds(1));

      result = co_await pipeline.Collect();
      pipeline.Close();

      co_return kJoin;
    };
  });

  // Assert
  EXPECT_TRUE(posted_ran.load());
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.diagnostics.empty());
  EXPECT_EQ(result.source_id, "buffer0");
  EXPECT_NE(result.cooked.content_hash, 0ULL);
}

} // namespace
