//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/ext/vector_float3.hpp>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>

namespace oxygen::vortex::testing {
namespace {
  using FixtureLightIndex
    = NamedType<std::size_t, struct FixtureLightIndexTag, Comparable>;
  using Rgb = std::array<double, 3>;
  using Image = std::vector<exposure::Pixel>;

  class LightingImageReferenceTest : public exposure::ExposureLightingGpuTest {
  protected:
    static constexpr std::uint32_t kWidth = 96U;
    static constexpr std::uint32_t kHeight = 64U;
    static constexpr std::size_t kLightCount = 33U;

    auto SetUp() -> void override
    {
      initial_scene_capacity = kLightCount + 2U;
      exposure::ExposureLightingGpuTest::SetUp();
      view.viewport.width = static_cast<float>(kWidth);
      view.viewport.height = static_cast<float>(kHeight);
      auto lens = camera.GetCameraAs<scene::PerspectiveCamera>();
      ASSERT_TRUE(lens.has_value());
      lens->get().SetViewport(view.viewport);
      lens->get().SetAspectRatio(static_cast<float>(kWidth) / kHeight);
      auto output = CreateRegisteredTexture({
        .width = kWidth,
        .height = kHeight,
        .format = Format::kRGBA32Float,
        .is_render_target = true,
        .initial_state = graphics::ResourceStates::kCommon,
      });
      framebuffer = Backend().CreateFramebuffer(
        graphics::FramebufferDesc {}.AddColorAttachment(output));
      ASSERT_NE(framebuffer, nullptr);
    }
  };

  NOLINT_TEST_F(LightingImageReferenceTest, MixedLocalLightsMatchSerialImages)
  {
    constexpr std::size_t light_count = kLightCount;
    constexpr std::size_t pixel_count
      = static_cast<std::size_t>(kWidth) * kHeight;
    auto nodes = std::vector<scene::SceneNode> {};
    nodes.reserve(light_count);
    auto all = std::vector<FixtureLightIndex> {};
    all.reserve(light_count);
    auto active = std::array<bool, light_count> {};
    const auto position = [](const FixtureLightIndex light) -> glm::vec3 {
      const auto index = light.get();
      const auto row = index / 7U;
      return {
        (static_cast<float>(index % 7U) - 3.0F) * 0.25F,
        (static_cast<float>(row) - 2.0F) * 0.2F,
        0.0F,
      };
    };
    for (std::size_t index = 0U; index < light_count; ++index) {
      auto node
        = scene->CreateNode("Image reference light " + std::to_string(index));
      node.GetTransform().SetLocalPosition(
        position(FixtureLightIndex { index }));
      const auto tint = std::array {
        glm::vec3 { 0.2F, 0.5F, 1.0F },
        glm::vec3 { 1.0F, 0.25F, 0.1F },
        glm::vec3 { 0.1F, 1.0F, 0.4F },
      }.at(index % 3U);
      const auto initialize = [&](auto& light) -> void {
        light.Common().affects_world = false;
        light.Common().casts_shadows = false;
        light.Common().color_rgb = tint;
        light.Common().exposure_compensation_ev
          = static_cast<float>(index % 3U) - 1.0F;
        light.SetRange(3.0F + (static_cast<float>(index % 3U) * 0.25F));
        light.SetLuminousFluxLm(1.0F + (static_cast<float>(index) * 0.1F));
      };
      if (index % 2U == 0U) {
        auto light = std::make_unique<scene::PointLight>();
        initialize(*light);
        ASSERT_TRUE(node.AttachLight(std::move(light)));
      } else {
        auto light = std::make_unique<scene::SpotLight>();
        initialize(*light);
        light->SetInnerConeAngleRadians(0.2F);
        light->SetOuterConeAngleRadians(1.2F);
        ASSERT_TRUE(node.AttachLight(std::move(light)));
        node.GetTransform().SetLocalRotation(glm::quat {
          0.70710678F,
          0.70710678F,
          0.0F,
          0.0F,
        });
      }
      nodes.push_back(node);
      all.emplace_back(index);
    }
    const auto select
      = [&](const std::span<const FixtureLightIndex> enabled) -> void {
      auto wanted = std::array<bool, light_count> {};
      for (const auto index : enabled) {
        wanted.at(index.get()) = true;
      }
      for (std::size_t index = 0U; index < light_count; ++index) {
        if (active.at(index) == wanted.at(index)) {
          continue;
        }
        // Replace the component so selection membership observes the edit.
        if (index % 2U == 0U) {
          const auto current = nodes.at(index).GetLightAs<scene::PointLight>();
          ASSERT_TRUE(current.has_value());
          auto replacement
            = std::make_unique<scene::PointLight>(current->get());
          replacement->Common().affects_world = wanted.at(index);
          ASSERT_TRUE(nodes.at(index).ReplaceLight(std::move(replacement)));
        } else {
          const auto current = nodes.at(index).GetLightAs<scene::SpotLight>();
          ASSERT_TRUE(current.has_value());
          auto replacement = std::make_unique<scene::SpotLight>(current->get());
          replacement->Common().affects_world = wanted.at(index);
          ASSERT_TRUE(nodes.at(index).ReplaceLight(std::move(replacement)));
        }
        active.at(index) = wanted.at(index);
      }
    };
    double maximum_scaled_error = 0.0;
    double minimum_single_peak = std::numeric_limits<double>::infinity();
    std::uint64_t compared_channels = 0U;
    std::uint64_t permutation_channels = 0U;
    bool reversed = false;
    for (const bool forward : { false, true }) {
      for (const auto domain : {
             data::MaterialDomain::kOpaque,
             data::MaterialDomain::kMasked,
             data::MaterialDomain::kAlphaBlended,
           }) {
        SCOPED_TRACE(forward);
        SCOPED_TRACE(static_cast<unsigned>(domain));
        SetSurface(domain);
        ASSERT_NO_FATAL_FAILURE(select({}));
        ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 3U));
        const auto render = [&](Image& image) -> void {
          ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 1U));
          ASSERT_EQ(probe->color->GetDescriptor().format, Format::kRGBA32Float);
          image = ReadFloatTexture(*probe->color);
          ASSERT_EQ(image.size(), pixel_count);
        };
        auto baseline = Image {};
        ASSERT_NO_FATAL_FAILURE(render(baseline));
        auto sum = std::vector<Rgb>(pixel_count);
        auto strongest = std::vector<Rgb>(pixel_count);
        double strongest_peak = 0.0;
        auto references = std::array<std::vector<Rgb>, 3> {};
        for (std::size_t index = 0U; index < light_count; ++index) {
          SCOPED_TRACE(index);
          const auto one = std::array { FixtureLightIndex { index } };
          ASSERT_NO_FATAL_FAILURE(select(one));
          auto image = Image {};
          ASSERT_NO_FATAL_FAILURE(render(image));
          double peak = 0.0;
          for (std::size_t pixel = 0U; pixel < pixel_count; ++pixel) {
            for (std::size_t channel = 0U; channel < 3U; ++channel) {
              const auto contribution
                = static_cast<double>(image.at(pixel).at(channel))
                - baseline.at(pixel).at(channel);
              ASSERT_TRUE(std::isfinite(contribution));
              EXPECT_GE(contribution, -2.0e-5);
              peak = std::max(peak, contribution);
              sum.at(pixel).at(channel) += contribution;
            }
          }
          EXPECT_GT(peak, 1.0e-5) << "A light with no visible contribution "
                                     "cannot qualify this fixture";
          minimum_single_peak = std::min(minimum_single_peak, peak);
          if (peak > strongest_peak) {
            strongest_peak = peak;
            for (std::size_t pixel = 0U; pixel < pixel_count; ++pixel) {
              for (std::size_t channel = 0U; channel < 3U; ++channel) {
                strongest.at(pixel).at(channel)
                  = static_cast<double>(image.at(pixel).at(channel))
                  - baseline.at(pixel).at(channel);
              }
            }
          }
          if (index >= 30U) {
            references.at(index - 30U) = sum;
          }
        }
        for (std::size_t count = 31U; count <= light_count; ++count) {
          SCOPED_TRACE(count);
          ASSERT_NO_FATAL_FAILURE(select(std::span(all).first(count)));
          auto combined = Image {};
          bool capture_finished = true;
          {
            // Capture one representative all-lights frame when requested by
            // the native fixture's existing RenderDoc environment control.
            const auto capture = forward
                && domain == data::MaterialDomain::kOpaque
                && count == light_count
              ? BeginOptionalCapture()
              : observer_ptr<graphics::FrameCaptureController> {};
            const auto finish_capture = ScopeGuard([&] noexcept -> void {
              try {
                if (capture) {
                  capture_finished = capture->EndCapture();
                }
              } catch (...) {
                capture_finished = false;
              }
            });
            ASSERT_NO_FATAL_FAILURE(render(combined));
          }
          ASSERT_TRUE(capture_finished);
          const auto& reference = references.at(count - 31U);
          unsigned omitted_rejections = 0U;
          unsigned duplicated_rejections = 0U;
          for (std::size_t pixel = 0U; pixel < pixel_count; ++pixel) {
            for (std::size_t channel = 0U; channel < 3U; ++channel) {
              const auto expected = baseline.at(pixel).at(channel)
                + reference.at(pixel).at(channel);
              const auto actual
                = static_cast<double>(combined.at(pixel).at(channel));
              ASSERT_TRUE(std::isfinite(actual));
              const auto tolerance = (0.005 * std::abs(expected)) + 2.0e-5;
              const auto error = std::abs(actual - expected);
              EXPECT_LE(error, tolerance)
                << "pixel=" << pixel << " channel=" << channel;
              maximum_scaled_error
                = std::max(maximum_scaled_error, error / tolerance);
              ++compared_channels;
              if (count == light_count) {
                const auto control = strongest.at(pixel).at(channel);
                const auto omitted = expected - control;
                const auto duplicated = expected + control;
                omitted_rejections += std::abs(actual - omitted)
                    > (0.005 * std::abs(omitted)) + 2.0e-5
                  ? 1U
                  : 0U;
                duplicated_rejections += std::abs(actual - duplicated)
                    > (0.005 * std::abs(duplicated)) + 2.0e-5
                  ? 1U
                  : 0U;
              }
            }
          }
          if (count == light_count) {
            EXPECT_GT(omitted_rejections, 0U);
            EXPECT_GT(duplicated_rejections, 0U);
          }
        }
        // Reverse physical light assignments while keeping node identities.
        // With 33 alternating point/spot records each swapped pair has the
        // same component type, so this permutes contributions without changing
        // the supported receiver or shading path.
        for (std::size_t first = 0U; first < light_count / 2U; ++first) {
          const auto second = light_count - 1U - first;
          if (first % 2U == 0U) {
            const auto a = nodes.at(first).GetLightAs<scene::PointLight>();
            const auto b = nodes.at(second).GetLightAs<scene::PointLight>();
            ASSERT_TRUE(a.has_value());
            ASSERT_TRUE(b.has_value());
            auto copy_a = std::make_unique<scene::PointLight>(a->get());
            auto copy_b = std::make_unique<scene::PointLight>(b->get());
            ASSERT_TRUE(nodes.at(first).ReplaceLight(std::move(copy_b)));
            ASSERT_TRUE(nodes.at(second).ReplaceLight(std::move(copy_a)));
          } else {
            const auto a = nodes.at(first).GetLightAs<scene::SpotLight>();
            const auto b = nodes.at(second).GetLightAs<scene::SpotLight>();
            ASSERT_TRUE(a.has_value());
            ASSERT_TRUE(b.has_value());
            auto copy_a = std::make_unique<scene::SpotLight>(a->get());
            auto copy_b = std::make_unique<scene::SpotLight>(b->get());
            ASSERT_TRUE(nodes.at(first).ReplaceLight(std::move(copy_b)));
            ASSERT_TRUE(nodes.at(second).ReplaceLight(std::move(copy_a)));
          }
          nodes.at(first).GetTransform().SetLocalPosition(
            position(FixtureLightIndex { reversed ? first : second }));
          nodes.at(second).GetTransform().SetLocalPosition(
            position(FixtureLightIndex { reversed ? second : first }));
        }
        reversed = !reversed;
        auto permuted = Image {};
        ASSERT_NO_FATAL_FAILURE(render(permuted));
        for (std::size_t pixel = 0U; pixel < pixel_count; ++pixel) {
          for (std::size_t channel = 0U; channel < 3U; ++channel) {
            const auto expected = baseline.at(pixel).at(channel)
              + references.back().at(pixel).at(channel);
            const auto tolerance = (0.005 * std::abs(expected)) + 2.0e-5;
            EXPECT_NEAR(permuted.at(pixel).at(channel), expected, tolerance);
            maximum_scaled_error = std::max(maximum_scaled_error,
              std::abs(permuted.at(pixel).at(channel) - expected) / tolerance);
            ++permutation_channels;
          }
        }
        ASSERT_NO_FATAL_FAILURE(select({}));
        auto cleared = Image {};
        ASSERT_NO_FATAL_FAILURE(render(cleared));
        for (std::size_t pixel = 0U; pixel < pixel_count; ++pixel) {
          for (std::size_t channel = 0U; channel < 3U; ++channel) {
            EXPECT_NEAR(cleared.at(pixel).at(channel),
              baseline.at(pixel).at(channel), 2.0e-5);
          }
        }
      }
    }
    RecordProperty("compared_channels", compared_channels);
    RecordProperty("permutation_channels", permutation_channels);
    RecordProperty("maximum_fraction_of_image_budget", maximum_scaled_error);
    RecordProperty("minimum_single_light_peak", minimum_single_peak);
  }
} // namespace
} // namespace oxygen::vortex::testing
