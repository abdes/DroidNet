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

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Environment/EnvironmentLightingService.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentAmbientBridgeBindings.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentEvaluationParameters.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentProbeBindings.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentProbeState.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Types/EnvironmentFrameBindings.h>

#include "Fakes/Graphics.h"

namespace {

using oxygen::kInvalidShaderVisibleIndex;
using oxygen::Graphics;
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
using oxygen::vortex::testing::FakeGraphics;

auto ReadTextFile(const std::filesystem::path& path) -> std::string
{
  auto input = std::ifstream(path);
  EXPECT_TRUE(input.is_open()) << "failed to open " << path.generic_string();
  return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

auto SourceRoot() -> std::filesystem::path
{
  return std::filesystem::path { __FILE__ }.parent_path().parent_path()
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
  return { new Renderer(
             std::weak_ptr<Graphics>(graphics), std::move(config), kCapabilities),
    DestroyRenderer };
}

auto MakeResolvedView(const float width, const float height) -> ResolvedView
{
  auto params = ResolvedView::Params {};
  params.view_config.viewport = ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = width,
    .height = height,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  params.view_matrix = glm::mat4(1.0F);
  params.proj_matrix = glm::mat4(1.0F);
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
  return ctx;
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  EnvironmentFrameBindingsExposePerViewProbeEvaluationAndBoundedBridgeState)
{
  const auto bindings = EnvironmentFrameBindings {};

  EXPECT_EQ(bindings.environment_static_slot, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.environment_view_slot, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.probes.environment_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.probes.irradiance_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.probes.prefiltered_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.probes.brdf_lut_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.probes.probe_revision, 0U);
  EXPECT_FLOAT_EQ(bindings.evaluation.ambient_intensity, 1.0F);
  EXPECT_FLOAT_EQ(bindings.evaluation.average_brightness, 1.0F);
  EXPECT_FLOAT_EQ(bindings.evaluation.blend_fraction, 0.0F);
  EXPECT_EQ(bindings.evaluation.flags, 0U);
  EXPECT_EQ(bindings.ambient_bridge.irradiance_map_srv, kInvalidShaderVisibleIndex);
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
  VortexModuleRegistersEnvironmentServiceFamilySourcesAndHeaders)
{
  const auto source_root = SourceRoot();
  const auto cmake_source = ReadTextFile(source_root / "Vortex/CMakeLists.txt");

  EXPECT_TRUE(cmake_source.contains("Environment/EnvironmentLightingService.h"));
  EXPECT_TRUE(cmake_source.contains("Environment/EnvironmentLightingService.cpp"));
  EXPECT_TRUE(cmake_source.contains("Environment/Internal/SkyRenderer.h"));
  EXPECT_TRUE(cmake_source.contains("Environment/Internal/AtmosphereRenderer.cpp"));
  EXPECT_TRUE(cmake_source.contains("Environment/Internal/FogRenderer.cpp"));
  EXPECT_TRUE(cmake_source.contains("Environment/Internal/IblProcessor.cpp"));
  EXPECT_TRUE(cmake_source.contains("Environment/Passes/SkyPass.cpp"));
  EXPECT_TRUE(cmake_source.contains("Environment/Passes/AtmosphereComposePass.cpp"));
  EXPECT_TRUE(cmake_source.contains("Environment/Passes/FogPass.cpp"));
  EXPECT_TRUE(cmake_source.contains("Environment/Passes/IblProbePass.cpp"));
  EXPECT_TRUE(cmake_source.contains("Environment/Types/EnvironmentProbeState.h"));
  EXPECT_TRUE(cmake_source.contains("Environment/Types/EnvironmentAmbientBridgeBindings.h"));
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  VortexShaderCatalogRegistersEnvironmentStage15AndProbeRefreshFamily)
{
  const auto source_root = SourceRoot();
  const auto catalog_source = ReadTextFile(
    source_root / "Graphics/Direct3D12/Shaders/EngineShaderCatalog.h");

  EXPECT_TRUE(
    catalog_source.contains("Vortex/Services/Environment/Sky.hlsl"));
  EXPECT_TRUE(catalog_source.contains(
    "Vortex/Services/Environment/AtmosphereCompose.hlsl"));
  EXPECT_TRUE(
    catalog_source.contains("Vortex/Services/Environment/Fog.hlsl"));
  EXPECT_TRUE(catalog_source.contains("VortexSkyPassVS"));
  EXPECT_TRUE(catalog_source.contains("VortexSkyPassPS"));
  EXPECT_TRUE(catalog_source.contains("VortexAtmosphereComposeVS"));
  EXPECT_TRUE(catalog_source.contains("VortexAtmosphereComposePS"));
  EXPECT_TRUE(catalog_source.contains("VortexFogPassVS"));
  EXPECT_TRUE(catalog_source.contains("VortexFogPassPS"));
  EXPECT_TRUE(catalog_source.contains("VortexIblIrradianceCS"));
  EXPECT_TRUE(catalog_source.contains("VortexIblPrefilterCS"));
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  EnvironmentServiceShaderFilesExistForStage15AndProbeRefresh)
{
  const auto source_root = SourceRoot();
  EXPECT_TRUE(std::filesystem::exists(
    source_root / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Sky.hlsl"));
  EXPECT_TRUE(std::filesystem::exists(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/AtmosphereCompose.hlsl"));
  EXPECT_TRUE(std::filesystem::exists(
    source_root / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Fog.hlsl"));
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  Stage15ShaderSourcesStopUsingZeroOutputPlaceholders)
{
  const auto source_root = SourceRoot();
  const auto sky_source = ReadTextFile(
    source_root / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Sky.hlsl");
  const auto atmosphere_source = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/AtmosphereCompose.hlsl");
  const auto fog_source = ReadTextFile(
    source_root / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Fog.hlsl");

  EXPECT_FALSE(sky_source.contains("* 0.0f"));
  EXPECT_FALSE(sky_source.contains("return float4(sky_color, 0.0f);"));
  EXPECT_FALSE(atmosphere_source.contains("* 0.0f"));
  EXPECT_FALSE(
    atmosphere_source.contains("return float4(0.0f, 0.0f, 0.0f, atmosphere_alpha);"));
  EXPECT_FALSE(fog_source.contains("* 0.0f"));
  EXPECT_FALSE(fog_source.contains("return float4(0.0f, 0.0f, 0.0f, fog_alpha);"));
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  SkyShaderUsesFarBackgroundMaskInsteadOfDepthEqualityShortcuts)
{
  const auto source_root = SourceRoot();
  const auto sky_source = ReadTextFile(
    source_root / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Sky.hlsl");

  EXPECT_TRUE(sky_source.contains("EvaluateFarBackgroundMask"));
  EXPECT_TRUE(sky_source.contains("SampleSceneDepth"));
  EXPECT_FALSE(sky_source.contains("depth == 1.0"));
  EXPECT_FALSE(sky_source.contains("scene_depth == 1.0"));
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  SkyShaderUsesProjectionAwareFarDepthReferenceWithoutDualEndpointFallback)
{
  const auto source_root = SourceRoot();
  const auto sky_source = ReadTextFile(
    source_root / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Sky.hlsl");
  const auto atmosphere_source = ReadTextFile(source_root
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/AtmosphereCompose.hlsl");
  const auto fog_source = ReadTextFile(
    source_root / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Fog.hlsl");

  EXPECT_TRUE(sky_source.contains("const float far_depth = ResolveFarDepthReference();"));
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
    / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/AtmosphereCompose.hlsl");
  const auto fog_source = ReadTextFile(
    source_root / "Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Fog.hlsl");

  EXPECT_TRUE(atmosphere_source.contains("SampleSceneDepth"));
  EXPECT_TRUE(atmosphere_source.contains("ReconstructWorldPosition"));
  EXPECT_TRUE(atmosphere_source.contains("atmosphere_alpha"));
  EXPECT_FALSE(atmosphere_source.contains("discard;"));

  EXPECT_TRUE(fog_source.contains("SampleSceneDepth"));
  EXPECT_TRUE(fog_source.contains("ReconstructWorldPosition"));
  EXPECT_TRUE(fog_source.contains("fog_alpha"));
  EXPECT_FALSE(fog_source.contains("discard;"));
}

NOLINT_TEST(EnvironmentLightingServiceSurfaceTest,
  Stage15ProofToolsUseFinalStage12BaselineAndEmitBlockingAsyncQualityKeys)
{
  const auto repo_root = SourceRoot().parent_path().parent_path();
  const auto vortexbasic_products = ReadTextFile(
    repo_root / "tools/vortex/AnalyzeRenderDocVortexBasicProducts.py");
  const auto async_products = ReadTextFile(
    repo_root / "tools/vortex/AnalyzeRenderDocAsyncProducts.py");
  const auto async_assert = ReadTextFile(
    repo_root / "tools/vortex/Assert-AsyncRuntimeProof.ps1");

  EXPECT_TRUE(vortexbasic_products.contains("choose_latest_stage_sample"));
  EXPECT_TRUE(vortexbasic_products.contains("stage12_final_last_draw_event"));
  EXPECT_FALSE(vortexbasic_products.contains("stage12_final_scene_color = find_output_sample(stage12_point, \"SceneColor\")"));

  EXPECT_TRUE(async_products.contains("stage15_async_scene_color_changed"));
  EXPECT_TRUE(async_products.contains("stage15_far_background_mask_valid"));
  EXPECT_TRUE(async_products.contains("stage15_sky_quality_ok"));

  EXPECT_TRUE(async_assert.contains("'stage15_async_scene_color_changed'"));
  EXPECT_TRUE(async_assert.contains("'stage15_far_background_mask_valid'"));
  EXPECT_TRUE(async_assert.contains("'stage15_sky_quality_ok'"));
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
  EXPECT_EQ(bindings->ambient_bridge.irradiance_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings->ambient_bridge.flags, 0U);
  EXPECT_EQ(service.GetLastPublicationState().ambient_bridge_view_count, 0U);
}

NOLINT_TEST_F(EnvironmentLightingServiceBehaviorTest,
  RenderSkyAndFogRecordsRealStage15DrawsInsteadOfStubExecutionFlags)
{
  auto service = EnvironmentLightingService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 6U }, oxygen::frame::Slot { 1U });
  auto scene_textures = SceneTextures(*graphics_, SceneTexturesConfig {
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
  auto scene_textures = SceneTextures(*graphics_, SceneTexturesConfig {
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
  auto ctx = MakeRenderContext(ViewId { 13U }, resolved_view, composition_view);

  graphics_->graphics_pipeline_log_.binds.clear();

  service.RenderSkyAndFog(ctx, scene_textures);

  const auto has_pipeline =
    [this](const std::string_view pipeline_name, const std::string_view source_path,
      const std::string_view entry_point, const bool expect_alpha_blend) -> bool {
    for (const auto& bind : graphics_->graphics_pipeline_log_.binds) {
      const auto& pixel_shader = bind.desc.PixelShader();
      if (pixel_shader.has_value() && pixel_shader->source_path == source_path
        && pixel_shader->entry_point == entry_point
        && bind.desc.GetName() == pipeline_name) {
        const auto& blend_state = bind.desc.BlendState();
        if (!expect_alpha_blend) {
          return blend_state.empty()
            || std::ranges::all_of(blend_state, [](const auto& target) {
                 return !target.blend_enable;
               });
        }
        return !blend_state.empty()
          && std::ranges::all_of(blend_state, [](const auto& target) {
               return target.blend_enable
                 && target.src_blend
                   == oxygen::graphics::BlendFactor::kSrcAlpha
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
  EXPECT_EQ(bindings->ambient_bridge.irradiance_map_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings->ambient_bridge.flags, 0U);
  EXPECT_EQ(service.GetLastPublicationState().published_view_count, 1U);
  EXPECT_EQ(service.GetLastPublicationState().ambient_bridge_view_count, 0U);
}

} // namespace
