//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <span>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Direct3D12/Test/Fixtures/ReadbackTestFixture.h>
#include <Oxygen/Scene/Environment/Background.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>

namespace oxygen::vortex::internal {
auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::vortex::internal

namespace {
using namespace oxygen;
using namespace oxygen::graphics;
using namespace oxygen::vortex;
using Pixel = std::array<float, 4>;

class ExposureGpuTest : public graphics::d3d12::testing::ReadbackTestFixture {
protected:
  struct Signal {
    std::shared_ptr<const Texture> texture;
    ShaderVisibleIndex srv;
  };
  struct Snapshot {
    ExposureStateData state;
    std::array<std::uint32_t, 264> histogram;
  };

  auto BackendConfigJson() const -> std::string override
  {
    return R"({"enable_debug_layer":true})";
  }
  auto PathFinderConfigJson() const -> std::string override
  {
    return R"({"workspace_root_path":")" OXYGEN_EXPOSURE_WORKSPACE R"("})";
  }
  auto SetUp() -> void override
  {
    ReadbackTestFixture::SetUp();
    auto config = RendererConfig {};
    config.upload_queue_key = QueueKeyFor().get();
    renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config);
    pass_ = std::make_unique<postprocess::ExposurePass>(*renderer_);
    ctx_.current_view.view_id = ViewId { 1U };
    ctx_.current_view.view_state_handle
      = CompositionView::ViewStateHandle { 1U };
    ctx_.frame_slot = frame::Slot { 0U };
  }
  auto TearDown() -> void override
  {
    FlushBackend();
    registered_targets_.clear();
    last_state_.reset();
    pass_.reset();
    if (renderer_) {
      renderer_->OnShutdown();
      renderer_.reset();
    }
    ReadbackTestFixture::TearDown();
  }
  auto MakeSignal(std::uint32_t width, std::uint32_t height,
    std::span<const Pixel> pixels) -> Signal
  {
    CHECK_F(pixels.size() == 1U || pixels.size() == width * height);
    auto texture = CreateRegisteredTexture({
      .width = width,
      .height = height,
      .format = Format::kRGBA32Float,
      .texture_type = TextureType::kTexture2D,
      .debug_name = "ExposureFloatFixture",
      .is_shader_resource = true,
      .initial_state = ResourceStates::kCommon,
    });
    const auto pitch = ((width * 16U + 255U) / 256U) * 256U;
    auto upload
      = CreateUploadBuffer(SizeBytes { std::uint64_t(pitch) * height });
    std::vector<std::byte> bytes(std::size_t(pitch) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
      for (std::uint32_t x = 0; x < width; ++x) {
        const auto& pixel = pixels[pixels.size() == 1U ? 0U : y * width + x];
        std::memcpy(
          bytes.data() + std::size_t(y) * pitch + x * 16U, pixel.data(), 16U);
      }
    }
    upload->Update(bytes.data(), bytes.size(), 0U);
    {
      auto recorder = AcquireRecorder("Exposure fixture upload");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, texture, ResourceStates::kCommon);
      recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBufferToTexture(*upload,
        {
          .buffer_offset = 0U,
          .buffer_row_pitch = pitch,
          .buffer_slice_pitch = std::uint64_t(pitch) * height,
          .dst_slice = { .width = width, .height = height, .depth = 1U },
        },
        *texture);
      recorder->RequireResourceStateFinal(
        *texture, ResourceStates::kShaderResource);
    }
    WaitForQueueIdle();
    auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
    auto handle = allocator.AllocateRaw(
      ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    auto srv = allocator.GetShaderVisibleIndex(handle);
    const auto view = Backend().GetResourceRegistry().RegisterView(*texture,
      std::move(handle),
      TextureViewDescription {
        .format = Format::kRGBA32Float,
        .dimension = TextureType::kTexture2D,
      });
    CHECK_F(view->IsValid());
    return { std::move(texture), srv };
  }
  auto Uniform(float value, std::uint32_t width = 1U, std::uint32_t height = 1U)
    -> Signal
  {
    const auto pixels
      = std::array<Pixel, 1> { Pixel { value, value, value, 1.0F } };
    return MakeSignal(width, height, pixels);
  }
  template <typename Payload>
  auto Read(const Buffer& source, ResourceStates final_state) -> Payload
  {
    auto readback
      = GetReadbackManager()->CreateBufferReadback("Exposure fixture readback");
    {
      auto recorder = AcquireRecorder("Exposure fixture copy");
      recorder->BeginTrackingResourceState(source, final_state, false);
      const auto ticket
        = readback->EnqueueCopy(*recorder, source, { 0U, sizeof(Payload) });
      CHECK_F(ticket.has_value());
      recorder->RequireResourceStateFinal(source, final_state);
    }
    const auto mapped = readback->MapNow();
    CHECK_F(mapped.has_value());
    Payload result {};
    std::memcpy(&result, mapped->Bytes().data(), sizeof(result));
    return result;
  }
  auto Run(const Signal& signal, scene::ExposureSettings settings = {},
    float dt = 0.0F, const Signal* mask = nullptr, float inverse_p = 1.0F,
    bool metering_available = true,
    std::optional<ExposureTransitionToken> transition = {},
    std::optional<float> camera_ev = {}, bool temporary_unit = false)
    -> Snapshot
  {
    settings.key = 12.5F;
    const auto resolved = scene::ResolveExposureSettings(settings, camera_ev);
    CHECK_F(resolved.has_value());
    auto config = PostProcessConfig {};
    config.resolved_exposure = *resolved;
    config.temporary_unit_exposure = temporary_unit;
    config.exposure_settings_revision = ++sequence_;
    config.metering_mode = settings.metering_mode;
    config.auto_exposure_min_log_luminance = settings.min_log_luminance;
    config.auto_exposure_log_luminance_range = settings.log_luminance_range;
    config.auto_exposure_min_ev = settings.min_ev;
    config.auto_exposure_max_ev = settings.max_ev;
    config.auto_exposure_low_percentile = settings.low_percentile;
    config.auto_exposure_high_percentile = settings.high_percentile;
    config.auto_exposure_speed_up = settings.speed_up;
    config.auto_exposure_speed_down = settings.speed_down;
    config.auto_exposure_spot_meter_radius = settings.spot_meter_radius;
    ctx_.frame_sequence = frame::SequenceNumber { sequence_ };
    ctx_.delta_time = dt;
    const auto result = pass_->Execute(ctx_, config,
      {
        .scene_signal = signal.texture.get(),
        .scene_signal_srv = signal.srv,
        .metering_mask = mask ? mask->texture.get() : nullptr,
        .metering_mask_srv = mask ? mask->srv : kInvalidShaderVisibleIndex,
        .one_over_pre_exposure = inverse_p,
        .metering_available = metering_available,
        .transition = transition,
      });
    CHECK_F(result.executed);
    last_state_ = result.state;
    auto snapshot
      = Snapshot { .state = Read<ExposureStateData>(*result.exposure_buffer,
                     ResourceStates::kShaderResource) };
    if (result.histogram_buffer) {
      snapshot.histogram = Read<std::array<std::uint32_t, 264>>(
        *result.histogram_buffer, ResourceStates::kCommon);
    }
    return snapshot;
  }
  auto ServicePixel(PostProcessService& service, const Signal& signal,
    scene::ExposureSettings settings = {}, bool diagnostic = false,
    float dt = 0.0F, std::function<void()> before_execute = {}) -> float
  {
    settings.key = 12.5F;
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    ctx_.delta_time = dt;
    ctx_.render_mode = diagnostic ? RenderMode::kWireframe : RenderMode::kSolid;
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    const auto& accepted = service.CaptureViewExposureSettings(
      ctx_.current_view.view_id, ctx_.current_view.view_state_handle, settings);
    auto config = PostProcessConfig {};
    config.resolved_exposure = accepted.resolved;
    config.exposure_settings_revision = accepted.revision;
    config.auto_exposure_min_ev = settings.min_ev;
    config.auto_exposure_max_ev = settings.max_ev;
    config.auto_exposure_speed_up = settings.speed_up;
    config.auto_exposure_speed_down = settings.speed_down;
    config.enable_auto_exposure
      = settings.enabled && settings.mode == engine::ExposureMode::kAuto;
    config.enable_bloom = false;
    config.bloom_intensity = 0.0F;
    config.tone_mapper = engine::ToneMapper::kNone;
    config.gamma = 1.0F;
    service.SetConfig(config);
    if (before_execute)
      before_execute();
    auto output_desc = TextureDesc {};
    output_desc.width = output_desc.height = 4U;
    output_desc.format = Format::kRGBA32Float;
    output_desc.is_render_target = output_desc.is_shader_resource = true;
    output_desc.initial_state = ResourceStates::kCommon;
    auto output = CreateRegisteredTexture(output_desc);
    auto framebuffer = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(output));
    auto textures
      = SceneTextures(Backend(), SceneTexturesConfig { .extent = { 4U, 4U } });
    service.Execute(ctx_.current_view.view_id, ctx_, textures,
      {
        .scene_signal = signal.texture.get(),
        .post_target = observer_ptr<const Framebuffer> { framebuffer.get() },
        .scene_signal_srv = signal.srv,
      });
    auto readback
      = GetReadbackManager()->CreateTextureReadback("Exposure service pixel");
    {
      auto recorder = AcquireRecorder("Exposure service pixel readback");
      CHECK_F(recorder->AdoptKnownResourceState(*output));
      const auto ticket = readback->EnqueueCopy(*recorder, *output,
        {
          .src_slice
          = { .x = 1U, .y = 0U, .width = 1U, .height = 1U, .depth = 1U },
        });
      CHECK_F(ticket.has_value());
    }
    const auto mapped = readback->MapNow();
    CHECK_F(mapped.has_value());
    Pixel pixel {};
    std::memcpy(pixel.data(), mapped->Data(), sizeof(pixel));
    return pixel[0];
  }

  auto ResetHistory() -> void
  {
    pass_->RemoveViewState(ctx_.current_view.view_state_handle);
  }
  auto PublishExposureOwner(engine::FrameContext& frame, const ViewId intent_id,
    CompositionView::ViewStateHandle handle, scene::ExposureSettings settings,
    ViewId source = kInvalidViewId, bool diagnostic = false) -> ViewId
  {
    auto texture = CreateRegisteredTexture(TextureDesc { .width = 4U,
      .height = 4U,
      .format = Format::kRGBA32Float,
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon });
    auto target = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(texture));
    registered_targets_.push_back(target);
    auto view = CompositionView {};
    view.id = intent_id;
    view.view_state_handle = handle;
    view.render_settings.exposure = std::move(settings);
    view.exposure_source_view_id = source;
    view.force_wireframe = diagnostic;
    return renderer_->PublishRuntimeCompositionView(frame,
      { .composition_view = view,
        .render_target = observer_ptr { target.get() } });
  }
  auto SharedConfig(scene::ExposureSettings settings = {},
    std::optional<float> camera_ev = {}) -> PostProcessConfig
  {
    settings.key = 12.5F;
    auto config = PostProcessConfig {};
    config.resolved_exposure
      = *scene::ResolveExposureSettings(settings, camera_ev);
    config.exposure_settings_revision = 1U;
    config.auto_exposure_min_ev = settings.min_ev;
    config.auto_exposure_max_ev = settings.max_ev;
    return config;
  }
  auto RecordShared(const Signal& signal, const PostProcessConfig& config,
    const postprocess::ExposurePass::Source* source = nullptr,
    std::optional<ExposureTransitionToken> token = {})
    -> postprocess::ExposurePass::Result
  {
    return pass_->Execute(ctx_, config,
      { .scene_signal = signal.texture.get(),
        .scene_signal_srv = signal.srv,
        .transition = token,
        .source = source });
  }
  auto ReadState(const postprocess::ExposurePass::Result& result)
    -> ExposureStateData
  {
    CHECK_NOTNULL_F(result.exposure_buffer);
    return Read<ExposureStateData>(
      *result.exposure_buffer, ResourceStates::kShaderResource);
  }
  std::unique_ptr<Renderer> renderer_;
  std::vector<std::shared_ptr<Framebuffer>> registered_targets_;
  std::unique_ptr<postprocess::ExposurePass> pass_;
  RenderContext ctx_;
  std::uint64_t sequence_ { 0U };
  postprocess::ExposurePass::StateLease last_state_;
};

NOLINT_TEST_F(ExposureGpuTest, ConservedTwoBinMassAndIndependentMeter)
{
  auto settings = scene::ExposureSettings {};
  settings.low_percentile = 0.0F;
  settings.high_percentile = 1.0F;
  const auto result = Run(Uniform(0.375F, 17U, 19U), settings);
  // Independent double-precision scatter at L=3/8, window [-12,13].
  const double x = (std::log2(0.375) + 12.0) * 255.0 / 25.0;
  const auto bin = static_cast<unsigned>(std::floor(x));
  const auto upper
    = static_cast<unsigned>(std::floor(4095.0 * (x - bin) + 0.5));
  EXPECT_EQ(result.histogram[bin], 323U * (4095U - upper));
  EXPECT_EQ(result.histogram[bin + 1U], 323U * upper);
  EXPECT_EQ(std::accumulate(
              result.histogram.begin(), result.histogram.begin() + 256, 0U),
    323U * 4095U);
  const double expected_log = -12.0 + (bin + upper / 4095.0) * 25.0 / 255.0;
  EXPECT_NEAR(
    result.state.raw_metered_ev, expected_log - std::log2(0.18), 2e-4);
  EXPECT_NEAR(std::log2(result.state.displayed_scale),
    std::log2(0.18) - expected_log, 2e-4);
  EXPECT_EQ(result.histogram[256], 323U);
  EXPECT_EQ(result.histogram[257], 323U);
  EXPECT_EQ(result.histogram[260], 0U);
}

NOLINT_TEST_F(ExposureGpuTest, MaximumGridMassRemainsBounded)
{
  const auto result = Run(Uniform(0.25F, 1024U, 513U));
  EXPECT_EQ(std::accumulate(
              result.histogram.begin(), result.histogram.begin() + 256, 0U),
    1073479680U);
  EXPECT_EQ(result.histogram[256], 262144U);
  EXPECT_EQ(result.histogram[257], 262144U);
}

NOLINT_TEST_F(ExposureGpuTest, DarkValidAndZeroMaskInvalidRemainDistinct)
{
  const auto dark = Uniform(0.0F);
  const auto zero_mask = Uniform(0.0F);
  const auto result = Run(dark);
  EXPECT_EQ(result.histogram[258], 1U);
  EXPECT_EQ(result.histogram[261], 1U);
  EXPECT_EQ(result.histogram[0], 0U);
  EXPECT_EQ(result.state.flags & 31U, 27U);
  EXPECT_FLOAT_EQ(result.state.displayed_scale, 64.0F);
  const auto invalid = Run(dark, {}, 1.0F, &zero_mask);
  EXPECT_EQ(invalid.histogram[257], 0U);
  EXPECT_EQ(invalid.state.flags & 12U, 0U);
  EXPECT_EQ(invalid.state.fallback_reason, 2U);
  EXPECT_EQ(invalid.state.displayed_scale, result.state.displayed_scale);
}

NOLINT_TEST_F(ExposureGpuTest, NonfiniteSamplesAreRejectedAndNeverBlack)
{
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  const auto inf = std::numeric_limits<float>::infinity();
  const auto pixels = std::array { Pixel { nan, 0, 0, 1 },
    Pixel { inf, 0, 0, 1 }, Pixel { .25F, .25F, .25F, 1 } };
  const auto result = Run(MakeSignal(3U, 1U, pixels));
  EXPECT_EQ(result.histogram[256], 1U);
  EXPECT_EQ(result.histogram[257], 1U);
  EXPECT_EQ(result.histogram[258], 0U);
  EXPECT_EQ(result.histogram[260], 2U);
  EXPECT_NEAR(result.state.raw_metered_ev, std::log2(.25 / .18), 2e-4);
}

NOLINT_TEST_F(ExposureGpuTest, TinyPercentileIntervalRetainsItsContainingBin)
{
  auto settings = scene::ExposureSettings {};
  settings.low_percentile = std::numeric_limits<float>::denorm_min();
  settings.high_percentile = settings.low_percentile * 2.0F;
  const auto result = Run(Uniform(.25F, 512U, 512U), settings);
  EXPECT_EQ(result.state.flags & 12U, 12U);
  EXPECT_NEAR(result.state.raw_metered_ev, std::log2(.25 / .18), 2e-4);
}

NOLINT_TEST_F(ExposureGpuTest, ZeroTargetRestoresImmediatelyFromLastValidMeter)
{
  auto settings = scene::ExposureSettings {};
  const auto signal = Uniform(.25F);
  const auto initial = Run(signal, settings);
  const auto zero_mask = Uniform(0.0F);
  settings.target_luminance = 0.0F;
  const auto black = Run(signal, settings, 0.0F, &zero_mask);
  EXPECT_EQ(black.state.displayed_scale, 0.0F);
  EXPECT_EQ(black.state.latent_scale, initial.state.latent_scale);
  settings.target_luminance = .36F;
  const auto restored = Run(signal, settings, 0.0F, &zero_mask);
  EXPECT_NEAR(restored.state.displayed_scale, 1.44F, 2e-5F);
  EXPECT_EQ(restored.state.flags & 12U, 0U);
}

NOLINT_TEST_F(ExposureGpuTest, ZeroSpeedAndPauseFreezeOrdinaryAdaptation)
{
  auto settings = scene::ExposureSettings {};
  const auto signal = Uniform(.25F);
  const auto initial = Run(signal, settings);
  settings.compensation_ev = -10.0F;
  EXPECT_EQ(
    Run(signal, settings, 0.0F).state.latent_scale, initial.state.latent_scale);
  settings.speed_up = 0.0F;
  EXPECT_EQ(Run(signal, settings, 10.0F).state.latent_scale,
    initial.state.latent_scale);
  settings.compensation_ev = 10.0F;
  settings.speed_down = 0.0F;
  EXPECT_EQ(Run(signal, settings, 10.0F).state.latent_scale,
    initial.state.latent_scale);
}

NOLINT_TEST_F(
  ExposureGpuTest, HybridCrossingMatchesElapsedTimeAcrossFrameSchedules)
{
  const auto signal = Uniform(.25F);
  // Starting target is .72, changed target is .72*2^-8. SpeedUp=3 EV/s,
  // D=1.5: at t=3, remaining error is 1.5*exp(-5/3) stops.
  const double expected = std::log2(.72) - 8.0 + 1.5 * std::exp(-5.0 / 3.0);
  for (const unsigned frequency : { 30U, 60U, 120U }) {
    ResetHistory();
    auto settings = scene::ExposureSettings {};
    Run(signal, settings);
    settings.compensation_ev = -8.0F;
    Snapshot result {};
    for (unsigned frame = 0; frame < 3U * frequency; ++frame) {
      result = Run(signal, settings, 1.0F / frequency);
    }
    EXPECT_NEAR(std::log2(result.state.latent_scale), expected, 5e-4)
      << frequency;
  }
}
NOLINT_TEST_F(
  ExposureGpuTest, HugeFiniteSpeedAndDistanceDoNotOverflowTheExponent)
{
  const auto signal = Uniform(.25F);
  auto settings = scene::ExposureSettings {};
  settings.min_ev = -16.0F;
  settings.max_ev = 16.0F;
  settings.compensation_ev = 8.0F;
  settings.target_luminance = .25F;
  const auto initial = Run(signal, settings);
  settings.compensation_ev = -8.0F;
  settings.speed_up = std::exp2(127.0F);
  settings.transition_distance = std::exp2(127.0F);
  const auto result = Run(signal, settings, 2.0F);
  const double expected = std::log2(double(initial.state.latent_scale)) - 16.0
    + 16.0 * std::exp(-2.0);
  EXPECT_NEAR(std::log2(result.state.latent_scale), expected, 5e-4);
}

NOLINT_TEST_F(ExposureGpuTest, TinySpeedTimesHugeDeltaRetainsFiniteLinearTravel)
{
  const auto signal = Uniform(.25F);
  auto settings = scene::ExposureSettings {};
  const auto initial = Run(signal, settings);
  settings.compensation_ev = -8.0F;
  settings.speed_up = std::exp2(-140.0F);
  const auto result = Run(signal, settings, std::exp2(127.0F));
  EXPECT_NEAR(std::log2(result.state.latent_scale),
    std::log2(initial.state.latent_scale) - std::exp2(-13.0F), 1e-5);
}
NOLINT_TEST_F(
  ExposureGpuTest, FractionalPercentileBoundariesMatchIndependentCdf)
{
  const auto pixels
    = std::array { Pixel { .25F, .25F, .25F, 1 }, Pixel { .25F, .25F, .25F, 1 },
        Pixel { .25F, .25F, .25F, 1 }, Pixel { 8, 8, 8, 1 } };
  auto settings = scene::ExposureSettings {};
  settings.low_percentile = .5F;
  settings.high_percentile = .875F;
  const auto result = Run(MakeSignal(4U, 1U, pixels), settings);
  // Retain one .25 sample and half an 8 sample: (-2 + .5*3)/1.5.
  EXPECT_NEAR(result.state.raw_metered_ev, -1.0 / 3.0 - std::log2(.18), 2e-4);
}

NOLINT_TEST_F(ExposureGpuTest, DarkInfluenceDoesNotAttenuatePositiveBinZeroMass)
{
  auto settings = scene::ExposureSettings {};
  settings.min_log_luminance = -2.0F;
  settings.log_luminance_range = 25.0F;
  settings.low_percentile = 0.0F;
  settings.high_percentile = 1.0F;
  const auto pixels = std::array { Pixel { 0, 0, 0, 1 },
    Pixel { .2578125F, .2578125F, .2578125F, 1 } };
  const auto signal = MakeSignal(2U, 1U, pixels);
  const auto zero = Run(signal, settings);
  EXPECT_GT(zero.histogram[0], 0U);
  EXPECT_GT(zero.histogram[1], 0U);
  EXPECT_EQ(zero.histogram[0] + zero.histogram[1], 4095U);
  settings.black_influence = .5F;
  const auto half = Run(signal, settings);
  EXPECT_EQ(half.histogram[0], zero.histogram[0] + 2048U);
  EXPECT_EQ(half.histogram[1], zero.histogram[1]);
}

NOLINT_TEST_F(ExposureGpuTest, BilinearMaskUsesOnlyClampedLinearRed)
{
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const auto pixels
    = std::array { Pixel { 0, nan, nan, nan }, Pixel { 1, nan, nan, nan } };
  const auto mask = MakeSignal(2U, 1U, pixels);
  const auto result = Run(Uniform(.25F), {}, 0.0F, &mask);
  EXPECT_EQ(result.histogram[102], 2048U);
  EXPECT_EQ(result.histogram[260], 0U);
  const auto high_mask = Uniform(4.0F);
  EXPECT_EQ(Run(Uniform(.25F), {}, 0.0F, &high_mask).histogram[102], 4095U);
  const auto low_mask = Uniform(-1.0F);
  EXPECT_EQ(Run(Uniform(.25F), {}, 0.0F, &low_mask).histogram[257], 0U);
}

NOLINT_TEST_F(
  ExposureGpuTest, CoverageUnpremultipliesAndScalesMassOnlyWithBackground)
{
  auto scene = scene::Scene("CoverageFixture", 1U);
  scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& background
    = scene.GetEnvironment()->AddSystem<scene::environment::Background>();
  background.SetEnabled(true);
  ctx_.scene = observer_ptr { &scene };
  const auto pixels
    = std::array<Pixel, 1> { Pixel { .125F, .125F, .125F, .5F } };
  const auto signal = MakeSignal(1U, 1U, pixels);
  const auto result = Run(signal);
  EXPECT_EQ(result.histogram[102], 2048U);
  EXPECT_NEAR(result.state.raw_metered_ev, std::log2(.25 / .18), 2e-4);
  background.SetEnabled(false);
  auto untrimmed = scene::ExposureSettings {};
  untrimmed.low_percentile = 0.0F;
  untrimmed.high_percentile = 1.0F;
  const auto opaque = Run(signal, untrimmed);
  EXPECT_EQ(std::accumulate(
              opaque.histogram.begin(), opaque.histogram.begin() + 256, 0U),
    4095U);
  EXPECT_NEAR(opaque.state.raw_metered_ev, std::log2(.125 / .18), 2e-4);
  ctx_.scene.reset();
}

NOLINT_TEST_F(
  ExposureGpuTest, ProfilesAndZeroRadiusUseNormalizedContentCoordinates)
{
  const auto signal = Uniform(.25F, 3U, 1U);
  auto settings = scene::ExposureSettings {};
  settings.metering_mode = engine::MeteringMode::kCenterWeighted;
  EXPECT_EQ(Run(signal, settings).histogram[102], 6825U);
  settings.metering_mode = engine::MeteringMode::kSpot;
  settings.spot_meter_radius = 1.0e-20F;
  EXPECT_EQ(Run(signal, settings).histogram[102], 4095U);
  settings.spot_meter_radius = 0.0F;
  EXPECT_EQ(Run(signal, settings).histogram[102], 4095U);
  EXPECT_EQ(Run(Uniform(.25F, 2U, 1U), settings).histogram[257], 0U);
}

NOLINT_TEST_F(ExposureGpuTest, ContentRectangleExcludesOutputBars)
{
  const auto pixels
    = std::array { Pixel { 8, 8, 8, 1 }, Pixel { .25F, .25F, .25F, 1 },
        Pixel { .25F, .25F, .25F, 1 }, Pixel { 8, 8, 8, 1 } };
  const auto signal = MakeSignal(4U, 1U, pixels);
  auto params = ResolvedView::Params {};
  params.view_config.viewport
    = { .top_left_x = 1.0F, .top_left_y = 0.0F, .width = 2.0F, .height = 1.0F };
  params.view_config.scissor = { .left = 1, .top = 0, .right = 3, .bottom = 1 };
  const auto view = ResolvedView { params };
  ctx_.current_view.resolved_view = observer_ptr { &view };
  const auto result = Run(signal);
  EXPECT_EQ(result.histogram[102], 8190U);
  EXPECT_EQ(result.histogram[256], 2U);
  EXPECT_NEAR(result.state.raw_metered_ev, std::log2(.25 / .18), 2e-4);
  ctx_.current_view.resolved_view.reset();
}

NOLINT_TEST_F(ExposureGpuTest, SceneReferredMeterIsInvariantToInputPreExposure)
{
  const auto ordinary = Run(Uniform(.25F));
  const auto scaled = Run(Uniform(1024.0F), {}, 0.0F, nullptr, 1.0F / 4096.0F);
  EXPECT_EQ(ordinary.histogram, scaled.histogram);
  EXPECT_EQ(ordinary.state.raw_metered_ev, scaled.state.raw_metered_ev);
}

NOLINT_TEST_F(ExposureGpuTest, LongAndIrregularHybridStepsRemainEquivalent)
{
  const auto signal = Uniform(.25F);
  auto settings = scene::ExposureSettings {};
  Run(signal, settings);
  settings.compensation_ev = -8.0F;
  Snapshot result {};
  for (float dt : { .125F, .875F, .25F, .25F, .5F, 1.0F }) {
    result = Run(signal, settings, dt);
  }
  EXPECT_NEAR(std::log2(result.state.latent_scale),
    std::log2(.72) - 8.0 + 1.5 * std::exp(-5.0 / 3.0), 5e-4);
  const auto settled = Run(signal, settings, 1e30F);
  EXPECT_NEAR(
    std::log2(settled.state.latent_scale), std::log2(.72) - 8.0, 5e-4);
}
NOLINT_TEST_F(ExposureGpuTest, NarrowPercentilesStraddleLargeIntegerCdfBoundary)
{
  std::vector<Pixel> pixels(512U * 512U, Pixel { 8, 8, 8, 1 });
  std::fill_n(pixels.begin(), pixels.size() / 2, Pixel { .25F, .25F, .25F, 1 });
  auto settings = scene::ExposureSettings {};
  settings.low_percentile = std::nextafter(.5F, 0.0F);
  settings.high_percentile = std::nextafter(.5F, 1.0F);
  const auto result = Run(MakeSignal(512U, 512U, pixels), settings);
  // Adjacent binary32 fractions straddle the half-mass boundary with a 1:2
  // retained-mass ratio, independent of the nearly 2^30 total count.
  EXPECT_NEAR(result.state.raw_metered_ev, 4.0 / 3.0 - std::log2(.18), 2e-4);
}

NOLINT_TEST_F(ExposureGpuTest, EightKOutputStillUsesAtMost512SquaredSamples)
{
  const auto result = Run(Uniform(.25F, 7680U, 4320U));
  EXPECT_EQ(result.histogram[256], 262144U);
  EXPECT_EQ(result.histogram[102], 1073479680U);
}
NOLINT_TEST_F(ExposureGpuTest, OrdinaryAutoCurveInterpolatesAtRawMeterEv)
{
  auto settings = scene::ExposureSettings {};
  settings.compensation_curve = { { 0.0F, -2.0F }, { 2.0F, 2.0F } };
  const auto result = Run(Uniform(.25F), settings);
  const double raw_ev = std::log2(.25 / .18);
  // Curve contributes 2*EV-2; the denominator contributes -EV.
  EXPECT_NEAR(std::log2(result.state.target_scale), raw_ev - 2.0, 2e-4);
  EXPECT_NEAR(std::log2(result.state.displayed_scale), raw_ev - 2.0, 2e-4);
}

NOLINT_TEST_F(ExposureGpuTest, OrdinaryAutoCurveClampsBothAuthoredEndpoints)
{
  auto settings = scene::ExposureSettings {};
  settings.compensation_curve = { { 1.0F, 2.0F }, { 2.0F, 4.0F } };
  const auto below = Run(Uniform(.25F), settings);
  EXPECT_NEAR(
    std::log2(below.state.target_scale), 2.0 - std::log2(.25 / .18), 2e-4);
  const auto above = Run(Uniform(8.0F), settings);
  EXPECT_NEAR(
    std::log2(above.state.target_scale), 4.0 - std::log2(8.0 / .18), 2e-4);
}

NOLINT_TEST_F(ExposureGpuTest, CurveInputIgnoresEvClampAndAdaptedHistory)
{
  auto settings = scene::ExposureSettings {};
  const auto signal = Uniform(.25F);
  const auto initial = Run(signal, settings);
  settings.min_ev = 0.0F;
  settings.max_ev = .1F;
  settings.compensation_curve = { { 0.0F, -2.0F }, { 2.0F, 2.0F } };
  settings.speed_up = settings.speed_down = 0.0F;
  const auto result = Run(signal, settings, 1.0F);
  const double expected = 2.0 * std::log2(.25 / .18) - 2.0 - .1;
  EXPECT_NEAR(std::log2(result.state.target_scale), expected, 2e-4);
  EXPECT_EQ(result.state.latent_scale, initial.state.latent_scale);
}

NOLINT_TEST_F(ExposureGpuTest, BrighteningUsesSpeedDownAcrossFrameSchedules)
{
  const auto signal = Uniform(.25F);
  const double expected = std::log2(.72) + 8.0 - 1.5 * std::exp(-1.0);
  for (const unsigned frequency : { 30U, 60U, 120U }) {
    ResetHistory();
    auto settings = scene::ExposureSettings {};
    Run(signal, settings);
    settings.compensation_ev = 8.0F;
    settings.speed_up = 7.0F;
    settings.speed_down = 1.0F;
    Snapshot result {};
    for (unsigned frame = 0; frame < 8U * frequency; ++frame) {
      result = Run(signal, settings, 1.0F / frequency);
    }
    EXPECT_NEAR(std::log2(result.state.latent_scale), expected, 5e-4)
      << frequency;
  }
}

NOLINT_TEST_F(ExposureGpuTest, MovingEdgeHasBoundedGridSamplingError)
{
  auto settings = scene::ExposureSettings {};
  settings.low_percentile = 0.0F;
  settings.high_percentile = 1.0F;
  // A 1024x1 content rectangle has 512 cell-centre samples at odd pixels.
  // Moving a sharp five-stop edge by one pixel changes at most one grid cell:
  // geometric-mean error against all pixels is bounded by 5/1024 EV.
  for (const unsigned edge : { 1U, 2U, 3U, 255U, 256U, 257U, 511U, 512U }) {
    std::vector<Pixel> pixels(1024U, Pixel { .25F, .25F, .25F, 1 });
    std::fill_n(pixels.begin(), edge, Pixel { 8, 8, 8, 1 });
    const auto result = Run(MakeSignal(1024U, 1U, pixels), settings);
    EXPECT_EQ(result.histogram[153], (edge / 2U) * 4095U);
    const double exact_sample_ev
      = -2.0 + 5.0 * (edge / 2U) / 512.0 - std::log2(.18);
    const double full_image_ev = -2.0 + 5.0 * edge / 1024.0 - std::log2(.18);
    EXPECT_NEAR(result.state.raw_metered_ev, exact_sample_ev, 2e-4);
    EXPECT_LE(std::abs(result.state.raw_metered_ev - full_image_ev),
      5.0 / 1024.0 + 2e-4);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, SingleBrightPixelRecordsSamplingAliasingWithoutAreaAveraging)
{
  auto settings = scene::ExposureSettings {};
  settings.low_percentile = 0.0F;
  settings.high_percentile = 1.0F;
  for (const unsigned x : { 0U, 1U, 2U, 3U, 510U, 511U, 1022U, 1023U }) {
    std::vector<Pixel> pixels(1024U, Pixel { .25F, .25F, .25F, 1 });
    pixels[x] = Pixel { 8, 8, 8, 1 };
    const auto result = Run(MakeSignal(1024U, 1U, pixels), settings);
    EXPECT_EQ(result.histogram[153], (x % 2U) * 4095U);
    EXPECT_LE(std::abs(result.state.raw_metered_ev
                - (-2.0 + 5.0 / 1024.0 - std::log2(.18))),
      5.0 / 1024.0 + 2e-4);
  }
}
NOLINT_TEST_F(
  ExposureGpuTest, UnavailableMaskPreventsMeterInitializationButNotLockedSolve)
{
  const auto signal = Uniform(.25F);
  const auto pending = Run(signal, {}, 1.0F, nullptr, 1.0F, false);
  EXPECT_EQ(pending.histogram[257], 0U);
  EXPECT_EQ(pending.state.flags & 15U, 0U);
  EXPECT_EQ(pending.state.displayed_scale, 1.0F);
  const auto ready = Run(signal);
  const auto invalid = Run(signal, {}, 1.0F, nullptr, 1.0F, false);
  EXPECT_EQ(invalid.state.displayed_scale, ready.state.displayed_scale);
  EXPECT_EQ(invalid.state.flags & 12U, 0U);
  auto locked = scene::ExposureSettings {};
  locked.min_ev = locked.max_ev = 2.0F;
  EXPECT_EQ(
    Run(signal, locked, 0.0F, nullptr, 1.0F, false).state.displayed_scale,
    .25F);
}

NOLINT_TEST_F(
  ExposureGpuTest, CookedMaskUploadAndResidentLeaseReachProductionHistogram)
{
  auto loader = vortex::testing::FakeAssetLoader {};
  auto service = PostProcessService(*renderer_, observer_ptr { &loader });
  auto payload = vortex::testing::MakeCookedTexture1x1Rgba8Payload();
  data::pak::render::TexturePayloadHeader header {};
  std::memcpy(&header,
    payload.data() + sizeof(data::pak::core::TextureResourceDesc),
    sizeof(header));
  payload[sizeof(data::pak::core::TextureResourceDesc)
    + header.data_offset_bytes] = 128U;
  auto settings = scene::ExposureSettings {};
  settings.metering_mask = loader.PreloadCookedTexture(std::span(payload));
  const auto tag = internal::RendererTagFactory::Get();
  renderer_->GetUploadCoordinator().OnFrameStart(tag, frame::Slot { 0U });
  service.OnFrameStart(frame::SequenceNumber { 1U }, frame::Slot { 0U });
  EXPECT_EQ(service
              .ResolveViewExposureSettings(
                ctx_.current_view.view_state_handle, settings)
              .mask_status,
    PostProcessService::ExposureMaskStatus::kPending);
  // Each step flushes submitted upload work and observes its completed ticket.
  // This is resource-readiness synchronization, not exposure-settling warmup.
  for (unsigned i = 0U; i < 8U; ++i) {
    WaitForQueueIdle();
    const auto slot = frame::Slot { (i + 1U) % 3U };
    renderer_->GetUploadCoordinator().OnFrameStart(tag, slot);
    service.OnFrameStart(frame::SequenceNumber { i + 2U }, slot);
    if (service
          .ResolveViewExposureSettings(
            ctx_.current_view.view_state_handle, settings)
          .mask_status
      == PostProcessService::ExposureMaskStatus::kReady) {
      break;
    }
  }
  const auto& accepted = service.ResolveViewExposureSettings(
    ctx_.current_view.view_state_handle, settings);
  ASSERT_EQ(
    accepted.mask_status, PostProcessService::ExposureMaskStatus::kReady);
  ASSERT_NE(accepted.mask, nullptr);
  const auto mask = Signal { accepted.mask->texture, accepted.mask->srv };
  const auto result = Run(Uniform(.25F), settings, 0.0F, &mask);
  EXPECT_EQ(result.histogram[102], 2056U); // round-half-up(4095*128/255).
  EXPECT_EQ(result.histogram[257], 1U);
  EXPECT_NEAR(result.state.raw_metered_ev, std::log2(.25 / .18), 2e-4);
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, LockedServiceGainReachesTonemapDespitePendingOrFailedMask)
{
  auto loader = vortex::testing::FakeAssetLoader {};
  auto service = PostProcessService(*renderer_, observer_ptr { &loader });
  const auto signal = Uniform(1.0F, 4U, 4U);
  auto output_desc = TextureDesc {};
  output_desc.width = output_desc.height = 4U;
  output_desc.format = Format::kRGBA32Float;
  output_desc.is_render_target = true;
  output_desc.initial_state = ResourceStates::kCommon;
  const auto output = CreateRegisteredTexture(output_desc);
  auto framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  auto textures
    = SceneTextures(Backend(), SceneTexturesConfig { .extent = { 4U, 4U } });
  unsigned id = 1U;
  for (bool previous : { false, true }) {
    for (bool failure : { false, true }) {
      const auto handle = CompositionView::ViewStateHandle { id++ };
      ctx_.current_view.view_state_handle = handle;
      ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
      service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
      auto requested = scene::ExposureSettings {};
      requested.key = 12.5F;
      if (previous) {
        static_cast<void>(
          service.ResolveViewExposureSettings(handle, requested));
      }
      requested.metering_mask = loader.MintSyntheticTextureKey();
      EXPECT_EQ(
        service.ResolveViewExposureSettings(handle, requested).mask_status,
        PostProcessService::ExposureMaskStatus::kPending);
      if (failure) {
        ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
        service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
        EXPECT_EQ(
          service.ResolveViewExposureSettings(handle, requested).mask_status,
          PostProcessService::ExposureMaskStatus::kFailed);
      }
      requested.min_ev = requested.max_ev = 2.0F;
      const auto& accepted
        = service.ResolveViewExposureSettings(handle, requested);
      ASSERT_EQ(accepted.resolved.authored.min_ev, 2.0F);
      auto config = PostProcessConfig {};
      config.resolved_exposure = accepted.resolved;
      config.auto_exposure_min_ev = config.auto_exposure_max_ev = 2.0F;
      config.enable_bloom = false;
      config.bloom_intensity = 0.0F;
      config.tone_mapper = engine::ToneMapper::kNone;
      config.gamma = 1.0F;
      service.SetConfig(config);
      ctx_.delta_time = 0.0F;
      service.Execute(ctx_.current_view.view_id, ctx_, textures,
        {
          .scene_signal = signal.texture.get(),
          .post_target = observer_ptr<const Framebuffer> { framebuffer.get() },
          .scene_signal_srv = signal.srv,
        });
      EXPECT_TRUE(service.GetLastExecutionState().tonemap_executed);
      auto readback
        = GetReadbackManager()->CreateTextureReadback("Locked service result");
      {
        auto recorder = AcquireRecorder("Read locked service tonemap");
        ASSERT_TRUE(recorder->AdoptKnownResourceState(*output));
        const auto ticket = readback->EnqueueCopy(*recorder, *output,
          {
            .src_slice
            = { .x = 1U, .y = 0U, .width = 1U, .height = 1U, .depth = 1U },
          });
        ASSERT_TRUE(ticket.has_value());
      }
      const auto mapped = readback->MapNow();
      ASSERT_TRUE(mapped.has_value());
      Pixel pixel {};
      std::memcpy(pixel.data(), mapped->Data(), sizeof(pixel));
      for (unsigned channel = 0U; channel < 3U; ++channel) {
        EXPECT_NEAR(pixel[channel], .25F, 2e-5F) << previous << failure;
      }
    }
  }
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, NarrowCdfBoundariesRetainIntegerAndFractionalProductBits)
{
  // Power-of-two CDF boundaries exercise both sides of the 32-bit word shift
  // and narrow fractional tails. Below each exact boundary binary32 spacing
  // is half the spacing above it, giving retained mass ratio 1:2.
  for (unsigned low_pixels : { 1U, 512U, 32768U, 131072U }) {
    std::vector<Pixel> pixels(262144U, Pixel { .25F, .25F, .25F, 1 });
    std::fill_n(pixels.begin(), low_pixels,
      Pixel { 1.0F / 128.0F, 1.0F / 128.0F, 1.0F / 128.0F, 1 });
    auto settings = scene::ExposureSettings {};
    const float boundary = static_cast<float>(low_pixels) / 262144.0F;
    settings.low_percentile = std::nextafter(boundary, 0.0F);
    settings.high_percentile = std::nextafter(boundary, 1.0F);
    const auto result = Run(MakeSignal(512U, 512U, pixels), settings);
    EXPECT_EQ(result.histogram[51], low_pixels * 4095U);
    EXPECT_EQ(result.histogram[102], (262144U - low_pixels) * 4095U);
    EXPECT_NEAR(result.state.raw_metered_ev, -11.0 / 3.0 - std::log2(.18), 2e-4)
      << low_pixels;
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, DarkCountersSeparateBlackPositiveBelowWindowAndNegative)
{
  const auto pixels = std::array { Pixel { 0, 0, 0, 1 },
    Pixel { 0x1p-20F, 0x1p-20F, 0x1p-20F, 1 },
    Pixel { -.25F, -.25F, -.25F, 1 } };
  const auto result = Run(MakeSignal(3U, 1U, pixels));
  EXPECT_EQ(result.histogram[256], 3U);
  EXPECT_EQ(result.histogram[257], 3U);
  EXPECT_EQ(result.histogram[258], 1U);
  EXPECT_EQ(result.histogram[259], 1U);
  EXPECT_EQ(result.histogram[260], 0U);
  EXPECT_EQ(result.histogram[261], 3U);
  EXPECT_EQ(result.state.flags & 31U, 27U);
  EXPECT_EQ(result.state.raw_metered_ev, -6.0F);
  EXPECT_EQ(result.state.displayed_scale, 64.0F);
}

NOLINT_TEST_F(ExposureGpuTest,
  AllNonfiniteInputKeepsAutoEvZeroFallbackIndependentOfManualEv)
{
  auto settings = scene::ExposureSettings {};
  settings.manual_ev = 31.0F;
  const auto result
    = Run(Uniform(std::numeric_limits<float>::quiet_NaN()), settings);
  EXPECT_EQ(result.histogram[256], 0U);
  EXPECT_EQ(result.histogram[260], 1U);
  EXPECT_EQ(result.state.flags & 15U, 0U);
  EXPECT_EQ(result.state.displayed_scale, 1.0F);
  EXPECT_EQ(result.state.latent_scale, 1.0F);
}

NOLINT_TEST_F(
  ExposureGpuTest, ZeroTargetContinuesLatentAdaptationAndUpdatesLastValidMeter)
{
  auto settings = scene::ExposureSettings {};
  Run(Uniform(.25F), settings);
  settings.target_luminance = 0.0F;
  const auto dark_output = Run(Uniform(8.0F), settings, 2.0F);
  EXPECT_EQ(dark_output.state.displayed_scale, 0.0F);
  EXPECT_EQ(dark_output.state.target_scale, 0.0F);
  EXPECT_NEAR(dark_output.state.latent_target_scale, .0225F, 2e-6);
  EXPECT_NEAR(std::log2(dark_output.state.latent_scale),
    std::log2(.0225) + 1.5 * std::exp(-5.0 / 3.0), 5e-4);
  EXPECT_NEAR(dark_output.state.raw_metered_ev, std::log2(8.0 / .18), 2e-4);
  settings.target_luminance = .36F;
  const auto restored
    = Run(Uniform(0.0F), settings, 0.0F, nullptr, 1.0F, false);
  EXPECT_NEAR(restored.state.displayed_scale, .045F, 2e-6);
}

NOLINT_TEST_F(ExposureGpuTest, LockedCurveUsesBoundInsteadOfRawMeteredEv)
{
  auto settings = scene::ExposureSettings {};
  settings.min_ev = settings.max_ev = 2.0F;
  settings.compensation_curve = { { 0.0F, -2.0F }, { 4.0F, 6.0F } };
  EXPECT_NEAR(Run(Uniform(.25F), settings).state.displayed_scale, 1.0F, 2e-6);
  EXPECT_NEAR(Run(Uniform(8.0F), settings).state.displayed_scale, 1.0F, 2e-6);
  EXPECT_NEAR(Run(Uniform(0.0F), settings, 0.0F, nullptr, 1.0F, false)
                .state.displayed_scale,
    1.0F, 2e-6);
}

NOLINT_TEST_F(
  ExposureGpuTest, SubnormalCurveCoordinatesInterpolateAtExactZeroMeterEv)
{
  for (const float coordinate :
    { 1.0e-40F, std::numeric_limits<float>::denorm_min() }) {
    ResetHistory();
    auto settings = scene::ExposureSettings {};
    settings.min_log_luminance = std::log2(.18F);
    settings.log_luminance_range = 25.0F;
    settings.low_percentile = 0.0F;
    settings.high_percentile = 0x1p-16F;
    settings.compensation_curve
      = { { -coordinate, -1.0F }, { coordinate, 1.0F } };
    // Brighter than the dark threshold, but entirely quantized to bin zero.
    // The exact bin position gives EV zero and midpoint compensation zero.
    const auto result = Run(Uniform(.1800001F), settings);
    ASSERT_EQ(result.histogram[261], 0U);
    ASSERT_EQ(result.histogram[0], 4095U);
    ASSERT_EQ(result.state.raw_metered_ev, 0.0F);
    EXPECT_NEAR(result.state.target_scale, 1.0F, 2e-5F);
    EXPECT_NEAR(result.state.displayed_scale, 1.0F, 2e-5F);
  }
}

NOLINT_TEST_F(ExposureGpuTest, PublicPausedFrameSessionFreezesGpuAdaptation)
{
  const auto before = Run(Uniform(.25F));
  auto output_desc = TextureDesc {};
  output_desc.width = output_desc.height = 1U;
  output_desc.format = Format::kRGBA8UNorm;
  output_desc.is_render_target = output_desc.is_shader_resource = true;
  output_desc.initial_state = ResourceStates::kCommon;
  const auto output = CreateRegisteredTexture(output_desc);
  auto framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  auto params = ResolvedView::Params {};
  params.view_config.viewport = { .width = 1.0F, .height = 1.0F };
  auto facade = renderer_->ForSinglePassHarness();
  facade.SetFrameSession(Renderer::FrameSessionInput {
    .frame_slot = frame::Slot { 0U },
    .frame_sequence = frame::SequenceNumber { 2U },
    .delta_time_seconds = 0.0F,
  });
  facade.SetResolvedView(Renderer::ResolvedViewInput {
    .view_id = ViewId { 1U }, .value = ResolvedView { params } });
  facade.SetOutputTarget(Renderer::OutputTargetInput {
    .framebuffer = observer_ptr<Framebuffer> { framebuffer.get() } });
  const auto paused = facade.Finalize();
  ASSERT_TRUE(paused.has_value());
  ASSERT_EQ(paused->GetRenderContext().delta_time, 0.0F);
  const auto after
    = Run(Uniform(8.0F), {}, paused->GetRenderContext().delta_time);
  EXPECT_EQ(after.state.latent_scale, before.state.latent_scale);
  EXPECT_NE(after.state.latent_target_scale, before.state.latent_target_scale);
}

NOLINT_TEST_F(ExposureGpuTest, ManualCameraAndDisabledWriteUnifiedGpuState)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 14.0F;
  const auto manual = Run(Signal {}, settings);
  EXPECT_EQ(manual.state.displayed_scale, 0x1p-14F);
  EXPECT_EQ(manual.state.latent_scale, 0x1p-14F);
  EXPECT_EQ(manual.state.flags & 12U, 0U);
  settings.mode = engine::ExposureMode::kManualCamera;
  const auto camera = Run(Signal {}, settings, 0.0F, nullptr, 1.0F, true, {},
    static_cast<float>(std::log2(15125.0)));
  EXPECT_NEAR(camera.state.displayed_scale, 1.0 / 15125.0, 2e-5 / 15125.0);
  EXPECT_EQ((camera.state.flags >> 10U) & 3U, 1U);
  settings.enabled = false;
  const auto disabled = Run(Signal {}, settings);
  EXPECT_EQ(disabled.state.displayed_scale, 1.0F);
  EXPECT_EQ(disabled.state.latent_scale, 1.0F);
  EXPECT_EQ((disabled.state.flags >> 10U) & 3U, 3U);
}

NOLINT_TEST_F(
  ExposureGpuTest, ManualToAutoPreservesGainForTransitionFrameThenAdapts)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto manual = Run(Uniform(.25F), settings);
  settings.mode = engine::ExposureMode::kAuto;
  const auto transition = Run(Uniform(.25F), settings, 1.0F);
  EXPECT_EQ(transition.state.displayed_scale, manual.state.displayed_scale);
  EXPECT_NEAR(transition.state.target_scale, .72F, 2e-5);
  const auto next = Run(Uniform(.25F), settings, 1.0F);
  EXPECT_NEAR(next.state.displayed_scale, .125F, 2e-5);
}

NOLINT_TEST_F(
  ExposureGpuTest, SeedUsesRequestedEvAndDuplicateGenerationDoesNotReapply)
{
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  const auto event = Run(Uniform(.25F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_EQ(event.state.displayed_scale, 0x1p-8F);
  EXPECT_EQ(event.state.applied_generation[0], token->generation);
  const auto next = Run(Uniform(.25F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_NEAR(next.state.displayed_scale, 0x1p-7F, 2e-5);
  EXPECT_EQ(next.state.applied_generation, event.state.applied_generation);
}

NOLINT_TEST_F(
  ExposureGpuTest, RemeterRemainsPendingWithoutInputAndAppliesAtZeroDelta)
{
  const auto token = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(token.has_value());
  const auto invalid = Run(Signal {}, {}, 0.0F, nullptr, 1.0F, false, *token);
  EXPECT_EQ(invalid.state.applied_generation[0], 0U);
  EXPECT_EQ(invalid.state.requested_generation[0], token->generation);
  const auto applied
    = Run(Uniform(.25F), {}, 0.0F, nullptr, 1.0F, true, *token);
  EXPECT_NEAR(applied.state.displayed_scale, .72F, 2e-5);
  EXPECT_EQ(applied.state.applied_generation[0], token->generation);
  const auto retry = Run(Uniform(8.0F), {}, 0.0F, nullptr, 1.0F, true, *token);
  EXPECT_EQ(retry.state.displayed_scale, applied.state.displayed_scale);
}

NOLINT_TEST_F(
  ExposureGpuTest, RejectedManualSeedDoesNotReactivateOnLaterAutoEntry)
{
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto rejected
    = Run(Signal {}, settings, 0.0F, nullptr, 1.0F, false, *token);
  EXPECT_EQ(rejected.state.applied_generation[0], 0U);
  EXPECT_NE(rejected.state.flags & (1U << 12U), 0U);
  settings.mode = engine::ExposureMode::kAuto;
  const auto next
    = Run(Uniform(.25F), settings, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_EQ(next.state.displayed_scale, 0x1p-4F);
  EXPECT_EQ(next.state.applied_generation[0], 0U);
}

NOLINT_TEST_F(
  ExposureGpuTest, StatelessAutoSolvesEachInvocationWithoutAdaptation)
{
  ctx_.current_view.view_state_handle
    = CompositionView::kInvalidViewStateHandle;
  const auto first = Run(Uniform(.25F), {}, 0.0F);
  const auto next = Run(Uniform(8.0F), {}, 0.0F);
  EXPECT_NEAR(first.state.displayed_scale, .72F, 2e-5);
  EXPECT_NEAR(next.state.displayed_scale, .0225F, 2e-5);
}

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
  auto config = PostProcessConfig {};
  config.resolved_exposure = *scene::ResolveExposureSettings(settings);
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto first = pass_->Execute(ctx_, config, {});
  ASSERT_TRUE(first.executed);
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 2U };
  ctx_.current_view.view_id = ViewId { 2U };
  settings.manual_ev = 8.0F;
  config.resolved_exposure = *scene::ResolveExposureSettings(settings);
  const auto second = pass_->Execute(ctx_, config, {});
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
  const auto duplicate = pass_->Execute(ctx_, config, {});
  EXPECT_FALSE(duplicate.executed);
  EXPECT_EQ(duplicate.state, second.state);
}

NOLINT_TEST_F(
  ExposureGpuTest, DiagnosticUnitStatePreservesAutoHistoryAndPendingSeed)
{
  const auto before = Run(Uniform(.25F));
  const auto persistent = last_state_;
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  for (unsigned i = 0U; i < 3U; ++i) {
    const auto diagnostic
      = Run(Uniform(8.0F), {}, 5.0F, nullptr, 1.0F, true, *token, {}, true);
    EXPECT_EQ(diagnostic.state.displayed_scale, 1.0F);
    EXPECT_EQ(diagnostic.state.applied_generation[0], 0U);
    const auto retained = Read<ExposureStateData>(
      *persistent->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(retained.displayed_scale, before.state.displayed_scale);
    EXPECT_EQ(retained.applied_generation, before.state.applied_generation);
  }
  const auto resumed
    = Run(Uniform(8.0F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_EQ(resumed.state.displayed_scale, 0x1p-8F);
  const auto next = Run(Uniform(8.0F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_NEAR(next.state.displayed_scale, 0x1p-7F, 2e-5);
}

NOLINT_TEST_F(ExposureGpuTest, DiagnosticUnitStateDoesNotOverwriteManualHistory)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto before = Run(Signal {}, settings);
  const auto persistent = last_state_;
  const auto diagnostic
    = Run(Signal {}, settings, 5.0F, nullptr, 1.0F, true, {}, {}, true);
  EXPECT_EQ(diagnostic.state.displayed_scale, 1.0F);
  EXPECT_EQ(Read<ExposureStateData>(
              *persistent->buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    before.state.displayed_scale);
  EXPECT_EQ(Run(Signal {}, settings).state.displayed_scale, 0x1p-4F);
}

NOLINT_TEST_F(
  ExposureGpuTest, ServiceDiagnosticFramesDoNotAcknowledgePendingTransition)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  EXPECT_NEAR(ServicePixel(service, signal), .18F, 2e-5);
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  for (unsigned i = 0U; i < 3U; ++i) {
    EXPECT_NEAR(ServicePixel(service, signal, {}, true, 3.0F), .25F, 2e-5);
    EXPECT_FALSE(service.GetLastExecutionState().auto_exposure_requested);
    EXPECT_FALSE(service.GetLastExecutionState().auto_exposure_executed);
    ASSERT_NE(service.InspectBindings(ctx_.current_view.view_id), nullptr);
    EXPECT_EQ(
      service.InspectBindings(ctx_.current_view.view_id)->enable_auto_exposure,
      0U);
    EXPECT_EQ(renderer_->InspectExposureTransition(token->target)->phase,
      ExposureTransitionPhase::kQueued);
  }
  EXPECT_NEAR(
    ServicePixel(service, signal, {}, false, 1.0F), .25F / 256.0F, 2e-5);
  EXPECT_EQ(renderer_->InspectExposureTransition(token->target)->phase,
    ExposureTransitionPhase::kQueued);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto status = renderer_->InspectExposureTransition(token->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kApplied);
  EXPECT_EQ(status->applied_generation, token->generation);
}

NOLINT_TEST_F(
  ExposureGpuTest, CompletedOldGenerationCannotConsumeNewerQueuedTransition)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  const auto first
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(first.has_value());
  EXPECT_NEAR(ServicePixel(service, signal), .25F / 256.0F, 2e-5);
  const auto second = renderer_->QueueExposureTransition(
    first->target, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(second.has_value());
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto intermediate = renderer_->InspectExposureTransition(first->target);
  ASSERT_TRUE(intermediate.has_value());
  EXPECT_EQ(intermediate->request.generation, second->generation);
  EXPECT_EQ(intermediate->phase, ExposureTransitionPhase::kQueued);
  EXPECT_EQ(intermediate->applied_generation, first->generation);
  EXPECT_NEAR(ServicePixel(service, signal), .18F, 2e-5);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  EXPECT_EQ(
    renderer_->InspectExposureTransition(first->target)->applied_generation,
    second->generation);
}

NOLINT_TEST_F(
  ExposureGpuTest, TransitionQueuedAfterFrameCaptureWaitsForNextFrame)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  std::optional<ExposureTransitionToken> token;
  const auto current = ServicePixel(service, signal, {}, false, 0.0F, [&] {
    const auto issued
      = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
        ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
    CHECK_F(issued.has_value());
    token = *issued;
  });
  EXPECT_NEAR(current, .18F, 2e-5);
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(renderer_->InspectExposureTransition(token->target)->phase,
    ExposureTransitionPhase::kQueued);
  EXPECT_NEAR(ServicePixel(service, signal), .25F / 256.0F, 2e-5);
}

NOLINT_TEST_F(
  ExposureGpuTest, RetiredViewAcknowledgementCannotApplyToReusedHandle)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  const auto first
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(first.has_value());
  ServicePixel(service, signal);
  service.RemoveViewState(
    ctx_.current_view.view_id, ctx_.current_view.view_state_handle);
  const auto next = renderer_->QueueExposureTransition(
    first->target, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(next.has_value());
  EXPECT_NE(next->lifetime, first->lifetime);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto status = renderer_->InspectExposureTransition(next->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kQueued);
  EXPECT_EQ(status->applied_generation, 0U);
  EXPECT_FALSE(renderer_->RetryExposureTransition(*first).has_value());
}

NOLINT_TEST_F(ExposureGpuTest, PreserveHoldsOnlyItsEventFrame)
{
  const auto before = Run(Uniform(.25F));
  const auto token = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kPreserve);
  ASSERT_TRUE(token.has_value());
  const auto event = Run(Uniform(8.0F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_EQ(event.state.displayed_scale, before.state.displayed_scale);
  EXPECT_EQ(event.state.applied_generation[0], token->generation);
  const auto next = Run(Uniform(8.0F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_NEAR(
    next.state.displayed_scale, before.state.displayed_scale / 8.0F, 2e-5F);
}

NOLINT_TEST_F(ExposureGpuTest, SeedOutsideLockedRangeOwnsOnlyItsEventFrame)
{
  auto settings = scene::ExposureSettings {};
  settings.min_ev = settings.max_ev = 4.0F;
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 12.0F);
  ASSERT_TRUE(token.has_value());
  const auto event
    = Run(Signal {}, settings, 0.0F, nullptr, 1.0F, false, *token);
  EXPECT_EQ(event.state.displayed_scale, 0x1p-12F);
  EXPECT_EQ(event.state.applied_generation[0], token->generation);
  const auto next
    = Run(Signal {}, settings, 0.0F, nullptr, 1.0F, false, *token);
  EXPECT_EQ(next.state.displayed_scale, 0x1p-4F);
}

NOLINT_TEST_F(ExposureGpuTest, ServiceManualConsumesExactGpuGain)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(4096.0F, 4U, 4U);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  for (const auto ev : { 14.0F, 16.0F, 32.0F }) {
    settings.manual_ev = ev;
    const auto expected = std::exp2(12.0F - ev);
    EXPECT_NEAR(
      ServicePixel(service, signal, settings), expected, expected * 2e-5F);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, CompletedUnsupportedSeedRejectsWithoutChangingGain)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  const auto before = ServicePixel(service, signal);
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 1000.0F);
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(ServicePixel(service, signal), before);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto status = renderer_->InspectExposureTransition(token->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kRejected);
  EXPECT_EQ(status->error, ExposureTransitionError::kUnsupportedSeed);
  EXPECT_EQ(status->applied_generation, 0U);
}

NOLINT_TEST_F(
  ExposureGpuTest, LateModeChangeCannotAlterCapturedTransitionSemantics)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  const auto pixel = ServicePixel(service, signal, settings, false, 0.0F, [&] {
    settings.key = 12.5F;
    settings.mode = engine::ExposureMode::kAuto;
    const auto& late = service.ResolveViewExposureSettings(
      ctx_.current_view.view_state_handle, settings);
    auto config = service.GetConfig();
    config.resolved_exposure = late.resolved;
    config.exposure_settings_revision = late.revision;
    config.enable_auto_exposure = true;
    service.SetConfig(config);
  });
  EXPECT_NEAR(pixel, .25F / 16.0F, 2e-5F);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto status = renderer_->InspectExposureTransition(token->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kRejected);
  EXPECT_EQ(status->error, ExposureTransitionError::kNotAuto);
  EXPECT_NEAR(
    ServicePixel(service, signal, settings, false, 1.0F), .25F / 16.0F, 2e-5F);
}

NOLINT_TEST_F(
  ExposureGpuTest, LateMaskRemovalCannotEnableMeteringForCapturedFrame)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  auto settings = scene::ExposureSettings {};
  settings.metering_mask = content::ResourceKey { 123U };
  const auto pixel = ServicePixel(service, signal, settings, false, 0.0F, [&] {
    settings.key = 12.5F;
    settings.metering_mask = {};
    const auto& late = service.ResolveViewExposureSettings(
      ctx_.current_view.view_state_handle, settings);
    auto config = service.GetConfig();
    config.resolved_exposure = late.resolved;
    config.exposure_settings_revision = late.revision;
    service.SetConfig(config);
  });
  // The initial invalid-mask fallback uses the canonical key 10 at EV0.
  EXPECT_NEAR(pixel, .25F * .8F, 2e-5F);
  EXPECT_NEAR(ServicePixel(service, signal, settings), .18F, 2e-5F);
}

NOLINT_TEST_F(
  ExposureGpuTest, SharedConsumersUsePriorOwnerStateInBothRenderOrders)
{
  const auto owner_signal = Uniform(.25F);
  const auto consumer_signal = Uniform(8.0F);
  const auto config = SharedConfig();
  const auto source = postprocess::ExposurePass::Source {
    .handle = CompositionView::ViewStateHandle { 1U }, .config = config
  };
  for (bool consumer_first : { false, true }) {
    pass_->RemoveViewState(CompositionView::ViewStateHandle { 1U });
    pass_->RemoveViewState(CompositionView::ViewStateHandle { 2U });
    for (unsigned frame_index = 0; frame_index < 2U; ++frame_index) {
      ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
      ctx_.delta_time = 0.0F;
      postprocess::ExposurePass::Result owner;
      postprocess::ExposurePass::Result consumer;
      const auto render_owner = [&] {
        ctx_.current_view.view_id = ViewId { 1U };
        ctx_.current_view.view_state_handle = source.handle;
        owner = RecordShared(owner_signal, config);
      };
      const auto render_consumer = [&] {
        ctx_.current_view.view_id = ViewId { 2U };
        ctx_.current_view.view_state_handle
          = CompositionView::ViewStateHandle { 2U };
        consumer = RecordShared(consumer_signal, config, &source);
      };
      if (consumer_first) {
        render_consumer();
        render_owner();
      } else {
        render_owner();
        render_consumer();
      }
      ASSERT_TRUE(owner.executed);
      ASSERT_TRUE(consumer.executed);
      EXPECT_EQ(consumer.histogram_buffer, nullptr);
      EXPECT_NE(owner.state, consumer.state);
      EXPECT_NEAR(ReadState(owner).displayed_scale, .72F, 2e-5F);
      const auto borrowed = ReadState(consumer);
      EXPECT_NEAR(
        borrowed.displayed_scale, frame_index == 0U ? 1.0F : .72F, 2e-5F);
      EXPECT_EQ(borrowed.flags & (4U | 8U | 512U), 0U);
      EXPECT_NE(borrowed.flags & 128U, 0U);
    }
  }
}

NOLINT_TEST_F(ExposureGpuTest, SharedSourceResetBecomesVisibleOnTheNextFrame)
{
  const auto signal = Uniform(.25F);
  const auto config = SharedConfig();
  auto source = postprocess::ExposurePass::Source {
    .handle = CompositionView::ViewStateHandle { 1U }, .config = config
  };
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto first = RecordShared(signal, config);
  ASSERT_TRUE(first.executed);
  const auto seed = renderer_->QueueExposureTransition(
    source.handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(seed.has_value());
  source.transition = *seed;
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto reset = RecordShared(signal, config, nullptr, *seed);
  ctx_.current_view.view_id = ViewId { 2U };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 2U };
  const auto same_frame = RecordShared(signal, config, &source);
  EXPECT_EQ(ReadState(reset).displayed_scale, 0x1p-8F);
  EXPECT_NEAR(ReadState(same_frame).displayed_scale, .72F, 2e-5F);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  // The source is inactive in this frame; retain its last completed
  // publication.
  const auto next = RecordShared(signal, config, &source);
  EXPECT_EQ(ReadState(next).displayed_scale, 0x1p-8F);
}

NOLINT_TEST_F(ExposureGpuTest, SharedBootstrapUsesSourceModeBoundsAndSeed)
{
  const auto signal = Uniform(8.0F);
  auto consumer_settings = scene::ExposureSettings {};
  consumer_settings.enabled = false;
  const auto consumer_config = SharedConfig(consumer_settings);
  auto settings = scene::ExposureSettings {};
  settings.min_ev = 4.0F;
  auto source = postprocess::ExposurePass::Source { .handle
    = CompositionView::ViewStateHandle { 1U },
    .config = SharedConfig(settings) };
  ctx_.current_view.view_id = ViewId { 2U };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 2U };
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto initial = RecordShared(signal, consumer_config, &source);
  EXPECT_EQ(ReadState(initial).displayed_scale, 0x1p-4F);
  EXPECT_EQ(ReadState(initial).fallback_reason, 3U);
  const auto seed = renderer_->QueueExposureTransition(
    source.handle, ExposureTransitionPolicy::kSeedFromEv100, -2.0F);
  ASSERT_TRUE(seed.has_value());
  source.transition = *seed;
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  EXPECT_EQ(
    ReadState(RecordShared(signal, consumer_config, &source)).displayed_scale,
    4.0F);
  source.transition.reset();
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 14.0F;
  source.config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  EXPECT_EQ(
    ReadState(RecordShared(signal, consumer_config, &source)).displayed_scale,
    0x1p-14F);
  settings.mode = engine::ExposureMode::kManualCamera;
  source.config
    = SharedConfig(settings, static_cast<float>(std::log2(15125.0)));
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  EXPECT_NEAR(
    ReadState(RecordShared(signal, consumer_config, &source)).displayed_scale,
    1.0 / 15125.0, 2e-5 / 15125.0);
  settings.enabled = false;
  source.config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  EXPECT_EQ(
    ReadState(RecordShared(signal, consumer_config, &source)).displayed_scale,
    1.0F);
  settings.enabled = true;
  settings.mode = engine::ExposureMode::kAuto;
  settings.target_luminance = 0.0F;
  source.config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto zero = ReadState(RecordShared(signal, consumer_config, &source));
  EXPECT_EQ(zero.displayed_scale, 0.0F);
  EXPECT_EQ(zero.latent_scale, 0x1p-4F);
}

NOLINT_TEST_F(
  ExposureGpuTest, SharedConsumerTransitionIsRejectedAfterGpuCompletion)
{
  auto service = PostProcessService(*renderer_);
  ctx_.current_view.view_id = ViewId { 2U };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 2U };
  ctx_.current_view.exposure_view_id = ViewId { 1U };
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::ViewStateHandle { 1U };
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  const auto pixel
    = ServicePixel(service, Uniform(.25F, 4U, 4U), {}, false, 0.0F, [&] {
        auto source = scene::ExposureSettings {};
        source.key = 12.5F;
        source.mode = engine::ExposureMode::kManual;
        source.manual_ev = 4.0F;
        static_cast<void>(service.CaptureViewExposureSettings(
          ViewId { 1U }, CompositionView::ViewStateHandle { 1U }, source));
      });
  EXPECT_NEAR(pixel, .25F / 16.0F, 2e-5F);
  EXPECT_FALSE(service.GetLastExecutionState().auto_exposure_executed);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto status = renderer_->InspectExposureTransition(token->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kRejected);
  EXPECT_EQ(status->error, ExposureTransitionError::kSharedConsumer);
}

NOLINT_TEST_F(ExposureGpuTest,
  RegisteredInactiveSourceBootstrapsConsumersWithoutConsumingItsSeed)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  auto texture = CreateRegisteredTexture(TextureDesc { .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto target = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(texture));
  auto view = CompositionView {};
  view.id = ViewId { 50U };
  view.view_state_handle = CompositionView::ViewStateHandle { 50U };
  view.render_settings.exposure = scene::ExposureSettings {};
  view.render_settings.exposure->key = 12.5F;
  const auto published = renderer_->PublishRuntimeCompositionView(frame,
    { .composition_view = view,
      .render_target = observer_ptr { target.get() } });
  ASSERT_NE(published, kInvalidViewId);
  const auto token = renderer_->QueueExposureTransition(
    view.view_state_handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  ctx_.current_view.view_id = ViewId { 900U };
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 900U };
  ctx_.current_view.exposure_view_id = published;
  ctx_.current_view.exposure_view_state_handle = view.view_state_handle;
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F, 4U, 4U)), .25F / 256.0F, 2e-5F);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  EXPECT_EQ(renderer_->InspectExposureTransition(token->target)->phase,
    ExposureTransitionPhase::kQueued);
}

NOLINT_TEST_F(
  ExposureGpuTest, SharingDiscardsDormantMeterHistoryAndExplicitDetachRemeters)
{
  ctx_.current_view.view_id = ViewId { 2U };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 2U };
  const auto before = Run(Uniform(.25F));
  EXPECT_NEAR(before.state.displayed_scale, .72F, 2e-5F);
  const auto config = SharedConfig();
  const auto source = postprocess::ExposurePass::Source {
    .handle = CompositionView::ViewStateHandle { 1U }, .config = config
  };
  const auto dark = Uniform(8.0F);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto shared = ReadState(RecordShared(dark, config, &source));
  EXPECT_EQ(shared.displayed_scale, 1.0F);
  EXPECT_EQ(shared.flags & (4U | 8U | 512U), 0U);
  const auto remeter = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(remeter.has_value());
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  ctx_.delta_time = 0.0F;
  const auto detached
    = ReadState(RecordShared(dark, config, nullptr, *remeter));
  EXPECT_NEAR(detached.displayed_scale, .0225F, 2e-5F);
  EXPECT_EQ(detached.applied_generation[0], remeter->generation);
  EXPECT_EQ(detached.flags & 128U, 0U);
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
    ASSERT_TRUE(token.has_value());
    latest = *token;
    const auto result = Run(signal, {}, 0.0F, nullptr, 1.0F, true, *token);
    EXPECT_EQ(result.state.applied_generation[0], token->generation);
    vortex::testing::RendererPublicationProbe::EnqueueExposureStatus(
      service, *token, last_state_, ctx_, sequence_);
  }
  const auto full
    = vortex::testing::RendererPublicationProbe::ExposureStatusCounts(
      service, latest->target);
  EXPECT_EQ(full.first, frame::kFramesInFlight.get());
  EXPECT_EQ(full.second, 1U);
  service.OnFrameStart(
    frame::SequenceNumber { ++sequence_ }, frame::Slot { 1U });
  EXPECT_EQ(renderer_->InspectExposureTransition(latest->target)->phase,
    ExposureTransitionPhase::kQueued);
  EXPECT_EQ(
    renderer_->InspectExposureTransition(latest->target)->applied_generation,
    3U);
  WaitForQueueIdle();
  service.OnFrameStart(
    frame::SequenceNumber { ++sequence_ }, frame::Slot { 2U });
  const auto completed = renderer_->InspectExposureTransition(latest->target);
  ASSERT_TRUE(completed.has_value());
  EXPECT_EQ(completed->phase, ExposureTransitionPhase::kApplied);
  EXPECT_EQ(completed->applied_generation, latest->generation);
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStatusCounts(
              service, latest->target),
    (std::pair<std::size_t, std::size_t> { 0U, 0U }));
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
    ASSERT_TRUE(token.has_value());
    submitted = *token;
    Run(signal, {}, 0.0F, nullptr, 1.0F, true, *token);
    vortex::testing::RendererPublicationProbe::EnqueueExposureStatus(
      service, *token, last_state_, ctx_, sequence_);
  }
  const auto pending = renderer_->QueueExposureTransition(
    submitted->target, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(pending.has_value());
  service.OnFrameStart(
    frame::SequenceNumber { ++sequence_ }, frame::Slot { 1U });
  WaitForQueueIdle();
  service.OnFrameStart(
    frame::SequenceNumber { ++sequence_ }, frame::Slot { 2U });
  const auto status = renderer_->InspectExposureTransition(pending->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->request, *pending);
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kQueued);
  EXPECT_EQ(status->applied_generation, submitted->generation);
}

NOLINT_TEST_F(
  ExposureGpuTest, InactiveInvalidRequestsCannotReactivateAfterSettingsChange)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  const auto signal = Uniform(.25F, 4U, 4U);
  for (unsigned kind = 0U; kind < 3U; ++kind) {
    const auto handle = CompositionView::ViewStateHandle { 50U + kind };
    const auto intent = ViewId { 50U + kind };
    auto settings = scene::ExposureSettings {};
    settings.key = 12.5F;
    settings.mode = kind == 0U ? engine::ExposureMode::kManual
                               : engine::ExposureMode::kAuto;
    settings.enabled = kind != 1U;
    const auto view = PublishExposureOwner(frame, intent, handle, settings);
    ASSERT_NE(view, kInvalidViewId);
    const auto token = renderer_->QueueExposureTransition(handle,
      ExposureTransitionPolicy::kSeedFromEv100, kind == 2U ? 1000.0F : 8.0F);
    ASSERT_TRUE(token.has_value());
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    service.CaptureRegisteredExposureControls(ctx_);
    const auto rejected = renderer_->InspectExposureTransition(handle);
    ASSERT_TRUE(rejected.has_value());
    EXPECT_EQ(rejected->phase, ExposureTransitionPhase::kRejected);
    EXPECT_EQ(rejected->error,
      kind == 2U ? ExposureTransitionError::kUnsupportedSeed
                 : ExposureTransitionError::kNotAuto);
    settings.enabled = true;
    settings.mode = engine::ExposureMode::kAuto;
    if (kind == 2U) {
      // The formerly unsupported seed would now produce gain one; the locked
      // target instead produces 1/16. A rejected generation must remain
      // rejected.
      settings.compensation_ev = 1000.0F;
      settings.min_ev = settings.max_ev = 1004.0F;
    }
    ASSERT_EQ(PublishExposureOwner(frame, intent, handle, settings), view);
    ctx_.current_view.view_id = view;
    ctx_.current_view.view_state_handle = handle;
    EXPECT_NEAR(ServicePixel(service, signal, settings),
      kind == 2U ? .25F / 16.0F : .18F, 2e-5F);
    const auto state
      = vortex::testing::RendererPublicationProbe::ExposureStateForView(
        service, handle);
    ASSERT_NE(state, nullptr);
    const auto gpu = Read<ExposureStateData>(
      *state->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(gpu.requested_generation[0], token->generation);
    EXPECT_EQ(gpu.applied_generation[0], 0U);
    EXPECT_NE(gpu.flags & (1U << 12U), 0U);
    EXPECT_EQ(renderer_->RetryExposureTransition(*token),
      ExposureTransitionPhase::kRejected);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, InactiveModeValidationCannotRejectAnObservedSubmission)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  const auto handle = CompositionView::ViewStateHandle { 50U };
  const auto view
    = PublishExposureOwner(frame, ViewId { 50U }, handle, settings);
  ctx_.current_view.view_id = view;
  ctx_.current_view.view_state_handle = handle;
  const auto token = renderer_->QueueExposureTransition(
    handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  Run(Uniform(.25F), settings, 0.0F, nullptr, 1.0F, true, *token);
  service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
  vortex::testing::RendererPublicationProbe::EnqueueExposureStatus(
    service, *token, last_state_, ctx_, sequence_);
  settings.mode = engine::ExposureMode::kManual;
  PublishExposureOwner(frame, ViewId { 50U }, handle, settings);
  service.CaptureRegisteredExposureControls(ctx_);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->phase,
    ExposureTransitionPhase::kQueued);
  WaitForQueueIdle();
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->phase,
    ExposureTransitionPhase::kApplied);
}

NOLINT_TEST_F(ExposureGpuTest, InactiveDiagnosticOwnerPreservesPendingRequest)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  settings.mode = engine::ExposureMode::kManual;
  const auto handle = CompositionView::ViewStateHandle { 50U };
  const auto view = PublishExposureOwner(
    frame, ViewId { 50U }, handle, settings, kInvalidViewId, true);
  const auto token = renderer_->QueueExposureTransition(
    handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  service.CaptureRegisteredExposureControls(ctx_);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->phase,
    ExposureTransitionPhase::kQueued);
  settings.mode = engine::ExposureMode::kAuto;
  PublishExposureOwner(frame, ViewId { 50U }, handle, settings);
  ctx_.current_view.view_id = view;
  ctx_.current_view.view_state_handle = handle;
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), settings),
    .25F / 256.0F, 2e-5F);
}

NOLINT_TEST_F(
  ExposureGpuTest, InactiveSharingConsumerRequestStaysRejectedAfterDetach)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  PublishExposureOwner(
    frame, ViewId { 50U }, CompositionView::ViewStateHandle { 50U }, settings);
  const auto handle = CompositionView::ViewStateHandle { 60U };
  const auto view = PublishExposureOwner(
    frame, ViewId { 60U }, handle, settings, ViewId { 50U });
  const auto token = renderer_->QueueExposureTransition(
    handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  service.CaptureRegisteredExposureControls(ctx_);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->error,
    ExposureTransitionError::kSharedConsumer);
  PublishExposureOwner(frame, ViewId { 60U }, handle, settings);
  ctx_.current_view.view_id = view;
  ctx_.current_view.view_state_handle = handle;
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F, 4U, 4U), settings), .18F, 2e-5F);
  EXPECT_EQ(renderer_->RetryExposureTransition(*token),
    ExposureTransitionPhase::kRejected);
}

} // namespace
