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
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Direct3D12/Test/Fixtures/ReadbackTestFixture.h>
#include <Oxygen/Scene/Environment/Background.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>

namespace {
using namespace oxygen;
using namespace oxygen::graphics;
using namespace oxygen::vortex;
using Pixel = std::array<float, 4>;

class ExposureGpuTest : public graphics::d3d12::testing::ReadbackTestFixture {
protected:
  struct Signal {
    std::shared_ptr<Texture> texture;
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
    float dt = 0.0F, const Signal* mask = nullptr, float inverse_p = 1.0F)
    -> Snapshot
  {
    settings.key = 12.5F;
    const auto resolved = scene::ResolveExposureSettings(settings);
    CHECK_F(resolved.has_value());
    auto config = PostProcessConfig {};
    config.resolved_exposure = *resolved;
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
      });
    CHECK_F(result.executed);
    return { Read<ExposureStateData>(
               *result.exposure_buffer, ResourceStates::kShaderResource),
      Read<std::array<std::uint32_t, 264>>(
        *result.histogram_buffer, ResourceStates::kCommon) };
  }
  auto ResetHistory() -> void
  {
    pass_->RemoveViewState(ctx_.current_view.view_state_handle);
  }
  std::unique_ptr<Renderer> renderer_;
  std::unique_ptr<postprocess::ExposurePass> pass_;
  RenderContext ctx_;
  std::uint64_t sequence_ { 0U };
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
} // namespace
