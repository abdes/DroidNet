//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/SceneRenderer/DepthPrePassPolicy.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderPayloads.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>

namespace oxygen::vortex::testing::exposure {

using graphics::ResourceStates;

namespace {
  struct RoughWhiteResponse {
    double direct;
    double diffuse_environment;
  };

  // Independent roughness=1, NoV=NoL=1 raster model-2 response. The directional
  // moments come from the certified reference, not a production lookup. The
  // reciprocal multi-scattering comparison is not the EX07 raster contract.
  auto EvaluateRoughWhiteResponse(const double f0) -> RoughWhiteResponse
  {
    constexpr double energy = 0.3068528194400547;
    constexpr double bias = 3.3614294725518486e-5;
    const double compensation = 1.0 + f0 * (1.0 / energy - 1.0);
    const double transmission
      = 1.0 - compensation * ((f0 * energy) + ((1.0 - f0) * bias));
    return {
      .direct = ((0.25 * f0 * compensation) + transmission) / std::numbers::pi,
      .diffuse_environment = transmission,
    };
  }
} // namespace

NOLINT_TEST_F(
  ExposureLightingGpuTest, DirectLightRadiancePreservesSupportedRange)
{
  auto sun = scene->CreateNode("Directional");
  auto directional = std::make_unique<scene::DirectionalLight>();
  directional->Common().casts_shadows = false;
  directional->Common().color_rgb = {
    .25F,
    .5F,
    1.0F,
  };
  directional->SetAtmosphereLightSlot(
    oxygen::scene::AtmosphereLightSlot::kPrimary);
  ASSERT_TRUE(sun.AttachLight(std::move(directional)));
  sun.GetTransform().SetLocalRotation(glm::quat {
    .70710678F,
    .70710678F,
    0,
    0,
  });
  auto point_node = scene->CreateNode("Point");
  auto point = std::make_unique<scene::PointLight>();
  point->Common().casts_shadows = false;
  point->Common().color_rgb = {
    .25F,
    .5F,
    1.0F,
  };
  point->SetRange(100);
  ASSERT_TRUE(point_node.AttachLight(std::move(point)));
  auto spot_node = scene->CreateNode("Spot");
  auto spot = std::make_unique<scene::SpotLight>();
  spot->Common().casts_shadows = false;
  spot->Common().color_rgb = {
    .25F,
    .5F,
    1.0F,
  };
  spot->SetRange(100);
  ASSERT_TRUE(spot_node.AttachLight(std::move(spot)));
  spot_node.GetTransform().SetLocalRotation(glm::quat {
    .70710678F,
    .70710678F,
    0,
    0,
  });
  unsigned cases = 0;
  for (const bool forward : {
         false,
         true,
       }) {
    for (const auto domain : {
           data::MaterialDomain::kOpaque,
           data::MaterialDomain::kMasked,
           data::MaterialDomain::kAlphaBlended,
         }) {
      SetSurface(domain);
      const bool forward_shader
        = forward || domain == data::MaterialDomain::kAlphaBlended;
      const double coverage
        = domain == data::MaterialDomain::kAlphaBlended ? .5 : 1;
      const double pi = std::numbers::pi;
      const double brdf = EvaluateRoughWhiteResponse(0.04).direct;
      // The packed specular half-code interval is input uncertainty, separate
      // from the unchanged floating-point transport tolerance.
      const double brdf_error = forward_shader
        ? 0.0
        : std::max(
            std::abs(
              EvaluateRoughWhiteResponse(0.04 - (0.04 / 255.0)).direct - brdf),
            std::abs(
              EvaluateRoughWhiteResponse(0.04 + (0.04 / 255.0)).direct - brdf));
      for (unsigned kind = 0; kind < 3; ++kind) {
        auto directional_light = sun.GetLightAs<scene::DirectionalLight>();
        auto point_light = point_node.GetLightAs<scene::PointLight>();
        auto spot_light = spot_node.GetLightAs<scene::SpotLight>();
        if (!directional_light.has_value()) {
          FAIL() << "Expected directional_light to contain a value";
        }
        if (!point_light.has_value()) {
          FAIL() << "Expected point_light to contain a value";
        }
        if (!spot_light.has_value()) {
          FAIL() << "Expected spot_light to contain a value";
        }
        ASSERT_TRUE(sun.EditLight<scene::DirectionalLight>(
          [kind](auto& light) { light.Common().affects_world = kind == 0; }));
        ASSERT_TRUE(point_node.EditLight<scene::PointLight>(
          [kind](auto& light) { light.Common().affects_world = kind == 1; }));
        ASSERT_TRUE(spot_node.EditLight<scene::SpotLight>(
          [kind](auto& light) { light.Common().affects_world = kind == 2; }));
        directional_light = sun.GetLightAs<scene::DirectionalLight>();
        point_light = point_node.GetLightAs<scene::PointLight>();
        spot_light = spot_node.GetLightAs<scene::SpotLight>();
        if (!directional_light.has_value()) {
          FAIL() << "Expected directional_light to contain a value";
        }
        // The receiver is one metre from the local source, with 100 m range.
        // Integrate the authored cosine-domain cone independently of the
        // production squared-half-angle representation and resolve lm -> cd.
        double attenuation = std::pow(1 - 1e-8, 2);
        double solid_angle = 4 * pi;
        if (kind == 0) {
          attenuation = 1;
          solid_angle = 1;
        } else if (kind == 2) {
          const double inner_cos = std::cos(
            static_cast<double>(spot_light->get().GetInnerConeAngleRadians()));
          const double outer_cos = std::cos(
            static_cast<double>(spot_light->get().GetOuterConeAngleRadians()));
          solid_angle
            = 2 * pi * ((1 - inner_cos) + ((inner_cos - outer_cos) / 3));
        }
        const double coefficient = brdf * attenuation / solid_angle;
        for (const double radiance : {
               0.0,
               0x1p-24,
               .25,
               131072.0,
               0x1p32 * .999,
               0x1p35,
             }) {
          for (const float ev : {
                 -32.0F,
                 0.0F,
                 32.0F,
               }) {
            SCOPED_TRACE(forward);
            SCOPED_TRACE(static_cast<int>(domain));
            SCOPED_TRACE(kind);
            SCOPED_TRACE(radiance);
            SCOPED_TRACE(ev);
            const auto intensity = static_cast<float>(radiance / coefficient);
            ASSERT_TRUE(sun.EditLight<scene::DirectionalLight>(
              [intensity](auto& light) { light.SetIntensityLux(intensity); }));
            ASSERT_TRUE(
              point_node.EditLight<scene::PointLight>([intensity](auto& light) {
                light.SetLuminousFluxLm(intensity);
              }));
            ASSERT_TRUE(
              spot_node.EditLight<scene::SpotLight>([intensity](auto& light) {
                light.SetLuminousFluxLm(intensity);
              }));
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, ev));
            const auto pixels = ReadFloatTexture(*probe->color);
            ASSERT_EQ(pixels.size(), 1U);
            for (unsigned channel = 0; channel < 3; ++channel) {
              const double expected = intensity * coefficient * coverage
                * std::exp2(static_cast<double>(channel) - 2 - ev);
              EXPECT_NEAR(pixels.at(0).at(channel), expected,
                (std::abs(expected) * (2e-5 + (brdf_error / brdf))) + 0x1p-120);
            }
            EXPECT_FLOAT_EQ(pixels.at(0).at(3), static_cast<float>(coverage));
            const auto status = Read<ExposureCompletedStatus>(
              *probe->exposure->current_state->status_buffer,
              ResourceStates::kCopySource);
            if (radiance > 0x1p32) {
              EXPECT_EQ(status.flags & 18U, 18U);
              EXPECT_NE(status.first_failure_kind & 32U, 0U);
              EXPECT_EQ(status.first_failure_product,
                domain == data::MaterialDomain::kAlphaBlended || forward ? 4U
                                                                         : 2U);
            } else {
              EXPECT_EQ(status.flags & 16U, 0U);
            }
            ++cases;
          }
        }
      }
    }
  }
  RecordProperty("direct_light_endpoint_cases", cases);
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  SourceFailureSurvivesPositiveLightingAndPreservesAutoHistory)
{
  auto point_node = scene->CreateNode("Cancellation control");
  auto light = std::make_unique<scene::PointLight>();
  light->Common().casts_shadows = false;
  light->SetRange(100);
  light->SetLuminousFluxLm(100);
  ASSERT_TRUE(point_node.AttachLight(std::move(light)));
  auto fill_node = scene->CreateNode("Positive fill");
  auto fill = std::make_unique<scene::PointLight>();
  fill->Common().casts_shadows = false;
  fill->SetRange(100);
  ASSERT_TRUE(fill_node.AttachLight(std::move(fill)));
  settings.mode = engine::ExposureMode::kAuto;
  frame_delta_seconds = .1F;
  unsigned cases = 0;
  for (const bool prepass : {
         true,
         false,
       }) {
    for (const bool invalid_light : {
           false,
           true,
         }) {
      for (const bool forward : {
             false,
             true,
           }) {
        for (const auto domain : {
               data::MaterialDomain::kOpaque,
               data::MaterialDomain::kMasked,
               data::MaterialDomain::kAlphaBlended,
             }) {
          SCOPED_TRACE(forward);
          SCOPED_TRACE(static_cast<int>(domain));
          SCOPED_TRACE(invalid_light);
          SCOPED_TRACE(prepass);
          depth_mode = prepass ? DepthPrePassMode::kOpaqueAndMasked
                               : DepthPrePassMode::kDisabled;
          ASSERT_TRUE(point_node.EditLight<scene::PointLight>(
            [invalid_light](auto& light) {
              light.SetLuminousFluxLm(invalid_light ? 0.0F : 100.0F);
            }));
          ASSERT_TRUE(fill_node.EditLight<scene::PointLight>(
            [invalid_light](auto& light) {
              light.SetLuminousFluxLm(invalid_light ? 200.0F : 0.0F);
            }));
          SetSurface(domain, 1);
          ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
          const auto before
            = Read<ExposureStateData>(*probe->exposure->current_state->buffer,
              ResourceStates::kShaderResource);
          auto status = Read<ExposureCompletedStatus>(
            *probe->exposure->current_state->status_buffer,
            ResourceStates::kCopySource);
          EXPECT_EQ(status.flags & 16U, 0U);
          EXPECT_EQ(before.flags & 12U, 12U);
          if (invalid_light) {
            const auto previous_exposure = probe->exposure;
            // Deliberately bypass ingress to exercise the renderer's defensive
            // failure gate.
            point_node.GetImpl()
              ->get()
              .GetComponent<scene::PointLight>()
              .SetLuminousFluxLm(-100.0F);
            ASSERT_NO_FATAL_FAILURE(
              RenderSurface(forward, 0, 1, ExpectedViewOutcome::kRejected));
            const auto retained = Read<ExposureStateData>(
              *previous_exposure->current_state->buffer,
              ResourceStates::kShaderResource);
            EXPECT_EQ(retained.displayed_scale, before.displayed_scale);
            EXPECT_EQ(retained.latent_scale, before.latent_scale);
            EXPECT_EQ(retained.flags, before.flags);
            ++cases;
            if (domain != data::MaterialDomain::kOpaque) {
              // Scene-light validation is independent of material visibility:
              // masked-out receivers cannot legitimize an invalid source.
              SetSurface(domain, 1.0F, true);
              ASSERT_NO_FATAL_FAILURE(
                RenderSurface(forward, 0, 1, ExpectedViewOutcome::kRejected));
              ++cases;
            }
            continue;
          }
          SetSurface(domain, -1);
          ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
          const auto pixels = ReadFloatTexture(*probe->color);
          ASSERT_EQ(pixels.size(), 1U);
          for (unsigned c = 0; c < 3; ++c) {
            ASSERT_TRUE(std::isfinite(pixels.at(0).at(c)));
            EXPECT_GT(pixels.at(0).at(c), 0);
          }
          status = Read<ExposureCompletedStatus>(
            *probe->exposure->current_state->status_buffer,
            ResourceStates::kCopySource);
          EXPECT_EQ(status.flags & 18U, 18U);
          EXPECT_EQ(status.first_failure_kind & 32U, 32U);
          EXPECT_EQ(status.first_failure_product,
            forward || domain == data::MaterialDomain::kAlphaBlended ? 4U
              : invalid_light                                        ? 2U
                                                                     : 1U);
          const auto after
            = Read<ExposureStateData>(*probe->exposure->current_state->buffer,
              ResourceStates::kShaderResource);
          EXPECT_EQ(after.displayed_scale, before.displayed_scale);
          EXPECT_EQ(after.latent_scale, before.latent_scale);
          EXPECT_EQ(after.flags & 12U, 0U);
          EXPECT_EQ(after.flags & 32U, 32U);
          ++cases;
          if (domain != data::MaterialDomain::kOpaque) {
            SetSurface(domain, invalid_light ? 1.0F : -1.0F, true);
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
            const auto empty = ReadFloatTexture(*probe->color);
            ASSERT_EQ(empty.size(), 1U);
            EXPECT_EQ(empty.at(0),
              (Pixel {
                0,
                0,
                0,
                0,
              }));
            status = Read<ExposureCompletedStatus>(
              *probe->exposure->current_state->status_buffer,
              ResourceStates::kCopySource);
            EXPECT_EQ(status.flags & 16U, 0U);
            ++cases;
          }
        }
      }
    }
  }
  RecordProperty("source_cancellation_and_rejection_cases", cases);
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, EmissiveFailureRespectsFinishedDepthAndMaskHoles)
{
  auto blocker = scene->CreateNode("Foreground");
  blocker.GetRenderable().SetGeometry(mesh_node.GetRenderable().GetGeometry());
  blocker.GetTransform().SetLocalPosition({
    0,
    0,
    .25F,
  });
  expected_draws = 2;
  settings.mode = engine::ExposureMode::kAuto;
  const auto set_blocker = [&](data::MaterialDomain domain, float emission,
                             bool hole = false) -> void {
    const auto saved = mesh_node;
    mesh_node = blocker;
    SetSurface(domain, emission, hole);
    mesh_node = saved;
  };
  unsigned cases = 0;
  for (const bool prepass : {
         true,
         false,
       }) {
    for (const bool forward : {
           false,
           true,
         }) {
      for (const auto domain : {
             data::MaterialDomain::kOpaque,
             data::MaterialDomain::kMasked,
           }) {
        for (const bool negative_first : {
               true,
               false,
             }) {
          SCOPED_TRACE(prepass);
          SCOPED_TRACE(forward);
          SCOPED_TRACE(static_cast<int>(domain));
          SCOPED_TRACE(negative_first);
          depth_mode = prepass ? DepthPrePassMode::kOpaqueAndMasked
                               : DepthPrePassMode::kDisabled;
          mesh_node.GetTransform().SetLocalPosition({
            0,
            0,
            0,
          });
          SetSurface(domain, negative_first ? -1.0F : 1.0F);
          blocker.GetRenderable().SetMaterialOverride(
            0, 0, mesh_node.GetRenderable().ResolveSubmeshMaterial(0, 0));
          ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
          // Register one material first, then the other, to exercise each
          // actual material-sorted raster order; assert the resulting order.
          if (negative_first) {
            set_blocker(domain, 1);
          } else {
            SetSurface(domain, -1);
          }
          ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
          ASSERT_EQ(probe->raster_depths.size(), 2U);
          if (probe->raster_depths.at(0) != (negative_first ? 0.0F : .25F)) {
            // Keep the image identical while reversing which scene node owns
            // each surface; verify the renderer's resulting raster order.
            const auto negative
              = mesh_node.GetRenderable().ResolveSubmeshMaterial(0, 0);
            const auto positive
              = blocker.GetRenderable().ResolveSubmeshMaterial(0, 0);
            mesh_node.GetRenderable().SetMaterialOverride(0, 0, positive);
            blocker.GetRenderable().SetMaterialOverride(0, 0, negative);
            mesh_node.GetTransform().SetLocalPosition({
              0,
              0,
              .25F,
            });
            blocker.GetTransform().SetLocalPosition({
              0,
              0,
              0,
            });
            std::swap(mesh_node, blocker);
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
            ASSERT_EQ(probe->raster_depths.size(), 2U);
          }
          EXPECT_EQ(probe->early_depth_complete, prepass);
          EXPECT_EQ(probe->raster_depths.at(0), negative_first ? 0.0F : .25F);
          const auto pixels = ReadFloatTexture(*probe->color);
          ASSERT_EQ(pixels.size(), 1U);
          EXPECT_GT(pixels.at(0).at(0), 0);
          auto status = Read<ExposureCompletedStatus>(
            *probe->exposure->current_state->status_buffer,
            ResourceStates::kCopySource);
          EXPECT_EQ(status.flags & 16U, 0U);
          const auto meter
            = Read<ExposureStateData>(*probe->exposure->current_state->buffer,
              ResourceStates::kShaderResource);
          EXPECT_EQ(meter.flags & 12U, 12U);
          ++cases;
          mesh_node.GetTransform().SetLocalPosition({
            0,
            0,
            .5F,
          });
          ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
          ASSERT_EQ(probe->raster_depths.size(), 2U);
          status = Read<ExposureCompletedStatus>(
            *probe->exposure->current_state->status_buffer,
            ResourceStates::kCopySource);
          EXPECT_EQ(status.flags & 18U, 18U);
          EXPECT_EQ(status.first_failure_kind & 32U, 32U);
          EXPECT_EQ(status.first_failure_product, forward ? 4U : 1U);
          ++cases;
          if (domain == data::MaterialDomain::kMasked) {
            mesh_node.GetTransform().SetLocalPosition({
              0,
              0,
              0,
            });
            set_blocker(domain, 1, true);
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
            ASSERT_EQ(probe->raster_depths.size(), 2U);
            status = Read<ExposureCompletedStatus>(
              *probe->exposure->current_state->status_buffer,
              ResourceStates::kCopySource);
            EXPECT_EQ(status.flags & 18U, 18U);
            EXPECT_EQ(status.first_failure_product, forward ? 4U : 1U);
            ++cases;
          }
        }
      }
    }
  }
  RecordProperty("emissive_depth_visibility_cases", cases);
}

NOLINT_TEST_F(ExposureLightingGpuTest, StaticSkyDiffusePreservesSupportedRange)
{
  auto& sky
    = scene->GetEnvironment()->AddSystem<scene::environment::SkyLight>();
  sky.SetEnabled(true);
  sky.SetSource(scene::environment::SkyLightSource::kSpecifiedCubemap);
  sky.SetLowerHemisphereIsSolidColor(false);
  sky.SetSpecularIntensity(0);
  sky.SetDiffuseIntensity(1);
  unsigned cases = 0;
  for (const Pixel source_color : {
         Pixel {
           .25F,
           .5F,
           1,
           1,
         },
         Pixel {
           0x1p-24F,
           .25F,
           0x1p30F,
           1,
         },
       }) {
    data::pak::core::TextureResourceDesc desc {};
    desc.texture_type = static_cast<std::uint8_t>(TextureType::kTextureCube);
    desc.width = desc.height = desc.depth = desc.mip_levels = 1;
    desc.array_layers = 6;
    desc.format = static_cast<std::uint8_t>(Format::kRGBA32Float);
    desc.alignment = 256;
    const auto key = owned_asset_loader_->MintSyntheticTextureKey();
    desc.content_hash = key.get();
    static_assert(
      6U * sizeof(Pixel) <= std::numeric_limits<std::uint32_t>::max());
    std::vector<std::uint8_t> data_region(6 * sizeof(Pixel));
    std::vector<data::pak::render::SubresourceLayout> layouts;
    for (unsigned face = 0; face < 6; ++face) {
      std::memcpy(
        std::span {
          data_region,
        }
          .subspan(face * sizeof(Pixel), sizeof(Pixel))
          .data(),
        source_color.data(), sizeof(Pixel));
      layouts.push_back({
        .offset_bytes = static_cast<std::uint32_t>(face * sizeof(Pixel)),
        .row_pitch_bytes = sizeof(Pixel),
        .size_bytes = sizeof(Pixel),
      });
    }
    auto payload = vortex::testing::detail::BuildV4TexturePayload(
      desc, layouts, data_region);
    desc.size_bytes = static_cast<std::uint32_t>(payload.size());
    owned_asset_loader_->SetTexture(
      key, std::make_shared<data::TextureResource>(desc, std::move(payload)));
    sky.SetCubemapResource(key);
    for (const bool forward : {
           false,
           true,
         }) {
      for (const auto domain : {
             data::MaterialDomain::kOpaque,
             data::MaterialDomain::kMasked,
             data::MaterialDomain::kAlphaBlended,
           }) {
        SetSurface(domain);
        const double coverage
          = domain == data::MaterialDomain::kAlphaBlended ? .5 : 1;
        for (const float multiplier : {
               0.0F,
               0x1p-24F,
               1.0F,
               0x1p32F * .999F,
               0x1p35F,
             }) {
          for (const float ev : {
                 -32.0F,
                 0.0F,
                 32.0F,
               }) {
            SCOPED_TRACE(source_color.at(2));
            SCOPED_TRACE(forward);
            SCOPED_TRACE(static_cast<int>(domain));
            SCOPED_TRACE(multiplier);
            SCOPED_TRACE(ev);
            sky.SetIntensityMul(multiplier);
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, ev));
            const auto pixels = ReadFloatTexture(*probe->color);
            ASSERT_EQ(pixels.size(), 1U);
            const double diffuse_response
              = EvaluateRoughWhiteResponse(0.04).diffuse_environment;
            const bool forward_shader
              = forward || domain == data::MaterialDomain::kAlphaBlended;
            const double input_error = forward_shader
              ? 0.0
              : std::max(
                  std::abs(EvaluateRoughWhiteResponse(0.04 - (0.04 / 255.0))
                             .diffuse_environment
                    - diffuse_response),
                  std::abs(EvaluateRoughWhiteResponse(0.04 + (0.04 / 255.0))
                             .diffuse_environment
                    - diffuse_response));
            for (unsigned channel = 0; channel < 3; ++channel) {
              const double expected
                = static_cast<double>(source_color.at(channel)) * multiplier
                * coverage * diffuse_response
                * std::exp2(-static_cast<double>(ev));
              EXPECT_NEAR(pixels.at(0).at(channel), expected,
                (std::abs(expected) * (2e-5 + (input_error / diffuse_response)))
                  + 0x1p-120);
            }
            EXPECT_FLOAT_EQ(pixels.at(0).at(3), static_cast<float>(coverage));
            const auto status = Read<ExposureCompletedStatus>(
              *probe->exposure->current_state->status_buffer,
              ResourceStates::kCopySource);
            if (static_cast<double>(source_color.at(2)) * multiplier > 0x1p32) {
              EXPECT_EQ(status.flags & 18U, 18U);
              EXPECT_NE(status.first_failure_kind & 32U, 0U);
              EXPECT_EQ(status.first_failure_product,
                domain == data::MaterialDomain::kAlphaBlended || forward ? 4U
                                                                         : 3U);
            } else {
              EXPECT_EQ(status.flags & 16U, 0U);
            }
            ++cases;
          }
        }
      }
    }
  }
  RecordProperty("static_sky_endpoint_cases", cases);
}

} // namespace oxygen::vortex::testing::exposure
