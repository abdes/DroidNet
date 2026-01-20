//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Content/Import/Internal/Pipelines/TexturePipeline.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/OxCo/asio.h>

using namespace oxygen::content::import;
using namespace oxygen::co;
namespace co = oxygen::co;

namespace {

//=== Test Helpers
//===---------------------------------------------------------//

auto MakeSourceBytes(std::vector<std::byte> bytes)
  -> TexturePipeline::SourceBytes
{
  auto owner = std::make_shared<std::vector<std::byte>>(std::move(bytes));
  const std::span<const std::byte> span(owner->data(), owner->size());
  return TexturePipeline::SourceBytes {
    .bytes = span,
    .owner = std::move(owner),
  };
}

auto MakeWorkItem(std::string source_id, std::string texture_id,
  TexturePipeline::SourceContent source,
  TexturePipeline::FailurePolicy failure_policy,
  std::stop_token stop_token = {}) -> TexturePipeline::WorkItem
{
  TextureImportDesc desc;
  desc.source_id = source_id;

  return TexturePipeline::WorkItem {
    .source_id = std::move(source_id),
    .texture_id = std::move(texture_id),
    .source_key = nullptr,
    .desc = std::move(desc),
    .packing_policy_id = "d3d12",
    .output_format_is_override = false,
    .failure_policy = failure_policy,
    .source = std::move(source),
    .stop_token = stop_token,
  };
}

//=== Basic Behavior Tests
//===-----------------------------------------------------//

class TexturePipelineTest : public ::testing::Test {
protected:
  ImportEventLoop loop_;
};

//! Verify placeholder policy reports failure without built-in placeholder.
NOLINT_TEST_F(TexturePipelineTest, Collect_WithPlaceholderPolicy_ReportsFailure)
{
  // Arrange
  TexturePipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    TexturePipeline pipeline(pool,
      TexturePipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);

      co_await pipeline.Submit(MakeWorkItem("missing.png", "missing.png",
        MakeSourceBytes({}), TexturePipeline::FailurePolicy::kPlaceholder));

      result = co_await pipeline.Collect();
      pipeline.Close();

      co_return kJoin;
    };
  });

  // Assert
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.used_placeholder);
  EXPECT_FALSE(result.cooked.has_value());
  ASSERT_EQ(result.diagnostics.size(), 1U);
  EXPECT_EQ(result.diagnostics[0].code, "texture.cook_failed");
}

//! Verify strict policy returns a failure diagnostic.
NOLINT_TEST_F(TexturePipelineTest, Collect_WithStrictPolicy_EmitsDiagnostic)
{
  // Arrange
  TexturePipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    TexturePipeline pipeline(pool,
      TexturePipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);

      co_await pipeline.Submit(MakeWorkItem("missing.png", "missing.png",
        MakeSourceBytes({}), TexturePipeline::FailurePolicy::kStrict));

      result = co_await pipeline.Collect();
      pipeline.Close();

      co_return kJoin;
    };
  });

  // Assert
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.used_placeholder);
  EXPECT_FALSE(result.cooked.has_value());
  ASSERT_EQ(result.diagnostics.size(), 1U);
  EXPECT_EQ(result.diagnostics[0].code, "texture.cook_failed");
}

//! Verify cancelled work returns a failed result without diagnostics.
NOLINT_TEST_F(TexturePipelineTest, Collect_WhenCancelled_ReturnsFailedResult)
{
  // Arrange
  std::stop_source stop_source;
  stop_source.request_stop();

  TexturePipeline::WorkResult result;
  co::ThreadPool pool(loop_, 2);

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    TexturePipeline pipeline(pool,
      TexturePipeline::Config {
        .queue_capacity = 4,
        .worker_count = 1,
      });

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);

      co_await pipeline.Submit(MakeWorkItem("cancel.png", "cancel.png",
        MakeSourceBytes({ std::byte { 0x00 } }),
        TexturePipeline::FailurePolicy::kStrict, stop_source.get_token()));

      result = co_await pipeline.Collect();
      pipeline.Close();

      co_return kJoin;
    };
  });

  // Assert
  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.diagnostics.empty());
}

} // namespace
