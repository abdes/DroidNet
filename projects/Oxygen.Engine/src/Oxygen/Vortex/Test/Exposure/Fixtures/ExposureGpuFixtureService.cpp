//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>

#include <bit>
#include <cmath>
#include <limits>
#include <utility>

#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>

namespace oxygen::vortex::testing::exposure {

using graphics::Framebuffer;
using graphics::FramebufferDesc;
using graphics::FrameCaptureController;
using graphics::ResourceStates;
using graphics::TextureDesc;

auto ExposureGpuTest::Run(const Signal& signal,
  scene::ExposureSettings settings, float dt, const Signal* mask,
  float inverse_p, bool metering_available,
  std::optional<ExposureTransitionToken> transition,
  std::optional<float> camera_ev, bool temporary_unit) -> Snapshot
{
  settings.key = 12.5F;
  auto authored = PostProcessConfig {
    .exposure = settings,
  };
  authored.temporary_unit_exposure = temporary_unit;
  const auto resolved
    = ResolvedPostProcessConfig::Resolve(authored, camera_ev, ++sequence_);
  CHECK_F(resolved.has_value());
  const auto& config = *resolved;
  ctx_.frame_sequence = frame::SequenceNumber {
    sequence_,
  };
  ctx_.delta_time = dt;
  auto inputs = postprocess::ExposurePass::Inputs {};
  inputs.scene_signal = signal.texture.get();
  inputs.scene_signal_srv = signal.srv;
  inputs.metering_mask = (mask != nullptr) ? mask->texture.get() : nullptr;
  inputs.metering_mask_srv
    = (mask != nullptr) ? mask->srv : kInvalidShaderVisibleIndex;
  inputs.one_over_pre_exposure = inverse_p;
  inputs.metering_available = metering_available;
  inputs.transition = transition;
  const auto result = pass_->Execute(ctx_, config, inputs);
  CHECK_F(result.executed);
  last_state_ = result.state;
  auto snapshot = Snapshot {
    .state = Read<ExposureStateData>(
      *result.exposure_buffer, ResourceStates::kShaderResource),
  };
  if (result.histogram_buffer != nullptr) {
    snapshot.histogram = Read<std::array<std::uint32_t, 264>>(
      *result.histogram_buffer, ResourceStates::kCommon);
  }
  return snapshot;
}

auto ExposureGpuTest::ServicePixel(PostProcessService& service,
  const Signal& signal, scene::ExposureSettings settings,
  ServicePixelOptions options) -> float
{
  settings.key = 12.5F;
  if (options.start_new_frame) {
    ++sequence_;
  }
  ctx_.frame_sequence = frame::SequenceNumber {
    sequence_,
  };
  ctx_.delta_time = options.delta_time_seconds;
  ctx_.render_mode
    = options.diagnostic ? RenderMode::kWireframe : RenderMode::kSolid;
  service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
  [[maybe_unused]] const auto& accepted = service.CaptureViewExposureSettings(
    ctx_.current_view.view_id, ctx_.current_view.view_state_handle, settings,
    {}, options.diagnostic, ctx_.GetScene());
  auto config = PostProcessConfig {};
  config.enable_bloom = options.bloom != nullptr;
  config.bloom_intensity = options.bloom_intensity;
  config.tone_mapper = options.tone_mapper;
  config.gamma = 1.0F;
  service.SetResolvedConfig(service.BuildPassConfig(
    config, ctx_.current_view.view_id, ctx_.current_view.view_state_handle));
  if (options.before_execute) {
    options.before_execute();
  }
  auto output_desc = TextureDesc {};
  output_desc.debug_name = "ExposureServiceOutput";
  output_desc.width = output_desc.height = 4U;
  output_desc.format = Format::kRGBA32Float;
  output_desc.is_render_target = output_desc.is_shader_resource = true;
  output_desc.initial_state = ResourceStates::kCommon;
  auto output = CreateRegisteredTexture(output_desc);
  auto framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  auto textures
    = SceneTextures(Backend(), SceneTexturesConfig { .extent = { 4U, 4U, }, });
  service.Execute(ctx_.current_view.view_id, ctx_, textures,
    {
      .scene_signal = signal.texture.get(),
      .post_target = observer_ptr<const Framebuffer> { framebuffer.get(), },
      .scene_signal_srv = signal.srv,
      .bloom_texture_srv = (options.bloom != nullptr)
        ? options.bloom->srv
        : kInvalidShaderVisibleIndex,
      .scene_fallback = (options.fallback != nullptr)
        ? options.fallback->texture.get()
        : nullptr,
      .scene_fallback_srv = (options.fallback != nullptr)
        ? options.fallback->srv
        : kInvalidShaderVisibleIndex,
      .checked_resolution = std::move(options.checked_resolution),
    },
    options.prepared);
  if (!service.GetLastExecutionState().tonemap_executed) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  auto readback
    = GetReadbackManager()->CreateTextureReadback("Exposure service pixel");
  {
    auto recorder = AcquireRecorder("Exposure service pixel readback");
    CHECK_F(recorder->AdoptKnownResourceState(*output));
    const auto ticket = readback->EnqueueCopy(*recorder, *output,
      {
        .src_slice
        = { .x = 1U, .y = 0U, .width = 1U, .height = 1U, .depth = 1U, },
      });
    CHECK_F(ticket.has_value());
  }
  const auto mapped = readback->MapNow();
  CHECK_F(mapped.has_value());
  Pixel pixel {};
  std::memcpy(pixel.data(), mapped->Data(), sizeof(pixel));
  return pixel.at(0);
}

auto ExposureGpuTest::Qualify(const Signal& signal, bool meter,
  scene::ExposureSettings settings, const Signal* mask, bool coverage)
  -> HdrSuitabilityData
{
  settings.mode = engine::ExposureMode::kManual;
  const auto config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber {
    ++sequence_,
  };
  auto frame_inputs = postprocess::ExposurePass::FrameInputs {};
  frame_inputs.use_fp32 = true;
  const auto frame = pass_->ResolveFrame(ctx_, config, frame_inputs);
  CHECK_NOTNULL_F(frame.get());
  CHECK_F(RecordShared(signal, config).executed);
  const std::array products {
    postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(),
      .srv = signal.srv,
      .id = 1U,
      .metering = meter,
      .coverage = coverage,
    },
  };
  auto metering = postprocess::ExposurePass::Inputs {};
  metering.metering_mask = (mask != nullptr) ? mask->texture.get() : nullptr;
  metering.metering_mask_srv
    = (mask != nullptr) ? mask->srv : kInvalidShaderVisibleIndex;
  CHECK_F(pass_->EvaluateFp16Products(ctx_, frame, config, products, metering));
  return Read<HdrSuitabilityData>(
    *frame->suitability_buffer, ResourceStates::kShaderResource);
}

auto ExposureGpuTest::EligibilityStep(const Signal& signal,
  // The qualification oracle takes frame sequence before layout revision,
  // matching the sequence/layout pairs used by its transition test tables.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  const ResolvedPostProcessConfig& config, std::uint64_t sequence,
  std::uint64_t layout, std::uint32_t expected, bool invalidate_previous,
  const postprocess::ExposurePass::Source* source,
  std::optional<ExposureTransitionToken> transition, bool metering_available,
  bool capture_eligibility)
  -> std::pair<ExposureStateData, ExposureCompletedStatus>
{
  ctx_.frame_sequence = frame::SequenceNumber {
    sequence,
  };
  const auto lifetime = transition ? transition->lifetime : 0U;
  auto frame_inputs = postprocess::ExposurePass::FrameInputs {};
  frame_inputs.use_fp32 = true;
  frame_inputs.source = source;
  frame_inputs.transition = transition;
  frame_inputs.lifetime = lifetime;
  const auto frame = pass_->ResolveFrame(ctx_, config, frame_inputs);
  CHECK_NOTNULL_F(frame.get());
  auto inputs = postprocess::ExposurePass::Inputs {};
  inputs.scene_signal = signal.texture.get();
  inputs.scene_signal_srv = signal.srv;
  inputs.metering_available = metering_available;
  inputs.transition = transition;
  inputs.source = source;
  inputs.lifetime = lifetime;
  const auto solved = pass_->Execute(ctx_, config, inputs);
  CHECK_F(solved.executed);
  const auto before = ReadState(solved);
  EXPECT_EQ(before.flags & 256U, 0U);
  EXPECT_EQ(before.fp16_eligible_streak, 0U);
  EXPECT_FALSE(pass_->FinalizeFp16Suitability(ctx_, frame,
    {
      .product_layout_revision = layout,
      .expected_products = expected,
    }));
  const std::array products {
    postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(),
      .srv = signal.srv,
      .id = 11U,
      .metering = true,
    },
  };
  const auto capture = capture_eligibility
    ? BeginOptionalCapture()
    : observer_ptr<FrameCaptureController> {};
  CHECK_F(pass_->EvaluateFp16Products(ctx_, frame, config, products, {}));
  CHECK_F(pass_->FinalizeFp16Suitability(ctx_, frame,
    {
      .product_layout_revision = layout,
      .expected_products = expected,
      .invalidate_previous = invalidate_previous,
    }));
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
  const auto state = ReadState(solved);
  const auto status = Read<ExposureCompletedStatus>(
    *solved.state->status_buffer, ResourceStates::kCopySource);
  EXPECT_EQ(std::memcmp(&before, &state, 24U), 0);
  EXPECT_EQ(before.settings_revision, state.settings_revision);
  EXPECT_EQ(before.fallback_reason, state.fallback_reason);
  EXPECT_EQ(before.requested_generation, state.requested_generation);
  EXPECT_EQ(before.applied_generation, state.applied_generation);
  EXPECT_EQ(before.frame_sequence, state.frame_sequence);
  EXPECT_EQ((before.flags ^ state.flags) & ~(32U | 256U), 0U);
  EXPECT_EQ(status.fp16_eligible_streak, state.fp16_eligible_streak);
  EXPECT_EQ(status.product_layout_revision, state.product_layout_revision);
  EXPECT_EQ(status.candidate_state_generation, state.frame_sequence);
  EXPECT_EQ(status.frame_sequence, state.frame_sequence);
  EXPECT_EQ(status.settings_revision, state.settings_revision);
  EXPECT_EQ(status.requested_generation, state.requested_generation);
  EXPECT_EQ(status.applied_generation, state.applied_generation);
  EXPECT_EQ(
    status.view_state_identity.at(0), static_cast<std::uint32_t>(lifetime));
  EXPECT_EQ(status.view_state_identity.at(1),
    static_cast<std::uint32_t>(lifetime >> 32U));
  EXPECT_EQ(status.reserved, 0U);
  EXPECT_EQ(status.flags & 4U, state.fp16_eligible_streak >= 2U ? 4U : 0U);
  CHECK_F(pass_->FinalizeFp16Suitability(ctx_, frame,
    {
      .product_layout_revision = layout,
      .expected_products = expected,
      .invalidate_previous = invalidate_previous,
    }));
  const auto duplicate = ReadState(solved);
  using StateBytes = std::array<std::byte, sizeof(ExposureStateData)>;
  EXPECT_EQ(
    std::bit_cast<StateBytes>(state), std::bit_cast<StateBytes>(duplicate));
  return {
    state,
    status,
  };
}

auto ExposureGpuTest::ResetHistory() -> void
{
  pass_->RemoveViewState(ctx_.current_view.view_state_handle);
}

auto ExposureGpuTest::PublishExposureOwner(engine::FrameContext& frame,
  const ViewId intent_id, CompositionView::ViewStateHandle handle,
  scene::ExposureSettings settings, ViewId source, bool diagnostic,
  std::optional<ShaderDebugMode> debug_override) -> ViewId
{
  auto texture = CreateRegisteredTexture(TextureDesc {
    .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon,
  });
  auto target = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(texture));
  registered_targets_.push_back(target);
  auto view = CompositionView {};
  view.id = intent_id;
  view.view_state_handle = handle;
  view.render_settings.exposure = std::move(settings);
  view.exposure_source_view_id = source;
  view.force_wireframe = diagnostic;
  view.render_settings.shader_debug_mode = debug_override;
  return renderer_->PublishRuntimeCompositionView(frame,
    { .composition_view = view,
      .render_target = observer_ptr { target.get(), }, });
}

auto ExposureGpuTest::SharedConfig(scene::ExposureSettings settings,
  std::optional<float> camera_ev, std::uint64_t revision)
  -> ResolvedPostProcessConfig
{
  settings.key = 12.5F;
  const auto config = ResolvedPostProcessConfig::Resolve(
    PostProcessConfig {
      .exposure = settings,
    },
    camera_ev, revision);
  CHECK_F(config.has_value());
  return *config;
}

auto ExposureGpuTest::RecordShared(const Signal& signal,
  const ResolvedPostProcessConfig& config,
  const postprocess::ExposurePass::Source* source,
  std::optional<ExposureTransitionToken> token, std::uint64_t lifetime)
  -> postprocess::ExposurePass::Result
{
  auto inputs = postprocess::ExposurePass::Inputs {};
  inputs.scene_signal = signal.texture.get();
  inputs.scene_signal_srv = signal.srv;
  inputs.transition = token;
  inputs.source = source;
  inputs.lifetime = lifetime;
  return pass_->Execute(ctx_, config, inputs);
}

auto ExposureGpuTest::ReadState(const postprocess::ExposurePass::Result& result)
  -> ExposureStateData
{
  CHECK_NOTNULL_F(result.exposure_buffer);
  return Read<ExposureStateData>(
    *result.exposure_buffer, ResourceStates::kShaderResource);
}

auto ExposureGpuTest::OwnedExposureService() -> PostProcessService&
{
  if (vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_)
    == nullptr) {
    auto texture = CreateRegisteredTexture(TextureDesc {
      .width = 4U,
      .height = 4U,
      .format = Format::kRGBA32Float,
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon,
    });
    auto target = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(texture));
    registered_targets_.push_back(target);
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
            { .view_id = ViewId { 1000U, }, .value = ResolvedView { params, }, })
          .SetOutputTarget({ .framebuffer = observer_ptr { target.get(), }, })
          .Finalize();
    CHECK_F(session.has_value());
  }
  return *vortex::testing::RendererPublicationProbe::GetPostProcessService(
    *vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_));
}

auto ExposureGpuTest::StartSharedServiceView(PostProcessService& service,
  engine::FrameContext& frame, scene::ExposureSettings consumer_settings)
  -> ViewId
{
  auto source_settings = scene::ExposureSettings {};
  source_settings.key = 12.5F;
  source_settings.mode = engine::ExposureMode::kManual;
  source_settings.manual_ev = 4.0F;
  const auto root = PublishExposureOwner(frame,
    ViewId {
      50U,
    },
    CompositionView::ViewStateHandle {
      50U,
    },
    source_settings);
  ctx_.current_view.view_id = root;
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
    50U,
  };
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::kInvalidViewStateHandle;
  ServicePixel(service, Uniform(.25F, 4U, 4U), source_settings);
  consumer_settings.key = 12.5F;
  const auto consumer = PublishExposureOwner(frame,
    ViewId {
      60U,
    },
    CompositionView::ViewStateHandle {
      60U,
    },
    consumer_settings,
    ViewId {
      50U,
    });
  ctx_.current_view.view_id = consumer;
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
    60U,
  };
  ctx_.current_view.exposure_view_id = root;
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::ViewStateHandle {
        50U,
      };
  ServicePixel(service, Uniform(8.0F, 4U, 4U), consumer_settings);
  return consumer;
}

} // namespace oxygen::vortex::testing::exposure
