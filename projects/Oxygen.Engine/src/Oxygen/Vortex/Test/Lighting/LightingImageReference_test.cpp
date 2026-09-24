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
#include <numbers>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/ext/vector_float3.hpp>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Lighting/Types/ForwardLocalLightRecord.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/Photometry.h>
#include <Oxygen/Vortex/Test/Lighting/UnculledLightingFixture.h>
#include <Oxygen/Vortex/ViewExtension.h>

namespace oxygen::vortex::testing {
namespace {
  using FixtureLightIndex
    = NamedType<std::size_t, struct FixtureLightIndexTag, Comparable>;
  using Rgb = std::array<double, 3>;
  using Image = std::vector<exposure::Pixel>;

  class LightingImageReferenceTest : public UnculledLightingGpuTest {
  protected:
    static constexpr std::uint32_t kWidth = 96U;
    static constexpr std::uint32_t kHeight = 64U;
    static constexpr std::size_t kLightCount = 33U;

    auto SetUp() -> void override
    {
      initial_scene_capacity = kLightCount + 2U;
      UnculledLightingGpuTest::SetUp();
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

  NOLINT_TEST_F(
    LightingImageReferenceTest, OrthographicHighlightsStayUniformAcrossTheImage)
  {
    for (unsigned channel = 0; channel < 3U; ++channel) {
      auto sun = scene->CreateNode("Directional channel " + std::to_string(channel));
      auto light = std::make_unique<scene::DirectionalLight>();
      light->Common().casts_shadows = false;
      light->Common().color_rgb = glm::vec3 { 0.0F };
      light->Common().color_rgb[channel] = 1.0F;
      light->SetIntensityLux(1.0F);
      ASSERT_TRUE(sun.AttachLight(std::move(light)));
      sun.GetTransform().SetLocalRotation(
        glm::quat { 0.70710678F, 0.70710678F, 0, 0 });
    }
    unsigned checked_images = 0;
    for (const bool forward : { false, true }) {
      for (const auto domain : {
             data::MaterialDomain::kOpaque,
             data::MaterialDomain::kMasked,
             data::MaterialDomain::kAlphaBlended,
           }) {
        data::pak::render::MaterialAssetDesc desc {};
        desc.material_domain = static_cast<std::uint8_t>(domain);
        desc.flags = data::pak::render::kMaterialFlag_NoTextureSampling
          | data::pak::render::kMaterialFlag_DoubleSided;
        if (domain == data::MaterialDomain::kMasked) {
          desc.flags |= data::pak::render::kMaterialFlag_AlphaTest;
        }
        for (auto& value : desc.base_color) {
          value = 1.0F;
        }
        desc.metalness = data::Unorm16 { 1.0F };
        desc.roughness = data::Unorm16 { 0.25F };
        desc.ambient_occlusion = data::Unorm16 { 1.0F };
        desc.normal_scale = 1.0F;
        desc.uv_scale[0] = desc.uv_scale[1] = 1.0F;
        mesh_node.GetRenderable().SetMaterialOverride(0, 0,
          std::make_shared<data::MaterialAsset>(
            data::AssetKey::FromVirtualPath("/Test/Lighting/Orthographic-"
              + std::to_string(++material_sequence) + ".omat"),
            desc, std::vector<data::ShaderReference> {}));
        for (unsigned variant = 0; variant < 4U; ++variant) {
          SCOPED_TRACE(forward);
          SCOPED_TRACE(static_cast<int>(domain));
          SCOPED_TRACE(variant);
          camera.GetTransform().SetLocalPosition(
            glm::vec3 { variant == 1U ? 0.3F : 0.0F, 0, 0 });
          camera.GetTransform().SetLocalRotation(variant == 2U
              ? glm::quat { 0.99500417F, 0, 0.09983342F, 0 }
              : glm::quat { 1, 0, 0, 0 });
          if (variant == 3U) {
            auto lens = std::make_unique<scene::PerspectiveCamera>();
            lens->SetViewport(view.viewport);
            lens->SetAspectRatio(static_cast<float>(kWidth) / kHeight);
            ASSERT_TRUE(camera.ReplaceCamera(std::move(lens)));
          } else {
            auto lens = std::make_unique<scene::OrthographicCamera>();
            lens->SetExtents(-0.5F, 0.5F, -0.4F, 0.4F, 0.1F, 10.0F);
            lens->SetViewport(view.viewport);
            ASSERT_TRUE(camera.ReplaceCamera(std::move(lens)));
          }
          ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 3U));
          ASSERT_NE(probe->color, nullptr);
          const auto pixels = ReadFloatTexture(*probe->color);
          float minimum = std::numeric_limits<float>::max();
          float maximum = 0.0F;
          // Interior rectangle is covered by the plane for every camera pose.
          for (std::uint32_t y = kHeight / 3U; y < 2U * kHeight / 3U; ++y) {
            for (std::uint32_t x = kWidth / 3U; x < 2U * kWidth / 3U; ++x) {
              const auto value = pixels.at((y * kWidth) + x).at(0);
              ASSERT_TRUE(std::isfinite(value));
              // Independent colored sources must all reach the shared draw.
              // Equal material channels and directions give equal RGB response.
              for (std::size_t channel = 1; channel < 3U; ++channel) {
                EXPECT_NEAR(pixels.at((y * kWidth) + x).at(channel), value,
                  (0.005F * value) + 2.0e-5F);
              }
              minimum = std::min(minimum, value);
              maximum = std::max(maximum, value);
            }
          }
          ASSERT_GT(minimum, 0.0F);
          if (variant == 3U) {
            EXPECT_GT(maximum - minimum, 0.05F * maximum);
          } else {
            EXPECT_LE(maximum - minimum, (0.005F * maximum) + 2.0e-5F);
          }
          ++checked_images;
        }
      }
    }
    RecordProperty("projection_images", checked_images);
  }

  using FiniteEmitterImageTest = exposure::ExposureLightingGpuTest;

  NOLINT_TEST_F(
    FiniteEmitterImageTest, CenterSupportAndAnalyticHorizonAgreeAcrossProductionPaths)
  {
    auto node = scene->CreateNode("Finite source");
    unsigned cases = 0;
    for (unsigned scenario = 0; scenario < 3U; ++scenario) {
      const auto positions = std::array {
        glm::vec3 { 0, 0, 0 },
        glm::vec3 { 1, 0, -1.1F },
        glm::vec3 { 0.7F, 0, 0 },
      };
      node.GetTransform().SetLocalPosition(positions.at(scenario));
      node.GetTransform().SetLocalRotation(
        glm::quat { 0.70710678F, 0.70710678F, 0, 0 });
      std::array<double, 3> deferred {};
      for (const bool forward : { false, true }) {
        for (const auto domain : {
               data::MaterialDomain::kOpaque,
               data::MaterialDomain::kMasked,
               data::MaterialDomain::kAlphaBlended,
             }) {
          SetSurface(domain);
          for (const bool finite : { false, true }) {
            SCOPED_TRACE(scenario);
            SCOPED_TRACE(forward);
            SCOPED_TRACE(static_cast<int>(domain));
            SCOPED_TRACE(finite);
            if (scenario == 2U) {
              auto light = std::make_unique<scene::SpotLight>();
              light->SetRange(3.0F);
              light->SetInnerConeAngleRadians(0.2F);
              light->SetOuterConeAngleRadians(0.3F);
              light->SetSourceRadius(finite ? 0.6F : 0.0F);
              light->SetLuminousFluxLm(10.0F);
              light->Common().casts_shadows = false;
              if (node.GetLightAs<scene::SpotLight>().has_value()) {
                ASSERT_TRUE(node.ReplaceLight(std::move(light)));
              } else {
                node.DetachLight();
                ASSERT_TRUE(node.AttachLight(std::move(light)));
              }
            } else {
              auto light = std::make_unique<scene::PointLight>();
              light->SetRange(scenario == 0U ? 0.8F : 2.0F);
              light->SetSourceRadius(finite ? 0.5F : 0.0F);
              light->SetLuminousFluxLm(10.0F);
              light->Common().casts_shadows = false;
              if (node.GetLightAs<scene::PointLight>().has_value()) {
                ASSERT_TRUE(node.ReplaceLight(std::move(light)));
              } else {
                ASSERT_TRUE(node.AttachLight(std::move(light)));
              }
            }
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 3U));
            ASSERT_NE(probe->color, nullptr);
            const auto pixels = ReadFloatTexture(*probe->color);
            ASSERT_EQ(pixels.size(), 1U);
            const auto measured = pixels.at(0).at(0);
            ASSERT_TRUE(std::isfinite(measured));
            if (!finite || scenario != 1U) {
              EXPECT_EQ(measured, 0.0F);
            } else {
              EXPECT_GT(measured, 1.0e-6F);
              auto slot = 0U;
              if (domain == data::MaterialDomain::kMasked) {
                slot = 1U;
              } else if (domain == data::MaterialDomain::kAlphaBlended) {
                slot = 2U;
              }
              if (forward) {
                EXPECT_NEAR(measured, deferred.at(slot),
                  (0.02 * deferred.at(slot)) + 2.0e-5);
              } else {
                deferred.at(slot) = measured;
              }
            }
            ++cases;
          }
        }
      }
    }
    RecordProperty("finite_support_images", cases);
  }

  class FiniteEmitterShadowImageTest
    : public exposure::ExposureLightingGpuTest {
  protected:
    auto AdditionalCapabilities() const -> CapabilitySet override
    {
      return RendererCapabilityFamily::kShadowing;
    }
  };

  NOLINT_TEST_F(FiniteEmitterShadowImageTest,
    ProjectedAndCubeVisibilityReachForwardAndDeferredReceivers)
  {
    auto blocker = scene->CreateNode("Off-camera shadow blocker");
    blocker.GetRenderable().SetGeometry(
      mesh_node.GetRenderable().GetGeometry());
    blocker.GetRenderable().SetMaterialOverride(
      0, 0, MakeEmissiveMaterial(0.0F));
    blocker.GetTransform().SetLocalScale(glm::vec3 { 0.1F });
    blocker.GetTransform().SetLocalPosition(glm::vec3 { 0.5F, 0.0F, -0.4F });
    expected_draws = 2U;
    auto node = scene->CreateNode("Cube shadow source");
    node.GetTransform().SetLocalPosition(glm::vec3 { 1, 0, 0 });
    node.GetTransform().SetLocalRotation(
      glm::quat { 0.70710678F, 0.70710678F, 0, 0 });
    unsigned cases = 0;
    for (const unsigned source_kind : { 0U, 1U, 2U }) {
      const bool spot = source_kind != 0U;
      for (const bool forward : { false, true }) {
        for (const auto domain : {
               data::MaterialDomain::kOpaque,
               data::MaterialDomain::kMasked,
               data::MaterialDomain::kAlphaBlended,
             }) {
          SetSurface(domain);
          float baseline = 0.0F;
          for (const bool shadowed : { false, true }) {
            SCOPED_TRACE(spot);
            SCOPED_TRACE(forward);
            SCOPED_TRACE(static_cast<int>(domain));
            SCOPED_TRACE(shadowed);
            node.DetachLight();
            if (spot) {
              auto light = std::make_unique<scene::SpotLight>();
              light->SetInnerConeAngleRadians(0.25F);
              light->SetOuterConeAngleRadians(source_kind == 1U
                ? std::numbers::pi_v<float> / 2.0F : 1.2F);
              light->SetRange(3.0F);
              light->SetSourceRadius(0.1F);
              light->SetLuminousFluxLm(10.0F);
              light->Common().casts_shadows = shadowed;
              ASSERT_TRUE(node.AttachLight(std::move(light)));
            } else {
              auto light = std::make_unique<scene::PointLight>();
              light->SetRange(3.0F);
              light->SetSourceRadius(0.1F);
              light->SetLuminousFluxLm(10.0F);
              light->Common().casts_shadows = shadowed;
              ASSERT_TRUE(node.AttachLight(std::move(light)));
            }
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 3U));
            const auto pixels = ReadFloatTexture(*probe->color);
            ASSERT_EQ(pixels.size(), 1U);
            const auto measured = pixels.at(0).at(0);
            ASSERT_TRUE(std::isfinite(measured));
            if (shadowed) {
              EXPECT_LT(measured, 0.1F * baseline);
              mesh_node.GetFlags()->get().SetLocalValue(
                scene::SceneNodeFlags::kReceivesShadows, false);
              ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 2U));
              const auto unshadowed = ReadFloatTexture(*probe->color).at(0).at(0);
              EXPECT_NEAR(unshadowed, baseline, 0.005F * baseline + 2.0e-5F);
              mesh_node.GetFlags()->get().SetLocalValue(
                scene::SceneNodeFlags::kReceivesShadows, true);
            } else {
              ASSERT_GT(measured, 1.0e-6F);
              baseline = measured;
            }
            ++cases;
          }
        }
      }
    }
    RecordProperty("finite_shadow_images", cases);
  }

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
    auto authored = std::array<ForwardLocalLightRecord, light_count> {};
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
      // Resolve directly from the authored recipe, without renderer gathering,
      // publication, cluster ranges or selection-index buffers.
      const auto flux = reference::LuminousFluxLumens { 1.0F
        + (static_cast<float>(index) * 0.1F) };
      const auto compensation
        = reference::SourceExposureEv { static_cast<float>(index % 3U) - 1.0F };
      const auto intensity = index % 2U == 0U
        ? reference::ResolvePointIntensity(flux, compensation)
        : reference::ResolveSpotPeakIntensity(flux,
            {
              .inner = reference::InnerHalfAngleRadians { 0.2F },
              .outer = reference::OuterHalfAngleRadians { 1.2F },
            },
            compensation);
      ASSERT_TRUE(intensity.has_value());
      auto& record = authored.at(index);
      record.position_ws = position(FixtureLightIndex { index });
      record.range_m = 3.0F + (static_cast<float>(index % 3U) * 0.25F);
      record.intensity_rgb_cd = tint * static_cast<float>(intensity->get());
      record.emitted_direction_ws = { 0.0F, 0.0F, -1.0F };
      record.kind = static_cast<std::uint32_t>(index % 2U);
      if (record.kind == 1U) {
        const auto inner_cosine = static_cast<float>(std::cos(static_cast<double>(0.2F)));
        record.outer_cone_cosine = static_cast<float>(std::cos(static_cast<double>(1.2F)));
        record.inverse_cone_cosine_width = 1.0F / (inner_cosine - record.outer_cone_cosine);
      }
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
    std::uint64_t unculled_channels = 0U;
    double maximum_unculled_error = 0.0;
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
        auto strongest_index = FixtureLightIndex { 0U };
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
            strongest_index = FixtureLightIndex { index };
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
          auto full_list = std::vector<ForwardLocalLightRecord> {};
          full_list.reserve(count);
          for (std::size_t index = 0U; index < count; ++index) {
            full_list.push_back(
              authored.at(reversed ? light_count - 1U - index : index));
          }
          auto unculled = Image {};
          bool capture_finished = true;
          {
            probe->after_render
              = [&](const ViewRenderGpuContext& hook) -> void {
              RecordUnculledReference(hook.recorder,
                {
                  .lights = full_list,
                  .baseline = baseline,
                  .extent = { kWidth, kHeight },
                  .forward_shading
                  = forward || domain == data::MaterialDomain::kAlphaBlended,
                });
            };
            const auto clear_reference
              = ScopeGuard([&] noexcept -> void { probe->after_render = {}; });
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
          ASSERT_NO_FATAL_FAILURE(ReadUnculledReference(unculled));
          ASSERT_EQ(unculled.size(), pixel_count);
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
              const auto full_expected
                = static_cast<double>(unculled.at(pixel).at(channel));
              ASSERT_TRUE(std::isfinite(full_expected));
              const auto full_tolerance
                = (0.005 * std::abs(full_expected)) + 2.0e-5;
              const auto full_error = std::abs(actual - full_expected);
              EXPECT_LE(full_error, full_tolerance)
                << "unculled pixel=" << pixel << " channel=" << channel;
              maximum_unculled_error
                = std::max(maximum_unculled_error, full_error / full_tolerance);
              ++unculled_channels;
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
            // Deliberately omit a visible source from the renderer, keeping
            // the independent reference unchanged. A shared selection bug
            // must not make the two paths agree.
            auto missing_one = all;
            std::erase(missing_one, strongest_index);
            ASSERT_NO_FATAL_FAILURE(select(missing_one));
            auto omitted_image = Image {};
            ASSERT_NO_FATAL_FAILURE(render(omitted_image));
            unsigned rejected_channels = 0U;
            for (std::size_t pixel = 0U; pixel < pixel_count; ++pixel) {
              for (std::size_t channel = 0U; channel < 3U; ++channel) {
                const auto expected
                  = static_cast<double>(unculled.at(pixel).at(channel));
                const auto error
                  = std::abs(omitted_image.at(pixel).at(channel) - expected);
                rejected_channels
                  += error > (0.005 * std::abs(expected)) + 2.0e-5 ? 1U : 0U;
              }
            }
            EXPECT_GT(rejected_channels, 0U);
            ASSERT_NO_FATAL_FAILURE(select(all));
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
    RecordProperty("unculled_channels", unculled_channels);
    RecordProperty(
      "maximum_unculled_fraction_of_image_budget", maximum_unculled_error);
  }
} // namespace
} // namespace oxygen::vortex::testing
