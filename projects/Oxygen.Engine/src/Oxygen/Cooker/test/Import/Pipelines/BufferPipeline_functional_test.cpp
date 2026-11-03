//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Pipelines/BufferPipeline.cpp

#include <atomic>
#include <chrono>
#include <cstddef>
#include <span>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/BufferImportTypes.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/BufferPipeline.h>
#include <Oxygen/Cooker/test/Support/PipelineHarness.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/OxCo/asio.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::co::Co;
using oxygen::co::kJoin;
using oxygen::co::SleepFor;
using oxygen::co::ThreadPool;
using oxygen::cooker::test::RunPipeline;
using oxygen::cooker::test::RunPipelineOnce;

namespace oxygen::content::import {
namespace {

  //=== Test Helpers
  //===---------------------------------------------------------//

  auto MakePayload() -> CookedBufferPayload
  {
    CookedBufferPayload cooked;
    cooked.alignment = 16;
    cooked.usage_flags = 0x01;
    cooked.element_stride = 32;
    cooked.element_format = 0;
    return cooked;
  }

  //! The work item for `bytes`: the pipeline hashes them into `cooked.data`.
  auto MakeWorkItem(std::string source_id, std::vector<std::byte> bytes,
    std::stop_token stop_token = {}) -> BufferPipeline::WorkItem
  {
    return BufferPipeline::WorkItem {
      .source_id = std::move(source_id),
      .bytes = std::move(bytes),
      .cooked = MakePayload(),
      .stop_token = std::move(stop_token),
    };
  }

  //=== Basic Behavior Tests
  //===-----------------------------------------------------//

  class BufferPipelineTest : public ::testing::Test {
  protected:
    ImportEventLoop loop_;
    ThreadPool pool_ { loop_, 2 };
  };

  NOLINT_TEST_F(BufferPipelineTest, CollectComputesHash)
  {
    std::vector<std::byte> bytes {
      std::byte { 0x10 },
      std::byte { 0x20 },
      std::byte { 0x30 },
      std::byte { 0x40 },
    };
    const std::span<const std::byte> span(bytes.data(), bytes.size());
    const auto expected_hash = oxygen::base::ComputeSha256(span);

    const auto result = RunPipelineOnce<BufferPipeline>(loop_,
      MakeWorkItem("buffer0", std::move(bytes)), pool_,
      BufferPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
      });

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.diagnostics.empty());
    EXPECT_EQ(result.source_id, "buffer0");
    EXPECT_EQ(result.cooked.data.Sha256(), expected_hash);
  }

  NOLINT_TEST_F(BufferPipelineTest, CollectWhenCancelledReturnsFailedResult)
  {
    std::stop_source stop_source;
    stop_source.request_stop();

    std::vector<std::byte> bytes(16, std::byte { 0x42 });

    const auto result = RunPipelineOnce<BufferPipeline>(loop_,
      MakeWorkItem("buffer0", std::move(bytes), stop_source.get_token()), pool_,
      BufferPipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
      });

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.diagnostics.empty());
    EXPECT_EQ(result.source_id, "buffer0");
  }

  NOLINT_TEST_F(BufferPipelineTest, CollectWhenCancelledAfterSubmitFails)
  {
    std::stop_source stop_source;

    std::vector<std::byte> bytes(
      size_t { 2 } * 1024 * 1024, std::byte { 0x77 });
    BufferPipeline::WorkResult result;

    co::Run(loop_, [&] -> Co<> {
      BufferPipeline pipeline(pool_,
        BufferPipeline::Config {
          .queue_capacity = 4,
          .worker_count = 1,
        });

      OXCO_WITH_NURSERY(n)
      {
        pipeline.Start(n);

        co_await pipeline.Submit(
          MakeWorkItem("buffer0", std::move(bytes), stop_source.get_token()));

        stop_source.request_stop();

        result = co_await pipeline.Collect();
        pipeline.Close();

        co_return kJoin;
      };
    });

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.diagnostics.empty());
    EXPECT_EQ(result.source_id, "buffer0");
    EXPECT_TRUE(result.cooked.data.Empty());
  }

  NOLINT_TEST_F(BufferPipelineTest, CollectMixedCancellationReturnsMixedResults)
  {
    std::stop_source stop_source;
    BufferPipeline::WorkResult canceled_result;
    BufferPipeline::WorkResult ok_result;

    co::Run(loop_, [&] -> Co<> {
      BufferPipeline pipeline(pool_,
        BufferPipeline::Config {
          .queue_capacity = 4,
          .worker_count = 1,
        });

      OXCO_WITH_NURSERY(n)
      {
        pipeline.Start(n);

        co_await pipeline.Submit(MakeWorkItem("canceled",
          std::vector<std::byte>(128, std::byte { 0x11 }),
          stop_source.get_token()));

        co_await pipeline.Submit(
          MakeWorkItem("ok", std::vector<std::byte>(128, std::byte { 0x22 })));

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

    EXPECT_FALSE(canceled_result.success);
    EXPECT_TRUE(canceled_result.diagnostics.empty());
    EXPECT_EQ(canceled_result.source_id, "canceled");
    EXPECT_TRUE(canceled_result.cooked.data.Empty());

    EXPECT_TRUE(ok_result.success);
    EXPECT_TRUE(ok_result.diagnostics.empty());
    EXPECT_EQ(ok_result.source_id, "ok");
    EXPECT_FALSE(ok_result.cooked.data.Empty());
  }

  NOLINT_TEST_F(BufferPipelineTest, CollectMultipleSubmissionsCollectsAll)
  {
    constexpr int kCount = 8;
    std::unordered_map<std::string, oxygen::base::Sha256Digest>
      expected_hash_by_id;
    expected_hash_by_id.reserve(kCount);

    for (int i = 0; i < kCount; ++i) {
      const std::string id = "buffer" + std::to_string(i);
      std::vector<std::byte> bytes(128 + i, static_cast<std::byte>(0x10 + i));
      const std::span<const std::byte> span(bytes.data(), bytes.size());
      expected_hash_by_id.emplace(id, oxygen::base::ComputeSha256(span));
    }

    std::vector<BufferPipeline::WorkItem> items;
    items.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
      const std::string id = "buffer" + std::to_string(i);
      std::vector<std::byte> bytes(128 + i, static_cast<std::byte>(0x10 + i));
      items.push_back(MakeWorkItem(id, std::move(bytes)));
    }

    const auto results
      = RunPipeline<BufferPipeline>(loop_, std::move(items), pool_,
        BufferPipeline::Config {
          .queue_capacity = 16,
          .worker_count = 2,
        });

    ASSERT_EQ(results.size(), static_cast<size_t>(kCount));
    for (const auto& r : results) {
      EXPECT_TRUE(r.success);
      EXPECT_TRUE(r.diagnostics.empty());
      ASSERT_TRUE(expected_hash_by_id.contains(r.source_id));
      EXPECT_EQ(r.cooked.data.Sha256(), expected_hash_by_id.at(r.source_id));
    }
  }

  //! Proxy check that hashing is dispatched off-thread via ThreadPool.
  NOLINT_TEST_F(BufferPipelineTest, SubmitEventLoopStaysResponsive)
  {
    std::atomic<bool> posted_ran { false };
    BufferPipeline::WorkResult result;

    co::Run(loop_, [&] -> Co<> {
      BufferPipeline pipeline(pool_,
        BufferPipeline::Config {
          .queue_capacity = 4,
          .worker_count = 1,
        });

      OXCO_WITH_NURSERY(n)
      {
        pipeline.Start(n);

        std::vector<std::byte> bytes(
          size_t { 2 } * 1024 * 1024, std::byte { 0xAB });
        co_await pipeline.Submit(MakeWorkItem("buffer0", std::move(bytes)));

        loop_.Post([&posted_ran] -> void { posted_ran.store(true); });

        EXPECT_TRUE(pipeline.HasPending());
        co_await SleepFor(loop_.IoContext(), std::chrono::milliseconds(1));

        result = co_await pipeline.Collect();
        pipeline.Close();

        co_return kJoin;
      };
    });

    EXPECT_TRUE(posted_ran.load());
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.diagnostics.empty());
    EXPECT_EQ(result.source_id, "buffer0");
    EXPECT_FALSE(result.cooked.data.Empty());
  }

} // namespace

} // namespace oxygen::content::import
