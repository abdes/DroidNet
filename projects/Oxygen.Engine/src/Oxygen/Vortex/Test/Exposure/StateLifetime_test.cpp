//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <utility>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/Resources/TextureBinder.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderPayloads.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>
#include <Oxygen/Vortex/Types/ExposureTransition.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>

namespace oxygen::vortex::testing::exposure {

using graphics::FramebufferDesc;
using graphics::ResourceStates;
using graphics::TextureDesc;

NOLINT_TEST_F(ExposureGpuTest, PriorStateLeaseRemainsImmutableAcrossLaterSolves)
{
  const auto first = Run(Uniform(.25F));
  const auto retained = last_state_;
  const auto second = Run(Uniform(8.0F), {}, 10.0F);
  EXPECT_NE(second.state.displayed_scale, first.state.displayed_scale);
  EXPECT_NE(retained->buffer, last_state_->buffer);
  const auto reread = Read<ExposureStateData>(
    *retained->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(reread.displayed_scale, first.state.displayed_scale);
  EXPECT_EQ(reread.frame_sequence, first.state.frame_sequence);
}

NOLINT_TEST_F(ExposureGpuTest,
  TwoViewsInitializeIndependentlyWithoutWaitingBetweenSubmissions)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 4.0F;
  auto config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber {
    1U,
  };
  const auto first = SubmitCommands(
    "Vortex Exposure", [&](graphics::CommandRecorder& recorder) -> auto {
      return pass_->Execute(ctx_, recorder, config, {});
    });
  ASSERT_TRUE(first.executed);
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
    2U,
  };
  ctx_.current_view.view_id = ViewId {
    2U,
  };
  settings.manual_ev = 8.0F;
  config = SharedConfig(settings);
  const auto second = SubmitCommands(
    "Vortex Exposure", [&](graphics::CommandRecorder& recorder) -> auto {
      return pass_->Execute(ctx_, recorder, config, {});
    });
  ASSERT_TRUE(second.executed);
  EXPECT_NE(first.exposure_buffer, second.exposure_buffer);
  EXPECT_EQ(Read<ExposureStateData>(
              *first.exposure_buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    0x1p-4F);
  EXPECT_EQ(Read<ExposureStateData>(
              *second.exposure_buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    0x1p-8F);
  const auto duplicate = SubmitCommands(
    "Vortex Exposure", [&](graphics::CommandRecorder& recorder) -> auto {
      return pass_->Execute(ctx_, recorder, config, {});
    });
  EXPECT_FALSE(duplicate.executed);
  EXPECT_EQ(duplicate.state, second.state);
}

NOLINT_TEST_F(ExposureGpuTest,
  BackloggedStatusAcknowledgesLatestSubmissionWithoutRenderingOwnerAgain)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F);
  std::optional<ExposureTransitionToken> latest;
  // Withhold CPU delivery while real GPU status copies fill the bounded queue.
  // Subsequent completed states must coalesce into one retained catch-up
  // record.
  for (unsigned i = 1U; i <= 6U; ++i) {
    const auto token
      = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
        ExposureTransitionPolicy::kSeedFromEv100, static_cast<float>(i));
    if (!token.has_value()) {
      FAIL() << "Expected token to contain a value";
    }
    latest = *token;
    const auto result = Run(signal, {}, 0.0F, nullptr, 1.0F, true, *token);
    EXPECT_EQ(result.state.applied_generation.at(0), token->generation);
    vortex::testing::RendererPublicationProbe::EnqueueExposureStatus(
      service, *token, last_state_, ctx_, sequence_);
  }
  if (!latest.has_value()) {
    FAIL() << "Expected latest to contain a value";
  }
  const auto full
    = vortex::testing::RendererPublicationProbe::ExposureStatusCounts(
      service, latest->target);
  EXPECT_EQ(full.first, frame::kFramesInFlight.get());
  EXPECT_EQ(full.second, 1U);
  service.OnFrameStart(
    frame::SequenceNumber {
      ++sequence_,
    },
    frame::Slot {
      1U,
    });
  EXPECT_EQ(InspectRequiredTransition(latest->target).phase,
    ExposureTransitionPhase::kQueued);
  EXPECT_EQ(InspectRequiredTransition(latest->target).applied_generation, 3U);
  WaitForQueueIdle();
  service.OnFrameStart(
    frame::SequenceNumber {
      ++sequence_,
    },
    frame::Slot {
      2U,
    });
  const auto completed = renderer_->InspectExposureTransition(latest->target);
  if (!completed.has_value()) {
    FAIL() << "Expected completed to contain a value";
  }
  EXPECT_EQ(completed->phase, ExposureTransitionPhase::kApplied);
  EXPECT_EQ(completed->applied_generation, latest->generation);
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStatusCounts(
              service, latest->target),
    (std::pair<std::size_t, std::size_t> {
      0U,
      0U,
    }));
}

NOLINT_TEST_F(
  ExposureGpuTest, DeferredOldAcknowledgementCannotConsumeNewUnsubmittedIntent)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F);
  std::optional<ExposureTransitionToken> submitted;
  for (unsigned i = 0U; i < 4U; ++i) {
    const auto token
      = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
        ExposureTransitionPolicy::kSeedFromEv100, static_cast<float>(i));
    if (!token.has_value()) {
      FAIL() << "Expected token to contain a value";
    }
    submitted = *token;
    Run(signal, {}, 0.0F, nullptr, 1.0F, true, *token);
    vortex::testing::RendererPublicationProbe::EnqueueExposureStatus(
      service, *token, last_state_, ctx_, sequence_);
  }
  if (!submitted.has_value()) {
    FAIL() << "Expected submitted to contain a value";
  }
  const auto pending = renderer_->QueueExposureTransition(
    submitted->target, ExposureTransitionPolicy::kRemeter);
  if (!pending.has_value()) {
    FAIL() << "Expected pending to contain a value";
  }
  service.OnFrameStart(
    frame::SequenceNumber {
      ++sequence_,
    },
    frame::Slot {
      1U,
    });
  WaitForQueueIdle();
  service.OnFrameStart(
    frame::SequenceNumber {
      ++sequence_,
    },
    frame::Slot {
      2U,
    });
  const auto status = renderer_->InspectExposureTransition(pending->target);
  if (!status.has_value()) {
    FAIL() << "Expected status to contain a value";
  }
  EXPECT_EQ(status->request, *pending);
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kQueued);
  EXPECT_EQ(status->applied_generation, submitted->generation);
}

NOLINT_TEST_F(ExposureGpuTest,
  PublicHandleReplacementRetiresHistoryRequestsAndMaskOwnership)
{
  auto frame = engine::FrameContext {};
  owned_asset_loader_ = std::make_unique<vortex::testing::FakeAssetLoader>();
  auto output = CreateRegisteredTexture(TextureDesc {
    .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon,
  });
  auto framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  auto params = ResolvedView::Params {};
  params.view_config.viewport = {
    .width = 4.0F,
    .height = 4.0F,
  };
  auto session
    = renderer_->ForSinglePassHarness()
        .SetFrameSession({ .frame_slot = frame::Slot { 0U, },
          .frame_sequence = frame::SequenceNumber { 1U, },
          .delta_time_seconds = 0.0F, })
        .SetResolvedView(
          { .view_id = ViewId { 1000U, }, .value = ResolvedView { params, } })
        .SetOutputTarget({ .framebuffer = observer_ptr { framebuffer.get(), } })
        .Finalize();
  if (!session.has_value()) {
    FAIL() << "Expected session to contain a value";
  }
  auto* scene_renderer
    = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
  ASSERT_NE(scene_renderer, nullptr);
  auto* service
    = vortex::testing::RendererPublicationProbe::GetPostProcessService(
      *scene_renderer);
  ASSERT_NE(service, nullptr);
  vortex::testing::RendererPublicationProbe::SetExposureAssetLoader(*service,
    observer_ptr {
      owned_asset_loader_.get(),
    });
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  const auto payload = vortex::testing::MakeCookedTexture1x1Rgba8Payload();
  settings.metering_mask
    = owned_asset_loader_->PreloadCookedTexture(std::span(payload));
  const auto h1 = CompositionView::ViewStateHandle {
    50U,
  };
  const auto h2 = CompositionView::ViewStateHandle {
    51U,
  };
  const auto first_view = PublishExposureOwner(frame,
    ViewId {
      50U,
    },
    h1, settings);
  std::weak_ptr<const resources::TextureBinder::ReadyTexture> old_mask;
  for (unsigned i = 0U; i < 8U; ++i) {
    WaitForQueueIdle();
    ctx_.frame_slot = frame::Slot {
      i % 3U,
    };
    renderer_->GetUploadCoordinator().OnFrameStart(
      internal::RendererTagFactory::Get(), ctx_.frame_slot);
    service->OnFrameStart(
      frame::SequenceNumber {
        ++sequence_,
      },
      ctx_.frame_slot);
    const auto& ready = service->ResolveViewExposureSettings(h1, settings);
    if (ready.mask) {
      old_mask = ready.mask;
      break;
    }
  }
  ASSERT_FALSE(old_mask.expired());
  const auto mask_slot = ctx_.frame_slot;
  ctx_.current_view.view_id = first_view;
  ctx_.current_view.view_state_handle = h1;
  const auto applied = renderer_->QueueExposureTransition(
    h1, ExposureTransitionPolicy::kSeedFromEv100, 4.0F);
  if (!applied.has_value()) {
    FAIL() << "Expected applied to contain a value";
  }
  EXPECT_NEAR(ServicePixel(*service, Uniform(.25F, 4U, 4U), settings),
    .25F / 16.0F, 2e-5F);
  const auto old_state
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      *service, h1);
  ASSERT_NE(old_state, nullptr);
  const auto pending = renderer_->QueueExposureTransition(
    h1, ExposureTransitionPolicy::kRemeter);
  if (!pending.has_value()) {
    FAIL() << "Expected pending to contain a value";
  }
  const auto old_mask_key = settings.metering_mask;
  settings.metering_mask = {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 8.0F;
  ASSERT_EQ(PublishExposureOwner(frame,
              ViewId {
                50U,
              },
              h2, settings),
    first_view);
  EXPECT_FALSE(renderer_->RetryExposureTransition(*pending).has_value());
  EXPECT_FALSE(renderer_->RetryExposureTransition(*applied).has_value());
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStateForView(
              *service, h1),
    nullptr);
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStatusCounts(
              *service, h1),
    (std::pair<std::size_t, std::size_t> {
      0U,
      0U,
    }));
  owned_asset_loader_->EmitTextureEviction(
    old_mask_key, content::EvictionReason::kRefCountZero);
  EXPECT_FALSE(old_mask.expired());
  const auto next_view = PublishExposureOwner(frame,
    ViewId {
      70U,
    },
    h1, settings);
  const auto next = renderer_->QueueExposureTransition(
    h1, ExposureTransitionPolicy::kPreserve);
  if (!next.has_value()) {
    FAIL() << "Expected next to contain a value";
  }
  EXPECT_NE(next->lifetime, applied->lifetime);
  ctx_.current_view.view_id = next_view;
  ctx_.current_view.view_state_handle = h1;
  ctx_.frame_slot = frame::Slot {
    (mask_slot.get() + 1U) % 3U,
  };
  EXPECT_NEAR(ServicePixel(*service, Uniform(.25F, 4U, 4U), settings),
    .25F / 256.0F, 2e-5F);
  EXPECT_EQ(
    Read<ExposureStateData>(*old_state->buffer, ResourceStates::kShaderResource)
      .displayed_scale,
    0x1p-4F);
  EXPECT_FALSE(old_mask.expired());
  WaitForQueueIdle();
  service->OnFrameStart(
    frame::SequenceNumber {
      ++sequence_,
    },
    mask_slot);
  EXPECT_TRUE(old_mask.expired());
  const auto next_state
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      *service, h1);
  ASSERT_NE(next_state, nullptr);
  const auto last = renderer_->QueueExposureTransition(
    h1, ExposureTransitionPolicy::kPreserve);
  if (!last.has_value()) {
    FAIL() << "Expected last to contain a value";
  }
  PublishExposureOwner(frame,
    ViewId {
      70U,
    },
    CompositionView::kInvalidViewStateHandle, settings);
  EXPECT_FALSE(renderer_->RetryExposureTransition(*last).has_value());
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStateForView(
              *service, h1),
    nullptr);
  EXPECT_EQ(Read<ExposureStateData>(
              *next_state->buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    0x1p-8F);
}

NOLINT_TEST_F(
  ExposureGpuTest, ThreeFramesInFlightKeepDistinctExposureRecordsAndUploads)
{
  std::array<postprocess::ExposurePass::Result, 3> frames;
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  for (unsigned i = 0U; i < frames.size(); ++i) {
    settings.manual_ev = 4.0F + (4.0F * static_cast<float>(i));
    ctx_.frame_sequence = frame::SequenceNumber {
      ++sequence_,
    };
    ctx_.frame_slot = frame::Slot {
      i,
    };
    frames.at(i) = RecordShared(Signal {}, SharedConfig(settings));
    ASSERT_TRUE(frames.at(i).executed);
  }
  // No CPU fence wait occurred between the three submissions.
  for (unsigned i = 0U; i < frames.size(); ++i) {
    const auto state = ReadState(frames.at(i));
    EXPECT_EQ(
      state.displayed_scale, std::exp2(-4.0F - (4.0F * static_cast<float>(i))));
    EXPECT_EQ(state.frame_sequence.at(0), i + 1U);
    for (unsigned j = i + 1U; j < frames.size(); ++j) {
      EXPECT_NE(frames.at(i).state, frames.at(j).state);
    }
  }
}

} // namespace oxygen::vortex::testing::exposure
