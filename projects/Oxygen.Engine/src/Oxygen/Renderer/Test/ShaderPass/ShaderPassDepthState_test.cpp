//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <cstdint>
#include <memory>
#include <string_view>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/RenderPass.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/Pipeline/DepthPrePassPolicy.h>
#include <Oxygen/Renderer/Test/DepthPrePass/DepthPrePassGpuTestFixture.h>

namespace {

using oxygen::Format;
using oxygen::TextureType;
using oxygen::engine::DepthPrePass;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::RenderPass;
using oxygen::engine::ShaderPass;
using oxygen::engine::ShaderPassConfig;
using oxygen::engine::testing::DepthPrePassGpuTestFixture;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;
using oxygen::graphics::Color;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::CompareOp;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::Texture;
using oxygen::renderer::DepthPrePassCompleteness;

auto PreparePassResources(RenderPass& pass,
  const oxygen::engine::RenderContext& rc, CommandRecorder& recorder) -> void
{
  oxygen::co::testing::TestEventLoop loop {};
  oxygen::co::Run(loop, [&]() -> oxygen::co::Co<> {
    co_await pass.PrepareResources(rc, recorder);
    co_return;
  });
}

class ShaderPassDepthStateTest : public DepthPrePassGpuTestFixture {
protected:
  auto CreateColorTexture(const std::uint32_t width, const std::uint32_t height,
    std::string_view debug_name) -> std::shared_ptr<Texture>
  {
    auto desc = oxygen::graphics::TextureDesc {};
    desc.width = width;
    desc.height = height;
    desc.format = Format::kRGBA8UNorm;
    desc.texture_type = TextureType::kTexture2D;
    desc.is_render_target = true;
    desc.use_clear_value = true;
    desc.clear_value = Color { 0.1F, 0.1F, 0.1F, 1.0F };
    desc.initial_state = ResourceStates::kCommon;
    desc.debug_name = std::string(debug_name);
    return CreateRegisteredTexture(desc);
  }
};

NOLINT_TEST_F(ShaderPassDepthStateTest,
  UsesIncompleteCanonicalDepthTextureWhileKeepingGreaterOrEqualFallback)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  constexpr std::uint32_t kWidth = 8U;
  constexpr std::uint32_t kHeight = 8U;

  auto color_texture = CreateColorTexture(kWidth, kHeight, "shader-pass.color");
  auto depth_texture = CreateDepthTexture(kWidth, kHeight, "shader-pass.depth");
  ASSERT_NE(color_texture, nullptr);
  ASSERT_NE(depth_texture, nullptr);

  auto prepared_frame = PreparedSceneFrame {};
  auto offscreen = renderer->BeginOffscreenFrame(
    { .frame_slot = Slot { 0U }, .frame_sequence = SequenceNumber { 1U } });
  offscreen.SetCurrentView(
    kTestViewId, MakeResolvedView(kWidth, kHeight), prepared_frame);
  auto& render_context = offscreen.GetRenderContext();

  auto depth_pass
    = DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .depth_texture = depth_texture,
      .debug_name = "shader-pass.depth-prepass",
    }));

  const auto depth_output = depth_pass.GetOutput();
  ASSERT_FALSE(depth_output.is_complete);
  ASSERT_EQ(depth_output.depth_texture, depth_texture.get());

  render_context.RegisterPass<DepthPrePass>(&depth_pass);
  render_context.current_view.depth_prepass_completeness
    = DepthPrePassCompleteness::kIncomplete;

  auto shader_pass
    = ShaderPass(std::make_shared<ShaderPassConfig>(ShaderPassConfig {
      .color_texture = color_texture,
      .debug_name = "shader-pass.depth-state",
    }));

  {
    auto recorder = AcquireRecorder("shader-pass.prepare.incomplete");
    ASSERT_NE(recorder, nullptr);
    EnsureTracked(*recorder, color_texture, ResourceStates::kCommon);
    EnsureTracked(*recorder, depth_texture, ResourceStates::kDepthWrite);
    PreparePassResources(shader_pass, render_context, *recorder);
  }

  EXPECT_TRUE(shader_pass.HasResolvedDepthTextureForTesting());
  EXPECT_EQ(
    shader_pass.GetBuiltDepthCompareOpForTesting(), CompareOp::kGreaterOrEqual);
}

NOLINT_TEST_F(ShaderPassDepthStateTest,
  RebuildsToDepthEqualWhenCompletenessTransitionsToComplete)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  constexpr std::uint32_t kWidth = 8U;
  constexpr std::uint32_t kHeight = 8U;

  auto color_texture
    = CreateColorTexture(kWidth, kHeight, "shader-pass.rebuild.color");
  auto depth_texture
    = CreateDepthTexture(kWidth, kHeight, "shader-pass.rebuild.depth");
  ASSERT_NE(color_texture, nullptr);
  ASSERT_NE(depth_texture, nullptr);

  auto prepared_frame = PreparedSceneFrame {};
  auto offscreen = renderer->BeginOffscreenFrame(
    { .frame_slot = Slot { 0U }, .frame_sequence = SequenceNumber { 2U } });
  offscreen.SetCurrentView(
    kTestViewId, MakeResolvedView(kWidth, kHeight), prepared_frame);
  auto& render_context = offscreen.GetRenderContext();

  auto depth_pass
    = DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .depth_texture = depth_texture,
      .debug_name = "shader-pass.rebuild.depth-prepass",
    }));

  render_context.RegisterPass<DepthPrePass>(&depth_pass);
  render_context.current_view.depth_prepass_completeness
    = DepthPrePassCompleteness::kIncomplete;

  auto shader_pass
    = ShaderPass(std::make_shared<ShaderPassConfig>(ShaderPassConfig {
      .color_texture = color_texture,
      .debug_name = "shader-pass.rebuild",
    }));

  {
    auto recorder = AcquireRecorder("shader-pass.rebuild.prepare.incomplete");
    ASSERT_NE(recorder, nullptr);
    EnsureTracked(*recorder, color_texture, ResourceStates::kCommon);
    EnsureTracked(*recorder, depth_texture, ResourceStates::kDepthWrite);
    PreparePassResources(shader_pass, render_context, *recorder);
  }

  ASSERT_EQ(
    shader_pass.GetBuiltDepthCompareOpForTesting(), CompareOp::kGreaterOrEqual);

  {
    auto recorder
      = AcquireRecorder("shader-pass.rebuild.depth-prepass.prepare");
    ASSERT_NE(recorder, nullptr);
    EnsureTracked(*recorder, depth_texture, ResourceStates::kCommon);
    PreparePassResources(depth_pass, render_context, *recorder);
  }

  ASSERT_TRUE(depth_pass.GetOutput().is_complete);
  render_context.current_view.depth_prepass_completeness
    = DepthPrePassCompleteness::kComplete;

  EXPECT_TRUE(shader_pass.NeedRebuildPipelineStateForTesting());

  {
    auto recorder = AcquireRecorder("shader-pass.rebuild.prepare.complete");
    ASSERT_NE(recorder, nullptr);
    EnsureTracked(*recorder, color_texture, ResourceStates::kCommon);
    EnsureTracked(*recorder, depth_texture, ResourceStates::kDepthWrite);
    PreparePassResources(shader_pass, render_context, *recorder);
  }

  EXPECT_EQ(shader_pass.GetBuiltDepthCompareOpForTesting(), CompareOp::kEqual);
}

} // namespace
