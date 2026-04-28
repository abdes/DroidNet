//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Console/Command.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/LocalFogVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneTraversal.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Environment/EnvironmentLightingService.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereLightState.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Passes/IblProbePass.h>
#include <Oxygen/Vortex/Environment/Types/AtmosphereLightModel.h>
#include <Oxygen/Vortex/Environment/Types/AtmosphereModel.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentAmbientBridgeBindings.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentEvaluationParameters.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentProbeBindings.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentProbeState.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentViewProducts.h>
#include <Oxygen/Vortex/Environment/Types/HeightFogModel.h>
#include <Oxygen/Vortex/Environment/Types/SkyLightEnvironmentModel.h>
#include <Oxygen/Vortex/Environment/Types/VolumetricFogModel.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Types/EnvironmentFrameBindings.h>
#include <Oxygen/Vortex/Types/EnvironmentStaticData.h>
#include <Oxygen/Vortex/ViewFeatureProfile.h>

#include "Fakes/Graphics.h"

namespace {

using oxygen::Graphics;
using oxygen::kInvalidShaderVisibleIndex;
using oxygen::RendererConfig;
using oxygen::ResolvedView;
using oxygen::ViewId;
using oxygen::ViewPort;
using oxygen::vortex::EnvironmentAmbientBridgeBindings;
using oxygen::vortex::EnvironmentEvaluationParameters;
using oxygen::vortex::EnvironmentFrameBindings;
using oxygen::vortex::EnvironmentLightingService;
using oxygen::vortex::EnvironmentProbeBindings;
using oxygen::vortex::EnvironmentProbeState;
using oxygen::vortex::EnvironmentStaticData;
using oxygen::vortex::kGpuFogFlagCubemapAuthored;
using oxygen::vortex::kGpuFogFlagCubemapUsable;
using oxygen::vortex::kGpuFogFlagDirectionalInscattering;
using oxygen::vortex::kGpuFogFlagEnabled;
using oxygen::vortex::kGpuFogFlagHeightFogEnabled;
using oxygen::vortex::kGpuFogFlagHoldout;
using oxygen::vortex::kGpuFogFlagRenderInMainPass;
using oxygen::vortex::kGpuFogFlagVisibleInRealTimeSkyCaptures;
using oxygen::vortex::kGpuFogFlagVisibleInReflectionCaptures;
using oxygen::vortex::kGpuVolumetricFogFlagEnabled;
using oxygen::vortex::kGpuVolumetricFogFlagIntegratedScatteringValid;
using oxygen::vortex::RenderContext;
using oxygen::vortex::Renderer;
using oxygen::vortex::RendererCapabilityFamily;
using oxygen::vortex::SceneTextures;
using oxygen::vortex::SceneTexturesConfig;
using oxygen::vortex::environment::AtmosphereLightModel;
using oxygen::vortex::environment::AtmosphereModel;
using oxygen::vortex::environment::EnvironmentViewProducts;
using oxygen::vortex::environment::HeightFogModel;
using oxygen::vortex::environment::IblProbePass;
using oxygen::vortex::environment::kAtmosphereLightSlotCount;
using oxygen::vortex::environment::kInvalidAtmosphereLightSlot;
using oxygen::vortex::environment::SkyLightEnvironmentModel;
using oxygen::vortex::environment::VolumetricFogModel;
using oxygen::vortex::testing::FakeGraphics;

auto DestroyRenderer(Renderer* renderer) -> void
{
  if (renderer != nullptr) {
    renderer->OnShutdown();
    std::default_delete<Renderer> {}(renderer);
  }
}

auto MakeRenderer(const std::shared_ptr<FakeGraphics>& graphics)
  -> std::shared_ptr<Renderer>
{
  auto config = RendererConfig {};
  config.upload_queue_key
    = graphics->QueueKeyFor(oxygen::graphics::QueueRole::kGraphics).get();
  constexpr auto kCapabilities = RendererCapabilityFamily::kEnvironmentLighting;
  return { new Renderer(std::weak_ptr<Graphics>(graphics), std::move(config),
             kCapabilities),
    DestroyRenderer };
}

auto MakeResolvedView(const float width, const float height,
  const float top_left_x = 0.0F, const float top_left_y = 0.0F,
  const glm::vec3 camera_position = glm::vec3(0.0F, 0.0F, 0.0F),
  const glm::mat4 view_matrix = glm::mat4(1.0F),
  const glm::mat4 projection_matrix = glm::mat4(1.0F)) -> ResolvedView
{
  auto params = ResolvedView::Params {};
  params.view_config.viewport = ViewPort {
    .top_left_x = top_left_x,
    .top_left_y = top_left_y,
    .width = width,
    .height = height,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  params.view_matrix = view_matrix;
  params.proj_matrix = projection_matrix;
  params.camera_position = camera_position;
  params.near_plane = 0.1F;
  params.far_plane = 1000.0F;
  return ResolvedView(params);
}

auto MakeRenderContext(const ViewId view_id, const ResolvedView& resolved_view,
  const oxygen::vortex::CompositionView& composition_view) -> RenderContext
{
  auto ctx = RenderContext {};
  ctx.frame_slot = oxygen::frame::Slot { 0U };
  ctx.frame_sequence = oxygen::frame::SequenceNumber { 1U };
  ctx.active_view_index = std::size_t { 0U };
  ctx.frame_views.push_back({
    .view_id = view_id,
    .is_scene_view = true,
    .composition_view
    = oxygen::observer_ptr<const oxygen::vortex::CompositionView> {
      &composition_view,
    },
    .shading_mode_override = {},
    .resolved_view = oxygen::observer_ptr<const ResolvedView> { &resolved_view },
    .primary_target = {},
  });
  ctx.current_view.view_id = view_id;
  ctx.current_view.exposure_view_id = view_id;
  ctx.current_view.composition_view
    = oxygen::observer_ptr<const oxygen::vortex::CompositionView> {
        &composition_view,
      };
  ctx.current_view.resolved_view
    = oxygen::observer_ptr<const ResolvedView> { &resolved_view };
  ctx.current_view.with_atmosphere = composition_view.with_atmosphere;
  ctx.current_view.with_height_fog = composition_view.with_height_fog;
  ctx.current_view.with_local_fog = composition_view.with_local_fog;
  ctx.current_view.feature_profile = composition_view.feature_profile;
  ctx.current_view.feature_mask = composition_view.feature_mask;
  return ctx;
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  EnvironmentFrameBindingsExposePerViewProbeEvaluationAndBoundedBridgeState)
{
  const auto bindings = EnvironmentFrameBindings {};

  EXPECT_EQ(bindings.environment_static_slot, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.environment_view_slot, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.atmosphere_model_slot, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.height_fog_model_slot, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.sky_light_model_slot, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.volumetric_fog_model_slot, kInvalidShaderVisibleIndex);
  EXPECT_EQ(
    bindings.environment_view_products_slot, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.contract_flags, 0U);
  EXPECT_EQ(bindings.transmittance_lut_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.multi_scattering_lut_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.sky_view_lut_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.camera_aerial_perspective_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.probes.environment_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.probes.irradiance_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.probes.prefiltered_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.probes.brdf_lut_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.probes.probe_revision, 0U);
  EXPECT_FLOAT_EQ(bindings.evaluation.ambient_intensity, 1.0F);
  EXPECT_FLOAT_EQ(bindings.evaluation.average_brightness, 1.0F);
  EXPECT_FLOAT_EQ(bindings.evaluation.blend_fraction, 0.0F);
  EXPECT_EQ(bindings.evaluation.flags, 0U);
  EXPECT_EQ(
    bindings.ambient_bridge.irradiance_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_FLOAT_EQ(bindings.ambient_bridge.ambient_intensity, 1.0F);
  EXPECT_FLOAT_EQ(bindings.ambient_bridge.average_brightness, 1.0F);
  EXPECT_FLOAT_EQ(bindings.ambient_bridge.blend_fraction, 0.0F);
  EXPECT_EQ(bindings.ambient_bridge.flags, 0U);
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  PersistentProbeStateRemainsSeparateFromPerViewEnvironmentPublication)
{
  const auto state = EnvironmentProbeState {};
  const auto bindings = EnvironmentFrameBindings {};

  EXPECT_FALSE(state.valid);
  EXPECT_EQ(state.flags, 0U);
  EXPECT_EQ(state.probes.environment_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(state.probes.irradiance_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(state.probes.prefiltered_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(state.probes.brdf_lut_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(state.probes.probe_revision, 0U);

  EXPECT_EQ(bindings.probes.probe_revision, state.probes.probe_revision);
  EXPECT_EQ(sizeof(EnvironmentProbeBindings), sizeof(bindings.probes));
  EXPECT_NE(static_cast<const void*>(&state.probes),
    static_cast<const void*>(&bindings.probes));
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  AmbientBridgeBindingsStayNarrowAndDoNotIntroduceStage13PolicyFields)
{
  const auto bridge = EnvironmentAmbientBridgeBindings {};
  const auto evaluation = EnvironmentEvaluationParameters {};

  EXPECT_EQ(bridge.irradiance_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_FLOAT_EQ(bridge.ambient_intensity, evaluation.ambient_intensity);
  EXPECT_FLOAT_EQ(bridge.average_brightness, evaluation.average_brightness);
  EXPECT_FLOAT_EQ(bridge.blend_fraction, evaluation.blend_fraction);
  EXPECT_EQ(bridge.flags, 0U);
  EXPECT_EQ(sizeof(EnvironmentAmbientBridgeBindings),
    2U * sizeof(std::uint32_t) + 3U * sizeof(float));
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  EnvironmentLightingServiceIsConcreteNonPlaceholderSubsystemSurface)
{
  EXPECT_TRUE((std::is_class_v<EnvironmentLightingService>));
  EXPECT_TRUE((std::is_destructible_v<EnvironmentLightingService>));
  EXPECT_TRUE((std::is_standard_layout_v<EnvironmentProbeBindings>));
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  VortexEnvironmentContractsExposeFutureAtmosphereFogAndSkyLightModelTypes)
{
  const auto atmosphere = AtmosphereModel {};
  const auto atmosphere_light = AtmosphereLightModel {};
  const auto height_fog = HeightFogModel {};
  const auto sky_light = SkyLightEnvironmentModel {};
  const auto volumetric_fog = VolumetricFogModel {};
  const auto view_products = EnvironmentViewProducts {};

  EXPECT_FALSE(atmosphere.enabled);
  EXPECT_FALSE(atmosphere_light.enabled);
  EXPECT_TRUE(height_fog.enable_height_fog);
  EXPECT_FALSE(height_fog.enable_volumetric_fog);
  EXPECT_FALSE(sky_light.enabled);
  EXPECT_FALSE(volumetric_fog.enabled);
  EXPECT_EQ(view_products.atmosphere_lights.size(), kAtmosphereLightSlotCount);
  EXPECT_FALSE(view_products.atmosphere_lights[0].enabled);
  EXPECT_FALSE(view_products.atmosphere_lights[1].enabled);
  EXPECT_EQ(view_products.atmosphere_light_count, 0U);
  EXPECT_EQ(view_products.conventional_shadow_authority_slot,
    kInvalidAtmosphereLightSlot);
  EXPECT_EQ(view_products.transmittance_lut_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(view_products.multi_scattering_lut_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(view_products.sky_view_lut_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(
    view_products.camera_aerial_perspective_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(
    view_products.distant_sky_light_lut_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(
    view_products.integrated_light_scattering_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(view_products.flags
      & oxygen::vortex::environment::
        kEnvironmentViewProductFlagSkyLightAuthoredEnabled,
    0U);
  EXPECT_EQ(view_products.flags
      & oxygen::vortex::environment::
        kEnvironmentViewProductFlagSkyLightIblValid,
    0U);
  EXPECT_EQ(view_products.flags
      & oxygen::vortex::environment::
        kEnvironmentViewProductFlagIntegratedLightScatteringValid,
    0U);
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  EnvironmentStaticSkyLightDefaultsExposeNoUsableIblSlots)
{
  const auto static_data = EnvironmentStaticData {};

  EXPECT_EQ(static_data.sky_light.enabled, 0U);
  EXPECT_EQ(static_data.sky_light.cubemap_slot, oxygen::kInvalidBindlessIndex);
  EXPECT_EQ(static_data.sky_light.brdf_lut_slot, oxygen::kInvalidBindlessIndex);
  EXPECT_EQ(
    static_data.sky_light.irradiance_map_slot, oxygen::kInvalidBindlessIndex);
  EXPECT_EQ(
    static_data.sky_light.prefilter_map_slot, oxygen::kInvalidBindlessIndex);
  EXPECT_EQ(static_data.sky_light.ibl_generation, 0U);
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  EnvironmentStaticHeightFogDefaultsExposeInactiveFogPayload)
{
  const auto static_data = EnvironmentStaticData {};

  EXPECT_EQ(sizeof(static_data.fog), 128U);
  EXPECT_EQ(sizeof(static_data.volumetric_fog), 96U);
  EXPECT_EQ(static_data.fog.flags, 0U);
  EXPECT_EQ(static_data.fog.primary_density, 0.0F);
  EXPECT_EQ(static_data.fog.secondary_density, 0.0F);
  EXPECT_EQ(static_data.fog.cubemap_srv, oxygen::kInvalidBindlessIndex);
  EXPECT_FLOAT_EQ(static_data.fog.max_opacity, 1.0F);
  EXPECT_FLOAT_EQ(static_data.fog.min_transmittance, 0.0F);
  EXPECT_EQ(static_data.volumetric_fog.flags, 0U);
  EXPECT_EQ(static_data.volumetric_fog.integrated_light_scattering_srv,
    oxygen::kInvalidBindlessIndex);
  EXPECT_FLOAT_EQ(static_data.volumetric_fog.grid_z_params[0], 0.0F);
  EXPECT_FLOAT_EQ(static_data.volumetric_fog.grid_z_params[1], 1.0F);
  EXPECT_FLOAT_EQ(static_data.volumetric_fog.grid_z_params[2], 1.0F);
}

class EnvironmentLightingServiceBehaviorTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    graphics_ = std::make_shared<FakeGraphics>();
    graphics_->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
    renderer_ = MakeRenderer(graphics_);
  }

  std::shared_ptr<FakeGraphics> graphics_;
  std::shared_ptr<Renderer> renderer_;
};

auto MakeSceneWithLocalFog() -> std::shared_ptr<oxygen::scene::Scene>
{
  auto scene = std::make_shared<oxygen::scene::Scene>("LocalFogScene", 16U);
  auto environment = std::make_unique<oxygen::scene::SceneEnvironment>();

  auto& atmosphere
    = environment->AddSystem<oxygen::scene::environment::SkyAtmosphere>();
  atmosphere.SetEnabled(true);
  atmosphere.SetPlanetRadiusMeters(7000000.0F);
  atmosphere.SetAtmosphereHeightMeters(120000.0F);
  atmosphere.SetSkyLuminanceFactorRgb({ 1.1F, 1.2F, 1.3F });

  auto& fog_system = environment->AddSystem<oxygen::scene::environment::Fog>();
  fog_system.SetEnabled(true);
  fog_system.SetEnableHeightFog(true);
  fog_system.SetEnableVolumetricFog(false);
  fog_system.SetExtinctionSigmaTPerMeter(0.05F);

  auto& sky_light
    = environment->AddSystem<oxygen::scene::environment::SkyLight>();
  sky_light.SetEnabled(true);
  sky_light.SetIntensityMul(1.25F);

  scene->SetEnvironment(std::move(environment));

  auto node = scene->CreateNode("LocalFog");
  const auto impl = node.GetImpl();
  EXPECT_TRUE(impl.has_value());
  if (impl.has_value()) {
    impl->get().AddComponent<oxygen::scene::environment::LocalFogVolume>();
    auto& fog
      = impl->get().GetComponent<oxygen::scene::environment::LocalFogVolume>();
    fog.SetEnabled(true);
    fog.SetRadialFogExtinction(0.45F);
    fog.SetHeightFogExtinction(0.25F);
    fog.SetHeightFogFalloff(0.2F);
    fog.SetHeightFogOffset(0.0F);
    fog.SetFogPhaseG(0.4F);
    fog.SetFogAlbedo({ 0.7F, 0.8F, 0.9F });
    fog.SetFogEmissive({ 0.1F, 0.2F, 0.3F });
    fog.SetSortPriority(2);
  }
  node.GetTransform().SetLocalPosition({ 0.0F, 0.0F, 0.5F });
  node.GetTransform().SetLocalScale({ 2.0F, 2.0F, 2.0F });
  scene->Update();
  return scene;
}

auto MakeSceneWithLocalFogVolumeCount(const std::uint32_t volume_count)
  -> std::shared_ptr<oxygen::scene::Scene>
{
  auto scene = std::make_shared<oxygen::scene::Scene>("LocalFogCapScene", 32U);
  auto environment = std::make_unique<oxygen::scene::SceneEnvironment>();
  scene->SetEnvironment(std::move(environment));

  for (std::uint32_t index = 0; index < volume_count; ++index) {
    auto node = scene->CreateNode(
      "LocalFogCap" + std::to_string(static_cast<unsigned long long>(index)));
    const auto impl = node.GetImpl();
    EXPECT_TRUE(impl.has_value());
    if (!impl.has_value()) {
      continue;
    }
    impl->get().AddComponent<oxygen::scene::environment::LocalFogVolume>();
    auto& fog
      = impl->get().GetComponent<oxygen::scene::environment::LocalFogVolume>();
    fog.SetEnabled(true);
    fog.SetRadialFogExtinction(0.25F + static_cast<float>(index) * 0.01F);
    fog.SetHeightFogExtinction(0.1F);
    fog.SetSortPriority(static_cast<int>(index) - 1);
    node.GetTransform().SetLocalPosition(
      { static_cast<float>(index) * 4.0F, 0.0F, 1.0F });
    node.GetTransform().SetLocalScale({ 2.0F, 2.0F, 2.0F });
  }
  scene->Update();
  return scene;
}

auto MakeSceneWithAtmosphereEnvironment()
  -> std::shared_ptr<oxygen::scene::Scene>
{
  auto scene = std::make_shared<oxygen::scene::Scene>("AtmosphereScene", 32U);
  auto environment = std::make_unique<oxygen::scene::SceneEnvironment>();

  auto& atmosphere
    = environment->AddSystem<oxygen::scene::environment::SkyAtmosphere>();
  atmosphere.SetEnabled(true);
  atmosphere.SetPlanetRadiusMeters(7000000.0F);
  atmosphere.SetAtmosphereHeightMeters(120000.0F);
  atmosphere.SetSkyLuminanceFactorRgb({ 1.1F, 1.2F, 1.3F });

  auto& fog = environment->AddSystem<oxygen::scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(false);
  fog.SetExtinctionSigmaTPerMeter(0.05F);

  auto& sky_light
    = environment->AddSystem<oxygen::scene::environment::SkyLight>();
  sky_light.SetEnabled(true);
  sky_light.SetIntensityMul(1.25F);

  scene->SetEnvironment(std::move(environment));
  scene->Update();
  return scene;
}

auto AddAtmosphereDirectionalLight(oxygen::scene::Scene& scene,
  std::string_view name, const oxygen::scene::AtmosphereLightSlot slot,
  const bool is_sun_light, const std::uint32_t cascade_count,
  const bool use_per_pixel_transmittance, const glm::vec3& disk_scale,
  const glm::vec3& color_rgb, const float illuminance_lux,
  const glm::quat& local_rotation = glm::quat(1.0F, 0.0F, 0.0F, 0.0F))
  -> oxygen::scene::SceneNode
{
  auto node = scene.CreateNode(std::string(name));
  EXPECT_TRUE(node.IsAlive());
  EXPECT_TRUE(
    node.AttachLight(std::make_unique<oxygen::scene::DirectionalLight>()));
  node.GetTransform().SetLocalRotation(local_rotation);

  auto light = node.GetLightAs<oxygen::scene::DirectionalLight>();
  EXPECT_TRUE(light.has_value());
  if (light.has_value()) {
    light->get().Common().affects_world = true;
    light->get().SetEnvironmentContribution(true);
    light->get().SetIsSunLight(is_sun_light);
    light->get().SetAngularSizeRadians(
      2.0F * oxygen::engine::atmos::kDefaultSunDiskAngularRadiusRad);
    light->get().SetAtmosphereLightSlot(slot);
    light->get().SetUsePerPixelAtmosphereTransmittance(
      use_per_pixel_transmittance);
    light->get().SetAtmosphereDiskLuminanceScale(
      { disk_scale.x, disk_scale.y, disk_scale.z, 1.0F });
    light->get().Common().color_rgb = color_rgb;
    light->get().SetIntensityLux(illuminance_lux);
    light->get().CascadedShadows().cascade_count = cascade_count;
  }

  return node;
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  ProbeRefreshKeepsBindingsPersistentAndUnboundUntilRealProbeDataExists)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 4U }, oxygen::frame::Slot { 2U });
  service.RefreshPersistentProbeState(true);

  const auto& probe_state = service.InspectProbeState();
  const auto& refresh_state = service.GetLastProbeRefreshState();
  EXPECT_TRUE(refresh_state.requested);
  EXPECT_TRUE(refresh_state.refreshed);
  EXPECT_FALSE(probe_state.valid);
  EXPECT_EQ(probe_state.probes.probe_revision, 1U);
  EXPECT_EQ(probe_state.probes.environment_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(probe_state.probes.irradiance_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(probe_state.probes.prefiltered_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(probe_state.probes.brdf_lut_srv, kInvalidShaderVisibleIndex);
  EXPECT_NE(
    probe_state.flags & oxygen::vortex::kEnvironmentProbeStateFlagUnavailable,
    0U);
  EXPECT_NE(
    probe_state.flags & oxygen::vortex::kEnvironmentProbeStateFlagStale, 0U);
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  IblProbeRefreshInvalidatesPreviouslyUsableResourcesWhenSourceChanges)
{
  const auto pass = IblProbePass {};
  auto current = EnvironmentProbeState {};
  current.valid = true;
  current.flags = oxygen::vortex::kEnvironmentProbeStateFlagResourcesValid;
  current.probes.environment_map_srv = oxygen::ShaderVisibleIndex { 11U };
  current.probes.irradiance_map_srv = oxygen::ShaderVisibleIndex { 12U };
  current.probes.prefiltered_map_srv = oxygen::ShaderVisibleIndex { 13U };
  current.probes.brdf_lut_srv = oxygen::ShaderVisibleIndex { 14U };
  current.probes.probe_revision = 7U;

  const auto refreshed = pass.Refresh(current, true);

  EXPECT_TRUE(refreshed.requested);
  EXPECT_TRUE(refreshed.refreshed);
  EXPECT_FALSE(refreshed.probe_state.valid);
  EXPECT_EQ(refreshed.probe_state.probes.probe_revision, 8U);
  EXPECT_EQ(refreshed.probe_state.probes.environment_map_srv,
    kInvalidShaderVisibleIndex);
  EXPECT_EQ(refreshed.probe_state.probes.irradiance_map_srv,
    kInvalidShaderVisibleIndex);
  EXPECT_EQ(refreshed.probe_state.probes.prefiltered_map_srv,
    kInvalidShaderVisibleIndex);
  EXPECT_EQ(
    refreshed.probe_state.probes.brdf_lut_srv, kInvalidShaderVisibleIndex);
  EXPECT_NE(refreshed.probe_state.flags
      & oxygen::vortex::kEnvironmentProbeStateFlagUnavailable,
    0U);
  EXPECT_NE(refreshed.probe_state.flags
      & oxygen::vortex::kEnvironmentProbeStateFlagStale,
    0U);
  EXPECT_EQ(refreshed.probe_state.flags
      & oxygen::vortex::kEnvironmentProbeStateFlagResourcesValid,
    0U);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  PublishedEnvironmentViewDataUsesUeStyleTranslatedPlanetFrameForComponentModes)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 4U }, oxygen::frame::Slot { 1U });
  auto scene = MakeSceneWithAtmosphereEnvironment();
  auto* environment = scene->GetEnvironment().get();
  ASSERT_NE(environment, nullptr);
  auto atmosphere
    = environment->TryGetSystem<oxygen::scene::environment::SkyAtmosphere>();
  ASSERT_NE(atmosphere, nullptr);
  atmosphere->SetTransformMode(oxygen::scene::environment::
      SkyAtmosphereTransformMode::kPlanetCenterAtComponentTransform);
  atmosphere->SetPlanetAnchorWorldPosition({ 1000.0F, 2000.0F, 3000.0F });
  scene->Update();

  const auto camera_position = glm::vec3 { 1400.0F, 2600.0F, 7200300.0F };
  const auto center = camera_position + glm::vec3 { 1.0F, 0.0F, 0.0F };
  const auto view_matrix
    = glm::lookAtRH(camera_position, center, glm::vec3 { 0.0F, 0.0F, 1.0F });
  auto resolved_view
    = MakeResolvedView(64.0F, 64.0F, 0.0F, 0.0F, camera_position, view_matrix);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 20U };
  composition_view.with_atmosphere = true;
  auto ctx = MakeRenderContext(ViewId { 20U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };

  static_cast<void>(service.PublishEnvironmentBindings(ctx));

  const auto* view_data = service.InspectEnvironmentViewData(ViewId { 20U });
  ASSERT_NE(view_data, nullptr);

  EXPECT_FLOAT_EQ(view_data->planet_center_ws_pad.x, 1000.0F);
  EXPECT_FLOAT_EQ(view_data->planet_center_ws_pad.y, 2000.0F);
  EXPECT_FLOAT_EQ(view_data->planet_center_ws_pad.z, 3000.0F);
  EXPECT_GT(
    view_data->sky_planet_translated_world_center_km_and_view_height_km.w,
    7000.0F);
  EXPECT_NEAR(
    view_data->sky_camera_translated_world_origin_km_pad.x, 0.0F, 1.0e-3F);
  EXPECT_NEAR(
    view_data->sky_camera_translated_world_origin_km_pad.y, 0.0F, 1.0e-3F);
  EXPECT_NEAR(
    view_data->sky_camera_translated_world_origin_km_pad.z, 0.0F, 1.0e-3F);
  EXPECT_NEAR(view_data->sky_view_lut_referential_row0.x, 0.0F, 1.0e-3F);
  EXPECT_NEAR(view_data->sky_view_lut_referential_row0.y, 1.0F, 1.0e-3F);
  EXPECT_NEAR(view_data->sky_view_lut_referential_row0.z, 0.0F, 1.0e-3F);
  EXPECT_NEAR(view_data->sky_view_lut_referential_row1.x, -1.0F, 1.0e-3F);
  EXPECT_NEAR(view_data->sky_view_lut_referential_row1.y, 0.0F, 1.0e-3F);
  EXPECT_NEAR(view_data->sky_view_lut_referential_row1.z, 0.0F, 1.0e-3F);
  EXPECT_NEAR(view_data->sky_view_lut_referential_row2.x, 0.0F, 1.0e-3F);
  EXPECT_NEAR(view_data->sky_view_lut_referential_row2.y, 0.0F, 1.0e-3F);
  EXPECT_NEAR(view_data->sky_view_lut_referential_row2.z, 1.0F, 1.0e-3F);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  SkyViewReferentialFollowsSunDirectionNotCameraForward)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 4U }, oxygen::frame::Slot { 1U });
  auto scene = MakeSceneWithAtmosphereEnvironment();
  static_cast<void>(AddAtmosphereDirectionalLight(*scene, "Sun",
    oxygen::scene::AtmosphereLightSlot::kPrimary, true, 2U, true,
    { 1.0F, 1.0F, 1.0F }, { 1.0F, 1.0F, 1.0F }, 100000.0F));
  scene->Update();

  const auto camera_position = glm::vec3 { 0.0F, 0.0F, 7000300.0F };
  const auto view_matrix_x = glm::lookAtRH(camera_position,
    camera_position + glm::vec3 { 1.0F, 0.0F, 0.0F },
    glm::vec3 { 0.0F, 0.0F, 1.0F });
  const auto view_matrix_up = glm::lookAtRH(camera_position,
    camera_position + glm::vec3 { 0.0F, 0.0F, 1.0F },
    glm::vec3 { 0.0F, 1.0F, 0.0F });

  auto view_x = MakeResolvedView(
    64.0F, 64.0F, 0.0F, 0.0F, camera_position, view_matrix_x);
  auto view_up = MakeResolvedView(
    64.0F, 64.0F, 0.0F, 0.0F, camera_position, view_matrix_up);
  auto composition_view_x = oxygen::vortex::CompositionView {};
  composition_view_x.id = ViewId { 201U };
  composition_view_x.with_atmosphere = true;
  auto composition_view_up = oxygen::vortex::CompositionView {};
  composition_view_up.id = ViewId { 202U };
  composition_view_up.with_atmosphere = true;

  auto ctx_x = MakeRenderContext(ViewId { 201U }, view_x, composition_view_x);
  ctx_x.scene = oxygen::observer_ptr { scene.get() };
  auto ctx_up
    = MakeRenderContext(ViewId { 202U }, view_up, composition_view_up);
  ctx_up.scene = oxygen::observer_ptr { scene.get() };

  static_cast<void>(service.PublishEnvironmentBindings(ctx_x));
  static_cast<void>(service.PublishEnvironmentBindings(ctx_up));

  const auto* view_data_x = service.InspectEnvironmentViewData(ViewId { 201U });
  const auto* view_data_up
    = service.InspectEnvironmentViewData(ViewId { 202U });
  ASSERT_NE(view_data_x, nullptr);
  ASSERT_NE(view_data_up, nullptr);

  EXPECT_NEAR(view_data_x->sky_view_lut_referential_row0.x, 0.0F, 1.0e-3F);
  EXPECT_NEAR(view_data_x->sky_view_lut_referential_row0.y, 1.0F, 1.0e-3F);
  EXPECT_NEAR(view_data_x->sky_view_lut_referential_row0.z, 0.0F, 1.0e-3F);
  EXPECT_NEAR(view_data_x->sky_view_lut_referential_row1.x, -1.0F, 1.0e-3F);
  EXPECT_NEAR(view_data_x->sky_view_lut_referential_row1.y, 0.0F, 1.0e-3F);
  EXPECT_NEAR(view_data_x->sky_view_lut_referential_row1.z, 0.0F, 1.0e-3F);

  EXPECT_NEAR(view_data_up->sky_view_lut_referential_row0.x,
    view_data_x->sky_view_lut_referential_row0.x, 1.0e-5F);
  EXPECT_NEAR(view_data_up->sky_view_lut_referential_row0.y,
    view_data_x->sky_view_lut_referential_row0.y, 1.0e-5F);
  EXPECT_NEAR(view_data_up->sky_view_lut_referential_row0.z,
    view_data_x->sky_view_lut_referential_row0.z, 1.0e-5F);
  EXPECT_NEAR(view_data_up->sky_view_lut_referential_row1.x,
    view_data_x->sky_view_lut_referential_row1.x, 1.0e-5F);
  EXPECT_NEAR(view_data_up->sky_view_lut_referential_row1.y,
    view_data_x->sky_view_lut_referential_row1.y, 1.0e-5F);
  EXPECT_NEAR(view_data_up->sky_view_lut_referential_row1.z,
    view_data_x->sky_view_lut_referential_row1.z, 1.0e-5F);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  StableAtmosphereStateResolvesSlot0AndSlot1ExplicitClaimsAndKeepsSlot0ShadowAuthority)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 4U }, oxygen::frame::Slot { 1U });
  auto scene = MakeSceneWithAtmosphereEnvironment();
  auto primary = AddAtmosphereDirectionalLight(*scene, "Primary",
    oxygen::scene::AtmosphereLightSlot::kPrimary, true, 2U, true,
    { 1.1F, 1.0F, 0.9F }, { 1.0F, 0.95F, 0.9F }, 120000.0F);
  auto secondary = AddAtmosphereDirectionalLight(*scene, "Secondary",
    oxygen::scene::AtmosphereLightSlot::kSecondary, false, 4U, false,
    { 0.4F, 0.5F, 0.6F }, { 0.8F, 0.85F, 1.0F }, 3000.0F);
  scene->Update();

  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 21U };
  composition_view.with_atmosphere = true;
  auto ctx = MakeRenderContext(ViewId { 21U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };

  static_cast<void>(service.PublishEnvironmentBindings(ctx));

  const auto& light_state = service.InspectAtmosphereLightState();
  const auto& atmosphere_state = service.InspectAtmosphereState();
  const auto* view_data = service.InspectEnvironmentViewData(ViewId { 21U });
  ASSERT_NE(view_data, nullptr);

  // slot 0 is the primary atmosphere light, slot 1 is the optional secondary.
  EXPECT_TRUE(light_state.atmosphere_lights[0].enabled);
  EXPECT_TRUE(light_state.atmosphere_lights[1].enabled);
  EXPECT_EQ(light_state.source_nodes[0].Index(), primary.GetHandle().Index());
  EXPECT_EQ(light_state.source_nodes[1].Index(), secondary.GetHandle().Index());
  EXPECT_TRUE(light_state.explicit_slot_claims[0]);
  EXPECT_TRUE(light_state.explicit_slot_claims[1]);
  EXPECT_EQ(light_state.active_light_count, 2U);
  EXPECT_EQ(light_state.conflict_count, 0U);
  EXPECT_TRUE(light_state.atmosphere_lights[0].use_per_pixel_transmittance);
  EXPECT_FALSE(light_state.atmosphere_lights[1].use_per_pixel_transmittance);
  EXPECT_NE(light_state.atmosphere_lights[0].direct_light_authority_flags
      & oxygen::vortex::environment::
        kAtmosphereDirectLightFlagPerPixelTransmittance,
    0U);
  EXPECT_EQ(light_state.atmosphere_lights[0].direct_light_authority_flags
      & oxygen::vortex::environment::
        kAtmosphereDirectLightFlagHasBakedGroundTransmittance,
    0U);
  EXPECT_EQ(light_state.atmosphere_lights[1].direct_light_authority_flags
      & oxygen::vortex::environment::
        kAtmosphereDirectLightFlagPerPixelTransmittance,
    0U);
  EXPECT_NE(light_state.atmosphere_lights[1].direct_light_authority_flags
      & oxygen::vortex::environment::
        kAtmosphereDirectLightFlagHasBakedGroundTransmittance,
    0U);

  EXPECT_EQ(atmosphere_state.conventional_shadow_authority_slot, 0U);
  EXPECT_EQ(atmosphere_state.conventional_shadow_cascade_count, 2U);
  EXPECT_TRUE(atmosphere_state.conventional_shadow_authority_slot0_only);
  EXPECT_EQ(
    atmosphere_state.view_products.conventional_shadow_authority_slot, 0U);

  const auto* bindings = service.InspectBindings(ViewId { 21U });
  ASSERT_NE(bindings, nullptr);
  EXPECT_NE(bindings->contract_flags
      & oxygen::vortex::kEnvironmentContractFlagAtmosphereLight0Enabled,
    0U);
  EXPECT_NE(bindings->contract_flags
      & oxygen::vortex::kEnvironmentContractFlagAtmosphereLight1Enabled,
    0U);
  EXPECT_NE(bindings->contract_flags
      & oxygen::vortex::kEnvironmentContractFlagShadowAuthoritySlot0Only,
    0U);

  constexpr auto kPi = 3.14159265358979323846F;
  const auto primary_half_apex_angle
    = 0.5F * light_state.atmosphere_lights[0].angular_size_radians;
  const auto primary_solid_angle
    = 2.0F * kPi * (1.0F - std::cos(primary_half_apex_angle));
  const auto expected_primary_disk_luminance
    = glm::vec3(light_state.atmosphere_lights[0].disk_luminance_scale_rgba.x
          * light_state.atmosphere_lights[0].illuminance_rgb_lux.x,
        light_state.atmosphere_lights[0].disk_luminance_scale_rgba.y
          * light_state.atmosphere_lights[0].illuminance_rgb_lux.y,
        light_state.atmosphere_lights[0].disk_luminance_scale_rgba.z
          * light_state.atmosphere_lights[0].illuminance_rgb_lux.z)
    / primary_solid_angle;
  EXPECT_NEAR(view_data->atmosphere_light0_direction_angular_size.x,
    light_state.atmosphere_lights[0].direction_to_light_ws.x, 1.0e-5F);
  EXPECT_NEAR(view_data->atmosphere_light0_direction_angular_size.y,
    light_state.atmosphere_lights[0].direction_to_light_ws.y, 1.0e-5F);
  EXPECT_NEAR(view_data->atmosphere_light0_direction_angular_size.z,
    light_state.atmosphere_lights[0].direction_to_light_ws.z, 1.0e-5F);
  EXPECT_NEAR(view_data->atmosphere_light0_direction_angular_size.w,
    primary_half_apex_angle, 1.0e-6F);
  EXPECT_NEAR(view_data->atmosphere_light0_disk_luminance_rgb.x,
    expected_primary_disk_luminance.x, 1.0e-2F);
  EXPECT_NEAR(view_data->atmosphere_light0_disk_luminance_rgb.y,
    expected_primary_disk_luminance.y, 1.0e-2F);
  EXPECT_NEAR(view_data->atmosphere_light0_disk_luminance_rgb.z,
    expected_primary_disk_luminance.z, 1.0e-2F);
  EXPECT_FLOAT_EQ(view_data->atmosphere_light0_disk_luminance_rgb.w, 1.0F);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  AtmosphereLightStatePublishesDirectionTowardSourceFromNodeForward)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 4U }, oxygen::frame::Slot { 1U });
  auto scene = MakeSceneWithAtmosphereEnvironment();
  auto sun = AddAtmosphereDirectionalLight(*scene, "Sun",
    oxygen::scene::AtmosphereLightSlot::kPrimary, true, 2U, true,
    { 1.0F, 0.95F, 0.9F }, { 1.0F, 1.0F, 1.0F }, 100000.0F,
    glm::angleAxis(-oxygen::math::HalfPi, oxygen::space::move::Right));
  scene->Update();

  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 210U };
  composition_view.with_atmosphere = true;
  auto ctx
    = MakeRenderContext(ViewId { 210U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };

  static_cast<void>(service.PublishEnvironmentBindings(ctx));

  const auto& light_state = service.InspectAtmosphereLightState();
  ASSERT_TRUE(light_state.atmosphere_lights[0].enabled);
  EXPECT_EQ(light_state.source_nodes[0].Index(), sun.GetHandle().Index());
  EXPECT_NEAR(
    light_state.atmosphere_lights[0].direction_to_light_ws.x, 0.0F, 1.0e-5F);
  EXPECT_NEAR(
    light_state.atmosphere_lights[0].direction_to_light_ws.y, 0.0F, 1.0e-5F);
  EXPECT_NEAR(
    light_state.atmosphere_lights[0].direction_to_light_ws.z, -1.0F, 1.0e-5F);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  StableAtmosphereStateFallsBackSlot0ToFirstSunWhenNoExplicitPrimaryExists)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 5U }, oxygen::frame::Slot { 1U });
  auto scene = MakeSceneWithAtmosphereEnvironment();
  auto secondary = AddAtmosphereDirectionalLight(*scene, "SecondaryOnly",
    oxygen::scene::AtmosphereLightSlot::kSecondary, false, 1U, false,
    { 0.7F, 0.7F, 0.9F }, { 0.6F, 0.7F, 1.0F }, 2000.0F);
  auto sun = AddAtmosphereDirectionalLight(*scene, "FallbackSun",
    oxygen::scene::AtmosphereLightSlot::kNone, true, 3U, true,
    { 1.0F, 1.0F, 1.0F }, { 1.0F, 0.9F, 0.8F }, 90000.0F);
  auto fill = AddAtmosphereDirectionalLight(*scene, "Fill",
    oxygen::scene::AtmosphereLightSlot::kNone, false, 4U, false,
    { 0.2F, 0.2F, 0.2F }, { 0.4F, 0.5F, 0.6F }, 500.0F);
  auto fill_light = fill.GetLightAs<oxygen::scene::DirectionalLight>();
  ASSERT_TRUE(fill_light.has_value());
  fill_light->get().SetEnvironmentContribution(false);
  scene->Update();

  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 22U };
  composition_view.with_atmosphere = true;
  auto ctx = MakeRenderContext(ViewId { 22U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };

  static_cast<void>(service.PublishEnvironmentBindings(ctx));

  const auto& light_state = service.InspectAtmosphereLightState();

  // slot 0 fallback prefers the first sun light when no explicit primary
  // exists.
  EXPECT_TRUE(light_state.atmosphere_lights[0].enabled);
  EXPECT_TRUE(light_state.atmosphere_lights[1].enabled);
  EXPECT_EQ(light_state.source_nodes[0].Index(), sun.GetHandle().Index());
  EXPECT_EQ(light_state.source_nodes[1].Index(), secondary.GetHandle().Index());
  EXPECT_FALSE(light_state.explicit_slot_claims[0]);
  EXPECT_TRUE(light_state.explicit_slot_claims[1]);
  EXPECT_EQ(light_state.shadow_authority_slot, 0U);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  StableAtmosphereLightResolutionUsesFirstWinsConflictHandlingForSlot0)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 6U }, oxygen::frame::Slot { 1U });
  auto scene = MakeSceneWithAtmosphereEnvironment();
  auto first = AddAtmosphereDirectionalLight(*scene, "FirstPrimary",
    oxygen::scene::AtmosphereLightSlot::kPrimary, false, 2U, false,
    { 0.9F, 0.8F, 0.7F }, { 1.0F, 1.0F, 1.0F }, 10000.0F);
  auto second = AddAtmosphereDirectionalLight(*scene, "SecondPrimary",
    oxygen::scene::AtmosphereLightSlot::kPrimary, true, 4U, true,
    { 0.5F, 0.4F, 0.3F }, { 0.7F, 0.8F, 1.0F }, 20000.0F);
  ASSERT_TRUE(scene->ReparentNode(second, first, true));
  scene->Update();

  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 23U };
  composition_view.with_atmosphere = true;
  auto ctx = MakeRenderContext(ViewId { 23U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };

  static_cast<void>(service.PublishEnvironmentBindings(ctx));

  const auto& light_state = service.InspectAtmosphereLightState();
  auto traversal_order = std::vector<oxygen::scene::NodeHandle> {};
  const auto visitor
    = [&traversal_order](const oxygen::scene::MutableVisitedNode& visited,
        const bool dry_run) -> oxygen::scene::VisitResult {
    static_cast<void>(dry_run);
    const auto& node = *visited.node_impl;
    if (!node.HasComponent<oxygen::scene::DirectionalLight>()) {
      return oxygen::scene::VisitResult::kContinue;
    }
    const auto& light = node.GetComponent<oxygen::scene::DirectionalLight>();
    if (light.Common().affects_world && light.GetEnvironmentContribution()
      && light.GetAtmosphereLightSlot()
        == oxygen::scene::AtmosphereLightSlot::kPrimary) {
      traversal_order.push_back(visited.handle);
    }
    return oxygen::scene::VisitResult::kContinue;
  };
  static_cast<void>(scene->Traverse().Traverse(
    visitor, oxygen::scene::TraversalOrder::kPreOrder));
  ASSERT_FALSE(traversal_order.empty());

  // first-wins conflict handling keeps the earliest slot 0 claimant in the
  // deterministic scene traversal order.
  EXPECT_EQ(
    light_state.source_nodes[0].Index(), traversal_order.front().Index());
  EXPECT_NE(first.GetHandle().Index(), second.GetHandle().Index());
  EXPECT_EQ(light_state.conflict_count, 1U);
  EXPECT_EQ(light_state.first_conflict_slot, 0U);
  EXPECT_TRUE(light_state.explicit_slot_claims[0]);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  StableAtmosphereStateLeavesSlot1DisabledWithoutExplicitSecondaryClaim)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 6U }, oxygen::frame::Slot { 1U });
  auto scene = MakeSceneWithAtmosphereEnvironment();
  auto sun = AddAtmosphereDirectionalLight(*scene, "Sun",
    oxygen::scene::AtmosphereLightSlot::kNone, true, 2U, false,
    { 1.0F, 0.95F, 0.9F }, { 1.0F, 1.0F, 1.0F }, 90000.0F);
  static_cast<void>(AddAtmosphereDirectionalLight(*scene, "MoonCandidate",
    oxygen::scene::AtmosphereLightSlot::kNone, false, 1U, false,
    { 0.5F, 0.6F, 0.8F }, { 0.7F, 0.8F, 1.0F }, 2500.0F));
  scene->Update();

  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 24U };
  composition_view.with_atmosphere = true;
  auto ctx = MakeRenderContext(ViewId { 24U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };

  static_cast<void>(service.PublishEnvironmentBindings(ctx));

  const auto& light_state = service.InspectAtmosphereLightState();
  EXPECT_TRUE(light_state.atmosphere_lights[0].enabled);
  EXPECT_FALSE(light_state.atmosphere_lights[1].enabled);
  EXPECT_EQ(light_state.source_nodes[0].Index(), sun.GetHandle().Index());
  EXPECT_FALSE(light_state.explicit_slot_claims[0]);
  EXPECT_FALSE(light_state.explicit_slot_claims[1]);
  EXPECT_EQ(light_state.active_light_count, 1U);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  StableAtmosphereStateInvalidatesOnlyOnAuthoredAtmosphereOrLightChanges)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 7U }, oxygen::frame::Slot { 1U });
  auto scene = MakeSceneWithAtmosphereEnvironment();
  auto primary = AddAtmosphereDirectionalLight(*scene, "Primary",
    oxygen::scene::AtmosphereLightSlot::kPrimary, true, 3U, false,
    { 1.0F, 1.0F, 1.0F }, { 1.0F, 0.95F, 0.9F }, 100000.0F);
  scene->Update();

  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 24U };
  composition_view.with_atmosphere = true;
  auto ctx = MakeRenderContext(ViewId { 24U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };

  static_cast<void>(service.PublishEnvironmentBindings(ctx));
  const auto base_light_revision
    = service.InspectAtmosphereLightState().revision;
  const auto base_atmosphere_revision
    = service.InspectAtmosphereState().atmosphere_revision;
  const auto base_stable_revision
    = service.InspectAtmosphereState().stable_revision;

  auto local_fog = scene->CreateNode("UnrelatedLocalFog");
  ASSERT_TRUE(local_fog.IsAlive());
  const auto local_fog_impl = local_fog.GetImpl();
  ASSERT_TRUE(local_fog_impl.has_value());
  local_fog_impl->get()
    .AddComponent<oxygen::scene::environment::LocalFogVolume>();
  scene->Update();

  static_cast<void>(service.PublishEnvironmentBindings(ctx));
  EXPECT_EQ(
    service.InspectAtmosphereLightState().revision, base_light_revision);
  EXPECT_EQ(service.InspectAtmosphereState().atmosphere_revision,
    base_atmosphere_revision);
  EXPECT_EQ(
    service.InspectAtmosphereState().stable_revision, base_stable_revision);

  auto primary_light = primary.GetLightAs<oxygen::scene::DirectionalLight>();
  ASSERT_TRUE(primary_light.has_value());
  primary_light->get().SetUsePerPixelAtmosphereTransmittance(true);

  static_cast<void>(service.PublishEnvironmentBindings(ctx));
  const auto light_changed_revision
    = service.InspectAtmosphereLightState().revision;
  const auto stable_changed_revision
    = service.InspectAtmosphereState().stable_revision;
  EXPECT_GT(light_changed_revision, base_light_revision);
  EXPECT_EQ(service.InspectAtmosphereState().atmosphere_revision,
    base_atmosphere_revision);
  EXPECT_GT(stable_changed_revision, base_stable_revision);

  auto atmosphere
    = scene->GetEnvironment()
        ->TryGetSystem<oxygen::scene::environment::SkyAtmosphere>();
  ASSERT_NE(atmosphere.get(), nullptr);
  atmosphere->SetTraceSampleCountScale(2.0F);

  static_cast<void>(service.PublishEnvironmentBindings(ctx));
  EXPECT_GT(service.InspectAtmosphereState().atmosphere_revision,
    base_atmosphere_revision);
  EXPECT_GT(
    service.InspectAtmosphereState().stable_revision, stable_changed_revision);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  EnvironmentPublicationLeavesAmbientBridgeDisabledByDefault)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 5U }, oxygen::frame::Slot { 1U });
  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 11U };
  composition_view.with_atmosphere = true;
  auto ctx = MakeRenderContext(ViewId { 11U }, resolved_view, composition_view);

  const auto slot = service.PublishEnvironmentBindings(ctx);
  ASSERT_NE(slot, kInvalidShaderVisibleIndex);
  const auto* bindings = service.InspectBindings(ViewId { 11U });
  ASSERT_NE(bindings, nullptr);
  EXPECT_EQ(
    bindings->ambient_bridge.irradiance_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings->ambient_bridge.flags, 0U);
  EXPECT_EQ(service.GetLastPublicationState().ambient_bridge_view_count, 0U);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  EnvironmentPublicationGeneratesAndPublishesPerViewAtmosphereProducts)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 6U }, oxygen::frame::Slot { 1U });
  auto scene = MakeSceneWithAtmosphereEnvironment();
  auto primary = AddAtmosphereDirectionalLight(*scene, "Primary",
    oxygen::scene::AtmosphereLightSlot::kPrimary, true, 2U, true,
    { 1.0F, 1.0F, 1.0F }, { 1.0F, 0.95F, 0.9F }, 100000.0F);
  static_cast<void>(primary);
  scene->Update();

  auto resolved_view = MakeResolvedView(96.0F, 54.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 18U };
  composition_view.with_atmosphere = true;
  auto ctx = MakeRenderContext(ViewId { 18U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };
  ctx.view_constants = graphics_->CreateBuffer({
    .size_bytes = 1024U,
    .usage = oxygen::graphics::BufferUsage::kConstant,
    .memory = oxygen::graphics::BufferMemory::kUpload,
    .debug_name
    = "EnvironmentLightingServiceBehaviorTest.AtmosphereProducts.ViewConstants",
  });
  ASSERT_NE(ctx.view_constants, nullptr);

  graphics_->dispatch_log_.dispatches.clear();
  graphics_->compute_pipeline_log_.binds.clear();

  const auto slot = service.PublishEnvironmentBindings(ctx);

  ASSERT_NE(slot, kInvalidShaderVisibleIndex);
  const auto* bindings = service.InspectBindings(ViewId { 18U });
  ASSERT_NE(bindings, nullptr);
  EXPECT_TRUE(bindings->environment_view_slot.IsValid());
  EXPECT_TRUE(bindings->environment_view_products_slot.IsValid());
  EXPECT_TRUE(bindings->transmittance_lut_srv.IsValid());
  EXPECT_TRUE(bindings->multi_scattering_lut_srv.IsValid());
  EXPECT_TRUE(bindings->sky_view_lut_srv.IsValid());
  EXPECT_TRUE(bindings->camera_aerial_perspective_srv.IsValid());
  EXPECT_TRUE(oxygen::bindless::generated::IsShaderVisibleIndexInDomain(
    oxygen::bindless::generated::kTexturesDomain,
    bindings->transmittance_lut_srv.get()));
  EXPECT_TRUE(oxygen::bindless::generated::IsShaderVisibleIndexInDomain(
    oxygen::bindless::generated::kTexturesDomain,
    bindings->multi_scattering_lut_srv.get()));
  EXPECT_TRUE(oxygen::bindless::generated::IsShaderVisibleIndexInDomain(
    oxygen::bindless::generated::kTexturesDomain,
    bindings->sky_view_lut_srv.get()));
  EXPECT_TRUE(oxygen::bindless::generated::IsShaderVisibleIndexInDomain(
    oxygen::bindless::generated::kTexturesDomain,
    bindings->camera_aerial_perspective_srv.get()));
  EXPECT_EQ(service.GetLastPublicationState().published_view_count, 1U);
  EXPECT_EQ(
    service.GetLastPublicationState().published_environment_view_count, 1U);
  EXPECT_EQ(
    service.GetLastPublicationState().published_environment_view_products_count,
    1U);

  const auto& generation = service.GetLastViewProductGenerationState();
  EXPECT_EQ(generation.view_id, ViewId { 18U });
  EXPECT_TRUE(generation.environment_view_published);
  EXPECT_TRUE(generation.environment_view_slot.IsValid());
  EXPECT_TRUE(generation.atmosphere_lut_cache_valid);
  EXPECT_GE(generation.atmosphere_lut_cache_revision, 3U);
  EXPECT_EQ(generation.atmosphere_light_count, 1U);
  EXPECT_FALSE(generation.dual_atmosphere_lights_participating);
  EXPECT_TRUE(generation.transmittance_lut_requested);
  EXPECT_TRUE(generation.transmittance_lut_executed);
  EXPECT_TRUE(generation.transmittance_lut_srv.IsValid());
  EXPECT_TRUE(oxygen::bindless::generated::IsShaderVisibleIndexInDomain(
    oxygen::bindless::generated::kTexturesDomain,
    generation.transmittance_lut_srv.get()));
  EXPECT_GE(generation.transmittance_dispatch_count_x, 1U);
  EXPECT_GE(generation.transmittance_dispatch_count_y, 1U);
  EXPECT_EQ(generation.transmittance_dispatch_count_z, 1U);
  EXPECT_TRUE(generation.multi_scattering_lut_requested);
  EXPECT_TRUE(generation.multi_scattering_lut_executed);
  EXPECT_TRUE(generation.multi_scattering_lut_srv.IsValid());
  EXPECT_TRUE(oxygen::bindless::generated::IsShaderVisibleIndexInDomain(
    oxygen::bindless::generated::kTexturesDomain,
    generation.multi_scattering_lut_srv.get()));
  EXPECT_GE(generation.multi_scattering_dispatch_count_x, 1U);
  EXPECT_GE(generation.multi_scattering_dispatch_count_y, 1U);
  EXPECT_EQ(generation.multi_scattering_dispatch_count_z, 1U);
  EXPECT_TRUE(generation.distant_sky_light_lut_requested);
  EXPECT_TRUE(generation.distant_sky_light_lut_executed);
  EXPECT_TRUE(generation.distant_sky_light_lut_srv.IsValid());
  EXPECT_TRUE(oxygen::bindless::generated::IsShaderVisibleIndexInDomain(
    oxygen::bindless::generated::kGlobalSrvDomain,
    generation.distant_sky_light_lut_srv.get()));
  EXPECT_EQ(generation.distant_sky_light_dispatch_count_x, 1U);
  EXPECT_EQ(generation.distant_sky_light_dispatch_count_y, 1U);
  EXPECT_EQ(generation.distant_sky_light_dispatch_count_z, 1U);
  EXPECT_TRUE(generation.sky_view_lut_requested);
  EXPECT_TRUE(generation.sky_view_lut_executed);
  EXPECT_TRUE(generation.sky_view_lut_srv.IsValid());
  EXPECT_TRUE(oxygen::bindless::generated::IsShaderVisibleIndexInDomain(
    oxygen::bindless::generated::kTexturesDomain,
    generation.sky_view_lut_srv.get()));
  EXPECT_GE(generation.sky_view_dispatch_count_x, 1U);
  EXPECT_GE(generation.sky_view_dispatch_count_y, 1U);
  EXPECT_EQ(generation.sky_view_dispatch_count_z, 1U);
  EXPECT_TRUE(generation.camera_aerial_perspective_requested);
  EXPECT_TRUE(generation.camera_aerial_perspective_executed);
  EXPECT_TRUE(generation.camera_aerial_perspective_srv.IsValid());
  EXPECT_TRUE(oxygen::bindless::generated::IsShaderVisibleIndexInDomain(
    oxygen::bindless::generated::kTexturesDomain,
    generation.camera_aerial_perspective_srv.get()));
  EXPECT_GE(generation.camera_aerial_dispatch_count_x, 1U);
  EXPECT_GE(generation.camera_aerial_dispatch_count_y, 1U);
  EXPECT_GE(generation.camera_aerial_dispatch_count_z, 1U);
  EXPECT_TRUE(generation.environment_view_products_published);
  EXPECT_TRUE(generation.environment_view_products_slot.IsValid());

  EXPECT_EQ(graphics_->dispatch_log_.dispatches.size(), 5U);
  EXPECT_TRUE(std::ranges::any_of(
    graphics_->compute_pipeline_log_.binds, [](const auto& bind) -> bool {
      return bind.desc.GetName()
        == "Vortex.Environment.AtmosphereTransmittanceLut"
        && bind.desc.ComputeShader().source_path
        == "Vortex/Services/Environment/AtmosphereTransmittanceLut.hlsl"
        && bind.desc.ComputeShader().entry_point
        == "VortexAtmosphereTransmittanceLutCS";
    }));
  EXPECT_TRUE(std::ranges::any_of(
    graphics_->compute_pipeline_log_.binds, [](const auto& bind) -> bool {
      return bind.desc.GetName()
        == "Vortex.Environment.AtmosphereMultiScatteringLut"
        && bind.desc.ComputeShader().source_path
        == "Vortex/Services/Environment/AtmosphereMultiScatteringLut.hlsl"
        && bind.desc.ComputeShader().entry_point
        == "VortexAtmosphereMultiScatteringLutCS";
    }));
  EXPECT_TRUE(std::ranges::any_of(
    graphics_->compute_pipeline_log_.binds, [](const auto& bind) -> bool {
      return bind.desc.GetName() == "Vortex.Environment.DistantSkyLightLut"
        && bind.desc.ComputeShader().source_path
        == "Vortex/Services/Environment/DistantSkyLightLut.hlsl"
        && bind.desc.ComputeShader().entry_point
        == "VortexDistantSkyLightLutCS";
    }));
  EXPECT_TRUE(std::ranges::any_of(
    graphics_->compute_pipeline_log_.binds, [](const auto& bind) -> bool {
      return bind.desc.GetName() == "Vortex.Environment.AtmosphereSkyViewLut"
        && bind.desc.ComputeShader().source_path
        == "Vortex/Services/Environment/AtmosphereSkyViewLut.hlsl"
        && bind.desc.ComputeShader().entry_point
        == "VortexAtmosphereSkyViewLutCS";
    }));
  EXPECT_TRUE(std::ranges::any_of(
    graphics_->compute_pipeline_log_.binds, [](const auto& bind) -> bool {
      return bind.desc.GetName()
        == "Vortex.Environment.AtmosphereCameraAerialPerspective"
        && bind.desc.ComputeShader().source_path
        == "Vortex/Services/Environment/AtmosphereCameraAerialPerspective.hlsl"
        && bind.desc.ComputeShader().entry_point
        == "VortexAtmosphereCameraAerialPerspectiveCS";
    }));
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  AerialPerspectivePublicationCarriesUe57CameraVolumeControls)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 6U }, oxygen::frame::Slot { 1U });
  auto scene = MakeSceneWithAtmosphereEnvironment();
  auto* environment = scene->GetEnvironment().get();
  ASSERT_NE(environment, nullptr);
  auto atmosphere
    = environment->TryGetSystem<oxygen::scene::environment::SkyAtmosphere>();
  ASSERT_NE(atmosphere, nullptr);
  atmosphere->SetAerialPerspectiveDistanceScale(1000.0F);
  atmosphere->SetAerialScatteringStrength(0.5F);
  atmosphere->SetAerialPerspectiveStartDepthMeters(250.0F);
  atmosphere->SetSkyAndAerialPerspectiveLuminanceFactorRgb(
    { 0.8F, 0.9F, 1.0F });
  atmosphere->SetHeightFogContribution(0.35F);
  static_cast<void>(AddAtmosphereDirectionalLight(*scene, "Primary",
    oxygen::scene::AtmosphereLightSlot::kPrimary, true, 2U, true,
    { 1.0F, 1.0F, 1.0F }, { 1.0F, 0.95F, 0.9F }, 100000.0F));
  scene->Update();

  auto resolved_view = MakeResolvedView(96.0F, 54.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 118U };
  composition_view.with_atmosphere = true;
  auto ctx
    = MakeRenderContext(ViewId { 118U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };
  ctx.view_constants = graphics_->CreateBuffer({
    .size_bytes = 1024U,
    .usage = oxygen::graphics::BufferUsage::kConstant,
    .memory = oxygen::graphics::BufferMemory::kUpload,
    .debug_name
    = "EnvironmentLightingServiceBehaviorTest.AerialPerspective.ViewConstants",
  });
  ASSERT_NE(ctx.view_constants, nullptr);

  static_cast<void>(service.PublishEnvironmentBindings(ctx));

  const auto* bindings = service.InspectBindings(ViewId { 118U });
  ASSERT_NE(bindings, nullptr);
  const auto* view_data = service.InspectEnvironmentViewData(ViewId { 118U });
  ASSERT_NE(view_data, nullptr);
  const auto* static_data
    = service.InspectEnvironmentStaticData(ViewId { 118U });
  ASSERT_NE(static_data, nullptr);
  const auto* products
    = service.InspectEnvironmentViewProducts(ViewId { 118U });
  ASSERT_NE(products, nullptr);

  EXPECT_FLOAT_EQ(view_data->aerial_perspective_distance_scale, 1000.0F);
  EXPECT_FLOAT_EQ(view_data->aerial_scattering_strength, 0.5F);
  EXPECT_FLOAT_EQ(
    view_data->sky_aerial_luminance_aerial_start_depth_km.w, 0.25F);
  EXPECT_FLOAT_EQ(
    view_data->sky_luminance_factor_height_fog_contribution.w, 0.35F);
  EXPECT_FLOAT_EQ(view_data->camera_aerial_volume_depth_params.x, 32.0F);
  EXPECT_FLOAT_EQ(view_data->camera_aerial_volume_depth_params.y, 1.0F / 32.0F);
  EXPECT_FLOAT_EQ(view_data->camera_aerial_volume_depth_params.z, 3.0F);
  EXPECT_FLOAT_EQ(view_data->camera_aerial_volume_depth_params.w, 1.0F / 3.0F);
  EXPECT_FLOAT_EQ(
    view_data
      ->trace_sample_scale_transmittance_min_light_elevation_holdout_mainpass.w,
    1.0F);
  EXPECT_TRUE(bindings->camera_aerial_perspective_srv.IsValid());
  EXPECT_EQ(static_data->atmosphere.camera_volume_lut_slot,
    bindings->camera_aerial_perspective_srv.get());
  EXPECT_EQ(products->camera_aerial_perspective_srv,
    bindings->camera_aerial_perspective_srv);

  const auto& generation = service.GetLastViewProductGenerationState();
  EXPECT_TRUE(generation.camera_aerial_perspective_requested);
  EXPECT_TRUE(generation.camera_aerial_perspective_executed);
  EXPECT_EQ(generation.camera_aerial_width, 64U);
  EXPECT_EQ(generation.camera_aerial_height, 64U);
  EXPECT_EQ(generation.camera_aerial_depth, 32U);
  EXPECT_FLOAT_EQ(generation.camera_aerial_sample_count_per_slice, 2.0F);
  EXPECT_EQ(generation.camera_aerial_dispatch_count_x, 16U);
  EXPECT_EQ(generation.camera_aerial_dispatch_count_y, 16U);
  EXPECT_EQ(generation.camera_aerial_dispatch_count_z, 8U);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  AerialPerspectiveLutCvarsDriveRuntimeVolumeControls)
{
  oxygen::console::Console console {};
  renderer_->RegisterConsoleBindings(oxygen::observer_ptr { &console });
  EXPECT_EQ(
    console.Execute("vtx.sky_atmosphere.aerial_perspective_lut.width 16")
      .status,
    oxygen::console::ExecutionStatus::kOk);
  EXPECT_EQ(console
              .Execute("vtx.sky_atmosphere.aerial_perspective_lut."
                       "depth_resolution 8")
              .status,
    oxygen::console::ExecutionStatus::kOk);
  EXPECT_EQ(
    console.Execute("vtx.sky_atmosphere.aerial_perspective_lut.depth_km 64")
      .status,
    oxygen::console::ExecutionStatus::kOk);
  EXPECT_EQ(console
              .Execute("vtx.sky_atmosphere.aerial_perspective_lut."
                       "sample_count_max_per_slice 6")
              .status,
    oxygen::console::ExecutionStatus::kOk);

  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 7U }, oxygen::frame::Slot { 1U });
  auto scene = MakeSceneWithAtmosphereEnvironment();
  auto* environment = scene->GetEnvironment().get();
  ASSERT_NE(environment, nullptr);
  auto atmosphere
    = environment->TryGetSystem<oxygen::scene::environment::SkyAtmosphere>();
  ASSERT_NE(atmosphere, nullptr);
  atmosphere->SetTraceSampleCountScale(4.0F);
  scene->Update();

  auto resolved_view = MakeResolvedView(96.0F, 54.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 119U };
  composition_view.with_atmosphere = true;
  auto ctx
    = MakeRenderContext(ViewId { 119U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };
  ctx.view_constants = graphics_->CreateBuffer({
    .size_bytes = 1024U,
    .usage = oxygen::graphics::BufferUsage::kConstant,
    .memory = oxygen::graphics::BufferMemory::kUpload,
    .debug_name = "EnvironmentLightingServiceBehaviorTest."
                  "AerialPerspectiveCvars.ViewConstants",
  });
  ASSERT_NE(ctx.view_constants, nullptr);

  static_cast<void>(service.PublishEnvironmentBindings(ctx));

  const auto* view_data = service.InspectEnvironmentViewData(ViewId { 119U });
  ASSERT_NE(view_data, nullptr);
  EXPECT_FLOAT_EQ(view_data->camera_aerial_volume_depth_params.x, 8.0F);
  EXPECT_FLOAT_EQ(view_data->camera_aerial_volume_depth_params.y, 1.0F / 8.0F);
  EXPECT_FLOAT_EQ(view_data->camera_aerial_volume_depth_params.z, 8.0F);
  EXPECT_FLOAT_EQ(view_data->camera_aerial_volume_depth_params.w, 1.0F / 8.0F);

  const auto& generation = service.GetLastViewProductGenerationState();
  EXPECT_EQ(generation.camera_aerial_width, 16U);
  EXPECT_EQ(generation.camera_aerial_height, 16U);
  EXPECT_EQ(generation.camera_aerial_depth, 8U);
  EXPECT_FLOAT_EQ(generation.camera_aerial_sample_count_per_slice, 6.0F);
  EXPECT_EQ(generation.camera_aerial_dispatch_count_x, 4U);
  EXPECT_EQ(generation.camera_aerial_dispatch_count_y, 4U);
  EXPECT_EQ(generation.camera_aerial_dispatch_count_z, 2U);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  AerialPerspectiveMainPassDisablePublishesApplyTimeGate)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 6U }, oxygen::frame::Slot { 1U });
  auto scene = MakeSceneWithAtmosphereEnvironment();
  auto* environment = scene->GetEnvironment().get();
  ASSERT_NE(environment, nullptr);
  auto atmosphere
    = environment->TryGetSystem<oxygen::scene::environment::SkyAtmosphere>();
  ASSERT_NE(atmosphere, nullptr);
  atmosphere->SetRenderInMainPass(false);
  scene->Update();

  auto resolved_view = MakeResolvedView(96.0F, 54.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 119U };
  composition_view.with_atmosphere = true;
  auto ctx
    = MakeRenderContext(ViewId { 119U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };

  static_cast<void>(service.PublishEnvironmentBindings(ctx));

  const auto* view_data = service.InspectEnvironmentViewData(ViewId { 119U });
  ASSERT_NE(view_data, nullptr);
  EXPECT_FLOAT_EQ(
    view_data
      ->trace_sample_scale_transmittance_min_light_elevation_holdout_mainpass.w,
    0.0F);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  AtmosphereLutCacheStopsRegeneratingSceneScopeLutsUntilStableStateChanges)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 6U }, oxygen::frame::Slot { 1U });
  auto scene = MakeSceneWithAtmosphereEnvironment();
  static_cast<void>(AddAtmosphereDirectionalLight(*scene, "Primary",
    oxygen::scene::AtmosphereLightSlot::kPrimary, true, 2U, true,
    { 1.0F, 1.0F, 1.0F }, { 1.0F, 0.95F, 0.9F }, 100000.0F));
  static_cast<void>(AddAtmosphereDirectionalLight(*scene, "Secondary",
    oxygen::scene::AtmosphereLightSlot::kSecondary, false, 1U, false,
    { 0.5F, 0.5F, 0.7F }, { 0.6F, 0.7F, 1.0F }, 3000.0F));
  scene->Update();

  auto resolved_view = MakeResolvedView(96.0F, 54.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 181U };
  composition_view.with_atmosphere = true;
  auto ctx
    = MakeRenderContext(ViewId { 181U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };
  ctx.view_constants = graphics_->CreateBuffer({
    .size_bytes = 1024U,
    .usage = oxygen::graphics::BufferUsage::kConstant,
    .memory = oxygen::graphics::BufferMemory::kUpload,
    .debug_name
    = "EnvironmentLightingServiceBehaviorTest.AtmosphereCache.ViewConstants",
  });
  ASSERT_NE(ctx.view_constants, nullptr);

  graphics_->dispatch_log_.dispatches.clear();
  static_cast<void>(service.PublishEnvironmentBindings(ctx));
  EXPECT_EQ(graphics_->dispatch_log_.dispatches.size(), 5U);
  auto first_generation = service.GetLastViewProductGenerationState();
  EXPECT_TRUE(first_generation.atmosphere_lut_cache_valid);
  EXPECT_TRUE(first_generation.dual_atmosphere_lights_participating);
  EXPECT_TRUE(first_generation.transmittance_lut_executed);
  EXPECT_TRUE(first_generation.multi_scattering_lut_executed);
  EXPECT_TRUE(first_generation.distant_sky_light_lut_executed);

  graphics_->dispatch_log_.dispatches.clear();
  static_cast<void>(service.PublishEnvironmentBindings(ctx));
  EXPECT_EQ(graphics_->dispatch_log_.dispatches.size(), 2U);
  auto second_generation = service.GetLastViewProductGenerationState();
  EXPECT_TRUE(second_generation.atmosphere_lut_cache_valid);
  EXPECT_TRUE(second_generation.dual_atmosphere_lights_participating);
  EXPECT_FALSE(second_generation.transmittance_lut_executed);
  EXPECT_FALSE(second_generation.multi_scattering_lut_executed);
  EXPECT_FALSE(second_generation.distant_sky_light_lut_executed);
  EXPECT_EQ(second_generation.transmittance_lut_srv,
    first_generation.transmittance_lut_srv);
  EXPECT_EQ(second_generation.multi_scattering_lut_srv,
    first_generation.multi_scattering_lut_srv);
  EXPECT_EQ(second_generation.distant_sky_light_lut_srv,
    first_generation.distant_sky_light_lut_srv);

  auto atmosphere
    = scene->GetEnvironment()
        ->TryGetSystem<oxygen::scene::environment::SkyAtmosphere>();
  ASSERT_NE(atmosphere.get(), nullptr);
  atmosphere->SetMultiScatteringFactor(0.75F);
  scene->Update();

  graphics_->dispatch_log_.dispatches.clear();
  static_cast<void>(service.PublishEnvironmentBindings(ctx));
  EXPECT_EQ(graphics_->dispatch_log_.dispatches.size(), 5U);
  const auto third_generation = service.GetLastViewProductGenerationState();
  EXPECT_TRUE(third_generation.transmittance_lut_executed);
  EXPECT_TRUE(third_generation.multi_scattering_lut_executed);
  EXPECT_TRUE(third_generation.distant_sky_light_lut_executed);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  RenderSkyAndFogRecordsRealStage15DrawsInsteadOfStubExecutionFlags)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 6U }, oxygen::frame::Slot { 1U });
  auto scene_textures = SceneTextures(*graphics_,
    SceneTexturesConfig {
      .extent = { 64U, 64U },
      .enable_velocity = false,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    });
  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 12U };
  composition_view.with_atmosphere = true;
  composition_view.with_height_fog = true;
  auto ctx = MakeRenderContext(ViewId { 12U }, resolved_view, composition_view);
  auto scene = MakeSceneWithAtmosphereEnvironment();
  ctx.scene = oxygen::observer_ptr { scene.get() };

  graphics_->draw_log_.draws.clear();
  graphics_->graphics_pipeline_log_.binds.clear();

  service.RenderSkyAndFog(ctx, scene_textures);

  const auto& stage15 = service.GetLastStage15State();
  EXPECT_TRUE(stage15.requested);
  EXPECT_TRUE(stage15.sky_requested);
  EXPECT_TRUE(stage15.sky_executed);
  EXPECT_TRUE(stage15.atmosphere_requested);
  EXPECT_TRUE(stage15.atmosphere_executed);
  EXPECT_TRUE(stage15.fog_requested);
  EXPECT_TRUE(stage15.fog_executed);
  ASSERT_EQ(graphics_->draw_log_.draws.size(), 3U);
  for (const auto& draw : graphics_->draw_log_.draws) {
    EXPECT_EQ(draw.vertex_num, 3U);
    EXPECT_EQ(draw.instances_num, 1U);
    EXPECT_EQ(draw.vertex_offset, 0U);
    EXPECT_EQ(draw.instance_offset, 0U);
  }
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  RenderSkyAndFogUsesDedicatedSkyAtmosphereAndFogPipelines)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 7U }, oxygen::frame::Slot { 1U });
  auto scene_textures = SceneTextures(*graphics_,
    SceneTexturesConfig {
      .extent = { 64U, 64U },
      .enable_velocity = false,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    });
  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 13U };
  composition_view.with_atmosphere = true;
  composition_view.with_height_fog = true;
  auto ctx = MakeRenderContext(ViewId { 13U }, resolved_view, composition_view);
  auto scene = MakeSceneWithAtmosphereEnvironment();
  ctx.scene = oxygen::observer_ptr { scene.get() };

  graphics_->graphics_pipeline_log_.binds.clear();

  service.RenderSkyAndFog(ctx, scene_textures);

  const auto has_pipeline
    = [this](const std::string_view pipeline_name,
        const std::string_view source_path, const std::string_view entry_point,
        const bool expect_alpha_blend) -> bool {
    for (const auto& bind : graphics_->graphics_pipeline_log_.binds) {
      const auto& pixel_shader = bind.desc.PixelShader();
      if (pixel_shader.has_value() && pixel_shader->source_path == source_path
        && pixel_shader->entry_point == entry_point
        && bind.desc.GetName() == pipeline_name) {
        const auto& blend_state = bind.desc.BlendState();
        if (!expect_alpha_blend) {
          return blend_state.empty()
            || std::ranges::all_of(blend_state,
              [](const auto& target) { return !target.blend_enable; });
        }
        const auto expected_src_blend
          = pipeline_name == "Vortex.Environment.Fog"
          ? oxygen::graphics::BlendFactor::kOne
          : oxygen::graphics::BlendFactor::kSrcAlpha;
        const auto expected_dest_blend
          = pipeline_name == "Vortex.Environment.Fog"
          ? oxygen::graphics::BlendFactor::kSrcAlpha
          : oxygen::graphics::BlendFactor::kInvSrcAlpha;
        return !blend_state.empty()
          && std::ranges::all_of(blend_state, [&](const auto& target) {
               return target.blend_enable
                 && target.src_blend == expected_src_blend
                 && target.dest_blend == expected_dest_blend
                 && target.src_blend_alpha
                 == oxygen::graphics::BlendFactor::kOne
                 && target.dest_blend_alpha
                 == oxygen::graphics::BlendFactor::kInvSrcAlpha;
             });
      }
    }
    return false;
  };

  const auto& stage15 = service.GetLastStage15State();
  EXPECT_EQ(stage15.sky_draw_count, 1U);
  EXPECT_EQ(stage15.atmosphere_draw_count, 1U);
  EXPECT_EQ(stage15.fog_draw_count, 1U);

  EXPECT_TRUE(has_pipeline("Vortex.Environment.Sky",
    "Vortex/Services/Environment/Sky.hlsl", "VortexSkyPassPS", false));
  EXPECT_TRUE(has_pipeline("Vortex.Environment.Atmosphere",
    "Vortex/Services/Environment/AtmosphereCompose.hlsl",
    "VortexAtmosphereComposePS", true));
  EXPECT_TRUE(has_pipeline("Vortex.Environment.Fog",
    "Vortex/Services/Environment/Fog.hlsl", "VortexFogPassPS", true));
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  PublishedHeightFogStaticDataPreservesUe57AuthoredParameters)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 12U }, oxygen::frame::Slot { 0U });
  auto scene = MakeSceneWithAtmosphereEnvironment();
  auto fog
    = scene->GetEnvironment()->TryGetSystem<oxygen::scene::environment::Fog>();
  ASSERT_NE(fog.get(), nullptr);
  fog->SetExtinctionSigmaTPerMeter(0.04F);
  fog->SetHeightFalloffPerMeter(0.35F);
  fog->SetHeightOffsetMeters(12.0F);
  fog->SetSecondFogDensity(0.015F);
  fog->SetSecondFogHeightFalloff(0.14F);
  fog->SetSecondFogHeightOffset(-8.0F);
  fog->SetFogInscatteringLuminance({ 0.2F, 0.3F, 0.4F });
  fog->SetSkyAtmosphereAmbientContributionColorScale({ 0.5F, 0.6F, 0.7F });
  fog->SetDirectionalInscatteringLuminance({ 3.0F, 4.0F, 5.0F });
  fog->SetDirectionalInscatteringExponent(8.0F);
  fog->SetDirectionalInscatteringStartDistance(77.0F);
  fog->SetStartDistanceMeters(23.0F);
  fog->SetEndDistanceMeters(450.0F);
  fog->SetFogCutoffDistanceMeters(900.0F);
  fog->SetMaxOpacity(0.65F);
  fog->SetHoldout(true);
  fog->SetVisibleInReflectionCaptures(false);
  fog->SetVisibleInRealTimeSkyCaptures(false);
  scene->Update();

  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 30U };
  composition_view.with_atmosphere = true;
  composition_view.with_height_fog = true;
  auto ctx = MakeRenderContext(ViewId { 30U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };

  const auto slot = service.PublishEnvironmentBindings(ctx);

  ASSERT_NE(slot, kInvalidShaderVisibleIndex);
  const auto* static_data
    = service.InspectEnvironmentStaticData(ViewId { 30U });
  ASSERT_NE(static_data, nullptr);
  const auto* products = service.InspectEnvironmentViewProducts(ViewId { 30U });
  ASSERT_NE(products, nullptr);
  const auto& gpu_fog = static_data->fog;
  EXPECT_NE(gpu_fog.flags & kGpuFogFlagEnabled, 0U);
  EXPECT_NE(gpu_fog.flags & kGpuFogFlagHeightFogEnabled, 0U);
  EXPECT_NE(gpu_fog.flags & kGpuFogFlagRenderInMainPass, 0U);
  EXPECT_NE(gpu_fog.flags & kGpuFogFlagDirectionalInscattering, 0U);
  EXPECT_NE(gpu_fog.flags & kGpuFogFlagHoldout, 0U);
  EXPECT_EQ(gpu_fog.flags & kGpuFogFlagVisibleInReflectionCaptures, 0U);
  EXPECT_EQ(gpu_fog.flags & kGpuFogFlagVisibleInRealTimeSkyCaptures, 0U);
  EXPECT_FLOAT_EQ(gpu_fog.primary_density, 0.04F);
  EXPECT_FLOAT_EQ(gpu_fog.primary_height_falloff, 0.35F);
  EXPECT_FLOAT_EQ(gpu_fog.primary_height_offset_m, 12.0F);
  EXPECT_FLOAT_EQ(gpu_fog.secondary_density, 0.015F);
  EXPECT_FLOAT_EQ(gpu_fog.secondary_height_falloff, 0.14F);
  EXPECT_FLOAT_EQ(gpu_fog.secondary_height_offset_m, -8.0F);
  EXPECT_FLOAT_EQ(gpu_fog.start_distance_m, 23.0F);
  EXPECT_FLOAT_EQ(gpu_fog.end_distance_m, 450.0F);
  EXPECT_FLOAT_EQ(gpu_fog.cutoff_distance_m, 900.0F);
  EXPECT_FLOAT_EQ(gpu_fog.max_opacity, 0.65F);
  EXPECT_FLOAT_EQ(gpu_fog.min_transmittance, 0.35F);
  EXPECT_FLOAT_EQ(gpu_fog.directional_start_distance_m, 77.0F);
  EXPECT_FLOAT_EQ(gpu_fog.directional_exponent, 8.0F);
  EXPECT_FLOAT_EQ(gpu_fog.fog_inscattering_luminance_rgb[0], 0.2F);
  EXPECT_FLOAT_EQ(gpu_fog.fog_inscattering_luminance_rgb[1], 0.3F);
  EXPECT_FLOAT_EQ(gpu_fog.fog_inscattering_luminance_rgb[2], 0.4F);
  EXPECT_FLOAT_EQ(gpu_fog.directional_inscattering_luminance_rgb[0], 3.0F);
  EXPECT_FLOAT_EQ(gpu_fog.directional_inscattering_luminance_rgb[1], 4.0F);
  EXPECT_FLOAT_EQ(gpu_fog.directional_inscattering_luminance_rgb[2], 5.0F);
  EXPECT_FLOAT_EQ(
    gpu_fog.sky_atmosphere_ambient_contribution_color_scale_rgb[0], 0.5F);
  EXPECT_FLOAT_EQ(
    gpu_fog.sky_atmosphere_ambient_contribution_color_scale_rgb[1], 0.6F);
  EXPECT_FLOAT_EQ(
    gpu_fog.sky_atmosphere_ambient_contribution_color_scale_rgb[2], 0.7F);
  EXPECT_FLOAT_EQ(products->height_fog.second_fog_density, 0.015F);
  EXPECT_FLOAT_EQ(products->height_fog.directional_inscattering_exponent, 8.0F);
  EXPECT_FALSE(products->height_fog.visible_in_reflection_captures);
  EXPECT_FALSE(products->height_fog.visible_in_real_time_sky_captures);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  DisabledHeightFogDoesNotPublishActiveGpuFog)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 13U }, oxygen::frame::Slot { 0U });
  auto scene = MakeSceneWithAtmosphereEnvironment();
  auto fog
    = scene->GetEnvironment()->TryGetSystem<oxygen::scene::environment::Fog>();
  ASSERT_NE(fog.get(), nullptr);
  fog->SetEnableHeightFog(false);
  scene->Update();

  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 31U };
  composition_view.with_atmosphere = true;
  composition_view.with_height_fog = true;
  auto ctx = MakeRenderContext(ViewId { 31U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };

  static_cast<void>(service.PublishEnvironmentBindings(ctx));

  const auto* static_data
    = service.InspectEnvironmentStaticData(ViewId { 31U });
  ASSERT_NE(static_data, nullptr);
  EXPECT_EQ(static_data->fog.flags & kGpuFogFlagEnabled, 0U);
  EXPECT_EQ(static_data->fog.flags & kGpuFogFlagHeightFogEnabled, 0U);
  EXPECT_FLOAT_EQ(static_data->fog.primary_density, 0.05F);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  CubemapInscatteringPublishesAuthoredButUnavailableResourceState)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 14U }, oxygen::frame::Slot { 0U });
  auto scene = MakeSceneWithAtmosphereEnvironment();
  auto fog
    = scene->GetEnvironment()->TryGetSystem<oxygen::scene::environment::Fog>();
  ASSERT_NE(fog.get(), nullptr);
  fog->SetInscatteringColorCubemapResource(
    oxygen::content::ResourceKey { 123U });
  fog->SetInscatteringColorCubemapAngle(90.0F);
  fog->SetInscatteringTextureTint({ 0.25F, 0.5F, 0.75F });
  fog->SetNonDirectionalInscatteringColorDistance(1000.0F);
  fog->SetFullyDirectionalInscatteringColorDistance(5000.0F);
  scene->Update();

  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 32U };
  composition_view.with_atmosphere = true;
  composition_view.with_height_fog = true;
  auto ctx = MakeRenderContext(ViewId { 32U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };

  static_cast<void>(service.PublishEnvironmentBindings(ctx));

  const auto* static_data
    = service.InspectEnvironmentStaticData(ViewId { 32U });
  ASSERT_NE(static_data, nullptr);
  const auto& gpu_fog = static_data->fog;
  EXPECT_NE(gpu_fog.flags & kGpuFogFlagCubemapAuthored, 0U);
  EXPECT_EQ(gpu_fog.flags & kGpuFogFlagCubemapUsable, 0U);
  EXPECT_EQ(gpu_fog.flags & kGpuFogFlagDirectionalInscattering, 0U);
  EXPECT_EQ(gpu_fog.cubemap_srv, oxygen::kInvalidBindlessIndex);
  EXPECT_FLOAT_EQ(
    gpu_fog.cubemap_angle_radians, 3.14159265358979323846F * 0.5F);
  EXPECT_FLOAT_EQ(gpu_fog.cubemap_fade_inv_range, 1.0F / 4000.0F);
  EXPECT_FLOAT_EQ(gpu_fog.cubemap_fade_bias, -1000.0F / 4000.0F);
  EXPECT_FLOAT_EQ(gpu_fog.inscattering_texture_tint_rgb[0], 0.25F);
  EXPECT_FLOAT_EQ(gpu_fog.inscattering_texture_tint_rgb[1], 0.5F);
  EXPECT_FLOAT_EQ(gpu_fog.inscattering_texture_tint_rgb[2], 0.75F);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  PublishedBindingsStayAbsentSafeWhileAmbientBridgeRemainsOptIn)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 8U }, oxygen::frame::Slot { 2U });
  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 14U };
  composition_view.with_atmosphere = true;
  auto ctx = MakeRenderContext(ViewId { 14U }, resolved_view, composition_view);

  const auto slot = service.PublishEnvironmentBindings(ctx);

  ASSERT_NE(slot, kInvalidShaderVisibleIndex);
  const auto* bindings = service.InspectBindings(ViewId { 14U });
  ASSERT_NE(bindings, nullptr);
  EXPECT_EQ(service.ResolveEnvironmentFrameSlot(ViewId { 14U }), slot);
  EXPECT_EQ(
    bindings->ambient_bridge.irradiance_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings->ambient_bridge.flags, 0U);
  EXPECT_EQ(service.GetLastPublicationState().published_view_count, 1U);
  EXPECT_EQ(service.GetLastPublicationState().ambient_bridge_view_count, 0U);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  EnabledSkyLightPublishesUnavailableIblInsteadOfRevisionOnlyResources)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 8U }, oxygen::frame::Slot { 2U });
  service.RefreshPersistentProbeState(true);
  auto scene = MakeSceneWithAtmosphereEnvironment();
  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 25U };
  composition_view.with_atmosphere = true;
  auto ctx = MakeRenderContext(ViewId { 25U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };

  const auto slot = service.PublishEnvironmentBindings(ctx);

  ASSERT_NE(slot, kInvalidShaderVisibleIndex);
  const auto* bindings = service.InspectBindings(ViewId { 25U });
  ASSERT_NE(bindings, nullptr);
  const auto* static_data
    = service.InspectEnvironmentStaticData(ViewId { 25U });
  ASSERT_NE(static_data, nullptr);
  const auto* products = service.InspectEnvironmentViewProducts(ViewId { 25U });
  ASSERT_NE(products, nullptr);

  EXPECT_NE(bindings->contract_flags
      & oxygen::vortex::kEnvironmentContractFlagSkyLightAuthoredEnabled,
    0U);
  EXPECT_NE(bindings->contract_flags
      & oxygen::vortex::kEnvironmentContractFlagSkyLightIblUnavailable,
    0U);
  EXPECT_EQ(bindings->contract_flags
      & oxygen::vortex::kEnvironmentContractFlagSkyLightIblValid,
    0U);
  EXPECT_EQ(bindings->probes.probe_revision, 1U);
  EXPECT_EQ(bindings->probes.environment_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings->probes.irradiance_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings->probes.prefiltered_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings->probes.brdf_lut_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(static_data->sky_light.enabled, 0U);
  EXPECT_EQ(static_data->sky_light.cubemap_slot, oxygen::kInvalidBindlessIndex);
  EXPECT_EQ(
    static_data->sky_light.brdf_lut_slot, oxygen::kInvalidBindlessIndex);
  EXPECT_EQ(
    static_data->sky_light.irradiance_map_slot, oxygen::kInvalidBindlessIndex);
  EXPECT_EQ(
    static_data->sky_light.prefilter_map_slot, oxygen::kInvalidBindlessIndex);
  EXPECT_EQ(static_data->sky_light.ibl_generation, 0U);
  EXPECT_NE(products->flags
      & oxygen::vortex::environment::
        kEnvironmentViewProductFlagSkyLightAuthoredEnabled,
    0U);
  EXPECT_NE(products->flags
      & oxygen::vortex::environment::
        kEnvironmentViewProductFlagSkyLightIblUnavailable,
    0U);
  EXPECT_EQ(products->flags
      & oxygen::vortex::environment::
        kEnvironmentViewProductFlagSkyLightIblValid,
    0U);
  EXPECT_TRUE(service.GetLastPublicationState().sky_light_authored_enabled);
  EXPECT_FALSE(service.GetLastPublicationState().sky_light_ibl_valid);
  EXPECT_TRUE(service.GetLastPublicationState().sky_light_ibl_unavailable);
  EXPECT_TRUE(service.GetLastPublicationState().sky_light_ibl_stale);
  EXPECT_TRUE(
    service.GetLastViewProductGenerationState().sky_light_authored_enabled);
  EXPECT_FALSE(service.GetLastViewProductGenerationState().sky_light_ibl_valid);
  EXPECT_TRUE(
    service.GetLastViewProductGenerationState().sky_light_ibl_unavailable);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  DisabledSkyLightDoesNotPublishUnavailableOrUsableIblState)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 9U }, oxygen::frame::Slot { 2U });
  auto scene = MakeSceneWithAtmosphereEnvironment();
  auto sky_light = scene->GetEnvironment()
                     ->TryGetSystem<oxygen::scene::environment::SkyLight>();
  ASSERT_NE(sky_light.get(), nullptr);
  sky_light->SetEnabled(false);
  scene->Update();
  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 26U };
  composition_view.with_atmosphere = true;
  auto ctx = MakeRenderContext(ViewId { 26U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };

  const auto slot = service.PublishEnvironmentBindings(ctx);

  ASSERT_NE(slot, kInvalidShaderVisibleIndex);
  const auto* bindings = service.InspectBindings(ViewId { 26U });
  ASSERT_NE(bindings, nullptr);
  const auto* products = service.InspectEnvironmentViewProducts(ViewId { 26U });
  ASSERT_NE(products, nullptr);
  EXPECT_EQ(bindings->contract_flags
      & oxygen::vortex::kEnvironmentContractFlagSkyLightAuthoredEnabled,
    0U);
  EXPECT_EQ(bindings->contract_flags
      & oxygen::vortex::kEnvironmentContractFlagSkyLightIblUnavailable,
    0U);
  EXPECT_EQ(bindings->contract_flags
      & oxygen::vortex::kEnvironmentContractFlagSkyLightIblValid,
    0U);
  EXPECT_EQ(products->flags
      & oxygen::vortex::environment::
        kEnvironmentViewProductFlagSkyLightAuthoredEnabled,
    0U);
  EXPECT_FALSE(service.GetLastPublicationState().sky_light_authored_enabled);
  EXPECT_FALSE(service.GetLastPublicationState().sky_light_ibl_valid);
  EXPECT_FALSE(service.GetLastPublicationState().sky_light_ibl_unavailable);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  AuthoredVolumetricFogStaysUnavailableWhenCurrentViewDoesNotRequestFog)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 10U }, oxygen::frame::Slot { 2U });
  auto scene = MakeSceneWithAtmosphereEnvironment();
  auto fog
    = scene->GetEnvironment()->TryGetSystem<oxygen::scene::environment::Fog>();
  ASSERT_NE(fog.get(), nullptr);
  fog->SetEnableVolumetricFog(true);
  scene->Update();
  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 27U };
  composition_view.with_atmosphere = true;
  auto ctx = MakeRenderContext(ViewId { 27U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };

  const auto slot = service.PublishEnvironmentBindings(ctx);

  ASSERT_NE(slot, kInvalidShaderVisibleIndex);
  const auto* bindings = service.InspectBindings(ViewId { 27U });
  ASSERT_NE(bindings, nullptr);
  const auto* products = service.InspectEnvironmentViewProducts(ViewId { 27U });
  ASSERT_NE(products, nullptr);
  EXPECT_TRUE(products->volumetric_fog.enabled);
  EXPECT_EQ(
    products->integrated_light_scattering_srv, kInvalidShaderVisibleIndex);
  EXPECT_NE(bindings->contract_flags
      & oxygen::vortex::kEnvironmentContractFlagVolumetricFogAuthoredEnabled,
    0U);
  EXPECT_NE(bindings->contract_flags
      & oxygen::vortex::
        kEnvironmentContractFlagIntegratedLightScatteringUnavailable,
    0U);
  EXPECT_NE(products->flags
      & oxygen::vortex::environment::
        kEnvironmentViewProductFlagVolumetricFogAuthoredEnabled,
    0U);
  EXPECT_NE(products->flags
      & oxygen::vortex::environment::
        kEnvironmentViewProductFlagIntegratedLightScatteringUnavailable,
    0U);
  EXPECT_TRUE(
    service.GetLastPublicationState().volumetric_fog_authored_enabled);
  EXPECT_FALSE(
    service.GetLastPublicationState().integrated_light_scattering_valid);
  EXPECT_TRUE(
    service.GetLastPublicationState().integrated_light_scattering_unavailable);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  AuthoredVolumetricFogPublishesIntegratedScatteringForFogView)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 11U }, oxygen::frame::Slot { 2U });
  auto scene = MakeSceneWithAtmosphereEnvironment();
  static_cast<void>(AddAtmosphereDirectionalLight(*scene, "Primary",
    oxygen::scene::AtmosphereLightSlot::kPrimary, true, 4U, true,
    { 1.0F, 1.0F, 1.0F }, { 1.0F, 0.95F, 0.9F }, 100000.0F));
  auto fog
    = scene->GetEnvironment()->TryGetSystem<oxygen::scene::environment::Fog>();
  ASSERT_NE(fog.get(), nullptr);
  fog->SetEnableVolumetricFog(true);
  fog->SetVolumetricFogDistance(96000.0F);
  fog->SetVolumetricFogStartDistance(50.0F);
  fog->SetVolumetricFogNearFadeInDistance(100.0F);
  fog->SetVolumetricFogAlbedo({ 0.6F, 0.7F, 0.8F });
  fog->SetVolumetricFogEmissive({ 0.01F, 0.02F, 0.03F });
  fog->SetVolumetricFogExtinctionScale(1.5F);
  scene->Update();
  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 31U };
  composition_view.with_atmosphere = true;
  composition_view.with_height_fog = true;
  auto ctx = MakeRenderContext(ViewId { 31U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };
  ctx.view_constants = graphics_->CreateBuffer({
    .size_bytes = 1024U,
    .usage = oxygen::graphics::BufferUsage::kConstant,
    .memory = oxygen::graphics::BufferMemory::kUpload,
    .debug_name
    = "EnvironmentLightingServiceBehaviorTest.VolumetricFog.ViewConstants",
  });
  ASSERT_NE(ctx.view_constants, nullptr);

  graphics_->dispatch_log_.dispatches.clear();

  const auto slot = service.PublishEnvironmentBindings(ctx);

  ASSERT_NE(slot, kInvalidShaderVisibleIndex);
  const auto* bindings = service.InspectBindings(ViewId { 31U });
  ASSERT_NE(bindings, nullptr);
  const auto* products = service.InspectEnvironmentViewProducts(ViewId { 31U });
  ASSERT_NE(products, nullptr);
  const auto* static_data
    = service.InspectEnvironmentStaticData(ViewId { 31U });
  ASSERT_NE(static_data, nullptr);
  EXPECT_TRUE(products->volumetric_fog.enabled);
  EXPECT_NE(
    products->integrated_light_scattering_srv, kInvalidShaderVisibleIndex);
  EXPECT_NE(bindings->contract_flags
      & oxygen::vortex::kEnvironmentContractFlagVolumetricFogAuthoredEnabled,
    0U);
  EXPECT_NE(bindings->contract_flags
      & oxygen::vortex::kEnvironmentContractFlagIntegratedLightScatteringValid,
    0U);
  EXPECT_EQ(bindings->contract_flags
      & oxygen::vortex::
        kEnvironmentContractFlagIntegratedLightScatteringUnavailable,
    0U);
  EXPECT_NE(products->flags
      & oxygen::vortex::environment::
        kEnvironmentViewProductFlagVolumetricFogAuthoredEnabled,
    0U);
  EXPECT_NE(products->flags
      & oxygen::vortex::environment::
        kEnvironmentViewProductFlagIntegratedLightScatteringValid,
    0U);
  EXPECT_EQ(products->flags
      & oxygen::vortex::environment::
        kEnvironmentViewProductFlagIntegratedLightScatteringUnavailable,
    0U);
  EXPECT_EQ(static_data->volumetric_fog.integrated_light_scattering_srv,
    products->integrated_light_scattering_srv.get());
  EXPECT_NE(
    static_data->volumetric_fog.flags & kGpuVolumetricFogFlagEnabled, 0U);
  EXPECT_NE(static_data->volumetric_fog.flags
      & kGpuVolumetricFogFlagIntegratedScatteringValid,
    0U);
  EXPECT_EQ(static_data->volumetric_fog.grid_width, 8U);
  EXPECT_EQ(static_data->volumetric_fog.grid_height, 8U);
  EXPECT_EQ(static_data->volumetric_fog.grid_depth, 32U);
  EXPECT_GT(static_data->volumetric_fog.grid_z_params[0], 0.0F);
  EXPECT_LT(static_data->volumetric_fog.grid_z_params[1], 1.0F);
  EXPECT_FLOAT_EQ(static_data->volumetric_fog.grid_z_params[2], 32.0F);
  EXPECT_FLOAT_EQ(static_data->volumetric_fog.distance_m, 96000.0F);
  EXPECT_FLOAT_EQ(static_data->volumetric_fog.start_distance_m, 50.0F);
  EXPECT_TRUE(
    service.GetLastPublicationState().volumetric_fog_authored_enabled);
  EXPECT_TRUE(
    service.GetLastPublicationState().integrated_light_scattering_valid);
  EXPECT_FALSE(
    service.GetLastPublicationState().integrated_light_scattering_unavailable);
  EXPECT_TRUE(graphics_->dispatch_log_.dispatches.size() >= 1U);

  const auto& generation = service.GetLastViewProductGenerationState();
  EXPECT_TRUE(generation.integrated_light_scattering_valid);
  EXPECT_FALSE(generation.integrated_light_scattering_unavailable);
  EXPECT_TRUE(generation.volumetric_fog_view_constants_bound);
  EXPECT_TRUE(generation.volumetric_fog_ue_log_depth_distribution);
  EXPECT_TRUE(generation.volumetric_fog_directional_shadowed_light_requested);
  EXPECT_TRUE(generation.volumetric_fog_height_fog_media_requested);
  EXPECT_TRUE(generation.volumetric_fog_height_fog_media_executed);
  EXPECT_TRUE(generation.volumetric_fog_sky_light_injection_requested);
  EXPECT_TRUE(generation.volumetric_fog_sky_light_injection_executed);
  EXPECT_TRUE(generation.volumetric_fog_temporal_history_requested);
  EXPECT_FALSE(
    generation.volumetric_fog_temporal_history_reprojection_executed);
  EXPECT_TRUE(generation.volumetric_fog_temporal_history_reset);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  NoVolumetricsFeatureProfileSuppressesIntegratedScatteringButKeepsEnvironment)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 11U }, oxygen::frame::Slot { 2U });
  auto scene = MakeSceneWithAtmosphereEnvironment();
  static_cast<void>(AddAtmosphereDirectionalLight(*scene, "Primary",
    oxygen::scene::AtmosphereLightSlot::kPrimary, true, 4U, true,
    { 1.0F, 1.0F, 1.0F }, { 1.0F, 0.95F, 0.9F }, 100000.0F));
  auto fog
    = scene->GetEnvironment()->TryGetSystem<oxygen::scene::environment::Fog>();
  ASSERT_NE(fog.get(), nullptr);
  fog->SetEnableVolumetricFog(true);
  fog->SetVolumetricFogDistance(96000.0F);
  fog->SetVolumetricFogStartDistance(50.0F);
  fog->SetVolumetricFogNearFadeInDistance(100.0F);
  scene->Update();
  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 33U };
  composition_view.with_atmosphere = true;
  composition_view.with_height_fog = true;
  composition_view.feature_profile
    = oxygen::vortex::CompositionView::ViewFeatureProfile::kNoVolumetrics;
  composition_view.feature_mask = oxygen::vortex::ResolveViewFeatureProfileSpec(
    composition_view.feature_profile)
                                    .feature_mask;
  auto ctx = MakeRenderContext(ViewId { 33U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };
  ctx.view_constants = graphics_->CreateBuffer({
    .size_bytes = 1024U,
    .usage = oxygen::graphics::BufferUsage::kConstant,
    .memory = oxygen::graphics::BufferMemory::kUpload,
    .debug_name = "EnvironmentLightingServiceBehaviorTest."
                  "NoVolumetrics.ViewConstants",
  });
  ASSERT_NE(ctx.view_constants, nullptr);

  const auto slot = service.PublishEnvironmentBindings(ctx);

  ASSERT_NE(slot, kInvalidShaderVisibleIndex);
  const auto* bindings = service.InspectBindings(ViewId { 33U });
  ASSERT_NE(bindings, nullptr);
  const auto* products = service.InspectEnvironmentViewProducts(ViewId { 33U });
  ASSERT_NE(products, nullptr);
  const auto* static_data
    = service.InspectEnvironmentStaticData(ViewId { 33U });
  ASSERT_NE(static_data, nullptr);
  EXPECT_NE(bindings->environment_static_slot, kInvalidShaderVisibleIndex);
  EXPECT_NE(bindings->environment_view_slot, kInvalidShaderVisibleIndex);
  EXPECT_FALSE(products->volumetric_fog.enabled);
  EXPECT_FALSE(products->height_fog.enable_volumetric_fog);
  EXPECT_EQ(
    products->integrated_light_scattering_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings->contract_flags
      & oxygen::vortex::kEnvironmentContractFlagVolumetricFogAuthoredEnabled,
    0U);
  EXPECT_EQ(bindings->contract_flags
      & oxygen::vortex::kEnvironmentContractFlagIntegratedLightScatteringValid,
    0U);
  EXPECT_EQ(bindings->contract_flags
      & oxygen::vortex::
        kEnvironmentContractFlagIntegratedLightScatteringUnavailable,
    0U);
  EXPECT_EQ(products->flags
      & oxygen::vortex::environment::
        kEnvironmentViewProductFlagVolumetricFogAuthoredEnabled,
    0U);
  EXPECT_EQ(products->flags
      & oxygen::vortex::environment::
        kEnvironmentViewProductFlagIntegratedLightScatteringValid,
    0U);
  EXPECT_EQ(products->flags
      & oxygen::vortex::environment::
        kEnvironmentViewProductFlagIntegratedLightScatteringUnavailable,
    0U);
  EXPECT_EQ(static_data->volumetric_fog.integrated_light_scattering_srv,
    oxygen::kInvalidBindlessIndex);
  EXPECT_EQ(
    static_data->volumetric_fog.flags & kGpuVolumetricFogFlagEnabled, 0U);
  EXPECT_EQ(static_data->volumetric_fog.flags
      & kGpuVolumetricFogFlagIntegratedScatteringValid,
    0U);
  EXPECT_EQ(
    static_data->fog.flags & oxygen::vortex::kGpuFogFlagVolumetricFogAuthored,
    0U);
  EXPECT_FALSE(
    service.GetLastPublicationState().volumetric_fog_authored_enabled);
  EXPECT_FALSE(
    service.GetLastPublicationState().integrated_light_scattering_valid);
  EXPECT_FALSE(
    service.GetLastPublicationState().integrated_light_scattering_unavailable);
  const auto& generation = service.GetLastViewProductGenerationState();
  EXPECT_TRUE(generation.environment_view_products_published);
  EXPECT_FALSE(generation.volumetric_fog_authored_enabled);
  EXPECT_FALSE(generation.integrated_light_scattering_valid);
  EXPECT_FALSE(generation.integrated_light_scattering_unavailable);
  EXPECT_FALSE(generation.volumetric_fog_view_constants_bound);
  EXPECT_FALSE(generation.volumetric_fog_height_fog_media_requested);
  EXPECT_FALSE(generation.volumetric_fog_sky_light_injection_requested);
  EXPECT_FALSE(generation.volumetric_fog_temporal_history_requested);
  EXPECT_FALSE(generation.volumetric_fog_local_fog_injection_requested);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  AuthoredVolumetricFogReusesPerViewTemporalHistoryOnSecondFrame)
{
  auto service = EnvironmentLightingService(*renderer_);
  auto scene = MakeSceneWithAtmosphereEnvironment();
  static_cast<void>(AddAtmosphereDirectionalLight(*scene, "Primary",
    oxygen::scene::AtmosphereLightSlot::kPrimary, true, 4U, true,
    { 1.0F, 1.0F, 1.0F }, { 1.0F, 0.95F, 0.9F }, 100000.0F));
  auto fog
    = scene->GetEnvironment()->TryGetSystem<oxygen::scene::environment::Fog>();
  ASSERT_NE(fog.get(), nullptr);
  fog->SetEnableVolumetricFog(true);
  fog->SetVolumetricFogDistance(96000.0F);
  fog->SetVolumetricFogStartDistance(50.0F);
  scene->Update();

  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 33U };
  composition_view.with_atmosphere = true;
  composition_view.with_height_fog = true;
  auto ctx = MakeRenderContext(ViewId { 33U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };
  ctx.view_constants = graphics_->CreateBuffer({
    .size_bytes = 1024U,
    .usage = oxygen::graphics::BufferUsage::kConstant,
    .memory = oxygen::graphics::BufferMemory::kUpload,
    .debug_name
    = "EnvironmentLightingServiceBehaviorTest.VolumetricHistory.ViewConstants",
  });
  ASSERT_NE(ctx.view_constants, nullptr);

  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 13U }, oxygen::frame::Slot { 2U });
  ASSERT_NE(
    service.PublishEnvironmentBindings(ctx), kInvalidShaderVisibleIndex);
  auto first_generation = service.GetLastViewProductGenerationState();
  EXPECT_TRUE(first_generation.volumetric_fog_temporal_history_requested);
  EXPECT_FALSE(
    first_generation.volumetric_fog_temporal_history_reprojection_executed);
  EXPECT_TRUE(first_generation.volumetric_fog_temporal_history_reset);

  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 14U }, oxygen::frame::Slot { 0U });
  ASSERT_NE(
    service.PublishEnvironmentBindings(ctx), kInvalidShaderVisibleIndex);
  const auto& second_generation = service.GetLastViewProductGenerationState();
  EXPECT_TRUE(second_generation.volumetric_fog_temporal_history_requested);
  EXPECT_TRUE(
    second_generation.volumetric_fog_temporal_history_reprojection_executed);
  EXPECT_FALSE(second_generation.volumetric_fog_temporal_history_reset);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  AuthoredVolumetricFogInjectsTiledLocalFogProductsWhenAvailable)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 12U }, oxygen::frame::Slot { 2U });
  auto scene_textures = SceneTextures(*graphics_,
    SceneTexturesConfig {
      .extent = { 64U, 64U },
      .enable_velocity = false,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    });
  auto scene = MakeSceneWithAtmosphereEnvironment();
  static_cast<void>(AddAtmosphereDirectionalLight(*scene, "Primary",
    oxygen::scene::AtmosphereLightSlot::kPrimary, true, 4U, true,
    { 1.0F, 1.0F, 1.0F }, { 1.0F, 0.95F, 0.9F }, 100000.0F));
  auto fog
    = scene->GetEnvironment()->TryGetSystem<oxygen::scene::environment::Fog>();
  ASSERT_NE(fog.get(), nullptr);
  fog->SetEnableVolumetricFog(true);
  fog->SetVolumetricFogDistance(96000.0F);
  fog->SetVolumetricFogStartDistance(10.0F);
  fog->SetVolumetricFogNearFadeInDistance(50.0F);

  auto local_fog_node = scene->CreateNode("VolumetricLocalFog");
  const auto impl = local_fog_node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  impl->get().AddComponent<oxygen::scene::environment::LocalFogVolume>();
  auto& local_fog
    = impl->get().GetComponent<oxygen::scene::environment::LocalFogVolume>();
  local_fog.SetEnabled(true);
  local_fog.SetRadialFogExtinction(0.45F);
  local_fog.SetHeightFogExtinction(0.25F);
  local_fog.SetHeightFogFalloff(0.2F);
  local_fog.SetFogAlbedo({ 0.7F, 0.8F, 0.9F });
  local_fog.SetFogEmissive({ 0.1F, 0.2F, 0.3F });
  local_fog_node.GetTransform().SetLocalPosition({ 0.0F, 0.0F, 12.0F });
  local_fog_node.GetTransform().SetLocalScale({ 4.0F, 4.0F, 4.0F });
  scene->Update();

  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 32U };
  composition_view.with_atmosphere = true;
  composition_view.with_height_fog = true;
  composition_view.with_local_fog = true;
  auto ctx = MakeRenderContext(ViewId { 32U }, resolved_view, composition_view);
  ctx.scene = oxygen::observer_ptr { scene.get() };
  ctx.view_constants = graphics_->CreateBuffer({
    .size_bytes = 1024U,
    .usage = oxygen::graphics::BufferUsage::kConstant,
    .memory = oxygen::graphics::BufferMemory::kUpload,
    .debug_name
    = "EnvironmentLightingServiceBehaviorTest.VolumetricLocalFog.ViewConstants",
  });
  ASSERT_NE(ctx.view_constants, nullptr);

  graphics_->dispatch_log_.dispatches.clear();

  const auto slot
    = service.PublishEnvironmentBindings(ctx, kInvalidShaderVisibleIndex,
      kInvalidShaderVisibleIndex, false, &scene_textures);

  ASSERT_NE(slot, kInvalidShaderVisibleIndex);
  const auto& generation = service.GetLastViewProductGenerationState();
  EXPECT_TRUE(generation.integrated_light_scattering_valid);
  EXPECT_TRUE(generation.volumetric_fog_height_fog_media_requested);
  EXPECT_TRUE(generation.volumetric_fog_height_fog_media_executed);
  EXPECT_TRUE(generation.volumetric_fog_sky_light_injection_requested);
  EXPECT_TRUE(generation.volumetric_fog_sky_light_injection_executed);
  EXPECT_TRUE(generation.volumetric_fog_temporal_history_requested);
  EXPECT_TRUE(generation.volumetric_fog_local_fog_injection_requested);
  EXPECT_TRUE(generation.volumetric_fog_local_fog_injection_executed);
  EXPECT_EQ(generation.volumetric_fog_local_fog_instance_count, 1U);
  EXPECT_GE(graphics_->dispatch_log_.dispatches.size(), 2U);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  LocalFogStage14AndStage15RunWhenSceneContainsLocalFogVolumes)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 9U }, oxygen::frame::Slot { 2U });
  auto scene_textures = SceneTextures(*graphics_,
    SceneTexturesConfig {
      .extent = { 64U, 64U },
      .enable_velocity = false,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    });
  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 15U };
  composition_view.with_atmosphere = true;
  composition_view.with_height_fog = true;
  composition_view.with_local_fog = true;
  auto ctx = MakeRenderContext(ViewId { 15U }, resolved_view, composition_view);
  ctx.current_view.screen_hzb_available = true;
  ctx.current_view.screen_hzb_frame_slot = oxygen::ShaderVisibleIndex { 17U };
  ctx.current_view.screen_hzb_width = 32U;
  ctx.current_view.screen_hzb_height = 32U;
  ctx.current_view.screen_hzb_mip_count = 5U;
  auto scene = MakeSceneWithLocalFog();
  ctx.scene = oxygen::observer_ptr { scene.get() };

  graphics_->draw_log_.draws.clear();
  graphics_->dispatch_log_.dispatches.clear();
  graphics_->indirect_log_.draws.clear();

  service.RenderSkyAndFog(ctx, scene_textures);

  const auto& stage14 = service.GetLastStage14State();
  EXPECT_TRUE(stage14.requested);
  EXPECT_TRUE(stage14.local_fog_requested);
  EXPECT_TRUE(stage14.local_fog_executed);
  EXPECT_TRUE(stage14.local_fog_hzb_consumed);
  EXPECT_TRUE(stage14.local_fog_buffer_ready);
  EXPECT_EQ(stage14.local_fog_instance_count, 1U);
  EXPECT_GE(stage14.local_fog_dispatch_count_x, 1U);
  EXPECT_GE(stage14.local_fog_dispatch_count_y, 1U);
  EXPECT_EQ(stage14.local_fog_dispatch_count_z, 1U);

  const auto& stage15 = service.GetLastStage15State();
  EXPECT_TRUE(stage15.local_fog_requested);
  EXPECT_TRUE(stage15.local_fog_executed);
  EXPECT_EQ(stage15.local_fog_draw_count, 1U);
  EXPECT_EQ(graphics_->dispatch_log_.dispatches.size(), 1U);
  EXPECT_EQ(graphics_->draw_log_.draws.size(), 3U);
  ASSERT_EQ(graphics_->indirect_log_.draws.size(), 1U);
  EXPECT_EQ(graphics_->indirect_log_.draws[0].command_desc.kind,
    oxygen::graphics::CommandRecorder::IndirectCommandKind::kDraw);
  EXPECT_EQ(graphics_->indirect_log_.draws[0].execution_desc.command_count,
    oxygen::graphics::CommandRecorder::IndirectCommandCount { 1U });
  EXPECT_EQ(
    graphics_->indirect_log_.draws[0].execution_desc.count_buffer, nullptr);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  LocalFogStage14UsesViewportTileResolutionForSubViewportViews)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 10U }, oxygen::frame::Slot { 2U });
  auto scene_textures = SceneTextures(*graphics_,
    SceneTexturesConfig {
      .extent = { 2048U, 128U },
      .enable_velocity = false,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    });
  auto resolved_view = MakeResolvedView(128.0F, 128.0F, 512.0F, 0.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 17U };
  composition_view.with_local_fog = true;
  auto ctx = MakeRenderContext(ViewId { 17U }, resolved_view, composition_view);
  ctx.frame_sequence = oxygen::frame::SequenceNumber { 10U };
  ctx.frame_slot = oxygen::frame::Slot { 2U };
  ctx.current_view.screen_hzb_available = true;
  ctx.current_view.screen_hzb_frame_slot = oxygen::ShaderVisibleIndex { 27U };
  ctx.current_view.screen_hzb_width = 64U;
  ctx.current_view.screen_hzb_height = 64U;
  ctx.current_view.screen_hzb_mip_count = 6U;
  auto scene = MakeSceneWithLocalFog();
  ctx.scene = oxygen::observer_ptr { scene.get() };

  graphics_->dispatch_log_.dispatches.clear();

  service.RenderSkyAndFog(ctx, scene_textures);

  const auto& stage14 = service.GetLastStage14State();
  EXPECT_TRUE(stage14.local_fog_requested);
  EXPECT_TRUE(stage14.local_fog_executed);
  EXPECT_TRUE(stage14.local_fog_hzb_consumed);
  EXPECT_EQ(stage14.local_fog_dispatch_count_x, 1U);
  EXPECT_EQ(stage14.local_fog_dispatch_count_y, 1U);
  EXPECT_EQ(stage14.local_fog_dispatch_count_z, 1U);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  LocalFogStage14ReportsHzbUnavailableWhenCullingRunsWithoutPublishedHzb)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 11U }, oxygen::frame::Slot { 2U });
  auto scene_textures = SceneTextures(*graphics_,
    SceneTexturesConfig {
      .extent = { 64U, 64U },
      .enable_velocity = false,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    });
  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 28U };
  composition_view.with_local_fog = true;
  auto ctx = MakeRenderContext(ViewId { 28U }, resolved_view, composition_view);
  auto scene = MakeSceneWithLocalFog();
  ctx.scene = oxygen::observer_ptr { scene.get() };

  graphics_->dispatch_log_.dispatches.clear();

  service.RenderSkyAndFog(ctx, scene_textures);

  const auto& stage14 = service.GetLastStage14State();
  EXPECT_TRUE(stage14.requested);
  EXPECT_TRUE(stage14.local_fog_requested);
  EXPECT_TRUE(stage14.local_fog_executed);
  EXPECT_FALSE(stage14.local_fog_hzb_consumed);
  EXPECT_TRUE(stage14.local_fog_hzb_unavailable);
  EXPECT_FALSE(stage14.local_fog_skipped);
  EXPECT_EQ(stage14.local_fog_instance_count, 1U);
  EXPECT_EQ(graphics_->dispatch_log_.dispatches.size(), 1U);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  LocalFogStage14HonorsUeShapedPerViewInstanceCapOfOne)
{
  oxygen::console::Console console {};
  renderer_->RegisterConsoleBindings(oxygen::observer_ptr { &console });
  EXPECT_EQ(console.Execute("vtx.local_fog.tile_max_instance_count 1").status,
    oxygen::console::ExecutionStatus::kOk);

  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 12U }, oxygen::frame::Slot { 2U });
  auto scene_textures = SceneTextures(*graphics_,
    SceneTexturesConfig {
      .extent = { 64U, 64U },
      .enable_velocity = false,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    });
  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 29U };
  composition_view.with_local_fog = true;
  auto ctx = MakeRenderContext(ViewId { 29U }, resolved_view, composition_view);
  auto scene = MakeSceneWithLocalFogVolumeCount(3U);
  ctx.scene = oxygen::observer_ptr { scene.get() };

  graphics_->dispatch_log_.dispatches.clear();

  service.RenderSkyAndFog(ctx, scene_textures);

  const auto& stage14 = service.GetLastStage14State();
  EXPECT_TRUE(stage14.local_fog_requested);
  EXPECT_TRUE(stage14.local_fog_executed);
  EXPECT_EQ(stage14.local_fog_instance_count, 1U);
  EXPECT_EQ(graphics_->dispatch_log_.dispatches.size(), 1U);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  OptionalFogLayersStayDisabledByDefaultEvenWhenSceneContainsLocalFogVolumes)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 10U }, oxygen::frame::Slot { 2U });
  auto scene_textures = SceneTextures(*graphics_,
    SceneTexturesConfig {
      .extent = { 64U, 64U },
      .enable_velocity = false,
      .enable_custom_depth = false,
      .gbuffer_count = 4U,
      .msaa_sample_count = 1U,
    });
  auto resolved_view = MakeResolvedView(64.0F, 64.0F);
  auto composition_view = oxygen::vortex::CompositionView {};
  composition_view.id = ViewId { 16U };
  auto ctx = MakeRenderContext(ViewId { 16U }, resolved_view, composition_view);
  ctx.current_view.screen_hzb_available = true;
  ctx.current_view.screen_hzb_frame_slot = oxygen::ShaderVisibleIndex { 17U };
  ctx.current_view.screen_hzb_width = 32U;
  ctx.current_view.screen_hzb_height = 32U;
  ctx.current_view.screen_hzb_mip_count = 5U;
  auto scene = MakeSceneWithLocalFog();
  ctx.scene = oxygen::observer_ptr { scene.get() };

  graphics_->draw_log_.draws.clear();
  graphics_->dispatch_log_.dispatches.clear();
  graphics_->indirect_log_.draws.clear();

  service.RenderSkyAndFog(ctx, scene_textures);

  const auto& stage14 = service.GetLastStage14State();
  EXPECT_FALSE(stage14.requested);
  EXPECT_FALSE(stage14.local_fog_requested);
  EXPECT_FALSE(stage14.local_fog_executed);

  const auto& stage15 = service.GetLastStage15State();
  EXPECT_FALSE(stage15.sky_requested);
  EXPECT_FALSE(stage15.sky_executed);
  EXPECT_FALSE(stage15.atmosphere_requested);
  EXPECT_FALSE(stage15.atmosphere_executed);
  EXPECT_FALSE(stage15.fog_requested);
  EXPECT_FALSE(stage15.fog_executed);
  EXPECT_FALSE(stage15.local_fog_requested);
  EXPECT_FALSE(stage15.local_fog_executed);
  EXPECT_EQ(stage15.sky_draw_count, 0U);
  EXPECT_EQ(stage15.atmosphere_draw_count, 0U);
  EXPECT_EQ(stage15.fog_draw_count, 0U);
  EXPECT_EQ(stage15.local_fog_draw_count, 0U);
  EXPECT_EQ(stage15.total_draw_count, 0U);
  EXPECT_TRUE(graphics_->draw_log_.draws.empty());
  EXPECT_TRUE(graphics_->dispatch_log_.dispatches.empty());
  EXPECT_TRUE(graphics_->indirect_log_.draws.empty());
}

} // namespace
