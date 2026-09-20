//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>

namespace oxygen::vortex::testing::exposure {

using graphics::ResourceStates;

NOLINT_TEST_F(ExposureGpuTest, ProducerChecksPreserveOrderedFailuresAndCounts)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  settings.key = 12.5F;
  const auto config = SharedConfig(settings);
  const auto meter = Uniform(.25F);
  const std::array<Pixel, 1> finite {
    Pixel {
      .25F,
      .5F,
      2.0F,
      .5F,
    },
  };
  const std::array<Pixel, 1> nonfinite {
    Pixel {
      .25F,
      .5F,
      2.0F,
      std::numeric_limits<float>::quiet_NaN(),
    },
  };
  const auto scene = MakeSignal(9U, 1U, finite);
  constexpr auto expected_products = (1U << 5U) | (1U << 9U) | (1U << 10U);
  const auto failure_bit = [](const unsigned id) -> unsigned int {
    if (id == 11U) {
      return 1U;
    }
    if (id == 6U) {
      return 2U;
    }
    return 4U;
  };
  for (const auto format : {
         Format::kRGBA32Float,
         Format::kRGBA16Float,
       }) {
    const auto producer = MakeSignal(9U, 3U, finite, 2U, format);
    const auto invalid = MakeSignal(9U, 3U, nonfinite, 2U, format);
    auto order = std::array {
      6U,
      10U,
      11U,
    };
    for (bool has_order = true; has_order;
      has_order = std::ranges::next_permutation(order).found) {
      for (const unsigned invalid_product : {
             0U,
             6U,
             10U,
           }) {
        const auto first_mask = invalid_product == 0U ? 0U : 7U;
        for (unsigned mask = first_mask; mask < 8U; ++mask) {
          SCOPED_TRACE(::testing::Message()
            << "format=" << static_cast<unsigned>(format) << " order="
            << order.at(0) << ',' << order.at(1) << ',' << order.at(2)
            << " invalid=" << invalid_product << " gain-mask=" << mask);
          ctx_.frame_sequence = frame::SequenceNumber {
            ++sequence_,
          };
          auto frame_inputs = postprocess::ExposurePass::FrameInputs {};
          frame_inputs.use_fp32 = true;
          const auto frame = pass_->ResolveFrame(ctx_, config, frame_inputs);
          ASSERT_NE(frame, nullptr);
          ASSERT_TRUE(RecordShared(meter, config).executed);
          std::array<postprocess::ExposurePass::HdrProduct, 3> products;
          auto expected_first = invalid_product;
          auto expected_rejected = invalid_product == 0U ? 0U : 54U;
          for (unsigned index = 0U; index < order.size(); ++index) {
            const auto id = order.at(index);
            const auto& producer_signal
              = id == invalid_product ? invalid : producer;
            const auto& signal = id == 11U ? scene : producer_signal;
            const auto bad_gain = (mask & failure_bit(id)) != 0U;
            products.at(index) = {
              .texture = signal.texture.get(),
              .srv = signal.srv,
              .id = id,
              .transmittance = id != 11U,
              .consumer_rgb_gain
              = bad_gain ? std::numeric_limits<float>::quiet_NaN() : 1.0F,
            };
            if (id != invalid_product && bad_gain) {
              expected_rejected += id == 11U ? 9U : 54U;
              if (expected_first == 0U) {
                expected_first = id;
              }
            }
            if (id != 11U) {
              ASSERT_TRUE(
                pass_->GatherFilterGradients(ctx_, frame, products.at(index)));
            }
          }
          ASSERT_TRUE(
            pass_->EvaluateFp16Products(ctx_, frame, config, products, {}));
          const auto report = Read<HdrSuitabilityData>(
            *frame->suitability_buffer, ResourceStates::kShaderResource);
          EXPECT_EQ(report.first_failure_product, expected_first);
          EXPECT_EQ(report.failure_flags, expected_rejected == 0U ? 0U : 1U);
          EXPECT_EQ(report.rejected_samples, expected_rejected);
          EXPECT_EQ(report.checked_samples, invalid_product == 0U ? 117U : 63U);
          EXPECT_EQ(report.checked_products, expected_products);
          EXPECT_EQ(report.expected_products, expected_products);
          EXPECT_EQ(report.image_failures, 0U);
          EXPECT_EQ(report.metering_failures, 0U);
          EXPECT_EQ(report.overflow_failures, 0U);
          EXPECT_EQ(report.reserved, 0U);
          EXPECT_EQ(report.candidate_pre_exposure, 4096.0F);
          EXPECT_EQ(std::bit_cast<std::uint32_t>(report.maximum_scene_rgb),
            0x40000004U);
          const auto status
            = Read<ExposureStatusStorage>(*frame->current_state->status_buffer,
              ResourceStates::kShaderResource);
          for (const auto id : {
                 6U,
                 10U,
               }) {
            const auto& bound = status.candidate_errors.at(id == 6U ? 1U : 2U);
            if (id == invalid_product) {
              EXPECT_TRUE(std::isinf(bound.rgb_absolute));
              EXPECT_TRUE(std::isinf(bound.transmittance_absolute));
            } else {
              for (const auto value : {
                     bound.rgb_relative,
                     bound.rgb_absolute,
                     bound.transmittance_relative,
                     bound.transmittance_absolute,
                   }) {
                EXPECT_TRUE(std::isfinite(value));
                EXPECT_GE(value, 0.0F);
                EXPECT_LE(value, 2e-5F);
              }
            }
          }
        }
      }
    }
  }
}

NOLINT_TEST_F(ExposureGpuTest, CurrentScaleProducerChecksKeepOrderedFailures)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  const auto meter = Uniform(.25F);
  const auto signal = Uniform(.5F, 9U, 1U);
  for (const bool reverse : {
         false,
         true,
       }) {
    ctx_.frame_sequence = frame::SequenceNumber {
      ++sequence_,
    };
    auto frame_inputs = postprocess::ExposurePass::FrameInputs {};
    frame_inputs.use_fp32 = true;
    const auto frame = pass_->ResolveFrame(ctx_, config, frame_inputs);
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(RecordShared(meter, config).executed);
    auto products = std::array {
      postprocess::ExposurePass::HdrProduct {
        .texture = signal.texture.get(),
        .srv = signal.srv,
        .id = 6U,
        .transmittance = true,
        .consumer_rgb_gain = std::numeric_limits<float>::quiet_NaN(),
      },
      postprocess::ExposurePass::HdrProduct {
        .texture = signal.texture.get(),
        .srv = signal.srv,
        .id = 10U,
        .transmittance = true,
        .consumer_rgb_gain = std::numeric_limits<float>::quiet_NaN(),
      },
    };
    if (reverse) {
      std::ranges::reverse(products);
    }
    ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products, {},
      postprocess::ExposurePass::SuitabilityScale::kCurrentFrame));
    const auto report = Read<HdrSuitabilityData>(
      *frame->conversion_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(report.first_failure_product, products.front().id);
    EXPECT_EQ(report.failure_flags, 1U);
    EXPECT_EQ(report.rejected_samples, 18U);
    EXPECT_EQ(report.checked_samples, 18U);
    EXPECT_EQ(report.image_failures, 0U);
    EXPECT_EQ(report.metering_failures, 0U);
    EXPECT_EQ(report.overflow_failures, 0U);
    EXPECT_EQ(report.reserved, 0U);
    EXPECT_EQ(report.candidate_pre_exposure, 1.0F);
  }
}

NOLINT_TEST_F(ExposureGpuTest, ProducerChecksKeepDivergentLaneFailures)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  settings.key = 12.5F;
  const auto config = SharedConfig(settings);
  const auto meter = Uniform(.25F);
  auto pixels = std::array<Pixel, 9> {};
  pixels.fill(Pixel {
    .25F,
    .5F,
    2.0F,
    .5F,
  });
  for (const auto index : {
         1U,
         3U,
       }) {
    pixels.at(index) = {
      0x1p-64F,
      0.0F,
      0.0F,
      .5F,
    };
  }
  for (const auto index : {
         2U,
         5U,
         8U,
       }) {
    pixels.at(index).at(3) = std::numeric_limits<float>::quiet_NaN();
  }
  const auto signal = MakeSignal(9U, 1U, pixels);
  for (const auto id : {
         6U,
         10U,
       }) {
    ctx_.frame_sequence = frame::SequenceNumber {
      ++sequence_,
    };
    auto frame_inputs = postprocess::ExposurePass::FrameInputs {};
    frame_inputs.use_fp32 = true;
    const auto frame = pass_->ResolveFrame(ctx_, config, frame_inputs);
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(RecordShared(meter, config).executed);
    const auto product = postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(),
      .srv = signal.srv,
      .id = id,
      .transmittance = true,
      .consumer_rgb_gain = 0x1p60F,
    };
    ASSERT_TRUE(pass_->GatherFilterGradients(ctx_, frame, product));
    ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config,
      std::span {
        &product,
        1U,
      },
      {}));
    const auto report = Read<HdrSuitabilityData>(
      *frame->suitability_buffer, ResourceStates::kShaderResource);
    // Three nonfinite texels are rejected by the maximum scan. Of six finite
    // texels, two lose RGB in half storage; their amplified contribution is
    // 1/16, so they exceed the independently specified absolute image budget.
    EXPECT_EQ(report.first_failure_product, id);
    EXPECT_EQ(report.failure_flags, 1U | 4U);
    EXPECT_EQ(report.rejected_samples, 3U);
    EXPECT_EQ(report.checked_samples, 6U);
    EXPECT_EQ(report.image_failures, 2U);
    EXPECT_EQ(report.metering_failures, 0U);
    EXPECT_EQ(report.overflow_failures, 0U);
    EXPECT_EQ(report.reserved, 0U);
  }
}

NOLINT_TEST_F(ExposureGpuTest, ProducerStoreBoundsMatchSerialUnsignedMax)
{
  using Words = std::array<std::uint32_t, 4>;
  struct Sample {
    Pixel value;
    Pixel low;
    Pixel high;
  };
  const auto tiny = std::bit_cast<float>(1U);
  const auto infinity = std::numeric_limits<float>::infinity();
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  const std::array samples {
    Sample {
      .value = { 0, 0, 0, 0, }, .low = { 0, 0, 0, 0, }, .high = { 0, 0, 0, 0, }, },
    Sample { .value = { -0.0F, 0, -0.0F, 0, },
      .low = { -0.0F, 0, -0.0F, 0, },
      .high = { -0.0F, 0, -0.0F, 0, }, },
    Sample { .value = { tiny, tiny, tiny, .5F, },
      .low = { tiny, tiny, tiny, .5F, },
      .high = { tiny, tiny, tiny, .5F, }, },
    Sample { .value = { 1.0F / 3.0F, .125F, 3.75F, .375F, },
      .low = { 1.0F / 3.0F, .125F, 3.75F, .375F, },
      .high = { 1.0F / 3.0F, .125F, 3.75F, .375F, }, },
    Sample { .value = { 2, 1, .5F, .5F, },
      .low = { 1.875F, .875F, .25F, .375F, },
      .high = { 2.125F, 1.25F, .75F, .625F, }, },
    Sample { .value = { 16, 8, 4, .75F, },
      .low = { 15, 7, 3, .5F, },
      .high = { 17, 9, 5, 1, }, },
    Sample { .value = { 0x1p-24F, 0x1p-14F, .125F, 1, },
      .low = { 0x1p-24F, 0x1p-14F, .125F, 1, },
      .high = { 0x1p-24F, 0x1p-14F, .125F, 1, }, },
    Sample { .value = { 1e-20F, 1e-12F, 1e-4F, .875F, },
      .low = { 0, 0, 0, .5F, },
      .high = { 2e-20F, 2e-12F, 2e-4F, 1, }, },
  };
  enum class Pattern : std::uint8_t {
    kFull,
    kPartial,
    kSparse,
    kInvalidSparse,
    kZero
  };
  unsigned cases = 0U;
  unsigned wave_width = 0U;
  for (const unsigned product : {
         5U,
         6U,
         10U,
       }) {
    for (const unsigned fp16 : {
           0U,
           1U,
         }) {
      for (const auto pattern : {
             Pattern::kFull,
             Pattern::kPartial,
             Pattern::kSparse,
             Pattern::kInvalidSparse,
             Pattern::kZero,
           }) {
        SCOPED_TRACE(::testing::Message()
          << "product=" << product << " fp16=" << fp16
          << " pattern=" << static_cast<unsigned>(pattern));
        constexpr unsigned lanes = 64U;
        unsigned index = 2U;
        if (product == 5U) {
          index = 0U;
        } else if (product == 6U) {
          index = 1U;
        }
        std::array<Words, 4U + (lanes * 4U)> inputs {};
        inputs.at(0) = {
          product,
          fp16,
          std::bit_cast<std::uint32_t>(8.0F),
          0U,
        };
        std::array<Words, 3> expected {
          Words {
            0x3d000000U,
            0x3b800000U,
            1U,
            0x3e000000U,
          },
          Words {
            0x3c000000U,
            0x3b000000U,
            2U,
            0x3d800000U,
          },
          Words {
            0x3b000000U,
            0x3a800000U,
            3U,
            0x3d000000U,
          },
        };
        expected.at(index) = {};
        if (pattern == Pattern::kPartial) {
          expected.at(index) = {
            std::bit_cast<std::uint32_t>(.75F),
            std::bit_cast<std::uint32_t>(.25F),
            0U,
            std::bit_cast<std::uint32_t>(.125F),
          };
        } else if (pattern == Pattern::kZero) {
          expected.at(index) = {
            0x80000000U,
            1U,
            0x7f800000U,
            0x3e800000U,
          };
        }
        for (unsigned peer = 0U; peer < expected.size(); ++peer) {
          inputs.at(1U + peer) = expected.at(peer);
        }
        for (unsigned lane = 0U; lane < lanes; ++lane) {
          auto sample = samples.at(lane % samples.size());
          unsigned control = 1U;
          if (pattern == Pattern::kPartial) {
            control = lane < 37U ? 1U : 0U;
          } else if (pattern == Pattern::kSparse) {
            control = lane % 7U == 3U ? 3U : 0U;
          } else if (pattern == Pattern::kInvalidSparse) {
            control = lane % 9U == 4U ? 1U : 0U;
            switch (lane % 4U) {
            case 0U: {
              sample.value.at(0) = nan;
              break;
            }
            case 1U: {
              sample.value.at(3) = infinity;
              break;
            }
            case 2U: {
              sample.low.at(1) = -infinity;
              break;
            }
            default: {
              sample.high.at(2) = nan;
              break;
            }
            }
          } else if (pattern == Pattern::kZero) {
            sample = samples.at(1U);
            control = 3U;
          }
          if (control == 0U) {
            // Inactive invalid coefficients must not join the reduction.
            sample.value = {
              nan,
              infinity,
              -infinity,
              nan,
            };
          }
          const auto base = 4U + (lane * 4U);
          inputs.at(base) = std::bit_cast<Words>(sample.value);
          inputs.at(base + 1U) = std::bit_cast<Words>(sample.low);
          inputs.at(base + 2U) = std::bit_cast<Words>(sample.high);
          inputs.at(base + 3U) = {
            control,
            0U,
            0U,
            0U,
          };
        }
        const auto result = RunToneProbe(std::as_bytes(std::span {
                                           inputs,
                                         }),
          lanes, 16384U, false);
        ASSERT_EQ(result.size(), lanes);
        const auto word = [&](const unsigned offset) -> unsigned int {
          return std::bit_cast<std::array<std::uint32_t, 8>>(
            result.at(offset / 32U))
            .at((offset % 32U) / 4U);
        };
        const auto observed_wave_width = word(0U);
        ASSERT_GT(observed_wave_width, 0U);
        ASSERT_LE(observed_wave_width, 128U);
        if (wave_width == 0U) {
          wave_width = observed_wave_width;
        }
        EXPECT_EQ(observed_wave_width, wave_width);
        EXPECT_EQ(word(4U), lanes);
        EXPECT_EQ(word(8U), product);
        EXPECT_EQ(word(12U), fp16);
        unsigned active_count = 0U;
        for (unsigned lane = 0U; lane < lanes; ++lane) {
          const auto wave_lane = word(1152U + (lane * 8U));
          ASSERT_LT(wave_lane, wave_width);
          const auto control = inputs.at(4U + (lane * 4U) + 3U).at(0);
          const bool active
            = (control & 1U) != 0U && ((control & 2U) == 0U || wave_lane != 0U);
          EXPECT_EQ(word(1156U + (lane * 8U)), active ? 1U : 0U);
          if (active) {
            ++active_count;
            for (unsigned component = 0U; component < 4U; ++component) {
              const auto coefficient
                = word(128U + (lane * 16U) + (component * 4U));
              // Independent serial reduction: no GPU reduction helper here.
              expected.at(index).at(component)
                = std::max(expected.at(index).at(component), coefficient);
              if (pattern == Pattern::kZero) {
                EXPECT_EQ(coefficient, 0U);
              }
            }
          }
        }
        ASSERT_GT(active_count, 0U);
        if (pattern == Pattern::kPartial) {
          EXPECT_EQ(active_count, 37U);
        }
        for (unsigned peer = 0U; peer < expected.size(); ++peer) {
          for (unsigned component = 0U; component < 4U; ++component) {
            EXPECT_EQ(word(80U + (peer * 16U) + (component * 4U)),
              expected.at(peer).at(component));
          }
        }
        const std::array canaries {
          0x13579bdfU,
          0x2468ace0U,
          0x55aa55aaU,
          0xaa55aa55U,
        };
        for (unsigned region = 0U; region < canaries.size(); ++region) {
          for (unsigned component = 0U; component < 4U; ++component) {
            EXPECT_EQ(word(16U + (region * 16U) + (component * 4U)),
              canaries.at(region));
          }
        }
        ++cases;
      }
    }
  }
  RecordProperty("publication_cases", cases);
  RecordProperty("probe_wave_lane_count", wave_width);
  RecordProperty("full_group_wave_count", (64U + wave_width - 1U) / wave_width);
}
} // namespace oxygen::vortex::testing::exposure
