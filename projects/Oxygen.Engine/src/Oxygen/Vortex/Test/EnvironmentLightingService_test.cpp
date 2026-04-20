//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <type_traits>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Config/RendererConfig.h>
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
using oxygen::vortex::RenderContext;
using oxygen::vortex::Renderer;
using oxygen::vortex::RendererCapabilityFamily;
using oxygen::vortex::SceneTextures;
using oxygen::vortex::SceneTexturesConfig;
using oxygen::vortex::environment::AtmosphereLightModel;
using oxygen::vortex::environment::AtmosphereModel;
using oxygen::vortex::environment::EnvironmentViewProducts;
using oxygen::vortex::environment::HeightFogModel;
using oxygen::vortex::environment::kAtmosphereLightSlotCount;
using oxygen::vortex::environment::kInvalidAtmosphereLightSlot;
using oxygen::vortex::environment::SkyLightEnvironmentModel;
using oxygen::vortex::environment::VolumetricFogModel;
using oxygen::vortex::testing::FakeGraphics;

auto ReadTextFile(const std::filesystem::path& path) -> std::string
{
  auto input = std::ifstream(path);
  EXPECT_TRUE(input.is_open()) << "failed to open " << path.generic_string();
  return { std::istreambuf_iterator<char>(input),
    std::istreambuf_iterator<char>() };
}

auto SourceRoot() -> std::filesystem::path
{
  return std::filesystem::path { __FILE__ }
    .parent_path()
    .parent_path()
    .parent_path();
}

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
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  VortexModuleRegistersEnvironmentServiceFamilySourcesAndHeaders)
{
  const auto source_root = SourceRoot();
  const auto cmake_source = ReadTextFile(source_root / "Vortex/CMakeLists.txt");
  const auto scene_renderer_header
    = ReadTextFile(source_root / "Vortex/SceneRenderer/SceneRenderer.h");
  const auto view_frame_bindings_header
    = ReadTextFile(source_root / "Vortex/Types/ViewFrameBindings.h");
  const auto view_frame_bindings_shader = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Contracts/ViewFrameBindings.hlsli");

  EXPECT_TRUE(
    cmake_source.contains("Environment/EnvironmentLightingService.h"));
  EXPECT_TRUE(
    cmake_source.contains("Environment/EnvironmentLightingService.cpp"));
  EXPECT_TRUE(
    cmake_source.contains("SceneRenderer/Stages/Hzb/ScreenHzbModule.h"));
  EXPECT_TRUE(
    cmake_source.contains("SceneRenderer/Stages/Hzb/ScreenHzbModule.cpp"));
  EXPECT_TRUE(cmake_source.contains("Types/ScreenHzbFrameBindings.h"));
  EXPECT_TRUE(cmake_source.contains("Environment/Internal/SkyRenderer.h"));
  EXPECT_TRUE(
    cmake_source.contains("Environment/Internal/AtmosphereRenderer.cpp"));
  EXPECT_TRUE(cmake_source.contains("Environment/Internal/FogRenderer.cpp"));
  EXPECT_TRUE(cmake_source.contains("Environment/Internal/IblProcessor.cpp"));
  EXPECT_TRUE(
    cmake_source.contains("Environment/Internal/LocalFogVolumeState.h"));
  EXPECT_TRUE(
    cmake_source.contains("Environment/Internal/LocalFogVolumeState.cpp"));
  EXPECT_TRUE(cmake_source.contains("Environment/Passes/SkyPass.cpp"));
  EXPECT_TRUE(
    cmake_source.contains("Environment/Passes/AtmosphereComposePass.cpp"));
  EXPECT_TRUE(
    cmake_source.contains("Environment/Internal/AtmosphereLutCache.h"));
  EXPECT_TRUE(
    cmake_source.contains("Environment/Internal/AtmosphereLutCache.cpp"));
  EXPECT_TRUE(cmake_source.contains(
    "Environment/Passes/AtmosphereTransmittanceLutPass.h"));
  EXPECT_TRUE(cmake_source.contains(
    "Environment/Passes/AtmosphereTransmittanceLutPass.cpp"));
  EXPECT_TRUE(cmake_source.contains(
    "Environment/Passes/AtmosphereMultiScatteringLutPass.h"));
  EXPECT_TRUE(cmake_source.contains(
    "Environment/Passes/AtmosphereMultiScatteringLutPass.cpp"));
  EXPECT_TRUE(
    cmake_source.contains("Environment/Passes/DistantSkyLightLutPass.h"));
  EXPECT_TRUE(
    cmake_source.contains("Environment/Passes/DistantSkyLightLutPass.cpp"));
  EXPECT_TRUE(
    cmake_source.contains("Environment/Passes/AtmosphereSkyViewLutPass.h"));
  EXPECT_TRUE(
    cmake_source.contains("Environment/Passes/AtmosphereSkyViewLutPass.cpp"));
  EXPECT_TRUE(cmake_source.contains(
    "Environment/Passes/AtmosphereCameraAerialPerspectivePass.h"));
  EXPECT_TRUE(cmake_source.contains(
    "Environment/Passes/AtmosphereCameraAerialPerspectivePass.cpp"));
  EXPECT_TRUE(cmake_source.contains("Environment/Passes/FogPass.cpp"));
  EXPECT_TRUE(cmake_source.contains("Environment/Passes/IblProbePass.cpp"));
  EXPECT_TRUE(cmake_source.contains(
    "Environment/Passes/LocalFogVolumeTiledCullingPass.cpp"));
  EXPECT_TRUE(
    cmake_source.contains("Environment/Passes/LocalFogVolumeComposePass.cpp"));
  EXPECT_TRUE(
    cmake_source.contains("Environment/Types/EnvironmentProbeState.h"));
  EXPECT_TRUE(cmake_source.contains(
    "Environment/Types/EnvironmentAmbientBridgeBindings.h"));
  EXPECT_TRUE(cmake_source.contains("Environment/Types/AtmosphereModel.h"));
  EXPECT_TRUE(cmake_source.contains("Environment/Types/HeightFogModel.h"));
  EXPECT_TRUE(
    cmake_source.contains("Environment/Types/SkyLightEnvironmentModel.h"));
  EXPECT_TRUE(cmake_source.contains("Environment/Types/VolumetricFogModel.h"));
  EXPECT_TRUE(
    cmake_source.contains("Environment/Types/EnvironmentViewProducts.h"));
  EXPECT_TRUE(scene_renderer_header.contains("ScreenHzbModule"));
  EXPECT_TRUE(scene_renderer_header.contains("GetPublishedScreenHzbBindings"));
  EXPECT_TRUE(view_frame_bindings_header.contains("screen_hzb_frame_slot"));
  EXPECT_TRUE(view_frame_bindings_shader.contains("screen_hzb_frame_slot"));
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  VortexShaderCatalogRegistersEnvironmentStage15AndProbeRefreshFamily)
{
  const auto source_root = SourceRoot();
  const auto catalog_source = ReadTextFile(
    source_root / "Graphics/Direct3D12/Shaders/EngineShaderCatalog.h");

  EXPECT_TRUE(catalog_source.contains("Vortex/Services/Environment/Sky.hlsl"));
  EXPECT_TRUE(catalog_source.contains(
    "Vortex/Services/Environment/AtmosphereCompose.hlsl"));
  EXPECT_TRUE(catalog_source.contains(
    "Vortex/Services/Environment/AtmosphereTransmittanceLut.hlsl"));
  EXPECT_TRUE(catalog_source.contains(
    "Vortex/Services/Environment/AtmosphereMultiScatteringLut.hlsl"));
  EXPECT_TRUE(catalog_source.contains(
    "Vortex/Services/Environment/DistantSkyLightLut.hlsl"));
  EXPECT_TRUE(catalog_source.contains(
    "Vortex/Services/Environment/AtmosphereSkyViewLut.hlsl"));
  EXPECT_TRUE(catalog_source.contains(
    "Vortex/Services/Environment/AtmosphereCameraAerialPerspective.hlsl"));
  EXPECT_TRUE(catalog_source.contains("Vortex/Services/Environment/Fog.hlsl"));
  EXPECT_TRUE(catalog_source.contains("VortexSkyPassVS"));
  EXPECT_TRUE(catalog_source.contains("VortexSkyPassPS"));
  EXPECT_TRUE(catalog_source.contains("VortexAtmosphereComposeVS"));
  EXPECT_TRUE(catalog_source.contains("VortexAtmosphereComposePS"));
  EXPECT_TRUE(catalog_source.contains("VortexAtmosphereTransmittanceLutCS"));
  EXPECT_TRUE(catalog_source.contains("VortexAtmosphereMultiScatteringLutCS"));
  EXPECT_TRUE(catalog_source.contains("VortexDistantSkyLightLutCS"));
  EXPECT_TRUE(catalog_source.contains("VortexAtmosphereSkyViewLutCS"));
  EXPECT_TRUE(
    catalog_source.contains("VortexAtmosphereCameraAerialPerspectiveCS"));
  EXPECT_TRUE(catalog_source.contains("VortexFogPassVS"));
  EXPECT_TRUE(catalog_source.contains("VortexFogPassPS"));
  EXPECT_TRUE(catalog_source.contains(
    "Vortex/Services/Environment/LocalFogVolumeTiledCulling.hlsl"));
  EXPECT_TRUE(catalog_source.contains("VortexLocalFogVolumeTiledCullingCS"));
  EXPECT_TRUE(catalog_source.contains(
    "Vortex/Services/Environment/LocalFogVolumeCompose.hlsl"));
  EXPECT_TRUE(catalog_source.contains("VortexLocalFogVolumeComposeVS"));
  EXPECT_TRUE(catalog_source.contains("VortexLocalFogVolumeComposePS"));
  EXPECT_TRUE(catalog_source.contains("VortexIblIrradianceCS"));
  EXPECT_TRUE(catalog_source.contains("VortexIblPrefilterCS"));
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  EnvironmentServiceShaderFilesExistForStage15AndProbeRefresh)
{
  const auto source_root = SourceRoot();
  EXPECT_TRUE(std::filesystem::exists(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Sky.hlsl"));
  EXPECT_TRUE(std::filesystem::exists(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/"
      "AtmosphereCompose.hlsl"));
  EXPECT_TRUE(std::filesystem::exists(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/"
      "AtmosphereTransmittanceLut.hlsl"));
  EXPECT_TRUE(std::filesystem::exists(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/"
      "AtmosphereMultiScatteringLut.hlsl"));
  EXPECT_TRUE(std::filesystem::exists(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/"
      "DistantSkyLightLut.hlsl"));
  EXPECT_TRUE(std::filesystem::exists(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/"
      "AtmosphereSkyViewLut.hlsl"));
  EXPECT_TRUE(std::filesystem::exists(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/"
      "AtmosphereUeMirrorCommon.hlsli"));
  EXPECT_TRUE(std::filesystem::exists(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/"
      "AtmosphereCameraAerialPerspective.hlsl"));
  EXPECT_TRUE(std::filesystem::exists(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Fog.hlsl"));
  EXPECT_TRUE(std::filesystem::exists(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/"
      "LocalFogVolumeTiledCulling.hlsl"));
  EXPECT_TRUE(std::filesystem::exists(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/"
      "LocalFogVolumeCompose.hlsl"));
  EXPECT_TRUE(std::filesystem::exists(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/"
      "LocalFogVolumeCommon.hlsli"));
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  LocalFogShadersUseTileListsAndRayIntegratedCompositionInsteadOfGlobalBruteForce)
{
  const auto source_root = SourceRoot();
  const auto compose_pass_source = ReadTextFile(
    source_root / "Vortex/Environment/Passes/LocalFogVolumeComposePass.cpp");
  const auto screen_hzb_contract = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Contracts/ScreenHzbBindings.hlsli");
  const auto culling_source = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/"
      "LocalFogVolumeTiledCulling.hlsl");
  const auto compose_source = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/"
      "LocalFogVolumeCompose.hlsl");
  const auto common_source = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/"
      "LocalFogVolumeCommon.hlsli");

  EXPECT_TRUE(culling_source.contains("tile_data_texture_slot"));
  EXPECT_TRUE(culling_source.contains("occupied_tile_buffer_slot"));
  EXPECT_TRUE(culling_source.contains("indirect_args_buffer_slot"));
  EXPECT_TRUE(culling_source.contains("indirect_count_buffer_slot"));
  EXPECT_TRUE(
    culling_source.contains("RWTexture2DArray<uint> tile_data_texture"));
  EXPECT_TRUE(
    culling_source.contains("RWStructuredBuffer<uint> occupied_tiles"));
  EXPECT_TRUE(culling_source.contains(
    "InterlockedAdd(indirect_count[0], 1u, write_index);"));
  EXPECT_TRUE(culling_source.contains("pass.left_plane"));
  EXPECT_TRUE(culling_source.contains("pass.right_plane"));
  EXPECT_TRUE(culling_source.contains("pass.near_plane"));
  EXPECT_TRUE(culling_source.contains("pass.view_to_tile_space_ratio"));
  EXPECT_TRUE(culling_source.contains("LocalFogSphereOutsidePlane"));
  EXPECT_TRUE(culling_source.contains("BuildLocalFogClipSphere"));
  EXPECT_TRUE(culling_source.contains("LocalFogIsOccludedByHzb"));
  EXPECT_TRUE(culling_source.contains(
    "tile_data_texture[uint3(tile_coord, 0u)] = tile_count;"));
  EXPECT_FALSE(culling_source.contains("scene_depth < -1.0f"));

  EXPECT_TRUE(common_source.contains("PackLocalFogTile"));
  EXPECT_TRUE(common_source.contains("UnpackLocalFogTile"));
  EXPECT_TRUE(common_source.contains("LocalFogSphereOutsidePlane"));
  EXPECT_TRUE(common_source.contains("RayIntersectUnitSphere"));
  EXPECT_TRUE(common_source.contains("EvaluateLocalFogVolumeIntegral"));
  EXPECT_TRUE(common_source.contains("GetLocalFogVolumeInstanceContribution"));
  EXPECT_TRUE(common_source.contains("HenyeyGreensteinPhase"));
  EXPECT_TRUE(common_source.contains("EvaluateLocalFogVolumeInScattering"));
  EXPECT_TRUE(common_source.contains("LoadEnvironmentStaticData"));
  EXPECT_TRUE(common_source.contains("GetSunLuminanceRGB"));
  EXPECT_TRUE(screen_hzb_contract.contains("GetViewportUvToHzbBufferUv"));
  EXPECT_TRUE(screen_hzb_contract.contains("GetHzbSize"));
  EXPECT_TRUE(screen_hzb_contract.contains("GetHzbViewRect"));
  EXPECT_TRUE(common_source.contains("GetViewportUvToHzbBufferUv(screen_hzb)"));
  EXPECT_TRUE(common_source.contains("GetHzbSize(screen_hzb)"));
  EXPECT_TRUE(common_source.contains("ProjectLocalFogClipSphereToHzb"));
  EXPECT_TRUE(common_source.contains("ResolveLocalFogNearestDepth"));
  EXPECT_TRUE(common_source.contains("LocalFogIsOccludedByHzb"));
  EXPECT_TRUE(culling_source.contains("IsScreenHzbFurthestValid(screen_hzb)"));

  EXPECT_TRUE(compose_source.contains("tile_data_texture_slot"));
  EXPECT_TRUE(compose_source.contains("occupied_tile_buffer_slot"));
  EXPECT_TRUE(compose_source.contains("GetLocalFogVolumeContribution"));
  EXPECT_TRUE(
    compose_source.contains("Texture2DArray<uint> tile_data_texture"));
  EXPECT_TRUE(compose_source.contains("SV_InstanceID"));
  EXPECT_TRUE(compose_source.contains("tile_pixel_size"));
  EXPECT_TRUE(compose_source.contains("view_width"));
  EXPECT_TRUE(compose_source.contains("view_height"));
  EXPECT_TRUE(compose_source.contains("pass.start_depth_z"));
  EXPECT_TRUE(compose_source.contains("tile_min_pixel"));
  EXPECT_TRUE(compose_source.contains(
    "translated_world_position = world_position - camera_position;"));
  EXPECT_FALSE(compose_source.contains("GenerateVortexFullscreenTriangle"));
  EXPECT_FALSE(
    compose_source.contains("for (uint instance_index = 0u; instance_index < "
                            "pass.instance_count; ++instance_index)"));
  EXPECT_FALSE(compose_source.contains(
    "float2(tile_coord)\n        / float2(pass.tile_resolution_x, "
    "pass.tile_resolution_y)"));
  EXPECT_TRUE(compose_pass_source.contains("ExecuteIndirect("));
  EXPECT_TRUE(
    compose_pass_source.contains("GetLocalFogGlobalStartDistanceMeters"));
  EXPECT_TRUE(compose_pass_source.contains("ComputeLocalFogStartDepthZ"));
  EXPECT_FALSE(compose_pass_source.contains("Draw(3U, 1U, 0U, 0U)"));
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  Stage15ShaderSourcesStopUsingZeroOutputPlaceholders)
{
  const auto source_root = SourceRoot();
  const auto sky_source = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Sky.hlsl");
  const auto atmosphere_source = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/"
      "AtmosphereCompose.hlsl");
  const auto fog_source = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Fog.hlsl");

  EXPECT_FALSE(sky_source.contains("* 0.0f"));
  EXPECT_FALSE(sky_source.contains("return float4(sky_color, 0.0f);"));
  EXPECT_FALSE(atmosphere_source.contains("* 0.0f"));
  EXPECT_FALSE(atmosphere_source.contains(
    "return float4(0.0f, 0.0f, 0.0f, atmosphere_alpha);"));
  EXPECT_FALSE(fog_source.contains("* 0.0f"));
  EXPECT_FALSE(
    fog_source.contains("return float4(0.0f, 0.0f, 0.0f, fog_alpha);"));
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  SkyShaderUsesFarBackgroundMaskInsteadOfDepthEqualityShortcuts)
{
  const auto source_root = SourceRoot();
  const auto sky_source = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Sky.hlsl");

  EXPECT_TRUE(sky_source.contains("EvaluateFarBackgroundMask"));
  EXPECT_TRUE(sky_source.contains("SampleSceneDepth"));
  EXPECT_TRUE(sky_source.contains("sky_view_lut_srv"));
  EXPECT_TRUE(sky_source.contains("ApplySkyViewLutReferential"));
  EXPECT_TRUE(sky_source.contains("SkyViewLutParamsToUv"));
  EXPECT_FALSE(sky_source.contains("view_direction.y * 0.5f + 0.5f"));
  EXPECT_FALSE(sky_source.contains("normalize(float3(0.25f, 0.9f, 0.35f))"));
  EXPECT_FALSE(sky_source.contains("horizon_color"));
  EXPECT_FALSE(sky_source.contains("zenith = float3"));
  EXPECT_FALSE(sky_source.contains("depth == 1.0"));
  EXPECT_FALSE(sky_source.contains("scene_depth == 1.0"));
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  SkyShaderUsesProjectionAwareFarDepthReferenceWithoutDualEndpointFallback)
{
  const auto source_root = SourceRoot();
  const auto sky_source = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Sky.hlsl");
  const auto atmosphere_source = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/"
      "AtmosphereCompose.hlsl");
  const auto fog_source = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Fog.hlsl");

  EXPECT_TRUE(
    sky_source.contains("const float far_depth = ResolveFarDepthReference();"));
  EXPECT_TRUE(sky_source.contains("abs(scene_depth - far_depth)"));
  EXPECT_TRUE(sky_source.contains("projection_matrix._33 > 0.0f"));
  EXPECT_TRUE(atmosphere_source.contains("projection_matrix._33 > 0.0f"));
  EXPECT_TRUE(fog_source.contains("projection_matrix._33 > 0.0f"));
  EXPECT_FALSE(sky_source.contains("reverse_z_far"));
  EXPECT_FALSE(sky_source.contains("forward_z_far"));
  EXPECT_FALSE(sky_source.contains("max(reverse_z_far, forward_z_far)"));
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  AtmosphereAndFogShadersStayDepthAwareSceneCompositionPasses)
{
  const auto source_root = SourceRoot();
  const auto atmosphere_source = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/"
      "AtmosphereCompose.hlsl");
  const auto fog_source = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Fog.hlsl");

  EXPECT_TRUE(atmosphere_source.contains("SampleSceneDepth"));
  EXPECT_TRUE(atmosphere_source.contains("ReconstructWorldPosition"));
  EXPECT_TRUE(atmosphere_source.contains("camera_aerial_perspective_srv"));
  EXPECT_TRUE(
    atmosphere_source.contains("SampleVortexCameraAerialPerspective"));
  EXPECT_TRUE(atmosphere_source.contains("atmosphere_alpha"));
  EXPECT_FALSE(atmosphere_source.contains("discard;"));

  EXPECT_TRUE(fog_source.contains("SampleSceneDepth"));
  EXPECT_TRUE(fog_source.contains("ReconstructWorldPosition"));
  EXPECT_TRUE(fog_source.contains("fog_alpha"));
  EXPECT_TRUE(fog_source.contains("world_position.z"));
  EXPECT_FALSE(fog_source.contains("world_position.y + 4.0f"));
  EXPECT_FALSE(fog_source.contains("discard;"));
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  Stage15ProofToolsUseFinalStage12BaselineAndEmitBlockingAsyncQualityKeys)
{
  const auto repo_root = SourceRoot().parent_path().parent_path();
  const auto vortexbasic_capture = ReadTextFile(
    repo_root / "tools/vortex/AnalyzeRenderDocVortexBasicCapture.py");
  const auto vortexbasic_products = ReadTextFile(
    repo_root / "tools/vortex/AnalyzeRenderDocVortexBasicProducts.py");
  const auto async_products
    = ReadTextFile(repo_root / "tools/vortex/AnalyzeRenderDocAsyncProducts.py");
  const auto async_assert
    = ReadTextFile(repo_root / "tools/vortex/Assert-AsyncRuntimeProof.ps1");
  const auto vortexbasic_assert = ReadTextFile(
    repo_root / "tools/vortex/Assert-VortexBasicRuntimeProof.ps1");

  EXPECT_TRUE(
    vortexbasic_capture.contains("compositing_present_operation_count"));
  EXPECT_TRUE(vortexbasic_capture.contains(
    "ID3D12GraphicsCommandList::CopyTextureRegion()"));
  EXPECT_TRUE(
    vortexbasic_capture.contains("Vortex.Stage14.LocalFogTiledCulling"));
  EXPECT_TRUE(vortexbasic_capture.contains("Vortex.Stage5.ScreenHzbBuild"));
  EXPECT_TRUE(vortexbasic_products.contains("choose_latest_stage_sample"));
  EXPECT_TRUE(vortexbasic_products.contains("find_last_named_record_any"));
  EXPECT_TRUE(vortexbasic_products.contains("COPY_NAME"));
  EXPECT_TRUE(vortexbasic_products.contains(
    "atmosphere_transmittance_lut_scope_count_match"));
  EXPECT_TRUE(vortexbasic_products.contains(
    "atmosphere_multi_scattering_lut_scope_count_match"));
  EXPECT_TRUE(
    vortexbasic_products.contains("atmosphere_sky_view_lut_scope_count_match"));
  EXPECT_TRUE(vortexbasic_products.contains(
    "atmosphere_camera_aerial_scope_count_match"));
  EXPECT_TRUE(
    vortexbasic_products.contains("atmosphere_camera_aerial_consumed"));
  EXPECT_TRUE(
    vortexbasic_products.contains("distant_sky_light_lut_scope_count_match"));
  EXPECT_TRUE(vortexbasic_products.contains("screen_hzb_expected_width"));
  EXPECT_TRUE(vortexbasic_products.contains("screen_hzb_published"));
  EXPECT_TRUE(vortexbasic_products.contains("local_fog_hzb_consumed"));
  EXPECT_TRUE(vortexbasic_products.contains("local_fog_indirect_draw_valid"));
  EXPECT_TRUE(vortexbasic_products.contains("stage12_final_last_draw_event"));
  EXPECT_TRUE(
    vortexbasic_products.contains("stage15_local_fog_scene_color_changed"));
  EXPECT_FALSE(vortexbasic_products.contains(
    "stage12_final_scene_color = find_output_sample(stage12_point, "
    "\"SceneColor\")"));

  EXPECT_TRUE(async_products.contains("stage15_async_scene_color_changed"));
  EXPECT_TRUE(
    async_products.contains("atmosphere_sky_view_lut_scope_count_match"));
  EXPECT_TRUE(
    async_products.contains("atmosphere_camera_aerial_scope_count_match"));
  EXPECT_TRUE(async_products.contains("atmosphere_camera_aerial_consumed"));
  EXPECT_TRUE(async_products.contains("stage15_far_background_mask_valid"));
  EXPECT_TRUE(async_products.contains("stage15_sky_quality_ok"));

  EXPECT_TRUE(async_assert.contains("'stage15_async_scene_color_changed'"));
  EXPECT_TRUE(
    async_assert.contains("'atmosphere_sky_view_lut_scope_count_match'"));
  EXPECT_TRUE(
    async_assert.contains("'atmosphere_camera_aerial_scope_count_match'"));
  EXPECT_TRUE(async_assert.contains("'atmosphere_camera_aerial_consumed'"));
  EXPECT_TRUE(async_assert.contains("'stage15_far_background_mask_valid'"));
  EXPECT_TRUE(async_assert.contains("'stage15_sky_quality_ok'"));
  EXPECT_TRUE(vortexbasic_assert.contains(
    "'atmosphere_transmittance_lut_scope_count_match'"));
  EXPECT_TRUE(vortexbasic_assert.contains(
    "'atmosphere_multi_scattering_lut_scope_count_match'"));
  EXPECT_TRUE(
    vortexbasic_assert.contains("'atmosphere_sky_view_lut_scope_count_match'"));
  EXPECT_TRUE(vortexbasic_assert.contains(
    "'atmosphere_camera_aerial_scope_count_match'"));
  EXPECT_TRUE(
    vortexbasic_assert.contains("'atmosphere_camera_aerial_consumed'"));
  EXPECT_TRUE(
    vortexbasic_assert.contains("'distant_sky_light_lut_scope_count_match'"));
  EXPECT_TRUE(vortexbasic_assert.contains("'transmittance_lut_published'"));
  EXPECT_TRUE(vortexbasic_assert.contains("'multi_scattering_lut_published'"));
  EXPECT_TRUE(vortexbasic_assert.contains("'distant_sky_light_lut_published'"));
  EXPECT_TRUE(vortexbasic_assert.contains("'sky_view_lut_published'"));
  EXPECT_TRUE(
    vortexbasic_assert.contains("'camera_aerial_perspective_published'"));
  EXPECT_TRUE(vortexbasic_assert.contains("'atmosphere_lut_cache_valid'"));
  EXPECT_TRUE(
    vortexbasic_assert.contains("'compositing_present_operation_count_match'"));
  EXPECT_TRUE(vortexbasic_assert.contains("'screen_hzb_published'"));
  EXPECT_TRUE(vortexbasic_assert.contains("screen_hzb_proof_source"));
  EXPECT_TRUE(vortexbasic_assert.contains("'local_fog_hzb_consumed'"));
  EXPECT_TRUE(vortexbasic_assert.contains("local_fog_hzb_proof_source"));
  EXPECT_TRUE(vortexbasic_assert.contains("'local_fog_indirect_draw_valid'"));
  EXPECT_TRUE(
    vortexbasic_assert.contains("'local_fog_volume_instance_count_valid'"));
  EXPECT_TRUE(vortexbasic_assert.contains("'local_fog_tiled_culling_valid'"));
  EXPECT_TRUE(vortexbasic_assert.contains("'local_fog_scene_color_changed'"));
  EXPECT_TRUE(vortexbasic_assert.contains("runtime_log+capture_report"));
  EXPECT_FALSE(vortexbasic_assert.contains(
    "Runtime log does not contain screen_hzb_published=true"));
  EXPECT_FALSE(vortexbasic_assert.contains(
    "Runtime log does not contain local_fog_hzb_consumed=true"));
  EXPECT_FALSE(vortexbasic_assert.contains(
    "Runtime log does not contain local_fog_indirect_draw_valid=true"));
  EXPECT_FALSE(vortexbasic_assert.contains("'compositing_draw_count_match'"));
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  VortexBasicSurfaceUsesExplicitCliFlagsForAtmosphereHeightFogAndLocalFog)
{
  const auto repo_root = std::filesystem::path { __FILE__ }
                           .parent_path()
                           .parent_path()
                           .parent_path()
                           .parent_path()
                           .parent_path();
  const auto main_impl
    = ReadTextFile(repo_root / "Examples/VortexBasic/main_impl.cpp");
  const auto main_module
    = ReadTextFile(repo_root / "Examples/VortexBasic/MainModule.cpp");
  const auto runtime_validation = ReadTextFile(
    repo_root / "tools/vortex/Run-VortexBasicRuntimeValidation.ps1");

  EXPECT_TRUE(main_impl.contains(".Long(\"with-atmosphere\")"));
  EXPECT_TRUE(main_impl.contains(".Long(\"with-height-fog\")"));
  EXPECT_TRUE(main_impl.contains(".Long(\"with-local-fog\")"));
  EXPECT_TRUE(main_impl.contains(".DefaultValue(false)"));
  EXPECT_TRUE(main_module.contains(
    "view_ctx.metadata.with_atmosphere = app_.with_atmosphere;"));
  EXPECT_TRUE(main_module.contains(
    "view_ctx.metadata.with_height_fog = app_.with_height_fog;"));
  EXPECT_TRUE(main_module.contains(
    "view_ctx.metadata.with_local_fog = app_.with_local_fog;"));
  EXPECT_TRUE(main_module.contains("if (app_.with_local_fog) {"));
  EXPECT_TRUE(runtime_validation.contains("'--with-atmosphere'"));
  EXPECT_TRUE(runtime_validation.contains("'--with-height-fog'"));
  EXPECT_TRUE(runtime_validation.contains("'--with-local-fog'"));
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
    light->get().SetAtmosphereLightSlot(slot);
    light->get().SetUsePerPixelAtmosphereTransmittance(
      use_per_pixel_transmittance);
    light->get().SetAtmosphereDiskLuminanceScale(disk_scale);
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
  atmosphere->SetTransformMode(
    oxygen::scene::environment::SkyAtmosphereTransformMode::
      kPlanetCenterAtComponentTransform);
  atmosphere->SetPlanetAnchorWorldPosition({ 1000.0F, 2000.0F, 3000.0F });
  scene->Update();

  const auto camera_position = glm::vec3 { 1400.0F, 2600.0F, 7200300.0F };
  const auto center = camera_position + glm::vec3 { 1.0F, 0.0F, 0.0F };
  const auto view_matrix = glm::lookAtRH(
    camera_position, center, glm::vec3 { 0.0F, 0.0F, 1.0F });
  auto resolved_view = MakeResolvedView(
    64.0F, 64.0F, 0.0F, 0.0F, camera_position, view_matrix);
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
  EXPECT_GT(view_data->sky_planet_translated_world_center_and_view_height.w,
    7000000.0F);
  EXPECT_NEAR(view_data->sky_camera_translated_world_origin_pad.x, 0.0F, 1.0e-3F);
  EXPECT_NEAR(view_data->sky_camera_translated_world_origin_pad.y, 0.0F, 1.0e-3F);
  EXPECT_NEAR(view_data->sky_camera_translated_world_origin_pad.z, 0.0F, 1.0e-3F);
  EXPECT_NEAR(view_data->sky_view_lut_referential_row0.x, 1.0F, 1.0e-3F);
  EXPECT_NEAR(view_data->sky_view_lut_referential_row0.y, 0.0F, 1.0e-3F);
  EXPECT_NEAR(view_data->sky_view_lut_referential_row0.z, 0.0F, 1.0e-3F);
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
  const auto* bindings = service.InspectBindings(ViewId { 21U });
  ASSERT_NE(bindings, nullptr);

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

  EXPECT_EQ(atmosphere_state.conventional_shadow_authority_slot, 0U);
  EXPECT_EQ(atmosphere_state.conventional_shadow_cascade_count, 2U);
  EXPECT_TRUE(atmosphere_state.conventional_shadow_authority_slot0_only);
  EXPECT_EQ(
    atmosphere_state.view_products.conventional_shadow_authority_slot, 0U);

  EXPECT_NE(bindings->contract_flags
      & oxygen::vortex::kEnvironmentContractFlagAtmosphereLight0Enabled,
    0U);
  EXPECT_NE(bindings->contract_flags
      & oxygen::vortex::kEnvironmentContractFlagAtmosphereLight1Enabled,
    0U);
  EXPECT_NE(bindings->contract_flags
      & oxygen::vortex::kEnvironmentContractFlagShadowAuthoritySlot0Only,
    0U);
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
  static_cast<void>(AddAtmosphereDirectionalLight(*scene, "Fill",
    oxygen::scene::AtmosphereLightSlot::kNone, false, 4U, false,
    { 0.2F, 0.2F, 0.2F }, { 0.4F, 0.5F, 0.6F }, 500.0F));
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
  EXPECT_GE(generation.transmittance_dispatch_count_x, 1U);
  EXPECT_GE(generation.transmittance_dispatch_count_y, 1U);
  EXPECT_EQ(generation.transmittance_dispatch_count_z, 1U);
  EXPECT_TRUE(generation.multi_scattering_lut_requested);
  EXPECT_TRUE(generation.multi_scattering_lut_executed);
  EXPECT_TRUE(generation.multi_scattering_lut_srv.IsValid());
  EXPECT_GE(generation.multi_scattering_dispatch_count_x, 1U);
  EXPECT_GE(generation.multi_scattering_dispatch_count_y, 1U);
  EXPECT_EQ(generation.multi_scattering_dispatch_count_z, 1U);
  EXPECT_TRUE(generation.distant_sky_light_lut_requested);
  EXPECT_TRUE(generation.distant_sky_light_lut_executed);
  EXPECT_TRUE(generation.distant_sky_light_lut_srv.IsValid());
  EXPECT_EQ(generation.distant_sky_light_dispatch_count_x, 1U);
  EXPECT_EQ(generation.distant_sky_light_dispatch_count_y, 1U);
  EXPECT_EQ(generation.distant_sky_light_dispatch_count_z, 1U);
  EXPECT_TRUE(generation.sky_view_lut_requested);
  EXPECT_TRUE(generation.sky_view_lut_executed);
  EXPECT_TRUE(generation.sky_view_lut_srv.IsValid());
  EXPECT_GE(generation.sky_view_dispatch_count_x, 1U);
  EXPECT_GE(generation.sky_view_dispatch_count_y, 1U);
  EXPECT_EQ(generation.sky_view_dispatch_count_z, 1U);
  EXPECT_TRUE(generation.camera_aerial_perspective_requested);
  EXPECT_TRUE(generation.camera_aerial_perspective_executed);
  EXPECT_TRUE(generation.camera_aerial_perspective_srv.IsValid());
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
        return !blend_state.empty()
          && std::ranges::all_of(blend_state, [](const auto& target) {
               return target.blend_enable
                 && target.src_blend == oxygen::graphics::BlendFactor::kSrcAlpha
                 && target.dest_blend
                 == oxygen::graphics::BlendFactor::kInvSrcAlpha
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
  graphics_->indirect_log_.counted_draws.clear();

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
  ASSERT_EQ(graphics_->indirect_log_.counted_draws.size(), 1U);
  EXPECT_EQ(graphics_->indirect_log_.counted_draws[0].command_desc.kind,
    oxygen::graphics::CommandRecorder::IndirectCommandKind::kDraw);
  EXPECT_NE(
    graphics_->indirect_log_.counted_draws[0].execution_desc.count_buffer,
    nullptr);
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
  graphics_->indirect_log_.counted_draws.clear();

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
  EXPECT_TRUE(graphics_->indirect_log_.counted_draws.empty());
}

} // namespace
