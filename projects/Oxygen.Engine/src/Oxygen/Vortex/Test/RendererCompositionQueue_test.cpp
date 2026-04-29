//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <memory>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Vortex/FacadePresets.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/ViewExtension.h>

#include "Fakes/Graphics.h"

namespace {

using oxygen::Format;
using oxygen::Graphics;
using oxygen::RendererConfig;
using oxygen::TextureType;
using oxygen::ViewId;
using oxygen::engine::FrameContext;
using oxygen::engine::ViewContext;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::QueueRole;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::Surface;
using oxygen::graphics::Texture;
using oxygen::vortex::Renderer;
using oxygen::vortex::ShadingMode;
using oxygen::vortex::CompositionView;
using oxygen::vortex::IViewExtension;
using oxygen::vortex::PostCompositionContext;
using oxygen::vortex::testing::FakeGraphics;

class FakeSurface final : public Surface {
public:
  explicit FakeSurface(std::shared_ptr<Texture> backbuffer)
    : Surface("VortexRendererCompositionQueueTest.Surface")
    , backbuffer_(std::move(backbuffer))
  {
  }

  auto Resize() -> void override { }
  auto GetCurrentBackBufferIndex() const -> uint32_t override { return 0U; }
  auto GetCurrentBackBuffer() const -> std::shared_ptr<Texture> override
  {
    return backbuffer_;
  }
  auto GetBackBuffer(uint32_t index) const -> std::shared_ptr<Texture> override
  {
    return index == 0U ? backbuffer_ : nullptr;
  }
  auto Present() const -> void override { ++present_count_; }
  [[nodiscard]] auto Width() const -> uint32_t override
  {
    return backbuffer_->GetDescriptor().width;
  }
  [[nodiscard]] auto Height() const -> uint32_t override
  {
    return backbuffer_->GetDescriptor().height;
  }

  mutable std::uint32_t present_count_ { 0U };

private:
  std::shared_ptr<Texture> backbuffer_;
};

class RendererCompositionQueueTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    graphics_ = std::make_shared<FakeGraphics>();
    graphics_->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

    auto config = RendererConfig {};
    config.upload_queue_key
      = graphics_->QueueKeyFor(QueueRole::kGraphics).get();
    renderer_ = std::make_unique<Renderer>(
      std::weak_ptr<Graphics>(graphics_), std::move(config));

    frame_context_ = std::make_unique<FrameContext>();
  }

  void TearDown() override
  {
    if (renderer_) {
      renderer_->OnShutdown();
    }
  }

  [[nodiscard]] auto MakeColorTexture(std::string_view debug_name) const
    -> std::shared_ptr<Texture>
  {
    auto desc = oxygen::graphics::TextureDesc {};
    desc.width = 64U;
    desc.height = 64U;
    desc.format = Format::kRGBA8UNorm;
    desc.texture_type = TextureType::kTexture2D;
    desc.is_render_target = true;
    desc.is_shader_resource = true;
    desc.use_clear_value = true;
    desc.clear_value = { 0.0F, 0.0F, 0.0F, 1.0F };
    desc.initial_state = ResourceStates::kCommon;
    desc.debug_name = std::string(debug_name);
    return graphics_->CreateTexture(desc);
  }

  [[nodiscard]] auto MakeFramebuffer(
    const std::shared_ptr<Texture>& texture) const
    -> std::shared_ptr<Framebuffer>
  {
    auto fb_desc = FramebufferDesc {};
    fb_desc.AddColorAttachment({ .texture = texture });
    return graphics_->CreateFramebuffer(fb_desc);
  }

  [[nodiscard]] auto MakeSubmission(
    std::string_view source_name, const std::shared_ptr<Framebuffer>& target)
    -> oxygen::vortex::CompositionSubmission
  {
    auto source = MakeColorTexture(source_name);
    graphics_->GetResourceRegistry().Register(source);

    oxygen::vortex::CompositionSubmission submission;
    submission.composite_target = target;
    submission.tasks.push_back(
      oxygen::vortex::CompositingTask::MakeTextureBlend(source,
        oxygen::ViewPort { .top_left_x = 0.0F,
          .top_left_y = 0.0F,
          .width = 64.0F,
          .height = 64.0F,
          .min_depth = 0.0F,
          .max_depth = 1.0F },
        1.0F));
    return submission;
  }

  [[nodiscard]] auto MakeRuntimeViewContext(
    const std::shared_ptr<Framebuffer>& render_target,
    const std::shared_ptr<Framebuffer>& composite_source) const -> ViewContext
  {
    auto view_context = ViewContext {};
    view_context.view.viewport = {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = 64.0F,
      .height = 64.0F,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    view_context.metadata.name = "RuntimeScene";
    view_context.metadata.purpose = "scene";
    view_context.metadata.is_scene_view = true;
    view_context.render_target = oxygen::observer_ptr { render_target.get() };
    view_context.composite_source
      = oxygen::observer_ptr { composite_source.get() };
    return view_context;
  }

  std::shared_ptr<FakeGraphics> graphics_;
  std::unique_ptr<Renderer> renderer_;
  std::unique_ptr<FrameContext> frame_context_;
};

NOLINT_TEST_F(RendererCompositionQueueTest,
  RegisterCompositionQueuesMultipleSubmissionsForSingleTarget)
{
  auto surface_texture = MakeColorTexture("Queue.Surface");
  auto surface = std::make_shared<FakeSurface>(surface_texture);
  auto target = MakeFramebuffer(surface_texture);

  frame_context_->AddSurface(oxygen::observer_ptr<Surface> { surface.get() });

  auto harness
    = oxygen::vortex::harness::single_pass::presets::ForFullscreenGraphicsPass(
      *renderer_,
      Renderer::FrameSessionInput {
        .frame_slot = oxygen::frame::Slot { 0U },
        .frame_sequence = oxygen::frame::SequenceNumber { 1U },
      },
      oxygen::observer_ptr<Framebuffer> { target.get() });
  auto active_frame = harness.Finalize();
  ASSERT_TRUE(active_frame.has_value());

  graphics_->draw_log_.draws.clear();

  renderer_->RegisterComposition(
    MakeSubmission("Queue.SourceA", target), surface);
  renderer_->RegisterComposition(
    MakeSubmission("Queue.SourceB", target), surface);

  auto loop = oxygen::co::testing::TestEventLoop {};
  oxygen::co::Run(loop, [&]() -> oxygen::co::Co<void> {
    co_await renderer_->OnCompositing(
      oxygen::observer_ptr<FrameContext> { frame_context_.get() });
  });

  EXPECT_EQ(graphics_->draw_log_.draws.size(), 2U);
  EXPECT_TRUE(frame_context_->IsSurfacePresentable(0));
}

NOLINT_TEST_F(RendererCompositionQueueTest,
  RegisterCompositionQueuesMultipleTargetsWithinFrame)
{
  auto surface_a_texture = MakeColorTexture("Queue.SurfaceA");
  auto surface_b_texture = MakeColorTexture("Queue.SurfaceB");
  auto surface_a = std::make_shared<FakeSurface>(surface_a_texture);
  auto surface_b = std::make_shared<FakeSurface>(surface_b_texture);
  auto target_a = MakeFramebuffer(surface_a_texture);
  auto target_b = MakeFramebuffer(surface_b_texture);

  frame_context_->AddSurface(oxygen::observer_ptr<Surface> { surface_a.get() });
  frame_context_->AddSurface(oxygen::observer_ptr<Surface> { surface_b.get() });

  auto harness
    = oxygen::vortex::harness::single_pass::presets::ForFullscreenGraphicsPass(
      *renderer_,
      Renderer::FrameSessionInput {
        .frame_slot = oxygen::frame::Slot { 0U },
        .frame_sequence = oxygen::frame::SequenceNumber { 1U },
      },
      oxygen::observer_ptr<Framebuffer> { target_a.get() });
  auto active_frame = harness.Finalize();
  ASSERT_TRUE(active_frame.has_value());

  renderer_->RegisterComposition(
    MakeSubmission("Queue.SourceA", target_a), surface_a);
  renderer_->RegisterComposition(
    MakeSubmission("Queue.SourceB", target_b), surface_b);

  auto loop = oxygen::co::testing::TestEventLoop {};
  oxygen::co::Run(loop, [&]() -> oxygen::co::Co<void> {
    co_await renderer_->OnCompositing(
      oxygen::observer_ptr<FrameContext> { frame_context_.get() });
  });

  EXPECT_EQ(graphics_->draw_log_.draws.size(), 2U);
  EXPECT_TRUE(frame_context_->IsSurfacePresentable(0));
  EXPECT_TRUE(frame_context_->IsSurfacePresentable(1));
}

NOLINT_TEST_F(
  RendererCompositionQueueTest, OnCompositingDrainsQueuedSubmissionsExactlyOnce)
{
  auto surface_texture = MakeColorTexture("Queue.SingleSurface");
  auto surface = std::make_shared<FakeSurface>(surface_texture);
  auto target = MakeFramebuffer(surface_texture);

  frame_context_->AddSurface(oxygen::observer_ptr<Surface> { surface.get() });

  auto harness
    = oxygen::vortex::harness::single_pass::presets::ForFullscreenGraphicsPass(
      *renderer_,
      Renderer::FrameSessionInput {
        .frame_slot = oxygen::frame::Slot { 0U },
        .frame_sequence = oxygen::frame::SequenceNumber { 1U },
      },
      oxygen::observer_ptr<Framebuffer> { target.get() });
  auto active_frame = harness.Finalize();
  ASSERT_TRUE(active_frame.has_value());

  renderer_->RegisterComposition(
    MakeSubmission("Queue.Drain", target), surface);

  auto loop = oxygen::co::testing::TestEventLoop {};
  oxygen::co::Run(loop, [&]() -> oxygen::co::Co<void> {
    co_await renderer_->OnCompositing(
      oxygen::observer_ptr<FrameContext> { frame_context_.get() });
  });

  const auto first_draw_count = graphics_->draw_log_.draws.size();
  EXPECT_EQ(first_draw_count, 1U);

  oxygen::co::Run(loop, [&]() -> oxygen::co::Co<void> {
    co_await renderer_->OnCompositing(
      oxygen::observer_ptr<FrameContext> { frame_context_.get() });
  });

  EXPECT_EQ(graphics_->draw_log_.draws.size(), first_draw_count);
}

NOLINT_TEST_F(RendererCompositionQueueTest,
  RegisterRuntimeCompositionCopiesPublishedSingleViewToPresentationTarget)
{
  auto surface_texture = MakeColorTexture("Queue.RuntimeSurface");
  auto surface = std::make_shared<FakeSurface>(surface_texture);
  auto present_target = MakeFramebuffer(surface_texture);

  frame_context_->AddSurface(oxygen::observer_ptr<Surface> { surface.get() });

  auto harness
    = oxygen::vortex::harness::single_pass::presets::ForFullscreenGraphicsPass(
      *renderer_,
      Renderer::FrameSessionInput {
        .frame_slot = oxygen::frame::Slot { 0U },
        .frame_sequence = oxygen::frame::SequenceNumber { 1U },
      },
      oxygen::observer_ptr<Framebuffer> { present_target.get() });
  auto active_frame = harness.Finalize();
  ASSERT_TRUE(active_frame.has_value());

  auto scene_texture = MakeColorTexture("Queue.RuntimeScene");
  graphics_->GetResourceRegistry().Register(scene_texture);
  auto scene_target = MakeFramebuffer(scene_texture);

  constexpr auto intent_view_id = ViewId { 41U };
  const auto published_view_id = renderer_->UpsertPublishedRuntimeView(
    *frame_context_, intent_view_id,
    MakeRuntimeViewContext(scene_target, scene_target), ShadingMode::kDeferred);
  EXPECT_EQ(
    renderer_->ResolvePublishedRuntimeViewId(intent_view_id), published_view_id);

  graphics_->texture_copy_log_.copies.clear();
  graphics_->draw_log_.draws.clear();

  renderer_->RegisterRuntimeComposition(Renderer::RuntimeCompositionInput {
    .layers = {
      Renderer::RuntimeCompositionLayer {
        .intent_view_id = intent_view_id,
        .viewport = {
          .top_left_x = 0.0F,
          .top_left_y = 0.0F,
          .width = 64.0F,
          .height = 64.0F,
          .min_depth = 0.0F,
          .max_depth = 1.0F,
        },
        .opacity = 1.0F,
      },
    },
    .composite_target = present_target,
    .target_surface = surface,
  });

  auto loop = oxygen::co::testing::TestEventLoop {};
  oxygen::co::Run(loop, [&]() -> oxygen::co::Co<void> {
    co_await renderer_->OnCompositing(
      oxygen::observer_ptr<FrameContext> { frame_context_.get() });
  });

  ASSERT_EQ(graphics_->texture_copy_log_.copies.size(), 1U);
  EXPECT_EQ(graphics_->texture_copy_log_.copies[0].src, scene_texture.get());
  EXPECT_EQ(graphics_->texture_copy_log_.copies[0].dst, surface_texture.get());
  EXPECT_TRUE(graphics_->draw_log_.draws.empty());
  EXPECT_TRUE(frame_context_->IsSurfacePresentable(0));
}

NOLINT_TEST_F(RendererCompositionQueueTest,
  RegisterRuntimeCompositionBlendsTextureLayerToPresentationTarget)
{
  auto surface_texture = MakeColorTexture("Queue.RuntimeTextureSurface");
  auto surface = std::make_shared<FakeSurface>(surface_texture);
  auto present_target = MakeFramebuffer(surface_texture);

  frame_context_->AddSurface(oxygen::observer_ptr<Surface> { surface.get() });

  auto harness
    = oxygen::vortex::harness::single_pass::presets::ForFullscreenGraphicsPass(
      *renderer_,
      Renderer::FrameSessionInput {
        .frame_slot = oxygen::frame::Slot { 0U },
        .frame_sequence = oxygen::frame::SequenceNumber { 1U },
      },
      oxygen::observer_ptr<Framebuffer> { present_target.get() });
  auto active_frame = harness.Finalize();
  ASSERT_TRUE(active_frame.has_value());

  auto source_texture = MakeColorTexture("Queue.RuntimeTextureLayer");
  graphics_->GetResourceRegistry().Register(source_texture);

  graphics_->texture_copy_log_.copies.clear();
  graphics_->draw_log_.draws.clear();

  renderer_->RegisterRuntimeComposition(Renderer::RuntimeCompositionInput {
    .texture_layers = {
      Renderer::RuntimeTextureCompositionLayer {
        .source_texture = source_texture,
        .viewport = {
          .top_left_x = 4.0F,
          .top_left_y = 6.0F,
          .width = 48.0F,
          .height = 40.0F,
          .min_depth = 0.0F,
          .max_depth = 1.0F,
        },
        .opacity = 1.0F,
        .debug_name = "Queue.RuntimeTextureLayer.Composite",
      },
    },
    .composite_target = present_target,
    .target_surface = surface,
  });

  auto loop = oxygen::co::testing::TestEventLoop {};
  oxygen::co::Run(loop, [&]() -> oxygen::co::Co<void> {
    co_await renderer_->OnCompositing(
      oxygen::observer_ptr<FrameContext> { frame_context_.get() });
  });

  EXPECT_TRUE(graphics_->texture_copy_log_.copies.empty());
  EXPECT_EQ(graphics_->draw_log_.draws.size(), 1U);
  EXPECT_TRUE(frame_context_->IsSurfacePresentable(0));
}

NOLINT_TEST_F(RendererCompositionQueueTest,
  SurfaceOverlayBatchesRunAfterCompositionBeforePresentable)
{
  auto surface_texture = MakeColorTexture("Queue.SurfaceOverlaySurface");
  auto surface = std::make_shared<FakeSurface>(surface_texture);
  auto target = MakeFramebuffer(surface_texture);

  frame_context_->AddSurface(oxygen::observer_ptr<Surface> { surface.get() });

  auto harness
    = oxygen::vortex::harness::single_pass::presets::ForFullscreenGraphicsPass(
      *renderer_,
      Renderer::FrameSessionInput {
        .frame_slot = oxygen::frame::Slot { 0U },
        .frame_sequence = oxygen::frame::SequenceNumber { 1U },
      },
      oxygen::observer_ptr<Framebuffer> { target.get() });
  auto active_frame = harness.Finalize();
  ASSERT_TRUE(active_frame.has_value());

  auto overlay_ran = false;
  auto submission = MakeSubmission("Queue.SurfaceOverlaySource", target);
  submission.surface_overlays.push_back(CompositionView::OverlayBatch {
    .lane = CompositionView::OverlayLane::kSurfaceScreen,
    .target = CompositionView::OverlayTarget::kSurface,
    .view_id = ViewId { 0U },
    .surface_id = CompositionView::kDefaultSurfaceRoute,
    .priority = 0,
    .debug_name = "Queue.SurfaceOverlay",
    .record = [&](oxygen::graphics::CommandRecorder& recorder) {
      EXPECT_FALSE(frame_context_->IsSurfacePresentable(0));
      overlay_ran = true;
      recorder.Draw(77U, 1U, 0U, 0U);
    },
  });

  renderer_->RegisterComposition(std::move(submission), surface);

  auto loop = oxygen::co::testing::TestEventLoop {};
  oxygen::co::Run(loop, [&]() -> oxygen::co::Co<void> {
    co_await renderer_->OnCompositing(
      oxygen::observer_ptr<FrameContext> { frame_context_.get() });
  });

  ASSERT_TRUE(overlay_ran);
  ASSERT_GE(graphics_->draw_log_.draws.size(), 2U);
  EXPECT_EQ(graphics_->draw_log_.draws.back().vertex_num, 77U);
  EXPECT_TRUE(frame_context_->IsSurfacePresentable(0));
}

NOLINT_TEST_F(RendererCompositionQueueTest,
  ViewExtensionPostCompositionRunsBeforePresentable)
{
  class RecordingExtension final : public IViewExtension {
  public:
    explicit RecordingExtension(bool& called)
      : called_(called)
    {
    }

    auto OnPostComposition(const PostCompositionContext& context)
      -> void override
    {
      EXPECT_EQ(
        context.surface_id, CompositionView::SurfaceRouteId { 12U });
      EXPECT_FALSE(context.frame_context.IsSurfacePresentable(0));
      called_ = true;
      context.recorder.Draw(88U, 1U, 0U, 0U);
    }

  private:
    bool& called_;
  };

  auto surface_texture = MakeColorTexture("Queue.ExtensionSurface");
  auto surface = std::make_shared<FakeSurface>(surface_texture);
  auto target = MakeFramebuffer(surface_texture);

  frame_context_->AddSurface(oxygen::observer_ptr<Surface> { surface.get() });

  auto harness
    = oxygen::vortex::harness::single_pass::presets::ForFullscreenGraphicsPass(
      *renderer_,
      Renderer::FrameSessionInput {
        .frame_slot = oxygen::frame::Slot { 0U },
        .frame_sequence = oxygen::frame::SequenceNumber { 1U },
      },
      oxygen::observer_ptr<Framebuffer> { target.get() });
  auto active_frame = harness.Finalize();
  ASSERT_TRUE(active_frame.has_value());

  auto extension_called = false;
  renderer_->RegisterViewExtension(
    std::make_shared<RecordingExtension>(extension_called));

  auto submission = MakeSubmission("Queue.ExtensionSource", target);
  submission.surface_id = CompositionView::SurfaceRouteId { 12U };
  renderer_->RegisterComposition(std::move(submission), surface);

  auto loop = oxygen::co::testing::TestEventLoop {};
  oxygen::co::Run(loop, [&]() -> oxygen::co::Co<void> {
    co_await renderer_->OnCompositing(
      oxygen::observer_ptr<FrameContext> { frame_context_.get() });
  });

  ASSERT_TRUE(extension_called);
  ASSERT_GE(graphics_->draw_log_.draws.size(), 2U);
  EXPECT_EQ(graphics_->draw_log_.draws.back().vertex_num, 88U);
  EXPECT_TRUE(frame_context_->IsSurfacePresentable(0));
}

} // namespace
