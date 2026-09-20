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
#include <cstring>
#include <limits>
#include <span>
#include <vector>

#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>

namespace oxygen::vortex::testing::exposure {

using graphics::ResourceStates;

NOLINT_TEST_F(ExposureGpuTest, FilterGradientsEncloseReferenceNeighborsAndRetry)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 3.0F; // P=1/8; transmittance must remain unscaled.
  const auto config = SharedConfig(settings);
  const auto capture = BeginOptionalCapture();
  unsigned case_index = 0U;
  for (const auto format : {
         Format::kRGBA32Float,
         Format::kRGBA16Float,
       }) {
    for (const auto shape : {
           std::array {
             9U,
             3U,
             1U,
           },
           std::array {
             9U,
             3U,
             2U,
           },
           std::array {
             1U,
             1U,
             2U,
           },
           std::array {
             1U,
             1U,
             1U,
           },
         }) {
      for (const bool retained : {
             false,
             true,
           }) {
        SCOPED_TRACE(case_index);
        const auto [width, height, depth] = shape;
        std::vector<Pixel> pixels(
          static_cast<std::size_t>(width) * height * depth);
        for (unsigned z = 0; z < depth; ++z) {
          for (unsigned y = 0; y < height; ++y) {
            for (unsigned x = 0; x < width; ++x) {
              pixels.at((((z * height) + y) * width) + x) = Pixel {
                static_cast<float>(x) + (static_cast<float>(y) / 4),
                (static_cast<float>(y) * 2) + (static_cast<float>(z) / 2),
                (static_cast<float>(z) * 4) + (static_cast<float>(x) / 8),
                static_cast<float>(x + y + z) / 16,
              };
            }
          }
        }
        const auto signal = MakeSignal(width, height, pixels, depth, format);
        const auto id = std::array { 5U, 6U, 10U, }.at(case_index++ % 3U);
        unsigned record_index = 2U;
        if (id == 5U) {
          record_index = 0U;
        } else if (id == 6U) {
          record_index = 1U;
        }
        const bool transmission = id != 5U;
        const HdrErrorBoundsData bounds = retained
          ? HdrErrorBoundsData { .rgb_relative = 1.0F / 128,
              .rgb_absolute = .125F,
              .transmittance_relative = 1.0F / 64,
              .transmittance_absolute = 1.0F / 512, }
          : HdrErrorBoundsData {};
        ctx_.frame_sequence = frame::SequenceNumber {
          ++sequence_,
        };
        const auto frame = SubmitCommands("Vortex Exposure Frame",
          [&](graphics::CommandRecorder& recorder) -> auto {
            return pass_->ResolveFrame(ctx_, recorder, config, {});
          });
        ASSERT_NE(frame, nullptr);
        auto upload = CreateUploadBuffer(SizeBytes {
          sizeof(bounds),
        });
        upload->Update(&bounds, sizeof(bounds), 0U);
        {
          auto recorder = AcquireRecorder("Filter gradient controlled bounds");
          EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
          ASSERT_TRUE(recorder->AdoptKnownResourceState(
            *frame->current_state->status_buffer));
          recorder->RequireResourceState(
            *frame->current_state->status_buffer, ResourceStates::kCopyDest);
          recorder->FlushBarriers();
          recorder->CopyBuffer(*frame->current_state->status_buffer,
            80U + (record_index * 16U), *upload, 0U, sizeof(bounds));
          recorder->RequireResourceStateFinal(
            *frame->current_state->status_buffer,
            ResourceStates::kUnorderedAccess);
        }
        const auto read_status = [&] -> oxygen::vortex::ExposureStatusStorage {
          return Read<ExposureStatusStorage>(
            *frame->current_state->status_buffer,
            ResourceStates::kUnorderedAccess);
        };
        const auto before = read_status();
        postprocess::ExposurePass::HdrProduct product {
          .texture = signal.texture.get(),
          .srv = signal.srv,
          .id = id,
          .transmittance = transmission,
        };
        ASSERT_TRUE(SubmitCommands("Vortex Exposure Filter Gradients",
          [&](graphics::CommandRecorder& recorder) -> auto {
            return pass_->GatherFilterGradients(ctx_, recorder, frame, product);
          }));
        EXPECT_TRUE(pass_->HasFilterGradients(frame, id));
        const auto after = read_status();
        EXPECT_TRUE(std::ranges::equal(std::as_bytes(std::span {
                                                       &before,
                                                       1,
                                                     })
                                         .first(160U),
          std::as_bytes(std::span {
                          &after,
                          1,
                        })
            .first(160U)));
        const auto& gradient = after.filter_gradients.at(record_index);
        EXPECT_EQ(gradient.flags, 1U);
        EXPECT_EQ(gradient.checked_texels, pixels.size());
        std::array<double, 3> expected_rgb {};
        std::array<double, 3> expected_t {};
        const auto interval
          = [&](float stored, unsigned channel) -> std::array<double, 2> {
          const double observed
            = static_cast<double>(stored) * (channel == 3U ? 1 : 8);
          const double relative = channel == 3U ? bounds.transmittance_relative
                                                : bounds.rgb_relative;
          const double absolute = channel == 3U ? bounds.transmittance_absolute
                                                : bounds.rgb_absolute;
          std::array result {
            std::max(0.0, (observed - absolute) / (1 + relative)),
            (observed + absolute) / (1 - relative),
          };
          if (channel == 3U) {
            result.at(0) = std::min(1.0, result.at(0));
            result.at(1) = std::min(1.0, result.at(1));
          }
          return result;
        };
        for (unsigned z = 0; z < depth; ++z) {
          for (unsigned y = 0; y < height; ++y) {
            for (unsigned x = 0; x < width; ++x) {
              const auto index = (((z * height) + y) * width) + x;
              const std::array coordinate {
                x,
                y,
                z,
              };
              for (unsigned axis = 0; axis < 3; ++axis) {
                const bool boundary
                  = coordinate.at(axis) + 1U >= shape.at(axis);
                if (boundary) {
                  continue;
                }
                const auto stride
                  = std::array { 1U, width, width * height, }.at(axis);
                const auto neighbor = index + stride;
                for (unsigned c = 0; c < (transmission ? 4U : 3U); ++c) {
                  const auto a = interval(pixels.at(index).at(c), c);
                  const auto b = interval(pixels.at(neighbor).at(c), c);
                  auto& expected
                    = c == 3U ? expected_t.at(axis) : expected_rgb.at(axis);
                  expected = std::max({
                    expected,
                    a.at(1) - b.at(0),
                    b.at(1) - a.at(0),
                  });
                }
              }
            }
          }
        }
        for (unsigned axis = 0; axis < 3; ++axis) {
          EXPECT_GE(
            static_cast<double>(gradient.rgb.at(axis)), expected_rgb.at(axis));
          EXPECT_GE(static_cast<double>(gradient.transmittance.at(axis)),
            expected_t.at(axis));
          EXPECT_LE(static_cast<double>(gradient.rgb.at(axis)),
            (expected_rgb.at(axis) * 1.001) + 1e-6);
          EXPECT_LE(static_cast<double>(gradient.transmittance.at(axis)),
            (expected_t.at(axis) * 1.001) + 1e-6);
          if (shape.at(axis) == 1U) {
            EXPECT_EQ(gradient.rgb.at(axis), 0.0F);
            EXPECT_EQ(gradient.transmittance.at(axis), 0.0F);
          }
        }
        for (unsigned peer = 0; peer < 3; ++peer) {
          if (peer != record_index) {
            EXPECT_TRUE(std::ranges::equal(std::as_bytes(std::span {
                                             &before.filter_gradients.at(peer),
                                             1,
                                           }),
              std::as_bytes(std::span {
                &after.filter_gradients.at(peer),
                1,
              })));
          }
        }
        const auto constant
          = Uniform(2.0F, retained ? 1U : 9U, retained ? 1U : 3U);
        ctx_.current_view.view_id = ViewId {
          2U,
        };
        ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
          2U,
        };
        const auto other = SubmitCommands("Vortex Exposure Frame",
          [&](graphics::CommandRecorder& recorder) -> auto {
            return pass_->ResolveFrame(ctx_, recorder, config, {});
          });
        ASSERT_TRUE(SubmitCommands("Vortex Exposure Filter Gradients",
          [&](graphics::CommandRecorder& recorder) -> auto {
            return pass_->GatherFilterGradients(ctx_, recorder, other,
              {
                .texture = constant.texture.get(),
                .srv = constant.srv,
                .id = id,
                .transmittance = transmission,
              });
          }));
        const auto retained_status = read_status();
        EXPECT_TRUE(std::ranges::equal(std::as_bytes(std::span {
                                         &gradient,
                                         1,
                                       }),
          std::as_bytes(std::span {
            &retained_status.filter_gradients.at(record_index),
            1,
          })));
        ctx_.current_view.view_id = ViewId {
          1U,
        };
        ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
          1U,
        };
        product.srv = kInvalidShaderVisibleIndex;
        EXPECT_FALSE(SubmitCommands("Vortex Exposure Filter Gradients",
          [&](graphics::CommandRecorder& recorder) -> auto {
            return pass_->GatherFilterGradients(ctx_, recorder, frame, product);
          }));
        EXPECT_FALSE(pass_->HasFilterGradients(frame, id));
        product.srv = signal.srv;
        auto& backend = FailureBackend();
        backend.fail_recorder_name = "Vortex Exposure Filter Gradients";
        EXPECT_FALSE(SubmitCommands("Vortex Exposure Filter Gradients",
          [&](graphics::CommandRecorder& recorder) -> auto {
            return pass_->GatherFilterGradients(ctx_, recorder, frame, product);
          }));
        EXPECT_FALSE(pass_->HasFilterGradients(frame, id));
        backend.fail_recorder_name.clear();
        ASSERT_TRUE(SubmitCommands("Vortex Exposure Filter Gradients",
          [&](graphics::CommandRecorder& recorder) -> auto {
            return pass_->GatherFilterGradients(ctx_, recorder, frame,
              {
                .texture = constant.texture.get(),
                .srv = constant.srv,
                .id = id,
                .transmittance = transmission,
              });
          }));
        EXPECT_TRUE(pass_->HasFilterGradients(frame, id));
        const auto retried = read_status().filter_gradients.at(record_index);
        EXPECT_EQ(retried.flags, 1U);
        EXPECT_EQ(retried.checked_texels, retained ? 1U : 27U);
        EXPECT_EQ(retried.rgb, (std::array<float, 3> {}));
        EXPECT_EQ(retried.transmittance, (std::array<float, 3> {}));
      }
    }
  }
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, FilterGradientsRejectInvalidIntervalsAndPreserveTinyGaps)
{
  struct Case {
    float rgb {
      1,
    };
    float alpha {
      .5F,
    };
    HdrErrorBoundsData bounds {};
    bool transmission {
      true,
    };
    bool valid {
      false,
    };
  };
  const auto infinity = std::numeric_limits<float>::infinity();
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  auto tiny_rgb = Case {};
  tiny_rgb.rgb = std::bit_cast<float>(1U);
  tiny_rgb.valid = true;

  auto tiny_alpha = Case {};
  tiny_alpha.alpha = std::bit_cast<float>(1U);
  tiny_alpha.valid = true;

  auto zero_alpha_gap = Case {};
  zero_alpha_gap.alpha = 0;
  zero_alpha_gap.bounds.transmittance_absolute = std::bit_cast<float>(1U);
  zero_alpha_gap.valid = true;

  auto normal_alpha_gap = Case {};
  normal_alpha_gap.alpha = std::numeric_limits<float>::min();
  normal_alpha_gap.bounds.transmittance_absolute
    = std::bit_cast<float>(0x007fffffU);
  normal_alpha_gap.valid = true;

  auto negative_zero_bound = Case {};
  negative_zero_bound.alpha = 0;
  negative_zero_bound.bounds.transmittance_relative = .5F;
  negative_zero_bound.bounds.transmittance_absolute = -0.0F;
  negative_zero_bound.valid = true;

  auto negative_rgb = Case {};
  negative_rgb.rgb = std::bit_cast<float>(0x80000001U);

  auto infinite_rgb = Case {};
  infinite_rgb.rgb = infinity;

  auto nan_rgb = Case {};
  nan_rgb.rgb = nan;

  auto excess_alpha = Case {};
  excess_alpha.alpha = 1.125F;

  auto ignored_alpha = Case {};
  ignored_alpha.alpha = nan;
  ignored_alpha.transmission = false;
  ignored_alpha.valid = true;

  auto invalid_rgb_relative = Case {};
  invalid_rgb_relative.bounds.rgb_relative = 1;

  auto negative_rgb_absolute = Case {};
  negative_rgb_absolute.bounds.rgb_absolute = std::bit_cast<float>(0x80000001U);

  auto invalid_transmission_relative = Case {};
  invalid_transmission_relative.bounds.transmittance_relative = 1;

  auto ignored_transmission_relative = Case {};
  ignored_transmission_relative.bounds.transmittance_relative = 1;
  ignored_transmission_relative.transmission = false;
  ignored_transmission_relative.valid = true;

  auto overflowing_rgb_bound = Case {};
  overflowing_rgb_bound.rgb = std::numeric_limits<float>::max();
  overflowing_rgb_bound.bounds.rgb_absolute = 1;

  const std::array cases {
    tiny_rgb,
    tiny_alpha,
    zero_alpha_gap,
    normal_alpha_gap,
    negative_zero_bound,
    negative_rgb,
    infinite_rgb,
    nan_rgb,
    excess_alpha,
    ignored_alpha,
    invalid_rgb_relative,
    negative_rgb_absolute,
    invalid_transmission_relative,
    ignored_transmission_relative,
    overflowing_rgb_bound,
  };
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 0.0F;
  for (const auto& test : cases) {
    SCOPED_TRACE(test.rgb);
    const auto id = test.transmission ? 6U : 5U;
    const auto record_index = test.transmission ? 1U : 0U;
    const std::array pixels {
      Pixel {
        0,
        0,
        0,
        0,
      },
      Pixel {
        test.rgb,
        0,
        0,
        test.alpha,
      },
    };
    const auto signal = MakeSignal(2U, 1U, pixels);
    ctx_.frame_sequence = frame::SequenceNumber {
      ++sequence_,
    };
    const auto frame = SubmitCommands("Vortex Exposure Frame",
      [&](graphics::CommandRecorder& recorder) -> auto {
        return pass_->ResolveFrame(ctx_, recorder, SharedConfig(settings), {});
      });
    ASSERT_NE(frame, nullptr);
    auto upload = CreateUploadBuffer(SizeBytes {
      sizeof(test.bounds),
    });
    upload->Update(&test.bounds, sizeof(test.bounds), 0U);
    {
      auto recorder = AcquireRecorder("Invalid gradient bounds fixture");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      ASSERT_TRUE(recorder->AdoptKnownResourceState(
        *frame->current_state->status_buffer));
      recorder->RequireResourceState(
        *frame->current_state->status_buffer, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(*frame->current_state->status_buffer,
        80U + (record_index * 16U), *upload, 0U, sizeof(test.bounds));
      recorder->RequireResourceStateFinal(
        *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    }
    ASSERT_TRUE(SubmitCommands("Vortex Exposure Filter Gradients",
      [&](graphics::CommandRecorder& recorder) -> auto {
        return pass_->GatherFilterGradients(ctx_, recorder, frame,
          {
            .texture = signal.texture.get(),
            .srv = signal.srv,
            .id = id,
            .transmittance = test.transmission,
          });
      }));
    const auto result = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess)
                          .filter_gradients.at(record_index);
    EXPECT_EQ(result.flags, test.valid ? 1U : 3U);
    EXPECT_EQ(result.checked_texels, 2U);
    if (test.valid) {
      EXPECT_GE(
        static_cast<double>(result.rgb.at(0)), static_cast<double>(test.rgb));
      EXPECT_GT(result.rgb.at(0), 0.0F);
      EXPECT_EQ(result.rgb.at(1), 0.0F);
      EXPECT_EQ(result.rgb.at(2), 0.0F);
      if (test.transmission) {
        const double upper = (static_cast<double>(test.alpha)
                               + test.bounds.transmittance_absolute)
          / (1.0 - test.bounds.transmittance_relative);
        EXPECT_GE(static_cast<double>(result.transmittance.at(0)),
          std::min(1.0, upper));
        if (upper == 0) {
          EXPECT_EQ(result.transmittance.at(0), 0.0F);
        }
      }
      if (!test.transmission) {
        EXPECT_EQ(result.transmittance, (std::array<float, 3> {}));
      }
    }
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, HardwareFilterEnclosuresRetainFormatAndHistoryError)
{
  std::vector<std::array<float, 4>> inputs;
  const std::array levels {
    0.0F,
    0x1p-149F,
    0x1p-126F,
    0x1p-24F,
    0x1p-14F,
    .5F,
    1.0F,
    65504.0F,
  };
  for (const auto value : levels) {
    for (const auto relative : {
           0.0F,
           0x1p-11F,
           .01F,
         }) {
      for (const auto gradient : {
             0.0F,
             0x1p-24F,
             .125F,
           }) {
        for (const auto width : {
               1.0F,
               32.0F,
               16384.0F,
             }) {
          for (const auto half : {
                 0.0F,
                 1.0F,
               }) {
            inputs.push_back({
              value,
              relative,
              0.0F,
              half,
            });
            inputs.push_back({
              gradient,
              gradient,
              gradient,
              value,
            });
            inputs.push_back({
              width,
              width,
              32.0F,
              0.0F,
            });
          }
        }
      }
    }
  }
  // Captured half-sampler rounding at the midpoint of two exactly stored
  // half texels. There is no texel-store error to hide the filtering error.
  inputs.push_back({
    .5419921875F,
    0.0F,
    0.0F,
    1.0F,
  });
  inputs.push_back({
    0.0F,
    0.0F,
    .06494140625F,
    .541748046875F,
  });
  inputs.push_back({
    1.0F,
    1.0F,
    32.0F,
    0.0F,
  });
  const auto count = static_cast<std::uint32_t>(inputs.size() / 3U);
  const auto results = RunToneProbe(std::as_bytes(std::span {
                                      inputs,
                                    }),
    count, 32U);
  for (std::size_t i = 0U; i < results.size(); ++i) {
    SCOPED_TRACE(i);
    const auto& interval = results.at(i);
    const auto reference = static_cast<double>(inputs.at((i * 3U) + 1U).at(3));
    EXPECT_LE(static_cast<double>(interval.at(0)), reference);
    EXPECT_GE(static_cast<double>(interval.at(1)), reference);
    EXPECT_TRUE(std::isfinite(interval.at(0)));
    EXPECT_TRUE(std::isfinite(interval.at(1)));
    if (inputs.at(i * 3U).at(1) == 0.0F && inputs.at(i * 3U).at(3) == 0.0F) {
      EXPECT_EQ(std::bit_cast<std::uint32_t>(interval.at(0)),
        std::bit_cast<std::uint32_t>(inputs.at(i * 3U).at(0)));
      EXPECT_EQ(std::bit_cast<std::uint32_t>(interval.at(1)),
        std::bit_cast<std::uint32_t>(inputs.at(i * 3U).at(0)));
    }
  }
  RecordProperty("hardware_filter_interval_cases", count);
}

} // namespace oxygen::vortex::testing::exposure
