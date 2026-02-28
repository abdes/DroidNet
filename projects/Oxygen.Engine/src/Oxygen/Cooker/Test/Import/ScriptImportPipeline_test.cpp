//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <string>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/ScriptAssetImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/ScriptingSidecarImportPipeline.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>

namespace oxygen::content::import::test {

namespace {

  namespace co = oxygen::co;

  class ScriptImportPipelineTest : public testing::Test {
  protected:
    ImportEventLoop loop_;
  };

  NOLINT_TEST_F(ScriptImportPipelineTest, ScriptAssetPipelineRejectsNullSession)
  {
    auto result = ScriptAssetImportPipeline::WorkResult {};

    co::Run(loop_, [&]() -> co::Co<> {
      auto pipeline
        = ScriptAssetImportPipeline(ScriptAssetImportPipeline::Config {
          .queue_capacity = 8, .worker_count = 1 });

      OXCO_WITH_NURSERY(n)
      {
        pipeline.Start(n);
        co_await pipeline.Submit(ScriptAssetImportPipeline::WorkItem {
          .source_id = "null-session-asset",
          .source_bytes = { std::byte { 0x1 } },
          .session = {},
          .index_registry = {},
          .on_started = {},
          .on_finished = {},
          .stop_token = {},
        });
        pipeline.Close();
        result = co_await pipeline.Collect();
        co_return co::kJoin;
      };
    });

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.source_id, "null-session-asset");
  }

  NOLINT_TEST_F(
    ScriptImportPipelineTest, ScriptingSidecarPipelineRejectsNullSession)
  {
    auto result = ScriptingSidecarImportPipeline::WorkResult {};

    co::Run(loop_, [&]() -> co::Co<> {
      auto pipeline = ScriptingSidecarImportPipeline(
        ScriptingSidecarImportPipeline::Config {
          .queue_capacity = 8,
          .worker_count = 1,
        });

      OXCO_WITH_NURSERY(n)
      {
        pipeline.Start(n);
        co_await pipeline.Submit(ScriptingSidecarImportPipeline::WorkItem {
          .source_id = "null-session-sidecar",
          .source_bytes = { std::byte { 0x1 } },
          .session = {},
          .index_registry = {},
          .on_started = {},
          .on_finished = {},
          .stop_token = {},
        });
        pipeline.Close();
        result = co_await pipeline.Collect();
        co_return co::kJoin;
      };
    });

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.source_id, "null-session-sidecar");
  }

} // namespace

} // namespace oxygen::content::import::test
