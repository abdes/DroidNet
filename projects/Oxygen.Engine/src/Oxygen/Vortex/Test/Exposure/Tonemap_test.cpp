//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <vector>

#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>

namespace oxygen::vortex::testing::exposure {

using graphics::Framebuffer;
using graphics::FramebufferDesc;
using graphics::ResourceStates;

NOLINT_TEST_F(ExposureGpuTest, ExternalBloomUsesFrameDomainAndHonorsDisable)
{
  auto service = PostProcessService(*renderer_);
  auto textures = SceneTextures(Backend(), { .extent = { 4U, 4U, }, });
  unsigned cases = 0;
  for (const float ev : {
         -16.0F,
         0.0F,
         16.0F,
       }) {
    for (const bool fp32 : {
           false,
           true,
         }) {
      for (const bool enabled : {
             false,
             true,
           }) {
        for (const float intensity : {
               0.0F,
               .5F,
             }) {
          SCOPED_TRACE(::testing::Message()
            << "ev=" << ev << " fp32=" << fp32 << " enabled=" << enabled
            << " intensity=" << intensity);
          ctx_.frame_sequence = frame::SequenceNumber {
            ++sequence_,
          };
          service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
          auto settings = scene::ExposureSettings {};
          settings.mode = engine::ExposureMode::kManual;
          settings.manual_ev = ev;
          settings.key = 12.5F;
          [[maybe_unused]] const auto& captured
            = service.CaptureViewExposureSettings(ctx_.current_view.view_id,
              ctx_.current_view.view_state_handle, settings);
          auto config = PostProcessConfig {};
          config.enable_bloom = enabled;
          config.bloom_intensity = intensity;
          config.tone_mapper = engine::ToneMapper::kNone;
          config.gamma = 1;
          service.SetResolvedConfig(service.BuildPassConfig(config,
            ctx_.current_view.view_id, ctx_.current_view.view_state_handle));
          const auto frame = service.PrepareFrameExposure(ctx_, fp32);
          ASSERT_NE(frame, nullptr);
          const double s = std::exp2(-static_cast<double>(ev));
          const double p = fp32 ? 1 : s;
          const auto source = Uniform(static_cast<float>(.125 * p / s), 4U, 4U);
          const auto bloom = Uniform(static_cast<float>(.25 * p / s), 4U, 4U);
          auto output = CreateRegisteredTexture({
            .width = 4,
            .height = 4,
            .format = Format::kRGBA32Float,
            .is_render_target = true,
            .initial_state = ResourceStates::kCommon,
          });
          auto target = Backend().CreateFramebuffer(
            FramebufferDesc {}.AddColorAttachment(output));
          {
            auto exposure_inputs = PostProcessService::Inputs {};
            exposure_inputs.scene_signal = source.texture.get();
            exposure_inputs.post_target = observer_ptr<const Framebuffer> {
              target.get(),
            };
            exposure_inputs.scene_signal_srv = source.srv;
            exposure_inputs.bloom_texture_srv = bloom.srv;
            service.Execute(
              ctx_.current_view.view_id, ctx_, textures, exposure_inputs);
          }
          ASSERT_TRUE(service.GetLastExecutionState().tonemap_executed);
          EXPECT_EQ(service.GetLastExecutionState().bloom_requested, enabled);
          // Pixel (1,0) has zero Bayer offset, independent of production
          // helpers.
          const auto pixels = ReadFloatTexture(*output);
          const double expected = .125 + (enabled ? .25 * intensity : 0);
          for (unsigned c = 0; c < 3; ++c) {
            EXPECT_NEAR(pixels.at(1).at(c), expected, 2e-6);
          }
          ++cases;
        }
      }
    }
  }
  RecordProperty("external_bloom_cases", cases);
}

NOLINT_TEST_F(
  ExposureGpuTest, FrameDomainToneCurvesRemainFiniteAtMaximumSceneTimesGain)
{
  auto service = PostProcessService(*renderer_);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = -32.0F;
  for (const auto mapper : {
         engine::ToneMapper::kAcesFitted,
         engine::ToneMapper::kFilmic,
         engine::ToneMapper::kReinhard,
       }) {
    auto pixel_options = ServicePixelOptions {};
    pixel_options.before_execute = [&] -> void {
      ASSERT_NE(service.PrepareFrameExposure(ctx_, true), nullptr);
    };
    pixel_options.tone_mapper = mapper;
    const auto pixel = ServicePixel(
      service, Uniform(0x1p32F, 4U, 4U), settings, pixel_options);
    EXPECT_TRUE(std::isfinite(pixel));
    EXPECT_NEAR(pixel, 1.0F, 2e-6F);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, FrameDomainToneCurvesPreserveOrdinaryNeutralResponse)
{
  auto service = PostProcessService(*renderer_);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  const auto filmic = [](const double x) -> double {
    return (((x * ((.15 * x) + .05)) + .004) / ((x * ((.15 * x) + .5)) + .06))
      - (.02 / .3);
  };
  for (const auto mapper : {
         engine::ToneMapper::kAcesFitted,
         engine::ToneMapper::kFilmic,
         engine::ToneMapper::kReinhard,
       }) {
    for (const double x : {
           .01,
           .18,
           1.0,
           16.0,
         }) {
      double reference = x / (x + 1.0);
      if (mapper == engine::ToneMapper::kAcesFitted) {
        reference = ((x * (x + .0245786)) - .000090537)
          / ((x * ((.983729 * x) + .4329510)) + .238081);
      } else if (mapper == engine::ToneMapper::kFilmic) {
        reference = filmic(2.0 * x) / filmic(11.2);
      }
      auto pixel_options = ServicePixelOptions {};
      pixel_options.before_execute = [&] -> void {
        ASSERT_NE(service.PrepareFrameExposure(ctx_, false), nullptr);
      };
      pixel_options.tone_mapper = mapper;
      const auto pixel = ServicePixel(service,
        Uniform(static_cast<float>(x), 4U, 4U), settings, pixel_options);
      EXPECT_NEAR(pixel, std::clamp(reference, 0.0, 1.0), 2e-5);
    }
  }
}

NOLINT_TEST_F(ExposureGpuTest, QuickToneBoundsCoverColorAndCoverageBoxes)
{
  const std::array levels {
    0.0F,
    .001F,
    .01F,
    .05F,
    .18F,
    1.0F,
    4.0F,
    100.0F,
  };
  std::vector<std::array<float, 4>> inputs;
  for (const float red : levels) {
    for (const float green : levels) {
      for (const float blue : levels) {
        for (const float alpha : {
               0.0F,
               .25F,
               .7F,
               1.0F,
             }) {
          Pixel low {
            red,
            green,
            blue,
            alpha,
          };
          Pixel high = low;
          for (unsigned c = 0U; c < 3U; ++c) {
            const float radius = std::max(low.at(c) * .0001F, 1e-8F);
            low.at(c) = std::max(0.0F, low.at(c) - radius);
            high.at(c) += radius;
          }
          if (alpha > 0.0F && alpha < 1.0F) {
            low.at(3) -= .0001F;
            high.at(3) += .0001F;
          }
          inputs.push_back(low);
          inputs.push_back(high);
        }
      }
    }
  }
  const auto count = static_cast<std::uint32_t>(inputs.size() / 2U);
  std::uint32_t mode = 3U; // ACES, gamma 2.2.
  char* mode_text = nullptr;
  std::size_t mode_size = 0U;
  if (_dupenv_s(&mode_text, &mode_size, "OXYGEN_EXPOSURE_TONE_PROBE_MODE") == 0
    && (mode_text != nullptr)) {
    const auto owned_mode = std::unique_ptr<char, decltype(&std::free)> {
      mode_text,
      &std::free,
    };
    mode
      = static_cast<std::uint32_t>(std::strtoul(owned_mode.get(), nullptr, 10));
    ASSERT_NE(mode & 1U, 0U);
  }
  const auto results = RunToneProbe(std::as_bytes(std::span {
                                      inputs,
                                    }),
    count, mode);
  for (std::size_t i = 0; i < results.size(); ++i) {
    SCOPED_TRACE(i);
    const auto& result = results.at(i);
    EXPECT_EQ(result.at(3), 1.0F);
    for (unsigned c = 0U; c < 3U; ++c) {
      EXPECT_GE(result.at(c), 0.0F);
      EXPECT_LE(result.at(c), result.at(c + 4U));
      EXPECT_LE(result.at(c + 4U), 1.0F);
    }
  }
}

NOLINT_TEST_F(ExposureGpuTest, ToneBoundsEncloseSignedTinyArithmetic)
{
  using Case = std::array<std::uint32_t, 4>;
  std::vector<Case> cases;
  const auto one = std::bit_cast<std::uint32_t>(1.0F);
  const auto large = std::bit_cast<std::uint32_t>(0x1p100F);
  for (const auto sign : {
         0U,
         0x80000000U,
       }) {
    for (const auto magnitude : {
           0U,
           1U,
           0x007fffffU,
           0x00800000U,
           0x00800001U,
           0x00800002U,
           0x00800003U,
           0x00800004U,
           0x00800005U,
           0x00800006U,
           0x00800007U,
           0x00800008U,
           0x00800009U,
           0x3f000000U,
           0x3f800000U,
           0x71800000U,
         }) {
      const auto value = magnitude | sign;
      cases.push_back({
        value,
        value,
        one,
        one,
      });
      cases.push_back({
        value,
        value,
        large,
        large,
      });
    }
  }
  cases.push_back({
    1U,
    1U,
    0x00800000U,
    0x00800000U,
  });
  cases.push_back({
    0x80000001U,
    0x80000001U,
    0x80800000U,
    0x80800000U,
  });
  cases.push_back({
    0x80000001U,
    1U,
    large,
    large,
  });
  const auto results = RunToneProbe(std::as_bytes(std::span {
                                      cases,
                                    }),
    static_cast<std::uint32_t>(cases.size()));
  for (std::size_t i = 0; i < cases.size(); ++i) {
    SCOPED_TRACE(i);
    const auto& result = results.at(i);
    const auto value = [&](unsigned index) -> double {
      return static_cast<double>(std::bit_cast<float>(cases.at(i).at(index)));
    };
    EXPECT_LE(result.at(0), value(0));
    EXPECT_GE(result.at(1), value(0));
    for (unsigned left = 0; left < 2; ++left) {
      for (unsigned right = 2; right < 4; ++right) {
        // Products of two finite binary32 values are exact in binary64.
        const double sum = value(left) + value(right);
        const double product = value(left) * value(right);
        EXPECT_LE(result.at(2), sum);
        EXPECT_GE(result.at(3), sum);
        EXPECT_LE(result.at(4), product);
        EXPECT_GE(result.at(5), product);
      }
    }
    for (unsigned endpoint = 0; endpoint < 2; ++endpoint) {
      const double positive = std::max(0.0, value(endpoint));
      EXPECT_LE(result.at(6), positive * positive);
      EXPECT_GE(result.at(7), positive * positive);
    }
  }
}

NOLINT_TEST_F(ExposureGpuTest, ConsumerArithmeticContainsAttenuationAndRounding)
{
  std::vector<std::array<float, 4>> inputs;
  for (const float relative : {
         0.0F,
         0x1p-11F,
         .01F,
       }) {
    for (const float absolute : {
           0.0F,
           0x1p-149F,
           0x1p-120F,
           .001F,
         }) {
      for (const float t_relative : {
             0.0F,
             .002F,
           }) {
        for (const float t_absolute : {
               0.0F,
               1e-8F,
             }) {
          for (const float maximum : {
                 0.0F,
                 0x1p-24F,
                 1.0F,
                 0x1p32F,
               }) {
            for (const float steps : {
                   0.0F,
                   128.0F,
                   4096.0F,
                 }) {
              for (const float inverse_p : {
                     0x1p-32F,
                     1.0F,
                     0x1p32F,
                   }) {
                inputs.push_back({
                  relative,
                  absolute,
                  t_relative,
                  t_absolute,
                });
                inputs.push_back({
                  maximum,
                  steps,
                  inverse_p,
                  0.0F,
                });
              }
            }
          }
        }
      }
    }
  }
  const auto count = static_cast<std::uint32_t>(inputs.size() / 2U);
  const auto results = RunToneProbe(std::as_bytes(std::span {
                                      inputs,
                                    }),
    count, 64U);
  for (std::size_t i = 0; i < results.size(); ++i) {
    SCOPED_TRACE(i);
    const auto& b = inputs.at(i * 2U);
    const auto& c = inputs.at((i * 2U) + 1U);
    const auto& result = results.at(i);
    for (const float value : result) {
      EXPECT_TRUE(std::isfinite(value));
      EXPECT_GE(value, 0.0F);
    }
    for (const double source_sign : {
           -1.0,
           1.0,
         }) {
      for (const double t_sign : {
             -1.0,
             1.0,
           }) {
        const double reference = static_cast<double>(c.at(0)) * .25;
        const double source = std::max(0.0,
          static_cast<double>(c.at(0))
            + (source_sign
              * ((static_cast<double>(b.at(0)) * c.at(0)) + b.at(1))));
        const double transmission = std::clamp(
          .25 + (t_sign * ((static_cast<double>(b.at(2)) * .25) + b.at(3))),
          0.0, 1.0);
        const double error = std::abs((source * transmission) - reference);
        EXPECT_LE(error,
          (static_cast<double>(result.at(0)) * reference) + result.at(1));
        EXPECT_LE(error,
          (static_cast<double>(result.at(2)) * reference) + result.at(3));
      }
    }
    EXPECT_EQ(result.at(6), 0.0F);
    EXPECT_GE(static_cast<double>(result.at(7)),
      (static_cast<double>(b.at(0)) * c.at(0)) + b.at(1));
  }
  RecordProperty("consumer_arithmetic_cases", count);
}

} // namespace oxygen::vortex::testing::exposure
