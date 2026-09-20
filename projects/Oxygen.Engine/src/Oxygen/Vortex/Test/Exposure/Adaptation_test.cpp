//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <limits>

#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>

namespace oxygen::vortex::testing::exposure {

using graphics::Framebuffer;
using graphics::FramebufferDesc;
using graphics::ResourceStates;
using graphics::TextureDesc;

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
  const double expected = std::log2(.72) - 8.0 + (1.5 * std::exp(-5.0 / 3.0));
  for (const unsigned frequency : {
         30U,
         60U,
         120U,
       }) {
    ResetHistory();
    auto settings = scene::ExposureSettings {};
    Run(signal, settings);
    settings.compensation_ev = -8.0F;
    Snapshot result {};
    for (unsigned frame = 0; frame < 3U * frequency; ++frame) {
      result = Run(signal, settings, 1.0F / static_cast<float>(frequency));
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
  const double expected
    = std::log2(static_cast<double>(initial.state.latent_scale)) - 16.0
    + (16.0 * std::exp(-2.0));
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

NOLINT_TEST_F(ExposureGpuTest, LongAndIrregularHybridStepsRemainEquivalent)
{
  const auto signal = Uniform(.25F);
  auto settings = scene::ExposureSettings {};
  Run(signal, settings);
  settings.compensation_ev = -8.0F;
  Snapshot result {};
  for (float dt : {
         .125F,
         .875F,
         .25F,
         .25F,
         .5F,
         1.0F,
       }) {
    result = Run(signal, settings, dt);
  }
  EXPECT_NEAR(std::log2(result.state.latent_scale),
    std::log2(.72) - 8.0 + (1.5 * std::exp(-5.0 / 3.0)), 5e-4);
  const auto settled = Run(signal, settings, 1e30F);
  EXPECT_NEAR(
    std::log2(settled.state.latent_scale), std::log2(.72) - 8.0, 5e-4);
}

NOLINT_TEST_F(ExposureGpuTest, OrdinaryAutoCurveInterpolatesAtRawMeterEv)
{
  auto settings = scene::ExposureSettings {};
  settings.compensation_curve = {
    {
      .metered_ev = 0.0F,
      .compensation_ev = -2.0F,
    },
    {
      .metered_ev = 2.0F,
      .compensation_ev = 2.0F,
    },
  };
  const auto result = Run(Uniform(.25F), settings);
  const double raw_ev = std::log2(.25 / .18);
  // Curve contributes 2*EV-2; the denominator contributes -EV.
  EXPECT_NEAR(std::log2(result.state.target_scale), raw_ev - 2.0, 2e-4);
  EXPECT_NEAR(std::log2(result.state.displayed_scale), raw_ev - 2.0, 2e-4);
}

NOLINT_TEST_F(ExposureGpuTest, OrdinaryAutoCurveClampsBothAuthoredEndpoints)
{
  auto settings = scene::ExposureSettings {};
  settings.compensation_curve = {
    {
      .metered_ev = 1.0F,
      .compensation_ev = 2.0F,
    },
    {
      .metered_ev = 2.0F,
      .compensation_ev = 4.0F,
    },
  };
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
  settings.compensation_curve = {
    {
      .metered_ev = 0.0F,
      .compensation_ev = -2.0F,
    },
    {
      .metered_ev = 2.0F,
      .compensation_ev = 2.0F,
    },
  };
  settings.speed_up = settings.speed_down = 0.0F;
  const auto result = Run(signal, settings, 1.0F);
  const double expected = (2.0 * std::log2(.25 / .18)) - 2.0 - .1;
  EXPECT_NEAR(std::log2(result.state.target_scale), expected, 2e-4);
  EXPECT_EQ(result.state.latent_scale, initial.state.latent_scale);
}

NOLINT_TEST_F(ExposureGpuTest, BrighteningUsesSpeedDownAcrossFrameSchedules)
{
  const auto signal = Uniform(.25F);
  const double expected = std::log2(.72) + 8.0 - (1.5 * std::exp(-1.0));
  for (const unsigned frequency : {
         30U,
         60U,
         120U,
       }) {
    ResetHistory();
    auto settings = scene::ExposureSettings {};
    Run(signal, settings);
    settings.compensation_ev = 8.0F;
    settings.speed_up = 7.0F;
    settings.speed_down = 1.0F;
    Snapshot result {};
    for (unsigned frame = 0; frame < 8U * frequency; ++frame) {
      result = Run(signal, settings, 1.0F / static_cast<float>(frequency));
    }
    EXPECT_NEAR(std::log2(result.state.latent_scale), expected, 5e-4)
      << frequency;
  }
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
    std::log2(.0225) + (1.5 * std::exp(-5.0 / 3.0)), 5e-4);
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
  settings.compensation_curve = {
    {
      .metered_ev = 0.0F,
      .compensation_ev = -2.0F,
    },
    {
      .metered_ev = 4.0F,
      .compensation_ev = 6.0F,
    },
  };
  EXPECT_NEAR(Run(Uniform(.25F), settings).state.displayed_scale, 1.0F, 2e-6);
  EXPECT_NEAR(Run(Uniform(8.0F), settings).state.displayed_scale, 1.0F, 2e-6);
  EXPECT_NEAR(Run(Uniform(0.0F), settings, 0.0F, nullptr, 1.0F, false)
                .state.displayed_scale,
    1.0F, 2e-6);
}

NOLINT_TEST_F(
  ExposureGpuTest, SubnormalCurveCoordinatesInterpolateAtExactZeroMeterEv)
{
  for (const float coordinate : {
         1.0e-40F,
         std::numeric_limits<float>::denorm_min(),
       }) {
    ResetHistory();
    auto settings = scene::ExposureSettings {};
    settings.min_log_luminance = std::log2(.18F);
    settings.log_luminance_range = 25.0F;
    settings.low_percentile = 0.0F;
    settings.high_percentile = 0x1p-16F;
    settings.compensation_curve = {
      {
        .metered_ev = -coordinate,
        .compensation_ev = -1.0F,
      },
      {
        .metered_ev = coordinate,
        .compensation_ev = 1.0F,
      },
    };
    // Brighter than the dark threshold, but entirely quantized to bin zero.
    // The exact bin position gives EV zero and midpoint compensation zero.
    const auto result = Run(Uniform(.1800001F), settings);
    ASSERT_EQ(result.histogram.at(261), 0U);
    ASSERT_EQ(result.histogram.at(0), 4095U);
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
  params.view_config.viewport = {
    .width = 1.0F,
    .height = 1.0F,
  };
  auto facade = renderer_->ForSinglePassHarness();
  facade.SetFrameSession(Renderer::FrameSessionInput {
    .frame_slot = frame::Slot { 0U, },
    .frame_sequence = frame::SequenceNumber { 2U, },
    .delta_time_seconds = 0.0F,
  });
  facade.SetResolvedView(Renderer::ResolvedViewInput {
    .view_id = ViewId { 1U, }, .value = ResolvedView { params, }, });
  facade.SetOutputTarget(Renderer::OutputTargetInput {
    .framebuffer = observer_ptr<Framebuffer> { framebuffer.get(), }, });
  const auto paused = facade.Finalize();
  if (!paused.has_value()) {
    FAIL() << "Expected paused to contain a value";
  }
  ASSERT_EQ(paused->GetRenderContext().delta_time, 0.0F);
  const auto after
    = Run(Uniform(8.0F), {}, paused->GetRenderContext().delta_time);
  EXPECT_EQ(after.state.latent_scale, before.state.latent_scale);
  EXPECT_NE(after.state.latent_target_scale, before.state.latent_target_scale);
}

} // namespace oxygen::vortex::testing::exposure
