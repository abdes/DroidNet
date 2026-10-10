//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Pipelines/TexturePipeline.cpp

#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/TexturePipeline.h>
#include <Oxygen/Cooker/Test/Support/PipelineHarness.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/OxCo/asio.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::content::import;
using namespace oxygen::co;
namespace co = oxygen::co;
using oxygen::cooker::test::RunPipelineOnce;

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
    .output_format_policy
    = TexturePipeline::OutputFormatPolicy::kPreserveSource,
    .failure_policy = failure_policy,
    .source = std::move(source),
    .stop_token = stop_token,
  };
}

//=== Basic Behavior Tests
//===-----------------------------------------------------//

class TexturePipelineTest : public testing::Test {
protected:
  ImportEventLoop loop_;
  ThreadPool pool_ { loop_, 2 };
};

NOLINT_TEST_F(TexturePipelineTest, CollectWithPlaceholderPolicyReportsFailure)
{
  const auto result = RunPipelineOnce<TexturePipeline>(loop_,
    MakeWorkItem("missing.png", "missing.png", MakeSourceBytes({}),
      TexturePipeline::FailurePolicy::kPlaceholder),
    pool_,
    TexturePipeline::Config {
      .queue_capacity = 4,
      .worker_count = 1,
    });

  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.used_placeholder);
  EXPECT_FALSE(result.cooked.has_value());
  ASSERT_EQ(result.diagnostics.size(), 1U);
  EXPECT_EQ(result.diagnostics[0].code, "texture.cook_failed");
}

NOLINT_TEST_F(TexturePipelineTest, CollectWithStrictPolicyEmitsDiagnostic)
{
  const auto result = RunPipelineOnce<TexturePipeline>(loop_,
    MakeWorkItem("missing.png", "missing.png", MakeSourceBytes({}),
      TexturePipeline::FailurePolicy::kStrict),
    pool_,
    TexturePipeline::Config {
      .queue_capacity = 4,
      .worker_count = 1,
    });

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.used_placeholder);
  EXPECT_FALSE(result.cooked.has_value());
  ASSERT_EQ(result.diagnostics.size(), 1U);
  EXPECT_EQ(result.diagnostics[0].code, "texture.cook_failed");
}

NOLINT_TEST_F(TexturePipelineTest, CollectWhenCancelledReturnsFailedResult)
{
  std::stop_source stop_source;
  stop_source.request_stop();

  const auto result = RunPipelineOnce<TexturePipeline>(loop_,
    MakeWorkItem("cancel.png", "cancel.png",
      MakeSourceBytes({ std::byte { 0x00 } }),
      TexturePipeline::FailurePolicy::kStrict, stop_source.get_token()),
    pool_,
    TexturePipeline::Config {
      .queue_capacity = 4,
      .worker_count = 1,
    });

  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.diagnostics.empty());
}

} // namespace
