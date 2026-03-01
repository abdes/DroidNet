//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/PhysicsSidecarImportPipeline.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>

namespace oxygen::content::import::test {

namespace {

  namespace co = oxygen::co;

  class PhysicsImportPipelineTest : public testing::Test {
  protected:
    ImportEventLoop loop_;
  };

  NOLINT_TEST_F(PhysicsImportPipelineTest, PipelineRejectsNullSession)
  {
    auto result = PhysicsSidecarImportPipeline::WorkResult {};

    co::Run(loop_, [&]() -> co::Co<> {
      auto pipeline
        = PhysicsSidecarImportPipeline(PhysicsSidecarImportPipeline::Config {
          .queue_capacity = 8,
          .worker_count = 1,
        });

      OXCO_WITH_NURSERY(n)
      {
        pipeline.Start(n);
        co_await pipeline.Submit(PhysicsSidecarImportPipeline::WorkItem {
          .source_id = "null-session-physics-sidecar",
          .source_bytes = { std::byte { 0x1 } },
          .session = {},
          .stop_token = {},
        });
        pipeline.Close();
        result = co_await pipeline.Collect();
        co_return co::kJoin;
      };
    });

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.source_id, "null-session-physics-sidecar");
  }

} // namespace

} // namespace oxygen::content::import::test
