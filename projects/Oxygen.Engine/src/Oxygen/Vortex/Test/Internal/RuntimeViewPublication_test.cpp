//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <limits>
#include <memory>
#include <stdexcept>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Time/SimulationClock.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Vortex/Internal/PerViewScope.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Renderer.h>

#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal

namespace {

using oxygen::Graphics;
using oxygen::RendererConfig;
using oxygen::ViewId;
using oxygen::engine::FrameContext;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::QueueRole;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::TextureDesc;
using oxygen::vortex::Renderer;
using oxygen::vortex::testing::FakeGraphics;
using oxygen::vortex::testing::RendererPublicationProbe;

class RuntimeViewPublicationTest : public ::testing::Test {
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
  }

  void TearDown() override
  {
    if (renderer_) {
      renderer_->OnShutdown();
    }
  }

  [[nodiscard]] static auto MakeViewContext(std::string_view name)
    -> oxygen::engine::ViewContext
  {
    oxygen::engine::ViewContext view {};
    view.view.viewport = {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = 64.0F,
      .height = 64.0F,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    view.metadata.name = std::string(name);
    view.metadata.purpose = "runtime";
    return view;
  }

  static auto PrepareFrameContext(
    FrameContext& frame_context, const std::uint32_t frame_number) -> void
  {
    frame_context.SetCurrentPhase(oxygen::core::PhaseId::kFrameStart,
      oxygen::engine::internal::EngineTagFactory::Get());
    frame_context.SetFrameSequenceNumber(
      oxygen::frame::SequenceNumber { frame_number },
      oxygen::engine::internal::EngineTagFactory::Get());
  }

  [[nodiscard]] auto MakeFramebuffer() const -> std::shared_ptr<Framebuffer>
  {
    auto color_desc = TextureDesc {};
    color_desc.width = 64U;
    color_desc.height = 64U;
    color_desc.format = oxygen::Format::kRGBA8UNorm;
    color_desc.texture_type = oxygen::TextureType::kTexture2D;
    color_desc.is_render_target = true;
    color_desc.is_shader_resource = true;
    color_desc.initial_state = ResourceStates::kCommon;
    color_desc.debug_name = "RuntimeViewPublicationTest.Color";

    auto color = graphics_->CreateTexture(color_desc);

    auto fb_desc = FramebufferDesc {};
    fb_desc.AddColorAttachment({ .texture = color });
    return graphics_->CreateFramebuffer(fb_desc);
  }

  [[nodiscard]] auto MaterializeViewConstantsBuffer(const ViewId view_id) const
    -> std::shared_ptr<const oxygen::graphics::Buffer>
  {
    if (!framebuffer_) {
      framebuffer_ = MakeFramebuffer();
    }

    auto params = oxygen::ResolvedView::Params {};
    params.view_config.viewport = {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = 64.0F,
      .height = 64.0F,
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };

    auto result = renderer_->ForSinglePassHarness()
                    .SetFrameSession(Renderer::FrameSessionInput {
                      .frame_slot = oxygen::frame::Slot { 0U },
                      .frame_sequence = oxygen::frame::SequenceNumber { 1U },
                      .delta_time_seconds
                      = oxygen::time::SimulationClock::kMinDeltaTimeSeconds,
                    })
                    .SetOutputTarget(Renderer::OutputTargetInput {
                      .framebuffer = oxygen::observer_ptr<Framebuffer>(
                        framebuffer_.get()),
                    })
                    .SetResolvedView(Renderer::ResolvedViewInput {
                      .view_id = view_id,
                      .value = oxygen::ResolvedView(params),
                    })
                    .SetPreparedFrame(Renderer::PreparedFrameInput {})
                    .Finalize();

    EXPECT_TRUE(result.has_value());
    if (!result.has_value()) {
      return {};
    }

    return result->GetRenderContext().view_constants;
  }

  std::shared_ptr<FakeGraphics> graphics_;
  auto PublishExposureView(FrameContext& frame, const ViewId intent,
    const oxygen::vortex::CompositionView::ViewStateHandle handle,
    const ViewId source = oxygen::kInvalidViewId) -> ViewId
  {
    if (!framebuffer_)
      framebuffer_ = MakeFramebuffer();
    auto view = oxygen::vortex::CompositionView {};
    view.id = intent;
    view.name = "Exposure owner fixture";
    view.view = MakeViewContext(view.name).view;
    view.view_state_handle = handle;
    view.exposure_source_view_id = source;
    return renderer_->PublishRuntimeCompositionView(frame,
      { .composition_view = view,
        .render_target = oxygen::observer_ptr { framebuffer_.get() } });
  }

  auto ExposureOwner(FrameContext& frame, const ViewId published)
    -> oxygen::vortex::CompositionView::ViewStateHandle
  {
    auto context = oxygen::vortex::RenderContext {};
    RendererPublicationProbe::PopulateRenderContextViewState(
      *renderer_, context, frame, false);
    for (const auto& view : context.frame_views) {
      if (view.view_id == published)
        return view.exposure_view_state_handle;
    }
    ADD_FAILURE() << "Published view missing from frame";
    return oxygen::vortex::CompositionView::kInvalidViewStateHandle;
  }

  mutable std::shared_ptr<Framebuffer> framebuffer_;
  std::unique_ptr<Renderer> renderer_;
};

NOLINT_TEST_F(RuntimeViewPublicationTest,
  UpsertPublishedRuntimeViewRegistersAndUpdatesStablePublishedView)
{
  auto frame_context = FrameContext {};
  PrepareFrameContext(frame_context, 1U);

  const auto intent_view_id = ViewId { 11U };
  const auto first_published = renderer_->UpsertPublishedRuntimeView(
    frame_context, intent_view_id, MakeViewContext("first"));
  EXPECT_NE(first_published, oxygen::kInvalidViewId);
  EXPECT_EQ(
    renderer_->ResolvePublishedRuntimeViewId(intent_view_id), first_published);

  const auto second_published = renderer_->UpsertPublishedRuntimeView(
    frame_context, intent_view_id, MakeViewContext("updated"));
  EXPECT_EQ(second_published, first_published);
  EXPECT_EQ(
    frame_context.GetViewContext(first_published).metadata.name, "updated");
}

NOLINT_TEST_F(RuntimeViewPublicationTest,
  RemovePublishedRuntimeViewClearsMappingAndFrameContextView)
{
  auto frame_context = FrameContext {};
  PrepareFrameContext(frame_context, 1U);

  const auto intent_view_id = ViewId { 12U };
  const auto published_view_id = renderer_->UpsertPublishedRuntimeView(
    frame_context, intent_view_id, MakeViewContext("removable"));
  ASSERT_NE(published_view_id, oxygen::kInvalidViewId);

  renderer_->RemovePublishedRuntimeView(frame_context, intent_view_id);

  EXPECT_EQ(renderer_->ResolvePublishedRuntimeViewId(intent_view_id),
    oxygen::kInvalidViewId);
  EXPECT_THROW(
    static_cast<void>(frame_context.GetViewContext(published_view_id)),
    std::out_of_range);
}

NOLINT_TEST_F(
  RuntimeViewPublicationTest, PruneStalePublishedRuntimeViewsEvictsOldMappings)
{
  auto frame_context = FrameContext {};
  PrepareFrameContext(frame_context, 1U);

  const auto intent_view_id = ViewId { 13U };
  const auto published_view_id = renderer_->UpsertPublishedRuntimeView(
    frame_context, intent_view_id, MakeViewContext("stale"));
  ASSERT_NE(published_view_id, oxygen::kInvalidViewId);

  frame_context.SetFrameSequenceNumber(oxygen::frame::SequenceNumber { 1000U },
    oxygen::engine::internal::EngineTagFactory::Get());
  const auto pruned = renderer_->PruneStalePublishedRuntimeViews(frame_context);

  ASSERT_EQ(pruned.size(), 1U);
  EXPECT_EQ(pruned.front(), intent_view_id);
  EXPECT_EQ(renderer_->ResolvePublishedRuntimeViewId(intent_view_id),
    oxygen::kInvalidViewId);
  EXPECT_THROW(
    static_cast<void>(frame_context.GetViewContext(published_view_id)),
    std::out_of_range);
}

NOLINT_TEST_F(RuntimeViewPublicationTest,
  RemovePublishedRuntimeViewReleasesTrackedViewConstantsForPublishedView)
{
  auto frame_context = FrameContext {};
  PrepareFrameContext(frame_context, 1U);

  const auto intent_view_id = ViewId { 14U };
  const auto published_view_id = renderer_->UpsertPublishedRuntimeView(
    frame_context, intent_view_id, MakeViewContext("removable-view-constants"));
  ASSERT_NE(published_view_id, oxygen::kInvalidViewId);

  const auto first_buffer = MaterializeViewConstantsBuffer(published_view_id);
  ASSERT_NE(first_buffer, nullptr);

  renderer_->RemovePublishedRuntimeView(frame_context, intent_view_id);

  const auto second_buffer = MaterializeViewConstantsBuffer(published_view_id);
  ASSERT_NE(second_buffer, nullptr);
  EXPECT_NE(second_buffer.get(), first_buffer.get());
}

NOLINT_TEST_F(RuntimeViewPublicationTest,
  RemovePublishedRuntimeViewWithoutFrameContextReleasesTrackedViewConstants)
{
  auto frame_context = FrameContext {};
  PrepareFrameContext(frame_context, 1U);

  const auto intent_view_id = ViewId { 16U };
  const auto published_view_id = renderer_->UpsertPublishedRuntimeView(
    frame_context, intent_view_id, MakeViewContext("shutdown-removable"));
  ASSERT_NE(published_view_id, oxygen::kInvalidViewId);

  const auto first_buffer = MaterializeViewConstantsBuffer(published_view_id);
  ASSERT_NE(first_buffer, nullptr);

  renderer_->RemovePublishedRuntimeView(intent_view_id);

  EXPECT_EQ(renderer_->ResolvePublishedRuntimeViewId(intent_view_id),
    oxygen::kInvalidViewId);
  const auto second_buffer = MaterializeViewConstantsBuffer(published_view_id);
  ASSERT_NE(second_buffer, nullptr);
  EXPECT_NE(second_buffer.get(), first_buffer.get());
}

NOLINT_TEST_F(RuntimeViewPublicationTest,
  PruneStalePublishedRuntimeViewsReleasesTrackedViewConstantsForPublishedView)
{
  auto frame_context = FrameContext {};
  PrepareFrameContext(frame_context, 1U);

  const auto intent_view_id = ViewId { 15U };
  const auto published_view_id = renderer_->UpsertPublishedRuntimeView(
    frame_context, intent_view_id, MakeViewContext("stale-view-constants"));
  ASSERT_NE(published_view_id, oxygen::kInvalidViewId);

  const auto first_buffer = MaterializeViewConstantsBuffer(published_view_id);
  ASSERT_NE(first_buffer, nullptr);

  frame_context.SetFrameSequenceNumber(oxygen::frame::SequenceNumber { 1000U },
    oxygen::engine::internal::EngineTagFactory::Get());
  const auto pruned = renderer_->PruneStalePublishedRuntimeViews(frame_context);

  ASSERT_EQ(pruned.size(), 1U);
  EXPECT_EQ(pruned.front(), intent_view_id);

  const auto second_buffer = MaterializeViewConstantsBuffer(published_view_id);
  ASSERT_NE(second_buffer, nullptr);
  EXPECT_NE(second_buffer.get(), first_buffer.get());
}

NOLINT_TEST_F(RuntimeViewPublicationTest,
  PublishedExposureOverridesResolveIndependentlyAndCanBeUpdatedAndCleared)
{
  using namespace oxygen::vortex;
  auto frame = FrameContext {};
  PrepareFrameContext(frame, 1U);
  auto scene
    = std::make_shared<oxygen::scene::Scene>("ExposurePublication", 8U);
  scene->SetEnvironment(std::make_unique<oxygen::scene::SceneEnvironment>());
  auto& post = scene->GetEnvironment()
                 ->AddSystem<oxygen::scene::environment::PostProcessVolume>();
  post.SetExposureMode(oxygen::engine::ExposureMode::kManual);
  post.SetExposureKey(12.5F);
  post.SetManualExposureEv(10.0F);
  auto target = MakeFramebuffer();
  auto scene_renderer = SceneRenderer(*renderer_, *graphics_,
    SceneTexturesConfig { .extent = { 64U, 64U } }, ShadingMode::kDeferred);
  auto first = CompositionView {};
  first.id = ViewId { 31U };
  first.name = "ExposureFirst";
  first.view = MakeViewContext("first").view;
  first.view_state_handle = CompositionView::ViewStateHandle { 101U };
  first.render_settings.exposure = post.GetExposureSettings();
  first.render_settings.exposure->manual_ev = 14.0F;
  auto second = first;
  second.id = ViewId { 32U };
  second.name = "ExposureSecond";
  second.view_state_handle = CompositionView::ViewStateHandle { 102U };
  second.render_settings.exposure->manual_ev = 16.0F;

  const auto run = [&](const std::uint32_t sequence, const float first_gain) {
    PrepareFrameContext(frame, sequence);
    const auto publish = [&](const CompositionView& view) {
      return renderer_->PublishRuntimeCompositionView(frame,
        Renderer::RuntimeViewPublishInput {
          .composition_view = view,
          .render_target = oxygen::observer_ptr { target.get() },
          .composite_source = oxygen::observer_ptr { target.get() },
        });
    };
    const auto first_id = publish(first);
    const auto second_id = publish(second);
    auto context = RenderContext {};
    context.scene = oxygen::observer_ptr { scene.get() };
    context.frame_sequence = oxygen::frame::SequenceNumber { sequence };
    context.frame_slot = oxygen::frame::Slot { 0U };
    context.view_constants = MaterializeViewConstantsBuffer(first_id);
    RendererPublicationProbe::PopulateRenderContextViewState(
      *renderer_, context, frame, false);
    ASSERT_EQ(context.frame_views.size(), 2U);
    scene_renderer.OnStandaloneFrameStart(
      context.frame_sequence, context.frame_slot, glm::uvec2 { 64U, 64U });
    for (std::size_t i = 0; i < context.frame_views.size(); ++i) {
      internal::PerViewScope scope(context, i);
      scene_renderer.OnRender(context);
    }
    const auto* service
      = RendererPublicationProbe::GetPostProcessService(scene_renderer);
    ASSERT_NE(service, nullptr);
    const auto* first_bindings = service->InspectBindings(first_id);
    const auto* second_bindings = service->InspectBindings(second_id);
    ASSERT_NE(first_bindings, nullptr);
    ASSERT_NE(second_bindings, nullptr);
    EXPECT_EQ(first_bindings->fixed_exposure, first_gain);
    EXPECT_EQ(second_bindings->fixed_exposure, 0x1p-16F);
    EXPECT_TRUE(service->GetLastExecutionState().tonemap_executed);
    EXPECT_EQ(post.GetManualExposureEv(), 10.0F);
  };
  run(1U, 0x1p-14F);
  first.render_settings.exposure->manual_ev = 15.0F;
  run(2U, 0x1p-15F);
  first.render_settings.exposure.reset();
  run(3U, 0x1p-10F);
}

NOLINT_TEST_F(RuntimeViewPublicationTest,
  OnShutdownResetsViewConstantsManagerAfterRuntimeViewCleanup)
{
  auto frame_context = FrameContext {};
  PrepareFrameContext(frame_context, 1U);

  const auto intent_view_id = ViewId { 17U };
  const auto published_view_id = renderer_->UpsertPublishedRuntimeView(
    frame_context, intent_view_id, MakeViewContext("shutdown-cleanup"));
  ASSERT_NE(published_view_id, oxygen::kInvalidViewId);
  ASSERT_NE(MaterializeViewConstantsBuffer(published_view_id), nullptr);

  renderer_->OnShutdown();

  EXPECT_EQ(renderer_->ResolvePublishedRuntimeViewId(intent_view_id),
    oxygen::kInvalidViewId);
  EXPECT_EQ(RendererPublicationProbe::GetViewConstantsManager(*renderer_),
    nullptr);
}

NOLINT_TEST_F(RuntimeViewPublicationTest,
  ExposureRequestsUseRendererGenerationsAndIdempotentRetry)
{
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  using Policy = oxygen::vortex::ExposureTransitionPolicy;
  using Phase = oxygen::vortex::ExposureTransitionPhase;
  const auto first = renderer_->QueueExposureTransition(
    Handle { 1U }, Policy::kSeedFromEv100, 14.0F);
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->generation, 1U);
  EXPECT_NE(first->lifetime, 0U);
  EXPECT_EQ(renderer_->RetryExposureTransition(*first), Phase::kQueued);
  EXPECT_EQ(renderer_->RetryExposureTransition(*first), Phase::kQueued);
  const auto next
    = renderer_->QueueExposureTransition(Handle { 1U }, Policy::kRemeter);
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(next->generation, 2U);
  EXPECT_EQ(next->lifetime, first->lifetime);
  EXPECT_EQ(renderer_->RetryExposureTransition(*first), Phase::kSuperseded);
  const auto status = renderer_->InspectExposureTransition(Handle { 1U });
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->request, *next);
  EXPECT_EQ(status->applied_generation, 0U);
  const auto other
    = renderer_->QueueExposureTransition(Handle { 2U }, Policy::kPreserve);
  ASSERT_TRUE(other.has_value());
  EXPECT_EQ(other->generation, 1U);
  EXPECT_NE(other->lifetime, first->lifetime);
}

NOLINT_TEST_F(RuntimeViewPublicationTest,
  ConflictingAndUnknownExposureTokensCannotReplaceIntent)
{
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  using Policy = oxygen::vortex::ExposureTransitionPolicy;
  using Error = oxygen::vortex::ExposureTransitionError;
  const auto issued = renderer_->QueueExposureTransition(
    Handle { 1U }, Policy::kSeedFromEv100, 14.0F);
  ASSERT_TRUE(issued.has_value());
  auto conflict = *issued;
  conflict.seed_ev = 15.0F;
  EXPECT_EQ(renderer_->RetryExposureTransition(conflict).error(),
    Error::kConflictingToken);
  auto unknown = *issued;
  ++unknown.generation;
  EXPECT_EQ(
    renderer_->RetryExposureTransition(unknown).error(), Error::kUnknownToken);
  unknown = *issued;
  ++unknown.lifetime;
  EXPECT_EQ(
    renderer_->RetryExposureTransition(unknown).error(), Error::kUnknownToken);
  EXPECT_EQ(
    renderer_->InspectExposureTransition(Handle { 1U })->request, *issued);
}

NOLINT_TEST_F(RuntimeViewPublicationTest,
  InvalidExposureRequestSyntaxDoesNotAllocateGeneration)
{
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  using Policy = oxygen::vortex::ExposureTransitionPolicy;
  using Error = oxygen::vortex::ExposureTransitionError;
  EXPECT_EQ(renderer_
              ->QueueExposureTransition(
                oxygen::vortex::CompositionView::kInvalidViewStateHandle,
                Policy::kRemeter)
              .error(),
    Error::kInvalidTarget);
  EXPECT_EQ(
    renderer_->QueueExposureTransition(Handle { 1U }, Policy::kSeedFromEv100)
      .error(),
    Error::kInvalidSeed);
  EXPECT_EQ(
    renderer_->QueueExposureTransition(Handle { 1U }, Policy::kPreserve, 0.0F)
      .error(),
    Error::kInvalidSeed);
  EXPECT_EQ(renderer_
              ->QueueExposureTransition(Handle { 1U }, Policy::kSeedFromEv100,
                std::numeric_limits<float>::infinity())
              .error(),
    Error::kInvalidSeed);
  EXPECT_FALSE(renderer_->InspectExposureTransition(Handle { 1U }).has_value());
  const auto valid
    = renderer_->QueueExposureTransition(Handle { 1U }, Policy::kRemeter);
  ASSERT_TRUE(valid.has_value());
  EXPECT_EQ(valid->generation, 1U);
}

NOLINT_TEST_F(
  RuntimeViewPublicationTest, ShutdownRejectsNewExposureRequestsAndRetries)
{
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  using Policy = oxygen::vortex::ExposureTransitionPolicy;
  using Error = oxygen::vortex::ExposureTransitionError;
  const auto issued
    = renderer_->QueueExposureTransition(Handle { 1U }, Policy::kRemeter);
  ASSERT_TRUE(issued.has_value());
  renderer_->OnShutdown();
  EXPECT_EQ(
    renderer_->QueueExposureTransition(Handle { 1U }, Policy::kRemeter).error(),
    Error::kRendererUnavailable);
  EXPECT_EQ(renderer_->RetryExposureTransition(*issued).error(),
    Error::kRendererUnavailable);
  EXPECT_FALSE(renderer_->InspectExposureTransition(Handle { 1U }).has_value());
}

NOLINT_TEST_F(RuntimeViewPublicationTest,
  PreparedViewFamilyPinsExposureThroughLateSceneMutation)
{
  using namespace oxygen::vortex;
  auto frame = FrameContext {};
  PrepareFrameContext(frame, 1U);
  auto scene = std::make_shared<oxygen::scene::Scene>("PinnedExposure", 8U);
  scene->SetEnvironment(std::make_unique<oxygen::scene::SceneEnvironment>());
  auto& post = scene->GetEnvironment()
                 ->AddSystem<oxygen::scene::environment::PostProcessVolume>();
  post.SetExposureMode(oxygen::engine::ExposureMode::kManual);
  post.SetExposureKey(12.5F);
  post.SetManualExposureEv(4.0F);
  const auto first = PublishExposureView(
    frame, ViewId { 1U }, CompositionView::ViewStateHandle { 11U });
  const auto second = PublishExposureView(
    frame, ViewId { 2U }, CompositionView::ViewStateHandle { 22U });
  auto scene_renderer = SceneRenderer(*renderer_, *graphics_,
    SceneTexturesConfig { .extent = { 64U, 64U } }, ShadingMode::kDeferred);
  auto context = RenderContext {};
  context.scene = oxygen::observer_ptr { scene.get() };
  context.frame_sequence = oxygen::frame::SequenceNumber { 1U };
  context.frame_slot = oxygen::frame::Slot { 0U };
  context.view_constants = MaterializeViewConstantsBuffer(first);
  RendererPublicationProbe::PopulateRenderContextViewState(
    *renderer_, context, frame, false);
  const auto render = [&](const float expected) {
    for (std::size_t i = 0; i < context.frame_views.size(); ++i) {
      internal::PerViewScope scope(context, i);
      scene_renderer.OnRender(context);
    }
    const auto* service
      = RendererPublicationProbe::GetPostProcessService(scene_renderer);
    ASSERT_NE(service, nullptr);
    for (const auto view : { first, second }) {
      const auto* bindings = service->InspectBindings(view);
      ASSERT_NE(bindings, nullptr);
      EXPECT_EQ(bindings->fixed_exposure, expected);
    }
  };
  scene_renderer.OnStandaloneFrameStart(
    context.frame_sequence, context.frame_slot, glm::uvec2 { 64U, 64U });
  scene_renderer.PrimePreparedViews(context);
  post.SetManualExposureEv(8.0F);
  render(0x1p-4F);
  context.frame_sequence = oxygen::frame::SequenceNumber { 2U };
  context.frame_slot = oxygen::frame::Slot { 1U };
  scene_renderer.OnStandaloneFrameStart(
    context.frame_sequence, context.frame_slot, glm::uvec2 { 64U, 64U });
  render(0x1p-8F);
}

NOLINT_TEST_F(RuntimeViewPublicationTest, ExposureChainsResolveTheCurrentRoot)
{
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  auto frame = FrameContext {};
  PrepareFrameContext(frame, 1U);
  const auto first = PublishExposureView(frame, ViewId { 1U }, Handle { 11U });
  const auto other = PublishExposureView(frame, ViewId { 4U }, Handle { 44U });
  const auto middle
    = PublishExposureView(frame, ViewId { 2U }, Handle { 22U }, ViewId { 1U });
  const auto leaf
    = PublishExposureView(frame, ViewId { 3U }, Handle { 33U }, ViewId { 2U });
  EXPECT_EQ(ExposureOwner(frame, first), Handle { 11U });
  EXPECT_EQ(ExposureOwner(frame, middle), Handle { 11U });
  EXPECT_EQ(ExposureOwner(frame, leaf), Handle { 11U });
  EXPECT_EQ(ExposureOwner(frame, other), Handle { 44U });
  EXPECT_EQ(
    PublishExposureView(frame, ViewId { 2U }, Handle { 22U }, ViewId { 4U }),
    middle);
  EXPECT_EQ(ExposureOwner(frame, leaf), Handle { 44U });
}

NOLINT_TEST_F(
  RuntimeViewPublicationTest, InvalidExposureEdgesAreRejectedAtomically)
{
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  auto frame = FrameContext {};
  PrepareFrameContext(frame, 1U);
  const auto root = PublishExposureView(frame, ViewId { 1U }, Handle { 11U });
  const auto child
    = PublishExposureView(frame, ViewId { 2U }, Handle { 22U }, ViewId { 1U });
  EXPECT_EQ(
    PublishExposureView(frame, ViewId { 1U }, Handle { 55U }, ViewId { 2U }),
    oxygen::kInvalidViewId);
  EXPECT_EQ(
    PublishExposureView(frame, ViewId { 2U }, Handle { 66U }, ViewId { 999U }),
    oxygen::kInvalidViewId);
  EXPECT_EQ(ExposureOwner(frame, root), Handle { 11U });
  EXPECT_EQ(ExposureOwner(frame, child), Handle { 11U });
  EXPECT_EQ(PublishExposureView(frame, ViewId { 3U }, Handle { 11U }),
    oxygen::kInvalidViewId);
  EXPECT_EQ(renderer_->ResolvePublishedRuntimeViewId(ViewId { 3U }),
    oxygen::kInvalidViewId);
}

NOLINT_TEST_F(
  RuntimeViewPublicationTest, ExposureSharingRequiresPersistentParticipants)
{
  using View = oxygen::vortex::CompositionView;
  using Handle = View::ViewStateHandle;
  auto frame = FrameContext {};
  PrepareFrameContext(frame, 1U);
  ASSERT_NE(
    PublishExposureView(frame, ViewId { 1U }, View::kInvalidViewStateHandle),
    oxygen::kInvalidViewId);
  EXPECT_EQ(
    PublishExposureView(frame, ViewId { 2U }, Handle { 22U }, ViewId { 1U }),
    oxygen::kInvalidViewId);
  const auto root = PublishExposureView(frame, ViewId { 1U }, Handle { 11U });
  EXPECT_EQ(PublishExposureView(frame, ViewId { 2U },
              View::kInvalidViewStateHandle, ViewId { 1U }),
    oxygen::kInvalidViewId);
  ASSERT_NE(
    PublishExposureView(frame, ViewId { 2U }, Handle { 22U }, ViewId { 1U }),
    oxygen::kInvalidViewId);
  EXPECT_EQ(
    PublishExposureView(frame, ViewId { 1U }, View::kInvalidViewStateHandle),
    oxygen::kInvalidViewId);
  EXPECT_EQ(ExposureOwner(frame, root), Handle { 11U });
}

NOLINT_TEST_F(
  RuntimeViewPublicationTest, ActiveConsumerRetainsInactiveExposureChain)
{
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  auto frame = FrameContext {};
  PrepareFrameContext(frame, 1U);
  PublishExposureView(frame, ViewId { 1U }, Handle { 11U });
  PublishExposureView(frame, ViewId { 2U }, Handle { 22U }, ViewId { 1U });
  const auto child
    = PublishExposureView(frame, ViewId { 3U }, Handle { 33U }, ViewId { 2U });
  frame.SetFrameSequenceNumber(oxygen::frame::SequenceNumber { 1000U },
    oxygen::engine::internal::EngineTagFactory::Get());
  ASSERT_EQ(
    PublishExposureView(frame, ViewId { 3U }, Handle { 33U }, ViewId { 2U }),
    child);
  EXPECT_TRUE(renderer_->PruneStalePublishedRuntimeViews(frame).empty());
  EXPECT_EQ(ExposureOwner(frame, child), Handle { 11U });
  renderer_->RemovePublishedRuntimeView(frame, ViewId { 3U });
  EXPECT_EQ(renderer_->PruneStalePublishedRuntimeViews(frame).size(), 2U);
}

NOLINT_TEST_F(
  RuntimeViewPublicationTest, RemovedExposureRootDetachesEveryConsumer)
{
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  auto frame = FrameContext {};
  PrepareFrameContext(frame, 1U);
  PublishExposureView(frame, ViewId { 1U }, Handle { 11U });
  const auto middle
    = PublishExposureView(frame, ViewId { 2U }, Handle { 22U }, ViewId { 1U });
  const auto leaf
    = PublishExposureView(frame, ViewId { 3U }, Handle { 33U }, ViewId { 2U });
  renderer_->RemovePublishedRuntimeView(frame, ViewId { 1U });
  EXPECT_EQ(ExposureOwner(frame, middle), Handle { 22U });
  EXPECT_EQ(ExposureOwner(frame, leaf), Handle { 33U });
}

NOLINT_TEST_F(
  RuntimeViewPublicationTest, RemovedIntermediateDetachesOnlyItsConsumers)
{
  using Handle = oxygen::vortex::CompositionView::ViewStateHandle;
  auto frame = FrameContext {};
  PrepareFrameContext(frame, 1U);
  PublishExposureView(frame, ViewId { 1U }, Handle { 11U });
  PublishExposureView(frame, ViewId { 2U }, Handle { 22U }, ViewId { 1U });
  const auto leaf
    = PublishExposureView(frame, ViewId { 3U }, Handle { 33U }, ViewId { 2U });
  const auto other
    = PublishExposureView(frame, ViewId { 4U }, Handle { 44U }, ViewId { 1U });
  renderer_->RemovePublishedRuntimeView(frame, ViewId { 2U });
  EXPECT_EQ(ExposureOwner(frame, leaf), Handle { 33U });
  EXPECT_EQ(ExposureOwner(frame, other), Handle { 11U });
}

} // namespace
