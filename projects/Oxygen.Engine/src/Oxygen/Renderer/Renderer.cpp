//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Command.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/GpuEventScope.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Internal/BrdfLutManager.h>
#include <Oxygen/Renderer/Internal/EnvironmentStaticDataManager.h>
#include <Oxygen/Renderer/Internal/GpuDebugManager.h>
#include <Oxygen/Renderer/Internal/IblManager.h>
#include <Oxygen/Renderer/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/Internal/SunResolver.h>
#include <Oxygen/Renderer/Internal/ViewConstantsManager.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/Passes/CompositingPass.h>
#include <Oxygen/Renderer/Passes/IblComputePass.h>
#include <Oxygen/Renderer/Passes/SkyAtmosphereLutComputePass.h>
#include <Oxygen/Renderer/Passes/SkyCapturePass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/RenderContextPool.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Resources/DrawMetadataEmitter.h>
#include <Oxygen/Renderer/Resources/GeometryUploader.h>
#include <Oxygen/Renderer/Resources/MaterialBinder.h>
#include <Oxygen/Renderer/Resources/TextureBinder.h>
#include <Oxygen/Renderer/Resources/TransformUploader.h>
#include <Oxygen/Renderer/ScenePrep/CollectionConfig.h>
#include <Oxygen/Renderer/ScenePrep/FinalizationConfig.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepPipeline.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepState.h>
#include <Oxygen/Renderer/ShadowManager.h>
#include <Oxygen/Renderer/Types/CompositingTask.h>
#include <Oxygen/Renderer/Types/DebugFrameBindings.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Types/EnvironmentFrameBindings.h>
#include <Oxygen/Renderer/Types/EnvironmentStaticData.h>
#include <Oxygen/Renderer/Types/EnvironmentViewData.h>
#include <Oxygen/Renderer/Types/LightingFrameBindings.h>
#include <Oxygen/Renderer/Types/MaterialShadingConstants.h>
#include <Oxygen/Renderer/Types/SyntheticSunData.h>
#include <Oxygen/Renderer/Types/ViewColorData.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Renderer/Types/ViewFrameBindings.h>
#include <Oxygen/Renderer/Upload/Errors.h>
#include <Oxygen/Renderer/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/Upload/UploadPolicy.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/Sun.h>
#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Scene/Scene.h>

namespace {
constexpr std::string_view kCVarRendererTextureDumpTopN
  = "rndr.texture_dump_top_n";
constexpr std::string_view kCVarRendererLastFrameStats
  = "rndr.last_frame_stats";
constexpr std::string_view kCVarRendererSyntheticDirectionalBias
  = "rndr.directional_shadow.synthetic_bias";
constexpr std::string_view kCVarRendererSyntheticDirectionalNormalBias
  = "rndr.directional_shadow.synthetic_normal_bias";
constexpr std::string_view kCVarRendererVsmDirectionalReceiverNormalBiasScale
  = "rndr.vsm.directional_receiver_normal_bias_scale";
constexpr std::string_view kCVarRendererVsmDirectionalReceiverConstantBiasScale
  = "rndr.vsm.directional_receiver_constant_bias_scale";
constexpr std::string_view kCVarRendererVsmDirectionalReceiverSlopeBiasScale
  = "rndr.vsm.directional_receiver_slope_bias_scale";
constexpr std::string_view kCVarRendererVsmDirectionalRasterConstantBiasScale
  = "rndr.vsm.directional_raster_constant_bias_scale";
constexpr std::string_view kCVarRendererVsmDirectionalRasterSlopeBiasScale
  = "rndr.vsm.directional_raster_slope_bias_scale";
constexpr std::string_view kCommandRendererDumpTextureMemory
  = "rndr.dump_texture_memory";
constexpr std::string_view kCommandRendererDumpStats = "rndr.dump_stats";
constexpr int64_t kDefaultTextureDumpTopN = 20;
constexpr int64_t kMinTextureDumpTopN = 1;
constexpr int64_t kMaxTextureDumpTopN = 500;
constexpr float kDefaultSyntheticDirectionalBias = 0.0F;
constexpr float kDefaultSyntheticDirectionalNormalBias = 0.02F;
constexpr float kDefaultVsmDirectionalReceiverNormalBiasScale = 1.0F;
constexpr float kDefaultVsmDirectionalReceiverConstantBiasScale = 0.0F;
constexpr float kDefaultVsmDirectionalReceiverSlopeBiasScale = 0.5F;
constexpr float kDefaultVsmDirectionalRasterConstantBiasScale = 0.1F;
constexpr float kDefaultVsmDirectionalRasterSlopeBiasScale = 0.35F;

auto BuildSyntheticSunShadowInput(
  const oxygen::engine::RenderContext& render_context,
  const oxygen::engine::SyntheticSunData& resolved_sun,
  const float synthetic_constant_bias, const float synthetic_normal_bias)
  -> std::optional<oxygen::renderer::ShadowManager::SyntheticSunShadowInput>
{
  const auto scene = render_context.GetScene();
  if (!scene || resolved_sun.enabled == 0U) {
    return std::nullopt;
  }

  const auto environment = scene->GetEnvironment();
  if (!environment) {
    return std::nullopt;
  }

  const auto sun = environment->TryGetSystem<oxygen::scene::environment::Sun>();
  if (!sun || !sun->IsEnabled()
    || sun->GetSunSource() != oxygen::scene::environment::SunSource::kSynthetic
    || !sun->CastsShadows()) {
    return std::nullopt;
  }
  const auto shadow_cascades
    = oxygen::scene::CanonicalizeCascadedShadowSettings(
      oxygen::scene::CascadedShadowSettings {});

  return oxygen::renderer::ShadowManager::SyntheticSunShadowInput {
    .enabled = true,
    .direction_ws = -resolved_sun.GetDirection(),
    .bias = synthetic_constant_bias,
    .normal_bias = synthetic_normal_bias,
    .resolution_hint = static_cast<std::uint32_t>(
      oxygen::scene::CommonLightProperties {}.shadow.resolution_hint),
    .cascade_count = shadow_cascades.cascade_count,
    .distribution_exponent = shadow_cascades.distribution_exponent,
    .cascade_distances = shadow_cascades.cascade_distances,
  };
}

auto ParseTextureDumpTopN(std::string_view value) -> std::optional<int64_t>
{
  int64_t parsed = 0;
  const auto* begin = value.data();
  const auto* end = begin + value.size();
  const auto [ptr, ec] = std::from_chars(begin, end, parsed);
  if (ec != std::errc() || ptr != end) {
    return std::nullopt;
  }
  return parsed;
}

auto ParseVerbosity(std::string_view value) -> std::optional<int>
{
  int parsed = 0;
  const auto* begin = value.data();
  const auto* end = begin + value.size();
  const auto [ptr, ec] = std::from_chars(begin, end, parsed);
  if (ec != std::errc() || ptr != end) {
    return std::nullopt;
  }
  if (parsed < loguru::Verbosity_ERROR || parsed > loguru::Verbosity_MAX) {
    return std::nullopt;
  }
  return parsed;
}

constexpr std::uint64_t kFnvOffsetBasis = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

auto HashBytes(const void* data, const std::size_t size,
  std::uint64_t hash = kFnvOffsetBasis) -> std::uint64_t
{
  const auto* bytes = static_cast<const std::byte*>(data);
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= static_cast<std::uint64_t>(bytes[i]);
    hash *= kFnvPrime;
  }
  return hash;
}

auto HashPreparedShadowCasterContent(
  const oxygen::engine::PreparedSceneFrame& prepared_frame) -> std::uint64_t
{
  if (prepared_frame.draw_metadata_bytes.empty()
    || prepared_frame.partitions.empty()
    || prepared_frame.world_matrices.empty()) {
    return 0U;
  }

  const auto* draws = reinterpret_cast<const oxygen::engine::DrawMetadata*>(
    prepared_frame.draw_metadata_bytes.data());
  const auto draw_count = prepared_frame.draw_metadata_bytes.size()
    / sizeof(oxygen::engine::DrawMetadata);
  const auto matrix_count = prepared_frame.world_matrices.size() / 16U;

  std::vector<std::uint64_t> draw_hashes {};
  draw_hashes.reserve(draw_count);
  for (const auto& partition : prepared_frame.partitions) {
    if (!partition.pass_mask.IsSet(
          oxygen::engine::PassMaskBit::kShadowCaster)) {
      continue;
    }
    for (std::uint32_t draw_index = partition.begin;
      draw_index < partition.end && draw_index < draw_count; ++draw_index) {
      const auto& draw = draws[draw_index];
      if (!draw.flags.IsSet(oxygen::engine::PassMaskBit::kShadowCaster)) {
        continue;
      }
      auto draw_hash = HashBytes(&draw, sizeof(draw));
      if (draw.transform_index < matrix_count) {
        const auto* matrix = prepared_frame.world_matrices.data()
          + static_cast<std::size_t>(draw.transform_index) * 16U;
        draw_hash = HashBytes(matrix, sizeof(float) * 16U, draw_hash);
      }
      draw_hashes.push_back(draw_hash);
    }
  }

  if (draw_hashes.empty()) {
    return 0U;
  }

  std::ranges::sort(draw_hashes);
  std::uint64_t hash = kFnvOffsetBasis;
  const auto hashed_draws = static_cast<std::uint32_t>(draw_hashes.size());
  hash = HashBytes(&hashed_draws, sizeof(hashed_draws), hash);
  hash = HashBytes(
    draw_hashes.data(), draw_hashes.size() * sizeof(draw_hashes.front()), hash);
  return hash;
}

auto FormatRendererLastFrameStats(
  const oxygen::engine::Renderer::LastFrameStats& last_frame) -> std::string
{
  return fmt::format(
    "sceneprep_ms={:.3f},view_render_ms={:.3f},render_graph_ms={:.3f},"
    "env_update_ms={:.3f},compositing_ms={:.3f},views={},scene_views={}",
    last_frame.sceneprep_ms, last_frame.view_render_ms,
    last_frame.render_graph_ms, last_frame.env_update_ms,
    last_frame.compositing_ms, last_frame.views, last_frame.scene_views);
}

auto LogRendererPerformanceStats(
  const oxygen::engine::Renderer::Stats& stats, const int level) -> void
{
  VLOG_SCOPE_F(level, "Renderer Performance Statistics");
  {
    VLOG_SCOPE_F(level, "Averages");
    VLOG_F(level, "sceneprep ms   : {:.3f}", stats.sceneprep_avg_ms);
    VLOG_F(level, "view render ms : {:.3f}", stats.avg_view_render_ms);
    VLOG_F(level, "render graph ms: {:.3f}", stats.avg_render_graph_ms);
    VLOG_F(level, "env update ms  : {:.3f}", stats.avg_env_update_ms);
    VLOG_F(level, "compositing ms : {:.3f}", stats.avg_compositing_ms);
  }
  {
    VLOG_SCOPE_F(level, "Last Frame");
    VLOG_F(level, "sceneprep ms   : {:.3f}", stats.last_frame.sceneprep_ms);
    VLOG_F(level, "view render ms : {:.3f}", stats.last_frame.view_render_ms);
    VLOG_F(level, "render graph ms: {:.3f}", stats.last_frame.render_graph_ms);
    VLOG_F(level, "env update ms  : {:.3f}", stats.last_frame.env_update_ms);
    VLOG_F(level, "compositing ms : {:.3f}", stats.last_frame.compositing_ms);
    VLOG_F(level, "views          : {}", stats.last_frame.views);
    VLOG_F(level, "scene views    : {}", stats.last_frame.scene_views);
  }
}

auto ResolveViewOutputTexture(const oxygen::engine::FrameContext& context,
  const oxygen::ViewId view_id) -> std::shared_ptr<oxygen::graphics::Texture>
{
  const auto& view_ctx = context.GetViewContext(view_id);
  if (!view_ctx.composite_source) {
    LOG_F(ERROR,
      "View {} ('{}'/{}) missing composite_source framebuffer for compositing",
      view_id.get(), view_ctx.metadata.name, view_ctx.metadata.purpose);
    return {};
  }
  const auto& fb_desc = view_ctx.composite_source->GetDescriptor();
  if (fb_desc.color_attachments.empty()
    || !fb_desc.color_attachments[0].texture) {
    LOG_F(ERROR,
      "View {} ('{}'/{}) composite_source has no color attachment texture",
      view_id.get(), view_ctx.metadata.name, view_ctx.metadata.purpose);
    return {};
  }
  return fb_desc.color_attachments[0].texture;
}

auto TrackCompositionFramebuffer(oxygen::graphics::CommandRecorder& recorder,
  const oxygen::graphics::Framebuffer& framebuffer) -> void
{
  const auto& fb_desc = framebuffer.GetDescriptor();
  for (const auto& attachment : fb_desc.color_attachments) {
    if (!attachment.texture) {
      continue;
    }
    auto initial = attachment.texture->GetDescriptor().initial_state;
    if (initial == oxygen::graphics::ResourceStates::kUnknown
      || initial == oxygen::graphics::ResourceStates::kUndefined) {
      initial = oxygen::graphics::ResourceStates::kPresent;
    }
    recorder.BeginTrackingResourceState(*attachment.texture, initial, true);
  }

  if (fb_desc.depth_attachment.texture) {
    recorder.BeginTrackingResourceState(*fb_desc.depth_attachment.texture,
      oxygen::graphics::ResourceStates::kDepthWrite, true);
    recorder.FlushBarriers();
  }
}

auto CopyTextureToRegion(oxygen::graphics::CommandRecorder& recorder,
  oxygen::graphics::Texture& source, oxygen::graphics::Texture& backbuffer,
  const oxygen::ViewPort& viewport) -> void
{
  recorder.BeginTrackingResourceState(
    source, oxygen::graphics::ResourceStates::kCommon);
  recorder.RequireResourceState(
    source, oxygen::graphics::ResourceStates::kCopySource);
  recorder.RequireResourceState(
    backbuffer, oxygen::graphics::ResourceStates::kCopyDest);
  recorder.FlushBarriers();

  const auto& src_desc = source.GetDescriptor();
  const auto& dst_desc = backbuffer.GetDescriptor();

  const uint32_t dst_x = static_cast<uint32_t>(
    std::clamp(viewport.top_left_x, 0.0F, static_cast<float>(dst_desc.width)));
  const uint32_t dst_y = static_cast<uint32_t>(
    std::clamp(viewport.top_left_y, 0.0F, static_cast<float>(dst_desc.height)));

  const uint32_t max_dst_w
    = dst_desc.width > dst_x ? dst_desc.width - dst_x : 0U;
  const uint32_t max_dst_h
    = dst_desc.height > dst_y ? dst_desc.height - dst_y : 0U;

  const uint32_t copy_width = std::min(src_desc.width, max_dst_w);
  const uint32_t copy_height = std::min(src_desc.height, max_dst_h);

  if (copy_width == 0 || copy_height == 0) {
    return;
  }

  const oxygen::graphics::TextureSlice src_slice {
    .x = 0,
    .y = 0,
    .z = 0,
    .width = copy_width,
    .height = copy_height,
    .depth = 1,
  };

  const oxygen::graphics::TextureSlice dst_slice {
    .x = dst_x,
    .y = dst_y,
    .z = 0,
    .width = copy_width,
    .height = copy_height,
    .depth = 1,
  };

  constexpr oxygen::graphics::TextureSubResourceSet subresources {
    .base_mip_level = 0,
    .num_mip_levels = 1,
    .base_array_slice = 0,
    .num_array_slices = 1,
  };

  recorder.CopyTexture(
    source, src_slice, subresources, backbuffer, dst_slice, subresources);
  recorder.RequireResourceState(
    source, oxygen::graphics::ResourceStates::kCommon);
  recorder.FlushBarriers();
}

auto BuildSkyAtmosphereParamsFromEnvironment(
  const oxygen::scene::SceneEnvironment& scene_env,
  const oxygen::engine::internal::SkyAtmosphereLutManager& lut_mgr)
  -> std::optional<oxygen::engine::GpuSkyAtmosphereParams>
{
  namespace env = oxygen::scene::environment;

  const auto atmo = scene_env.TryGetSystem<env::SkyAtmosphere>();
  if (!atmo || !atmo->IsEnabled()) {
    return std::nullopt;
  }

  oxygen::engine::GpuSkyAtmosphereParams params {};
  params.enabled = 1U;
  params.planet_radius_m = atmo->GetPlanetRadiusMeters();
  params.atmosphere_height_m = atmo->GetAtmosphereHeightMeters();
  params.ground_albedo_rgb = atmo->GetGroundAlbedoRgb();
  params.rayleigh_scattering_rgb = atmo->GetRayleighScatteringRgb();
  params.rayleigh_scale_height_m = atmo->GetRayleighScaleHeightMeters();
  params.mie_scattering_rgb = atmo->GetMieScatteringRgb();
  params.mie_extinction_rgb
    = atmo->GetMieScatteringRgb() + atmo->GetMieAbsorptionRgb();
  params.mie_scale_height_m = atmo->GetMieScaleHeightMeters();
  params.mie_g = atmo->GetMieAnisotropy();
  params.absorption_rgb = atmo->GetAbsorptionRgb();
  params.absorption_density = atmo->GetOzoneDensityProfile();
  params.multi_scattering_factor = atmo->GetMultiScatteringFactor();
  params.aerial_perspective_distance_scale
    = atmo->GetAerialPerspectiveDistanceScale();

  float sun_disk_radius = env::Sun::kDefaultDiskAngularRadiusRad;
  if (const auto sun = scene_env.TryGetSystem<env::Sun>(); sun) {
    sun_disk_radius = sun->GetDiskAngularRadiusRadians();
  }
  params.sun_disk_angular_radius_radians = sun_disk_radius;
  params.sun_disk_enabled
    = (atmo->GetSunDiskEnabled() && sun_disk_radius > 0.0F) ? 1U : 0U;

  params.sky_view_lut_slices = lut_mgr.GetSkyViewLutSlices();
  params.sky_view_alt_mapping_mode = lut_mgr.GetAltMappingMode();

  return params;
}
} // namespace

namespace oxygen::renderer::internal {
auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::renderer::internal

using oxygen::Graphics;
using oxygen::data::Mesh;
using oxygen::data::detail::IndexType;
using oxygen::engine::MaterialShadingConstants;
using oxygen::engine::Renderer;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::SingleQueueStrategy;

//===----------------------------------------------------------------------===//
// Renderer Implementation
//===----------------------------------------------------------------------===//

Renderer::Renderer(std::weak_ptr<Graphics> graphics, RendererConfig config)
  : gfx_weak_(std::move(graphics))
  , config_(config)
  , scene_prep_(std::make_unique<sceneprep::ScenePrepPipelineImpl<
        decltype(sceneprep::CreateBasicCollectionConfig()),
        decltype(sceneprep::CreateStandardFinalizationConfig())>>(
      sceneprep::CreateBasicCollectionConfig(),
      sceneprep::CreateStandardFinalizationConfig()))
{
  LOG_F(
    2, "Renderer::Renderer [this={}] - constructor", static_cast<void*>(this));

  CHECK_F(!gfx_weak_.expired(), "Renderer constructed with expired Graphics");
  auto gfx = gfx_weak_.lock();

  // Require a non-empty upload queue key in the renderer configuration.
  CHECK_F(!config_.upload_queue_key.empty(),
    "RendererConfig.upload_queue_key must not be empty");

  // Build upload policy and honour configured upload queue from Renderer
  // configuration.
  auto policy = upload::DefaultUploadPolicy();
  policy.upload_queue_key = graphics::QueueKey { config_.upload_queue_key };

  uploader_ = std::make_unique<upload::UploadCoordinator>(
    observer_ptr { gfx.get() }, policy);
  upload_staging_provider_
    = uploader_->CreateRingBufferStaging(frame::kFramesInFlight, 16,
      upload::kDefaultRingBufferStagingSlack, "Renderer.UploadStaging");

  inline_transfers_ = std::make_unique<upload::InlineTransfersCoordinator>(
    observer_ptr { gfx.get() });
  inline_staging_provider_
    = uploader_->CreateRingBufferStaging(frame::kFramesInFlight, 16,
      upload::kDefaultRingBufferStagingSlack, "Renderer.InlineStaging");
  inline_transfers_->RegisterProvider(inline_staging_provider_);

  // Initialize the render-context pool helper used to claim per-frame
  // render contexts during PreRender/Render phases.
  render_context_pool_ = std::make_unique<RenderContextPool>();
}

Renderer::~Renderer()
{
  const auto stats = GetStats();
  LogRendererPerformanceStats(stats, loguru::Verbosity_INFO);

  OnShutdown();
  scene_prep_.reset();
}

auto Renderer::GetStats() const noexcept -> Stats
{
  const double sceneprep_avg_ms = sceneprep_profile_frames_ > 0
    ? static_cast<double>(sceneprep_profile_total_ns_) / 1'000'000.0
      / static_cast<double>(sceneprep_profile_frames_)
    : 0.0;
  const double avg_view_render_ms = render_profile_frames_ > 0
    ? static_cast<double>(render_profile_view_render_total_ns_) / 1'000'000.0
      / static_cast<double>(render_profile_frames_)
    : 0.0;
  const double avg_render_graph_ms = render_profile_frames_ > 0
    ? static_cast<double>(render_profile_render_graph_total_ns_) / 1'000'000.0
      / static_cast<double>(render_profile_frames_)
    : 0.0;
  const double avg_env_update_ms = render_profile_frames_ > 0
    ? static_cast<double>(render_profile_env_update_total_ns_) / 1'000'000.0
      / static_cast<double>(render_profile_frames_)
    : 0.0;
  const double avg_compositing_ms = compositing_profile_frames_ > 0
    ? static_cast<double>(compositing_profile_total_ns_) / 1'000'000.0
      / static_cast<double>(compositing_profile_frames_)
    : 0.0;

  return Stats {
    .sceneprep_avg_ms = sceneprep_avg_ms,
    .avg_view_render_ms = avg_view_render_ms,
    .avg_render_graph_ms = avg_render_graph_ms,
    .avg_env_update_ms = avg_env_update_ms,
    .avg_compositing_ms = avg_compositing_ms,
    .last_frame = LastFrameStats {
      .sceneprep_ms = static_cast<double>(sceneprep_last_frame_ns_)
        / 1'000'000.0,
      .view_render_ms = static_cast<double>(render_last_frame_view_render_ns_)
        / 1'000'000.0,
      .render_graph_ms
      = static_cast<double>(render_last_frame_render_graph_ns_) / 1'000'000.0,
      .env_update_ms
      = static_cast<double>(render_last_frame_env_update_ns_) / 1'000'000.0,
      .compositing_ms
      = static_cast<double>(compositing_last_frame_ns_) / 1'000'000.0,
      .views = sceneprep_last_frame_view_count_,
      .scene_views = sceneprep_last_frame_scene_view_count_,
    },
  };
}

auto Renderer::ResetStats() noexcept -> void
{
  sceneprep_profile_frames_ = 0;
  sceneprep_profile_total_ns_ = 0;
  sceneprep_last_frame_ns_ = 0;
  sceneprep_last_frame_view_count_ = 0;
  sceneprep_last_frame_scene_view_count_ = 0;
  render_profile_frames_ = 0;
  render_profile_view_render_total_ns_ = 0;
  render_profile_render_graph_total_ns_ = 0;
  render_profile_env_update_total_ns_ = 0;
  render_last_frame_view_render_ns_ = 0;
  render_last_frame_render_graph_ns_ = 0;
  render_last_frame_env_update_ns_ = 0;
  compositing_profile_frames_ = 0;
  compositing_profile_total_ns_ = 0;
  compositing_last_frame_ns_ = 0;
}

auto Renderer::OnAttached(observer_ptr<IAsyncEngine> engine) noexcept -> bool
{
  DCHECK_NOTNULL_F(engine);

  asset_loader_ = engine->GetAssetLoader();
  if (!asset_loader_) {
    LOG_F(ERROR, "AssetLoader unavailable; cannot initialize TextureBinder");
    return false;
  }

  if (!scene_prep_state_) {
    auto gfx = gfx_weak_.lock();
    if (!gfx) {
      LOG_F(ERROR, "Graphics expired during Renderer::OnAttached");
      return false;
    }

    auto geom_uploader
      = std::make_unique<renderer::resources::GeometryUploader>(
        observer_ptr { gfx.get() }, observer_ptr { uploader_.get() },
        observer_ptr { upload_staging_provider_.get() },
        observer_ptr { asset_loader_.get() });
    auto xform_uploader
      = std::make_unique<renderer::resources::TransformUploader>(
        observer_ptr { gfx.get() },
        observer_ptr { inline_staging_provider_.get() },
        observer_ptr { inline_transfers_.get() });

    auto texture_binder = std::make_unique<renderer::resources::TextureBinder>(
      observer_ptr { gfx.get() },
      observer_ptr { upload_staging_provider_.get() },
      observer_ptr { uploader_.get() }, observer_ptr { asset_loader_.get() });

    auto mat_binder = std::make_unique<renderer::resources::MaterialBinder>(
      observer_ptr { gfx.get() }, observer_ptr { uploader_.get() },
      observer_ptr { upload_staging_provider_.get() },
      observer_ptr { texture_binder.get() },
      observer_ptr { asset_loader_.get() });

    auto emitter = std::make_unique<renderer::resources::DrawMetadataEmitter>(
      observer_ptr { gfx.get() },
      observer_ptr { inline_staging_provider_.get() },
      observer_ptr { geom_uploader.get() }, observer_ptr { mat_binder.get() },
      observer_ptr { inline_transfers_.get() });

    auto light_manager
      = std::make_unique<renderer::LightManager>(observer_ptr { gfx.get() },
        observer_ptr { inline_staging_provider_.get() },
        observer_ptr { inline_transfers_.get() });

    scene_prep_state_ = std::make_unique<sceneprep::ScenePrepState>(
      std::move(geom_uploader), std::move(xform_uploader),
      std::move(mat_binder), std::move(emitter), std::move(light_manager));
    texture_binder_ = std::move(texture_binder);
    shadow_manager_
      = std::make_unique<renderer::ShadowManager>(observer_ptr { gfx.get() },
        observer_ptr { inline_staging_provider_.get() },
        observer_ptr { inline_transfers_.get() }, config_.shadow_quality_tier,
        config_.directional_shadow_policy);
    shadow_manager_->SetDirectionalVirtualBiasSettings(
      directional_shadow_bias_state_.virtual_directional);

    // Initialize the ViewConstants manager for per-view, per-slot upload-heap
    // storage.
    // buffers.
    view_const_manager_ = std::make_unique<internal::ViewConstantsManager>(
      observer_ptr { gfx.get() },
      static_cast<std::uint32_t>(sizeof(ViewConstants::GpuData)));

    view_frame_bindings_publisher_ = std::make_unique<
      internal::PerViewStructuredPublisher<ViewFrameBindings>>(
      observer_ptr { gfx.get() }, *inline_staging_provider_,
      observer_ptr { inline_transfers_.get() }, "ViewFrameBindings");
    draw_frame_bindings_publisher_ = std::make_unique<
      internal::PerViewStructuredPublisher<DrawFrameBindings>>(
      observer_ptr { gfx.get() }, *inline_staging_provider_,
      observer_ptr { inline_transfers_.get() }, "DrawFrameBindings");
    view_color_data_publisher_
      = std::make_unique<internal::PerViewStructuredPublisher<ViewColorData>>(
        observer_ptr { gfx.get() }, *inline_staging_provider_,
        observer_ptr { inline_transfers_.get() }, "ViewColorData");
    debug_frame_bindings_publisher_ = std::make_unique<
      internal::PerViewStructuredPublisher<DebugFrameBindings>>(
      observer_ptr { gfx.get() }, *inline_staging_provider_,
      observer_ptr { inline_transfers_.get() }, "DebugFrameBindings");
    lighting_frame_bindings_publisher_ = std::make_unique<
      internal::PerViewStructuredPublisher<LightingFrameBindings>>(
      observer_ptr { gfx.get() }, *inline_staging_provider_,
      observer_ptr { inline_transfers_.get() }, "LightingFrameBindings");
    shadow_frame_bindings_publisher_ = std::make_unique<
      internal::PerViewStructuredPublisher<ShadowFrameBindings>>(
      observer_ptr { gfx.get() }, *inline_staging_provider_,
      observer_ptr { inline_transfers_.get() }, "ShadowFrameBindings");
    environment_view_data_publisher_ = std::make_unique<
      internal::PerViewStructuredPublisher<EnvironmentViewData>>(
      observer_ptr { gfx.get() }, *inline_staging_provider_,
      observer_ptr { inline_transfers_.get() }, "EnvironmentViewData");
    environment_frame_bindings_publisher_ = std::make_unique<
      internal::PerViewStructuredPublisher<EnvironmentFrameBindings>>(
      observer_ptr { gfx.get() }, *inline_staging_provider_,
      observer_ptr { inline_transfers_.get() }, "EnvironmentFrameBindings");

    // Precompute and bind BRDF integration LUTs (bindless SRV slot provider).
    brdf_lut_manager_ = std::make_unique<internal::BrdfLutManager>(
      observer_ptr { gfx.get() }, observer_ptr { uploader_.get() },
      observer_ptr { upload_staging_provider_.get() });

    // Create sky capture pass
    sky_capture_pass_config_ = std::make_shared<SkyCapturePassConfig>();
    sky_capture_pass_config_->resolution = 128u;
    sky_capture_pass_ = std::make_unique<SkyCapturePass>(
      observer_ptr { gfx.get() }, sky_capture_pass_config_);

    // Create sky atmosphere LUT compute pass (explicitly executed before sky
    // capture so the capture never runs against stale LUTs).
    sky_atmo_lut_compute_pass_config_
      = std::make_shared<SkyAtmosphereLutComputePassConfig>();
    sky_atmo_lut_compute_pass_ = std::make_unique<SkyAtmosphereLutComputePass>(
      observer_ptr { gfx.get() }, sky_atmo_lut_compute_pass_config_);

    // Initialize IBL Manager
    ibl_manager_
      = std::make_unique<internal::IblManager>(observer_ptr { gfx.get() });

    // Initialize environment static data single-owner manager (bindless SRV).
    // TextureBinder is passed directly to the constructor for cubemap
    // resolution.
    env_static_manager_
      = std::make_unique<internal::EnvironmentStaticDataManager>(
        observer_ptr { gfx.get() }, observer_ptr { texture_binder_.get() },
        observer_ptr { brdf_lut_manager_.get() },
        observer_ptr { ibl_manager_.get() },
        observer_ptr { sky_capture_pass_.get() });

    ibl_compute_pass_ = std::make_unique<IblComputePass>("IblComputePass");

    // Initialize GPU debug manager.
    gpu_debug_manager_
      = std::make_unique<internal::GpuDebugManager>(observer_ptr { gfx.get() });
  }
  return true;
}

auto Renderer::RegisterConsoleBindings(
  const observer_ptr<console::Console> console) noexcept -> void
{
  if (console == nullptr) {
    return;
  }
  console_ = console;

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarRendererTextureDumpTopN),
    .help = "Default top-N count for rndr.dump_texture_memory",
    .default_value = kDefaultTextureDumpTopN,
    .flags = console::CVarFlags::kDevOnly,
    .min_value = static_cast<double>(kMinTextureDumpTopN),
    .max_value = static_cast<double>(kMaxTextureDumpTopN),
  });

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarRendererLastFrameStats),
    .help = "Renderer last-frame timing summary",
    .default_value = FormatRendererLastFrameStats(GetStats().last_frame),
    .flags = console::CVarFlags::kDevOnly,
    .min_value = std::nullopt,
    .max_value = std::nullopt,
  });

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarRendererSyntheticDirectionalBias),
    .help = "Synthetic sun directional constant bias",
    .default_value = static_cast<double>(kDefaultSyntheticDirectionalBias),
    .flags = console::CVarFlags::kArchive,
    .min_value = 0.0,
    .max_value = 0.01,
  });

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarRendererSyntheticDirectionalNormalBias),
    .help = "Synthetic sun directional normal bias",
    .default_value
    = static_cast<double>(kDefaultSyntheticDirectionalNormalBias),
    .flags = console::CVarFlags::kArchive,
    .min_value = 0.0,
    .max_value = 8.0,
  });

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarRendererVsmDirectionalReceiverNormalBiasScale),
    .help = "Directional VSM receiver normal-bias scale",
    .default_value
    = static_cast<double>(kDefaultVsmDirectionalReceiverNormalBiasScale),
    .flags = console::CVarFlags::kArchive,
    .min_value = 0.0,
    .max_value = 8.0,
  });

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarRendererVsmDirectionalReceiverConstantBiasScale),
    .help = "Directional VSM receiver constant-bias scale",
    .default_value
    = static_cast<double>(kDefaultVsmDirectionalReceiverConstantBiasScale),
    .flags = console::CVarFlags::kArchive,
    .min_value = 0.0,
    .max_value = 8.0,
  });

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarRendererVsmDirectionalReceiverSlopeBiasScale),
    .help = "Directional VSM receiver slope-bias scale",
    .default_value
    = static_cast<double>(kDefaultVsmDirectionalReceiverSlopeBiasScale),
    .flags = console::CVarFlags::kArchive,
    .min_value = 0.0,
    .max_value = 8.0,
  });

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarRendererVsmDirectionalRasterConstantBiasScale),
    .help = "Directional VSM raster constant-bias scale",
    .default_value
    = static_cast<double>(kDefaultVsmDirectionalRasterConstantBiasScale),
    .flags = console::CVarFlags::kArchive,
    .min_value = 0.0,
    .max_value = 8.0,
  });

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarRendererVsmDirectionalRasterSlopeBiasScale),
    .help = "Directional VSM raster slope-bias scale",
    .default_value
    = static_cast<double>(kDefaultVsmDirectionalRasterSlopeBiasScale),
    .flags = console::CVarFlags::kArchive,
    .min_value = 0.0,
    .max_value = 8.0,
  });

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarRendererVsmDirectionalRasterConstantBiasScale),
    .help = "Directional VSM raster constant-bias scale",
    .default_value
    = static_cast<double>(kDefaultVsmDirectionalRasterConstantBiasScale),
    .flags = console::CVarFlags::kArchive,
    .min_value = 0.0,
    .max_value = 8.0,
  });

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarRendererVsmDirectionalRasterSlopeBiasScale),
    .help = "Directional VSM raster slope-bias scale",
    .default_value
    = static_cast<double>(kDefaultVsmDirectionalRasterSlopeBiasScale),
    .flags = console::CVarFlags::kArchive,
    .min_value = 0.0,
    .max_value = 8.0,
  });

  (void)console->RegisterCommand(console::CommandDefinition {
    .name = std::string(kCommandRendererDumpTextureMemory),
    .help = "Dump renderer texture memory usage [top_n]",
    .flags = console::CommandFlags::kDevOnly,
    .handler = [this](const std::vector<std::string>& args,
                 const console::CommandContext&) -> console::ExecutionResult {
      int64_t top_n = kDefaultTextureDumpTopN;
      if (!args.empty()) {
        const auto parsed_top_n = ParseTextureDumpTopN(args.front());
        if (!parsed_top_n.has_value()) {
          return console::ExecutionResult {
            .status = console::ExecutionStatus::kInvalidArguments,
            .exit_code = 2,
            .output = {},
            .error = "top_n must be an integer",
          };
        }
        top_n
          = std::clamp(*parsed_top_n, kMinTextureDumpTopN, kMaxTextureDumpTopN);
      }

      DumpEstimatedTextureMemory(static_cast<std::size_t>(top_n));
      return console::ExecutionResult {
        .status = console::ExecutionStatus::kOk,
        .exit_code = 0,
        .output = "renderer texture memory dump emitted",
        .error = {},
      };
    },
  });

  (void)console->RegisterCommand(console::CommandDefinition {
    .name = std::string(kCommandRendererDumpStats),
    .help = "Dump renderer performance statistics [verbosity]",
    .flags = console::CommandFlags::kDevOnly,
    .handler = [this](const std::vector<std::string>& args,
                 const console::CommandContext&) -> console::ExecutionResult {
      if (args.size() > 1) {
        return console::ExecutionResult {
          .status = console::ExecutionStatus::kInvalidArguments,
          .exit_code = 2,
          .output = {},
          .error = "usage: rndr.dump_stats [verbosity]",
        };
      }

      int verbosity = loguru::Verbosity_INFO;
      if (!args.empty()) {
        const auto parsed = ParseVerbosity(args.front());
        if (!parsed.has_value()) {
          return console::ExecutionResult {
            .status = console::ExecutionStatus::kInvalidArguments,
            .exit_code = 2,
            .output = {},
            .error = fmt::format("verbosity must be integer in [{}, {}]",
              static_cast<int>(loguru::Verbosity_ERROR),
              static_cast<int>(loguru::Verbosity_MAX)),
          };
        }
        verbosity = *parsed;
      }

      const auto stats = GetStats();
      LogRendererPerformanceStats(stats, verbosity);
      return console::ExecutionResult {
        .status = console::ExecutionStatus::kOk,
        .exit_code = 0,
        .output = FormatRendererLastFrameStats(stats.last_frame),
        .error = {},
      };
    },
  });
}

auto Renderer::ApplyConsoleCVars(
  const observer_ptr<const console::Console> console) noexcept -> void
{
  if (console == nullptr) {
    return;
  }

  double synthetic_constant_bias
    = directional_shadow_bias_state_.synthetic_constant_bias;
  if (console->TryGetCVarValue<double>(
        kCVarRendererSyntheticDirectionalBias, synthetic_constant_bias)) {
    directional_shadow_bias_state_.synthetic_constant_bias
      = static_cast<float>(synthetic_constant_bias);
  }

  double synthetic_normal_bias
    = directional_shadow_bias_state_.synthetic_normal_bias;
  if (console->TryGetCVarValue<double>(
        kCVarRendererSyntheticDirectionalNormalBias, synthetic_normal_bias)) {
    directional_shadow_bias_state_.synthetic_normal_bias
      = static_cast<float>(synthetic_normal_bias);
  }

  double receiver_normal_bias_scale
    = directional_shadow_bias_state_.virtual_directional
        .receiver_normal_bias_scale;
  if (console->TryGetCVarValue<double>(
        kCVarRendererVsmDirectionalReceiverNormalBiasScale,
        receiver_normal_bias_scale)) {
    directional_shadow_bias_state_.virtual_directional
      .receiver_normal_bias_scale
      = static_cast<float>(receiver_normal_bias_scale);
  }

  double receiver_constant_bias_scale
    = directional_shadow_bias_state_.virtual_directional
        .receiver_constant_bias_scale;
  if (console->TryGetCVarValue<double>(
        kCVarRendererVsmDirectionalReceiverConstantBiasScale,
        receiver_constant_bias_scale)) {
    directional_shadow_bias_state_.virtual_directional
      .receiver_constant_bias_scale
      = static_cast<float>(receiver_constant_bias_scale);
  }

  double receiver_slope_bias_scale
    = directional_shadow_bias_state_.virtual_directional
        .receiver_slope_bias_scale;
  if (console->TryGetCVarValue<double>(
        kCVarRendererVsmDirectionalReceiverSlopeBiasScale,
        receiver_slope_bias_scale)) {
    directional_shadow_bias_state_.virtual_directional
      .receiver_slope_bias_scale
      = static_cast<float>(receiver_slope_bias_scale);
  }

  double raster_constant_bias_scale
    = directional_shadow_bias_state_.virtual_directional
        .raster_constant_bias_scale;
  if (console->TryGetCVarValue<double>(
        kCVarRendererVsmDirectionalRasterConstantBiasScale,
        raster_constant_bias_scale)) {
    directional_shadow_bias_state_.virtual_directional
      .raster_constant_bias_scale
      = static_cast<float>(raster_constant_bias_scale);
  }

  double raster_slope_bias_scale
    = directional_shadow_bias_state_.virtual_directional
        .raster_slope_bias_scale;
  if (console->TryGetCVarValue<double>(
        kCVarRendererVsmDirectionalRasterSlopeBiasScale,
        raster_slope_bias_scale)) {
    directional_shadow_bias_state_.virtual_directional
      .raster_slope_bias_scale
      = static_cast<float>(raster_slope_bias_scale);
  }

  static bool logged_renderer_bias_cvars = false;
  if (!logged_renderer_bias_cvars) {
    LOG_F(INFO,
      "Renderer bias CVars: synthetic_constant_bias={} synthetic_normal_bias={} "
      "receiver_normal_scale={} receiver_constant_scale={} receiver_slope_scale={} "
      "raster_constant_scale={} raster_slope_scale={}",
      directional_shadow_bias_state_.synthetic_constant_bias,
      directional_shadow_bias_state_.synthetic_normal_bias,
      directional_shadow_bias_state_.virtual_directional.receiver_normal_bias_scale,
      directional_shadow_bias_state_.virtual_directional.receiver_constant_bias_scale,
      directional_shadow_bias_state_.virtual_directional.receiver_slope_bias_scale,
      directional_shadow_bias_state_.virtual_directional.raster_constant_bias_scale,
      directional_shadow_bias_state_.virtual_directional.raster_slope_bias_scale);
    logged_renderer_bias_cvars = true;
  }

  if (shadow_manager_) {
    shadow_manager_->SetDirectionalVirtualBiasSettings(
      directional_shadow_bias_state_.virtual_directional);
  }
}

auto Renderer::OnShutdown() noexcept -> void
{
  if (uploader_) {
    if (const auto shutdown_result = uploader_->Shutdown();
      !shutdown_result.has_value()) {
      LOG_F(WARNING, "Renderer::OnShutdown upload drain failed: {}",
        make_error_code(shutdown_result.error()).message());
    }
  }

  console_ = nullptr;
  asset_loader_ = nullptr;
  render_context_ = nullptr;

  {
    std::unique_lock registration_lock(view_registration_mutex_);
    render_graphs_.clear();
  }

  {
    std::unique_lock state_lock(view_state_mutex_);
    view_ready_states_.clear();
  }

  {
    std::lock_guard lock(composition_mutex_);
    composition_submission_.reset();
    composition_surface_.reset();
  }

  {
    std::lock_guard lock(pending_cleanup_mutex_);
    pending_cleanup_.clear();
  }

  prepared_frames_.clear();
  resolved_views_.clear();
  per_view_storage_.clear();
  per_view_runtime_state_.clear();
  per_view_atmo_luts_.clear();
  last_atmo_generation_.clear();
  last_seen_view_frame_seq_.clear();

  sky_capture_pass_.reset();
  sky_capture_pass_config_.reset();
  sky_atmo_lut_compute_pass_.reset();
  sky_atmo_lut_compute_pass_config_.reset();
  ibl_compute_pass_.reset();
  compositing_pass_.reset();
  compositing_pass_config_.reset();

  texture_binder_.reset();
  gpu_debug_manager_.reset();
  scene_prep_state_.reset();
  env_static_manager_.reset();
  ibl_manager_.reset();
  brdf_lut_manager_.reset();

  view_frame_bindings_publisher_.reset();
  draw_frame_bindings_publisher_.reset();
  view_color_data_publisher_.reset();
  debug_frame_bindings_publisher_.reset();
  lighting_frame_bindings_publisher_.reset();
  shadow_manager_.reset();
  shadow_frame_bindings_publisher_.reset();
  environment_view_data_publisher_.reset();
  environment_frame_bindings_publisher_.reset();
  view_const_manager_.reset();
  render_context_pool_.reset();

  inline_transfers_.reset();
  upload_staging_provider_.reset();
  inline_staging_provider_.reset();
  uploader_.reset();

  if (auto gfx = gfx_weak_.lock()) {
    gfx->GetDeferredReclaimer().OnRendererShutdown();
  }
}

auto Renderer::GetGraphics() -> std::shared_ptr<Graphics>
{
  auto graphics_ptr = gfx_weak_.lock();
  if (!graphics_ptr) {
    throw std::runtime_error("Graphics expired in Renderer::GetGraphics");
  }
  return graphics_ptr;
}

auto Renderer::GetStagingProvider() -> upload::StagingProvider&
{
  DCHECK_NOTNULL_F(
    inline_staging_provider_, "StagingProvider is not initialized");
  return *inline_staging_provider_;
}

auto Renderer::GetInlineTransfersCoordinator()
  -> upload::InlineTransfersCoordinator&
{
  DCHECK_NOTNULL_F(
    inline_transfers_, "InlineTransfersCoordinator is not initialized");
  return *inline_transfers_;
}

auto Renderer::GetLightManager() const noexcept
  -> observer_ptr<renderer::LightManager>
{
  if (!scene_prep_state_) {
    return nullptr;
  }
  return scene_prep_state_->GetLightManager();
}

auto Renderer::GetShadowManager() const noexcept
  -> observer_ptr<renderer::ShadowManager>
{
  return observer_ptr { shadow_manager_.get() };
}

auto Renderer::GetSkyAtmosphereLutManagerForView(
  const ViewId view_id) const noexcept
  -> observer_ptr<internal::SkyAtmosphereLutManager>
{
  if (const auto it = per_view_atmo_luts_.find(view_id);
    it != per_view_atmo_luts_.end()) {
    return observer_ptr { it->second.get() };
  }
  return nullptr;
}

auto Renderer::GetEnvironmentStaticDataManager() const noexcept
  -> observer_ptr<internal::EnvironmentStaticDataManager>
{
  return observer_ptr { env_static_manager_.get() };
}

auto Renderer::GetIblManager() const noexcept
  -> observer_ptr<internal::IblManager>
{
  return observer_ptr { ibl_manager_.get() };
}

auto Renderer::GetIblComputePass() const noexcept
  -> observer_ptr<IblComputePass>
{
  return observer_ptr { ibl_compute_pass_.get() };
}

auto Renderer::DumpEstimatedTextureMemory(const std::size_t top_n) const -> void
{
  if (!texture_binder_) {
    LOG_F(
      WARNING, "TextureBinder is not initialized; cannot dump texture memory");
    return;
  }

  texture_binder_->DumpEstimatedTextureMemory(top_n);
}

//=== Debug Overrides ===-----------------------------------------------------//

auto Renderer::RequestIblRegeneration() noexcept -> void
{
  if (!ibl_compute_pass_) {
    return;
  }

  ibl_compute_pass_->RequestRegenerationOnce();
}

auto Renderer::RequestSkyCapture() noexcept -> void
{
  sky_capture_requested_ = true;
  if (sky_capture_pass_) {
    for (const auto& [view_id, _] : render_graphs_) {
      sky_capture_pass_->MarkDirty(view_id);
    }
  }
}

auto Renderer::SetAtmosphereBlueNoiseEnabled(const bool enabled) noexcept
  -> void
{
  atmosphere_blue_noise_enabled_ = enabled;
  if (env_static_manager_) {
    env_static_manager_->SetBlueNoiseEnabled(enabled);
  }
}

/*!
 Returns the active render context for the current frame.

 @return Observer pointer to the active render context, or null if the
   renderer is outside a frame scope.

### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None
 - Optimization: None
*/
auto Renderer::OverrideMaterialUvTransform(const data::MaterialAsset& material,
  const glm::vec2 uv_scale, const glm::vec2 uv_offset) -> bool
{
  if (!scene_prep_state_) {
    return false;
  }

  const auto materials = scene_prep_state_->GetMaterialBinder();
  if (!materials) {
    return false;
  }

  return materials->OverrideUvTransform(material, uv_scale, uv_offset);
}

auto Renderer::RegisterViewRenderGraph(
  ViewId view_id, RenderGraphFactory factory, ResolvedView view) -> void
{
  std::unique_lock lock(view_registration_mutex_);
  render_graphs_.insert_or_assign(view_id, std::move(factory));
  resolved_views_.insert_or_assign(view_id, std::move(view));
  DLOG_F(1, "RegisterViewRenderGraph: view_id={}, total_views={}",
    view_id.get(), render_graphs_.size());
}

auto Renderer::UnregisterViewRenderGraph(ViewId view_id) -> void
{
  std::size_t removed_graph = 0;
  {
    std::unique_lock lock(view_registration_mutex_);
    removed_graph = render_graphs_.erase(view_id);
    resolved_views_.erase(view_id);
  }

  DLOG_F(1, "UnregisterViewRenderGraph: view_id={}, removed_factory={}",
    view_id.get(), removed_graph);

  std::size_t pending_size = 0;
  {
    std::lock_guard lock(pending_cleanup_mutex_);
    pending_cleanup_.insert(view_id);
    pending_size = pending_cleanup_.size();
  }

  DLOG_F(
    1, "UnregisterViewRenderGraph: pending_cleanup_count={}", pending_size);

  {
    std::unique_lock state_lock(view_state_mutex_);
    view_ready_states_.erase(view_id);
  }
}

auto Renderer::RegisterComposition(CompositionSubmission submission,
  std::shared_ptr<graphics::Surface> target_surface) -> void
{
  std::lock_guard lock(composition_mutex_);
  composition_submission_ = std::move(submission);
  composition_surface_ = std::move(target_surface);
}

auto Renderer::IsViewReady(ViewId view_id) const -> bool
{
  std::shared_lock lock(view_state_mutex_);
  const auto it = view_ready_states_.find(view_id);
  return it != view_ready_states_.end() && it->second;
}

auto Renderer::OnPreRender(observer_ptr<FrameContext> context) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  const auto dt = context->GetModuleTimingData().game_delta_time;
  const auto dt_ns = dt.get();
  const auto dt_seconds
    = std::chrono::duration_cast<std::chrono::duration<float>>(dt_ns).count();
  if (dt_seconds > 0.0F) {
    last_frame_dt_seconds_ = dt_seconds;
  } else {
    last_frame_dt_seconds_ = 1.0F / 60.0F;
  }

  DrainPendingViewCleanup("OnPreRender");

  {
    std::shared_lock lock(view_registration_mutex_);
    if (render_graphs_.empty()) {
      DLOG_F(WARNING, "no render graphs registered; skipping");
      co_return;
    }
  }

  // Failing to acquire a slot will throw, and drop the frame.
  render_context_
    = observer_ptr { &render_context_pool_->Acquire(context->GetFrameSlot()) };

  {
    auto graphics_p = gfx_weak_.lock();
    if (!graphics_p) {
      LOG_F(ERROR, "Graphics expired during OnPreRender");
      co_return;
    }
    render_context_->SetRenderer(this, graphics_p.get());
  }

  render_context_->scene = observer_ptr<const scene::Scene> {
    context->GetScene().get(),
  };

  // Populate frame identity on the pooled RenderContext as early as possible.
  // Several subsystems (e.g. EnvironmentStaticDataManager) are invoked during
  // OnPreRender and rely on these values for correct per-frame publication and
  // diagnostics.
  render_context_->frame_slot = context->GetFrameSlot();
  render_context_->frame_sequence = context->GetFrameSequenceNumber();
  render_context_->delta_time = last_frame_dt_seconds_;

  // Clear per-frame prepared state. Resolved views are published externally
  // before OnPreRender and are reset in OnFrameStart.
  // Deferred cleanup of unregistered views is performed at frame end
  // (OnFrameEnd) to avoid destroying entries while other modules may add
  // registrations during the frame start.
  {
    std::unique_lock state_lock(view_state_mutex_);
    view_ready_states_.clear();
  }
  prepared_frames_.clear();
  per_view_storage_.clear();

  // EnvStatic is now updated per view in PrepareAndWireViewConstantsForView.

  // Iterate all views registered in FrameContext and prepare each one
  auto views_range = context->GetViews();
  const auto frame_view_count
    = static_cast<std::size_t>(std::ranges::distance(views_range));
  std::size_t resolved_scene_view_count = 0;
  for (const auto& view_ref : views_range) {
    const auto& view_ctx = view_ref.get();
    const bool is_scene_view = view_ctx.metadata.is_scene_view;
    if (is_scene_view && resolved_views_.contains(view_ctx.id)) {
      ++resolved_scene_view_count;
    }
  }

  const bool single_view_mode = (resolved_scene_view_count == 1);
  std::uint64_t sceneprep_frame_ns = 0;
  bool first = true;

  auto prep_views_range = context->GetViews();
  for (const auto& view_ref : prep_views_range) {
    const auto& view_ctx = view_ref.get();
    if (!view_ctx.metadata.is_scene_view) {
      DLOG_F(2, "Skipping ScenePrep for non-scene view {}", view_ctx.id.get());
      continue;
    }
    DLOG_SCOPE_F(2,
      fmt::format(
        "View {} ({})", nostd::to_string(view_ctx.id), view_ctx.metadata.name)
        .c_str());
    try {
      const auto resolved_it = resolved_views_.find(view_ctx.id);
      if (resolved_it == resolved_views_.end()) {
        LOG_F(2, "View {} has no resolved view; skipping", view_ctx.id.get());
        continue;
      }
      const auto& resolved = resolved_it->second;

      // Build frame data for this view (scene prep, culling, draw list)
      const auto prep_begin = std::chrono::steady_clock::now();
      [[maybe_unused]] const auto draw_count = RunScenePrep(
        view_ctx.id, resolved, *context, first, single_view_mode);
      const auto prep_end = std::chrono::steady_clock::now();
      sceneprep_frame_ns += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          prep_end - prep_begin)
          .count());
      first = false;

      DLOG_F(2, "view prepared with {} draws", draw_count);

      // Mark view as ready for rendering
      // Note: ViewId not directly available from GetViews() range
      // Apps should use ViewContext metadata to identify views if needed

    } catch (const std::exception& ex) {
      LOG_F(WARNING, "-failed- : {}", ex.what());
    }
  }

  if (resolved_scene_view_count > 0) {
    ++sceneprep_profile_frames_;
    sceneprep_profile_total_ns_ += sceneprep_frame_ns;

    sceneprep_last_frame_ns_ = sceneprep_frame_ns;
    sceneprep_last_frame_view_count_
      = static_cast<std::uint32_t>(frame_view_count);
    sceneprep_last_frame_scene_view_count_
      = static_cast<std::uint32_t>(resolved_scene_view_count);
  } else {
    sceneprep_last_frame_ns_ = 0;
    sceneprep_last_frame_view_count_
      = static_cast<std::uint32_t>(frame_view_count);
    sceneprep_last_frame_scene_view_count_ = 0;
  }

  co_return;
}

auto Renderer::OnRender(observer_ptr<FrameContext> context) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  // Early exit if no render render context
  if (!render_context_) {
    DLOG_F(WARNING, "no render context available; skipping");
    co_return;
  }

  auto graphics_ptr = gfx_weak_.lock();
  if (!graphics_ptr) {
    LOG_F(WARNING, "Graphics expired; skipping");
    co_return;
  }
  auto& graphics = *graphics_ptr;

  // Iterate all views and execute their registered render graphs.
  // Take a snapshot of the registered factories under lock so
  // UnregisterViewRenderGraph() can safely mutate the underlying containers
  // without invalidating our iteration.
  std::vector<std::pair<ViewId, RenderGraphFactory>> graphs_snapshot;
  {
    std::shared_lock lock(view_registration_mutex_);
    graphs_snapshot.reserve(render_graphs_.size());
    for (const auto& kv : render_graphs_) {
      graphs_snapshot.emplace_back(kv.first, kv.second);
    }
  }

  std::unordered_set<ViewId> active_views;
  active_views.reserve(graphs_snapshot.size());
  std::uint64_t frame_view_render_ns = 0;
  std::uint64_t frame_render_graph_ns = 0;
  std::uint64_t frame_env_update_ns = 0;

  for (const auto& [view_id, factory] : graphs_snapshot) {
    const auto view_begin = std::chrono::steady_clock::now();
    std::uint64_t view_render_graph_ns = 0;
    std::uint64_t view_env_update_ns = 0;
    bool view_timing_recorded = false;
    auto record_view_timing = [&]() -> void {
      if (view_timing_recorded) {
        return;
      }
      view_timing_recorded = true;
      const auto view_end = std::chrono::steady_clock::now();
      frame_view_render_ns += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          view_end - view_begin)
          .count());
      frame_render_graph_ns += view_render_graph_ns;
      frame_env_update_ns += view_env_update_ns;
    };

    active_views.insert(view_id);
    last_seen_view_frame_seq_[view_id] = context->GetFrameSequenceNumber();
    DLOG_SCOPE_F(2, fmt::format("View {}", nostd::to_string(view_id)).c_str());

    // Mark view as not ready initially
    {
      std::unique_lock state_lock(view_state_mutex_);
      view_ready_states_[view_id] = false;
    }

    try {
      // Get the ViewContext for this view to access render target framebuffer
      const auto& view_ctx = context->GetViewContext(view_id);

      CHECK_NOTNULL_F(view_ctx.render_target.get(),
        "View {} ('{}'/{}) has no render_target framebuffer", view_id.get(),
        view_ctx.metadata.name, view_ctx.metadata.purpose);

      // Acquire command recorder for this view
      auto recorder_ptr = AcquireRecorderForView(view_id, graphics);
      if (!recorder_ptr) {
        LOG_F(ERROR, "Could not acquire recorder for view {}; skipping",
          view_id.get());
        record_view_timing();
        continue;
      }
      auto recorder = recorder_ptr.get();

      graphics::GpuEventScope view_scope(
        *recorder, fmt::format("View {}", view_id.get()));

      auto update_view_state = [&](ViewId view_id, bool success) -> void {
        std::unique_lock state_lock(view_state_mutex_);
        view_ready_states_[view_id] = success;
      };
      const bool allow_atmosphere = view_ctx.metadata.with_atmosphere;
      const bool is_scene_view = view_ctx.metadata.is_scene_view;

      // --- STEP 1: Wire all constants and context data ---
      // Scene views require prepared scene data and per-view ViewConstants.
      // Overlay views (e.g. ImGui) do not depend on ScenePrep output.
      if (is_scene_view) {
        // This MUST happen before any scene pass (SkyCapture, IBL, or Graph)
        // runs.
        if (!PrepareAndWireViewConstantsForView(
              view_id, *context, *render_context_)) {
          // Failure already logged inside helper; mark the view failed and
          // skip this view's render graph.
          update_view_state(view_id, false);
          record_view_timing();
          continue;
        }
      } else {
        // Keep per-view context explicit for overlays while bypassing
        // scene-dependent constants wiring.
        render_context_->current_view = {};
        render_context_->current_view.view_id = view_id;
      }

      namespace env = scene::environment;

      // --- STEP 2: Run environment update passes ---
      const auto env_update_begin = std::chrono::steady_clock::now();
      auto atmo_lut_manager = render_context_->current_view.atmo_lut_manager;
      if (!allow_atmosphere) {
        if (env_static_manager_) {
          env_static_manager_->EraseViewState(view_id);
        }
      } else if (sky_atmo_lut_compute_pass_ && atmo_lut_manager) {
        const auto swap_count_before = atmo_lut_manager->GetSwapCount();
        if (atmo_lut_manager->IsDirty()
          || !atmo_lut_manager->HasBeenGenerated()) {
          try {
            graphics::GpuEventScope lut_scope(
              *recorder, "Atmosphere LUT Compute");
            co_await sky_atmo_lut_compute_pass_->PrepareResources(
              *render_context_, *recorder);
            co_await sky_atmo_lut_compute_pass_->Execute(
              *render_context_, *recorder);
          } catch (const std::exception& ex) {
            LOG_F(ERROR, "SkyAtmosphereLutComputePass failed: {}", ex.what());
          }
        }
        if (env_static_manager_
          && atmo_lut_manager->GetSwapCount() != swap_count_before) {
          auto tag = oxygen::renderer::internal::RendererTagFactory::Get();
          env_static_manager_->UpdateIfNeeded(tag, *render_context_, view_id);
        }
      }

      if (allow_atmosphere && sky_capture_pass_) {
        const bool capture_requested
          = sky_capture_requested_ || !sky_capture_pass_->IsCaptured(view_id);
        bool needs_capture = capture_requested;
        const auto capture_gen_before
          = sky_capture_pass_->GetCaptureGeneration(view_id);
        bool atmo_gen_changed = false;
        bool atmo_stable_for_capture = true;
        if (atmo_lut_manager) {
          const auto current_atmo_gen = atmo_lut_manager->GetGeneration();
          atmo_stable_for_capture = atmo_lut_manager->HasBeenGenerated()
            && !atmo_lut_manager->IsDirty();
          if (atmo_stable_for_capture
            && current_atmo_gen != last_atmo_generation_[view_id]) {
            needs_capture = true;
            atmo_gen_changed = true;
          }
        }

        if (atmo_lut_manager && !atmo_stable_for_capture) {
          if (needs_capture) {
            DLOG_F(2,
              "SkyCapture deferred for view {}: atmosphere LUTs are not stable "
              "(generated={}, dirty={})",
              view_id.get(), atmo_lut_manager->HasBeenGenerated(),
              atmo_lut_manager->IsDirty());
          }
          needs_capture = false;
        }

        if (needs_capture) {
          if (atmo_gen_changed && sky_capture_pass_->IsCaptured(view_id)) {
            sky_capture_pass_->MarkDirty(view_id);
          }
          try {
            graphics::GpuEventScope capture_scope(*recorder, "Sky Capture");
            co_await sky_capture_pass_->PrepareResources(
              *render_context_, *recorder);
            co_await sky_capture_pass_->Execute(*render_context_, *recorder);
            const auto capture_gen_after
              = sky_capture_pass_->GetCaptureGeneration(view_id);
            if (env_static_manager_
              && capture_gen_after != capture_gen_before) {
              auto tag = oxygen::renderer::internal::RendererTagFactory::Get();
              env_static_manager_->UpdateIfNeeded(
                tag, *render_context_, view_id);
              env_static_manager_->RequestIblRegeneration(view_id);
            }
            if (atmo_lut_manager) {
              last_atmo_generation_[view_id]
                = atmo_lut_manager->GetGeneration();
            }
          } catch (const std::exception& ex) {
            LOG_F(ERROR, "SkyCapturePass failed: {}", ex.what());
          }
        }
      }

      if (allow_atmosphere && ibl_compute_pass_) {
        if (auto env_static = GetEnvironmentStaticDataManager();
          env_static && env_static->IsIblRegenerationRequested(view_id)) {
          ibl_compute_pass_->RequestRegenerationOnce();
          env_static->MarkIblRegenerationClean(view_id);
        }
        try {
          graphics::GpuEventScope ibl_scope(*recorder, "IBL Compute");
          co_await ibl_compute_pass_->PrepareResources(
            *render_context_, *recorder);
          co_await ibl_compute_pass_->Execute(*render_context_, *recorder);
        } catch (const std::exception& ex) {
          LOG_F(ERROR, "IblComputePass failed: {}", ex.what());
        }
      }
      const auto env_update_end = std::chrono::steady_clock::now();
      view_env_update_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          env_update_end - env_update_begin)
          .count());

      // --- STEP 3: Setup main scene framebuffer ---
      // This starts tracking the depth and color buffers for the actual view.
      if (!SetupFramebufferForView(
            *context, view_id, *recorder, *render_context_)) {
        LOG_F(ERROR, "Failed to setup framebuffer for view {}; skipping",
          view_id.get());
        record_view_timing();
        continue;
      }

      // --- STEP 4: Execute RenderGraph ---
      const auto render_graph_begin = std::chrono::steady_clock::now();
      graphics::GpuEventScope graph_scope(*recorder, "RenderGraph");
      const bool rv = co_await ExecuteRenderGraphForView(
        view_id, factory, *render_context_, *recorder);
      const auto render_graph_end = std::chrono::steady_clock::now();
      view_render_graph_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          render_graph_end - render_graph_begin)
          .count());

      // Finalize state and instrumentation
      update_view_state(view_id, rv);
      record_view_timing();
    } catch (const std::exception& ex) {
      LOG_F(ERROR, "Failed to render view {}: {}", view_id.get(), ex.what());
      std::unique_lock state_lock(view_state_mutex_);
      view_ready_states_[view_id] = false;
      record_view_timing();
    }
  }

  if (!graphs_snapshot.empty()) {
    ++render_profile_frames_;
    render_profile_view_render_total_ns_ += frame_view_render_ns;
    render_profile_render_graph_total_ns_ += frame_render_graph_ns;
    render_profile_env_update_total_ns_ += frame_env_update_ns;
    render_last_frame_view_render_ns_ = frame_view_render_ns;
    render_last_frame_render_graph_ns_ = frame_render_graph_ns;
    render_last_frame_env_update_ns_ = frame_env_update_ns;
  } else {
    render_last_frame_view_render_ns_ = 0;
    render_last_frame_render_graph_ns_ = 0;
    render_last_frame_env_update_ns_ = 0;
  }

  sky_capture_requested_ = false;
  EvictInactivePerViewState(context->GetFrameSequenceNumber(), active_views);

  // Return the pooled context for this slot to a clean state and clear the
  // debug in-use marker.
  render_context_pool_->Release(context->GetFrameSlot());
  render_context_.reset();

  co_return;
}

auto Renderer::OnCompositing(observer_ptr<FrameContext> context) -> co::Co<>
{
  const auto compositing_begin = std::chrono::steady_clock::now();
  std::optional<CompositionSubmission> submission;
  std::shared_ptr<graphics::Surface> target_surface;
  {
    std::lock_guard lock(composition_mutex_);
    if (!composition_submission_) {
      compositing_last_frame_ns_ = 0;
      co_return;
    }
    submission = std::move(composition_submission_);
    composition_submission_.reset();
    target_surface = std::move(composition_surface_);
    composition_surface_.reset();
  }

  CHECK_F(submission.has_value(), "Compositing submission required");
  auto& payload = *submission;
  if (payload.tasks.empty()) {
    compositing_last_frame_ns_ = 0;
    co_return;
  }

  CHECK_F(static_cast<bool>(payload.composite_target),
    "Compositing requires a target framebuffer");

  auto gfx = GetGraphics();
  CHECK_F(static_cast<bool>(gfx), "Graphics required for compositing");

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder_ptr
    = gfx->AcquireCommandRecorder(queue_key, "Renderer Compositing");
  CHECK_F(
    static_cast<bool>(recorder_ptr), "Compositing recorder acquisition failed");
  auto& recorder = *recorder_ptr;
  auto& target_fb = *payload.composite_target;
  TrackCompositionFramebuffer(recorder, target_fb);

  const auto& fb_desc = target_fb.GetDescriptor();
  CHECK_F(!fb_desc.color_attachments.empty(),
    "Compositing requires a color attachment");
  CHECK_F(static_cast<bool>(fb_desc.color_attachments[0].texture),
    "Compositing target missing color texture");
  auto& backbuffer = *fb_desc.color_attachments[0].texture;
  DLOG_F(1,
    "Log compositing target ptr={} size={}x{} fmt={} samples={} name={}",
    fmt::ptr(&backbuffer), backbuffer.GetDescriptor().width,
    backbuffer.GetDescriptor().height, backbuffer.GetDescriptor().format,
    backbuffer.GetDescriptor().sample_count,
    backbuffer.GetDescriptor().debug_name);

  RenderContext comp_context {};
  comp_context.SetRenderer(this, gfx.get());
  comp_context.pass_target = observer_ptr { payload.composite_target.get() };
  comp_context.frame_slot = frame_slot_;
  comp_context.frame_sequence = frame_seq_num;

  if (!compositing_pass_) {
    compositing_pass_config_ = std::make_shared<CompositingPassConfig>();
    compositing_pass_config_->debug_name = "CompositingPass";
    compositing_pass_
      = std::make_shared<CompositingPass>(compositing_pass_config_);
  }

  for (const auto& task : payload.tasks) {
    switch (task.type) {
    case CompositingTaskType::kCopy: {
      if (!IsViewReady(task.copy.source_view_id)) {
        DLOG_F(
          1, "Skip copy: view {} not ready", task.copy.source_view_id.get());
        continue;
      }
      auto source
        = ResolveViewOutputTexture(*context, task.copy.source_view_id);
      if (!source) {
        DLOG_F(1, "Skip copy: missing source texture for view {}",
          task.copy.source_view_id.get());
        continue;
      }
      DLOG_F(2, "Log copy: view={} ptr={} size={}x{} fmt={} samples={}",
        task.copy.source_view_id.get(), fmt::ptr(source.get()),
        source->GetDescriptor().width, source->GetDescriptor().height,
        source->GetDescriptor().format, source->GetDescriptor().sample_count);
      DLOG_F(2, "Log copy viewport: ({}, {}) {}x{}",
        task.copy.viewport.top_left_x, task.copy.viewport.top_left_y,
        task.copy.viewport.width, task.copy.viewport.height);
      if (source->GetDescriptor().format != backbuffer.GetDescriptor().format) {
        DLOG_F(1, "Fallback to blend: format mismatch for view {}",
          task.copy.source_view_id.get());
        CHECK_NOTNULL_F(
          compositing_pass_config_.get(), "CompositingPass config missing");
        compositing_pass_config_->source_texture = source;
        compositing_pass_config_->viewport = task.copy.viewport;
        compositing_pass_config_->alpha = 1.0F;

        co_await compositing_pass_->PrepareResources(comp_context, recorder);
        co_await compositing_pass_->Execute(comp_context, recorder);
        break;
      }
      CopyTextureToRegion(recorder, *source, backbuffer, task.copy.viewport);
      break;
    }
    case CompositingTaskType::kBlend: {
      if (!IsViewReady(task.blend.source_view_id)) {
        DLOG_F(
          1, "Skip blend: view {} not ready", task.blend.source_view_id.get());
        continue;
      }
      auto source
        = ResolveViewOutputTexture(*context, task.blend.source_view_id);
      if (!source) {
        DLOG_F(1, "Skip blend: missing source texture for view {}",
          task.blend.source_view_id.get());
        continue;
      }
      DLOG_F(2, "Blend view={} ptr={} size={}x{} fmt={} samples={}",
        task.blend.source_view_id.get(), fmt::ptr(source.get()),
        source->GetDescriptor().width, source->GetDescriptor().height,
        source->GetDescriptor().format, source->GetDescriptor().sample_count);
      DLOG_F(2, "Blend viewport=({}, {}) {}x{} alpha={}",
        task.blend.viewport.top_left_x, task.blend.viewport.top_left_y,
        task.blend.viewport.width, task.blend.viewport.height,
        task.blend.alpha);

      CHECK_NOTNULL_F(
        compositing_pass_config_.get(), "CompositingPass config missing");
      compositing_pass_config_->source_texture = source;
      compositing_pass_config_->viewport = task.blend.viewport;
      compositing_pass_config_->alpha = task.blend.alpha;

      co_await compositing_pass_->PrepareResources(comp_context, recorder);
      co_await compositing_pass_->Execute(comp_context, recorder);
      break;
    }
    case CompositingTaskType::kBlendTexture: {
      if (!task.texture_blend.source_texture) {
        DLOG_F(1, "Skip blend texture: missing source texture");
        continue;
      }
      DLOG_F(2, "Blend texture ptr={} size={}x{} fmt={} samples={} name={}",
        fmt::ptr(task.texture_blend.source_texture.get()),
        task.texture_blend.source_texture->GetDescriptor().width,
        task.texture_blend.source_texture->GetDescriptor().height,
        task.texture_blend.source_texture->GetDescriptor().format,
        task.texture_blend.source_texture->GetDescriptor().sample_count,
        task.texture_blend.source_texture->GetDescriptor().debug_name);
      DLOG_F(2, "Blend texture viewport=({}, {}) {}x{} alpha={}",
        task.texture_blend.viewport.top_left_x,
        task.texture_blend.viewport.top_left_y,
        task.texture_blend.viewport.width, task.texture_blend.viewport.height,
        task.texture_blend.alpha);

      CHECK_NOTNULL_F(
        compositing_pass_config_.get(), "CompositingPass config missing");
      compositing_pass_config_->source_texture
        = task.texture_blend.source_texture;
      compositing_pass_config_->viewport = task.texture_blend.viewport;
      compositing_pass_config_->alpha = task.texture_blend.alpha;

      co_await compositing_pass_->PrepareResources(comp_context, recorder);
      co_await compositing_pass_->Execute(comp_context, recorder);
      break;
    }
    case CompositingTaskType::kTaa:
    default:
      DLOG_F(1, "Skip compositing: task type not implemented");
      break;
    }
  }

  recorder.RequireResourceStateFinal(
    backbuffer, graphics::ResourceStates::kPresent);
  recorder.FlushBarriers();

  if (target_surface) {
    const auto surfaces = context->GetSurfaces();
    for (size_t i = 0; i < surfaces.size(); ++i) {
      if (surfaces[i].get() == target_surface.get()) {
        context->SetSurfacePresentable(i, true);
        break;
      }
    }
  }

  const auto compositing_end = std::chrono::steady_clock::now();
  const auto compositing_ns = static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      compositing_end - compositing_begin)
      .count());
  ++compositing_profile_frames_;
  compositing_profile_total_ns_ += compositing_ns;
  compositing_last_frame_ns_ = compositing_ns;

  co_return;
}

auto Renderer::OnFrameEnd(observer_ptr<FrameContext> /*context*/) -> void
{
  LOG_SCOPE_FUNCTION(2);

  texture_binder_->OnFrameEnd();
  if (console_ != nullptr) {
    const auto last_frame_text
      = FormatRendererLastFrameStats(GetStats().last_frame);
    (void)console_->SetCVarFromText(
      { .name = std::string(kCVarRendererLastFrameStats),
        .text = last_frame_text },
      { .source = console::CommandSource::kAutomation,
        .shipping_build = false,
        .record_history = false });
  }
  DrainPendingViewCleanup("OnFrameEnd");
}

auto Renderer::DrainPendingViewCleanup(std::string_view reason) -> void
{
  CHECK_F(
    !reason.empty(), "DrainPendingViewCleanup requires a non-empty reason");

  std::unordered_set<ViewId> pending;
  {
    std::lock_guard lock(pending_cleanup_mutex_);
    if (pending_cleanup_.empty()) {
      return;
    }
    pending.swap(pending_cleanup_);
  }

  DLOG_F(1, "Process pending cleanup: {} views ({})", pending.size(), reason);

  for (const auto& id : pending) {
    resolved_views_.erase(id);
    prepared_frames_.erase(id);
    per_view_storage_.erase(id);
  }

  {
    std::unique_lock state_lock(view_state_mutex_);
    for (const auto& id : pending) {
      view_ready_states_.erase(id);
    }
  }
}

//===----------------------------------------------------------------------===//
// PreExecute helper implementations
//===----------------------------------------------------------------------===//

// Removed legacy draw-metadata helpers; lifecycle now handled by
// DrawMetadataEmitter via ScenePrepState

auto Renderer::WireContext(RenderContext& render_context,
  const std::shared_ptr<graphics::Buffer>& view_constants) -> void
{
  DLOG_SCOPE_FUNCTION(3);

  render_context.view_constants = view_constants;
  render_context.frame_slot = frame_slot_;
  render_context.frame_sequence = frame_seq_num;
  render_context.delta_time = last_frame_dt_seconds_;
  render_context.gpu_debug_manager.reset(gpu_debug_manager_.get());

  const auto graphics_ptr = gfx_weak_.lock();
  if (!graphics_ptr) {
    throw std::runtime_error("Graphics expired in Renderer::WireContext");
  }
  render_context.SetRenderer(this, graphics_ptr.get());
}

auto Renderer::AcquireRecorderForView(ViewId view_id, Graphics& gfx)
  -> std::shared_ptr<oxygen::graphics::CommandRecorder>
{
  DLOG_SCOPE_FUNCTION(3);

  const auto queue_key
    = gfx.QueueKeyFor(oxygen::graphics::QueueRole::kGraphics);
  return gfx.AcquireCommandRecorder(
    queue_key, std::string("View_") + std::to_string(view_id.get()));
}

auto Renderer::SetupFramebufferForView(const FrameContext& frame_context,
  ViewId view_id, graphics::CommandRecorder& recorder,
  RenderContext& render_context) -> bool
{
  DLOG_SCOPE_FUNCTION(3);

  const auto& view_ctx = frame_context.GetViewContext(view_id);

  if (!view_ctx.render_target) {
    LOG_F(WARNING, "View {} has no render target", view_id.get());
    return false;
  }

  const auto& fb_desc = view_ctx.render_target->GetDescriptor();
  for (const auto& attachment : fb_desc.color_attachments) {
    if (attachment.texture) {
      // Use the texture's own descriptor initial_state when available.
      // Previously we assumed swapchain backbuffers for all color
      // attachments and used kPresent which breaks for render-to-texture
      // targets (e.g. EditorView). Honoring the texture descriptor avoids
      // conflicting initial states being tracked and prevents invalid
      // barrier sequences.
      auto initial = attachment.texture->GetDescriptor().initial_state;
      if (initial == graphics::ResourceStates::kUnknown
        || initial == graphics::ResourceStates::kUndefined) {
        initial = graphics::ResourceStates::kPresent;
      }
      recorder.BeginTrackingResourceState(*attachment.texture, initial, true);
      recorder.RequireResourceState(
        *attachment.texture, graphics::ResourceStates::kRenderTarget);
    }
  }

  if (fb_desc.depth_attachment.texture) {
    auto initial
      = fb_desc.depth_attachment.texture->GetDescriptor().initial_state;
    if (initial == graphics::ResourceStates::kUnknown
      || initial == graphics::ResourceStates::kUndefined) {
      initial = graphics::ResourceStates::kDepthWrite;
    }
    recorder.BeginTrackingResourceState(
      *fb_desc.depth_attachment.texture, initial, true);
    recorder.RequireResourceState(
      *fb_desc.depth_attachment.texture, graphics::ResourceStates::kDepthWrite);
    recorder.FlushBarriers();
  }

  recorder.BindFrameBuffer(*view_ctx.render_target);
  render_context.pass_target = view_ctx.render_target;
  return true;
}

auto Renderer::PrepareAndWireViewConstantsForView(ViewId view_id,
  const FrameContext& frame_context, RenderContext& render_context) -> bool
{
  DLOG_SCOPE_FUNCTION(3);

  auto resolved_it = resolved_views_.find(view_id);
  auto prepared_it = prepared_frames_.find(view_id);

  if (resolved_it == resolved_views_.end()
    || prepared_it == prepared_frames_.end()) {
    LOG_F(2, "No cached data for view {} (resolved={}, prepared={})",
      view_id.get(), resolved_it != resolved_views_.end(),
      prepared_it != prepared_frames_.end());
    return false;
  }

  // Populate render_context.current_view before EnvStatic update.
  render_context.current_view.view_id = view_id;
  render_context.current_view.resolved_view.reset(&resolved_it->second);
  render_context.current_view.prepared_frame.reset(&prepared_it->second);
  const auto& view_ctx = frame_context.GetViewContext(view_id);
  const bool allow_atmosphere = view_ctx.metadata.with_atmosphere;
  bool atmo_enabled = false;
  if (const auto scene = render_context.scene; allow_atmosphere && scene) {
    if (const auto scene_env = scene->GetEnvironment()) {
      if (const auto atmo
        = scene_env->TryGetSystem<scene::environment::SkyAtmosphere>();
        atmo && atmo->IsEnabled()) {
        atmo_enabled = true;
      }
    }
  }
  render_context.current_view.atmo_lut_manager = atmo_enabled
    ? GetOrCreateSkyAtmosphereLutManagerForView(view_id)
    : nullptr;
  // Preserve per-view atmosphere resources across active scene transitions.
  // RenderScene stages fallback/no-atmosphere scenes while switching, and
  // tearing down live LUT resources here can invalidate in-flight GPU work.

  if (env_static_manager_) {
    if (allow_atmosphere) {
      auto tag = oxygen::renderer::internal::RendererTagFactory::Get();
      env_static_manager_->UpdateIfNeeded(tag, render_context, view_id);
    } else {
      env_static_manager_->EraseViewState(view_id);
    }
  }
  return RepublishCurrentViewBindings(
    render_context, ViewBindingRepublishMode::kFull);
}

auto Renderer::RepublishCurrentViewBindings(
  const RenderContext& render_context, const ViewBindingRepublishMode mode)
  -> bool
{
  const auto view_id = render_context.current_view.view_id;
  if (view_id == ViewId {}) {
    LOG_F(ERROR, "Renderer: cannot republish bindings for invalid view id");
    return false;
  }
  if (!render_context.current_view.resolved_view
    || !render_context.current_view.prepared_frame) {
    LOG_F(ERROR,
      "Renderer: cannot republish bindings for view {} without current view "
      "state",
      view_id.get());
    return false;
  }

  const auto& resolved = *render_context.current_view.resolved_view;
  const auto& prepared = *render_context.current_view.prepared_frame;
  auto& runtime_state = per_view_runtime_state_[view_id];
  const bool lighting_only = mode == ViewBindingRepublishMode::kLightingOnly;
  const bool can_reuse_cached_view_bindings
    = lighting_only && runtime_state.has_published_view_bindings;

  ViewConstants view_constants = view_const_cpu_;

  if (gpu_debug_manager_) {
    static bool logged_gpu_debug_slots = false;
    if (!logged_gpu_debug_slots) {
      DLOG_F(1,
        "Renderer: bindless GPU debug slots set (line_srv={}, counter_uav={})",
        gpu_debug_manager_->GetLineBufferSrvIndex(),
        gpu_debug_manager_->GetCounterBufferUavIndex());
      logged_gpu_debug_slots = true;
    }
  }

  view_constants.SetViewMatrix(resolved.ViewMatrix())
    .SetProjectionMatrix(resolved.ProjectionMatrix())
    .SetStableProjectionMatrix(resolved.StableProjectionMatrix())
    .SetCameraPosition(resolved.CameraPosition())
    .SetFrameSlot(render_context.frame_slot, ViewConstants::kRenderer)
    .SetFrameSequenceNumber(
      render_context.frame_sequence, ViewConstants::kRenderer);

  const auto environment_static_slot = env_static_manager_
    ? env_static_manager_->GetSrvIndex(view_id)
    : kInvalidShaderVisibleIndex;

  if (view_frame_bindings_publisher_) {
    ViewFrameBindings view_bindings = can_reuse_cached_view_bindings
      ? runtime_state.published_view_bindings
      : ViewFrameBindings {};

    if (!can_reuse_cached_view_bindings && draw_frame_bindings_publisher_) {
      DLOG_F(3, "   worlds: {}", prepared.bindless_worlds_slot);
      DLOG_F(3, "  normals: {}", prepared.bindless_normals_slot);
      DLOG_F(
        3, "material shading: {}", prepared.bindless_material_shading_slot);
      DLOG_F(3, " metadata: {}", prepared.bindless_draw_metadata_slot);
      DLOG_F(3, " instance: {}", prepared.bindless_instance_data_slot);

      const DrawFrameBindings draw_bindings {
        .draw_metadata_slot
        = BindlessDrawMetadataSlot(prepared.bindless_draw_metadata_slot),
        .transforms_slot = BindlessWorldsSlot(prepared.bindless_worlds_slot),
        .normal_matrices_slot
        = BindlessNormalsSlot(prepared.bindless_normals_slot),
        .material_shading_constants_slot = BindlessMaterialShadingConstantsSlot(
          prepared.bindless_material_shading_slot),
        .procedural_grid_material_constants_slot
        = BindlessProceduralGridMaterialConstantsSlot(
          scene_prep_state_->GetMaterialBinder()
            ? scene_prep_state_->GetMaterialBinder()
                ->GetProceduralGridMaterialsSrvIndex()
            : kInvalidShaderVisibleIndex),
        .instance_data_slot
        = BindlessInstanceDataSlot(prepared.bindless_instance_data_slot),
      };
      view_bindings.draw_frame_slot
        = draw_frame_bindings_publisher_->Publish(view_id, draw_bindings);
    }

    if (!can_reuse_cached_view_bindings && view_color_data_publisher_) {
      const ViewColorData view_color_data {
        .exposure = prepared.exposure,
      };
      view_bindings.view_color_frame_slot
        = view_color_data_publisher_->Publish(view_id, view_color_data);
    }

    if (!can_reuse_cached_view_bindings && gpu_debug_manager_
      && debug_frame_bindings_publisher_) {
      const DebugFrameBindings debug_bindings {
        .line_buffer_srv_slot
        = ShaderVisibleIndex(gpu_debug_manager_->GetLineBufferSrvIndex()),
        .line_buffer_uav_slot
        = ShaderVisibleIndex(gpu_debug_manager_->GetLineBufferUavIndex()),
        .counter_buffer_uav_slot
        = ShaderVisibleIndex(gpu_debug_manager_->GetCounterBufferUavIndex()),
      };
      view_bindings.debug_frame_slot
        = debug_frame_bindings_publisher_->Publish(view_id, debug_bindings);
    }

    if (lighting_frame_bindings_publisher_) {
      auto directional_lights_slot = kInvalidShaderVisibleIndex;
      auto positional_lights_slot = kInvalidShaderVisibleIndex;
      if (const auto light_manager = scene_prep_state_->GetLightManager()) {
        directional_lights_slot = light_manager->GetDirectionalLightsSrvIndex();
        positional_lights_slot = light_manager->GetPositionalLightsSrvIndex();
      }

      const LightingFrameBindings lighting_bindings {
        .directional_lights_slot = directional_lights_slot,
        .positional_lights_slot = positional_lights_slot,
        .light_culling = runtime_state.light_culling,
        .sun = runtime_state.sun,
      };
      view_bindings.lighting_frame_slot
        = lighting_frame_bindings_publisher_->Publish(
          view_id, lighting_bindings);
    }

    if (!can_reuse_cached_view_bindings && shadow_frame_bindings_publisher_
      && shadow_manager_) {
      auto shadow_instance_metadata_slot = kInvalidShaderVisibleIndex;
      auto directional_shadow_metadata_slot = kInvalidShaderVisibleIndex;
      auto virtual_shadow_page_table_slot = kInvalidShaderVisibleIndex;
      auto virtual_shadow_page_flags_slot = kInvalidShaderVisibleIndex;
      auto virtual_shadow_physical_pool_slot = kInvalidShaderVisibleIndex;
      auto virtual_directional_shadow_metadata_slot
        = kInvalidShaderVisibleIndex;
      auto virtual_shadow_physical_page_metadata_slot
        = kInvalidShaderVisibleIndex;
      auto virtual_shadow_physical_page_lists_slot
        = kInvalidShaderVisibleIndex;
      auto sun_shadow_index = 0xFFFFFFFFU;
      if (const auto light_manager = scene_prep_state_->GetLightManager()) {
        const auto prepared_it = prepared_frames_.find(view_id);
        const auto shadow_caster_bounds = prepared_it != prepared_frames_.end()
          ? prepared_it->second.shadow_caster_bounding_spheres
          : std::span<const glm::vec4> {};
        const auto visible_receiver_bounds
          = prepared_it != prepared_frames_.end()
          ? prepared_it->second.visible_receiver_bounding_spheres
          : std::span<const glm::vec4> {};
        const auto synthetic_sun_shadow = BuildSyntheticSunShadowInput(
          render_context, runtime_state.sun,
          directional_shadow_bias_state_.synthetic_constant_bias,
          directional_shadow_bias_state_.synthetic_normal_bias);
        const auto shadow_caster_content_hash
          = HashPreparedShadowCasterContent(prepared);
        const auto shadow_view
          = shadow_manager_->PublishForView(view_id, view_constants,
            *light_manager, shadow_caster_bounds, visible_receiver_bounds,
            synthetic_sun_shadow ? &*synthetic_sun_shadow : nullptr,
            frame_budget_stats_.gpu_budget, shadow_caster_content_hash);
        shadow_instance_metadata_slot
          = shadow_view.shadow_instance_metadata_srv;
        directional_shadow_metadata_slot
          = shadow_view.directional_shadow_metadata_srv;
        const auto directional_shadow_texture_slot
          = shadow_view.directional_shadow_texture_srv;
        virtual_shadow_page_table_slot
          = shadow_view.virtual_shadow_page_table_srv;
        virtual_shadow_page_flags_slot
          = shadow_view.virtual_shadow_page_flags_srv;
        virtual_shadow_physical_pool_slot
          = shadow_view.virtual_shadow_physical_pool_srv;
        virtual_directional_shadow_metadata_slot
          = shadow_view.virtual_directional_shadow_metadata_srv;
        virtual_shadow_physical_page_metadata_slot
          = shadow_view.virtual_shadow_physical_page_metadata_srv;
        virtual_shadow_physical_page_lists_slot
          = shadow_view.virtual_shadow_physical_page_lists_srv;
        sun_shadow_index = shadow_view.sun_shadow_index;

        const ShadowFrameBindings shadow_bindings {
          .shadow_instance_metadata_slot = shadow_instance_metadata_slot,
          .directional_shadow_metadata_slot = directional_shadow_metadata_slot,
          .directional_shadow_texture_slot = directional_shadow_texture_slot,
          .virtual_shadow_page_table_slot = virtual_shadow_page_table_slot,
          .virtual_shadow_page_flags_slot = virtual_shadow_page_flags_slot,
          .virtual_shadow_physical_pool_slot
          = virtual_shadow_physical_pool_slot,
          .virtual_directional_shadow_metadata_slot
          = virtual_directional_shadow_metadata_slot,
          .virtual_shadow_physical_page_metadata_slot
          = virtual_shadow_physical_page_metadata_slot,
          .virtual_shadow_physical_page_lists_slot
          = virtual_shadow_physical_page_lists_slot,
          .sun_shadow_index = sun_shadow_index,
        };
        LOG_F(INFO,
          "Renderer: frame={} view={} shadow publication shadow_meta={} "
          "dir_meta={} dir_tex={} vsm_meta={} vsm_table={} vsm_flags={} "
          "vsm_pool={} vsm_phys_meta={} vsm_phys_lists={} sun_shadow_index={}",
          render_context.frame_sequence.get(), view_id.get(),
          shadow_instance_metadata_slot.get(),
          directional_shadow_metadata_slot.get(),
          directional_shadow_texture_slot.get(),
          virtual_directional_shadow_metadata_slot.get(),
          virtual_shadow_page_table_slot.get(),
          virtual_shadow_page_flags_slot.get(),
          virtual_shadow_physical_pool_slot.get(),
          virtual_shadow_physical_page_metadata_slot.get(),
          virtual_shadow_physical_page_lists_slot.get(), sun_shadow_index);
        view_bindings.shadow_frame_slot
          = shadow_frame_bindings_publisher_->Publish(view_id, shadow_bindings);
      } else {
        const ShadowFrameBindings shadow_bindings {};
        view_bindings.shadow_frame_slot
          = shadow_frame_bindings_publisher_->Publish(view_id, shadow_bindings);
        LOG_F(WARNING,
          "Renderer: view {} published empty shadow bindings because "
          "LightManager is unavailable",
          view_id.get());
      }
    }

    if (!can_reuse_cached_view_bindings && environment_frame_bindings_publisher_) {
      auto environment_view_slot = kInvalidShaderVisibleIndex;
      if (environment_view_data_publisher_) {
        environment_view_slot = environment_view_data_publisher_->Publish(
          view_id, runtime_state.environment_view);
      }

      const EnvironmentFrameBindings environment_bindings {
        .environment_static_slot = environment_static_slot,
        .environment_view_slot = environment_view_slot,
      };
      view_bindings.environment_frame_slot
        = environment_frame_bindings_publisher_->Publish(
          view_id, environment_bindings);
    }

    const auto view_bindings_slot
      = view_frame_bindings_publisher_->Publish(view_id, view_bindings);
    if (shadow_manager_) {
      shadow_manager_->SetPublishedViewFrameBindingsSlot(
        view_id, BindlessViewFrameBindingsSlot(view_bindings_slot));
    }
    runtime_state.published_view_bindings = view_bindings;
    runtime_state.has_published_view_bindings = true;
    LOG_F(INFO,
      "Renderer: frame={} view={} republish mode={} cached_view_bindings={} "
      "view_frame_slot={} shadow_frame_slot={} lighting_frame_slot={}",
      render_context.frame_sequence.get(), view_id.get(),
      lighting_only ? "lighting-only" : "full",
      can_reuse_cached_view_bindings, view_bindings_slot,
      view_bindings.shadow_frame_slot.get(),
      view_bindings.lighting_frame_slot.get());
    view_constants.SetBindlessViewFrameBindingsSlot(
      BindlessViewFrameBindingsSlot(view_bindings_slot),
      ViewConstants::kRenderer);
  }

  const auto& snapshot = view_constants.GetSnapshot();
  if (snapshot.frame_slot != render_context.frame_slot.get()) {
    LOG_F(ERROR,
      "Renderer: ViewConstants frame_slot mismatch (view={} snapshot={} "
      "expected={})",
      view_id.get(), snapshot.frame_slot, render_context.frame_slot.get());
  }

  auto buffer_info = view_const_manager_->WriteViewConstants(
    view_id, &snapshot, sizeof(ViewConstants::GpuData));
  if (!buffer_info.buffer) {
    LOG_F(ERROR, "Failed to write ViewConstants for view {}", view_id);
    return false;
  }

  // Render passes only see a const RenderContext; rewiring the authoritative
  // scene-constants buffer remains a renderer-owned operation.
  WireContext(const_cast<RenderContext&>(render_context), buffer_info.buffer);
  return true;
}

auto Renderer::UpdateCurrentViewLightCullingConfig(
  const RenderContext& render_context, const LightCullingConfig& config) -> void
{
  const auto view_id = render_context.current_view.view_id;
  if (view_id == ViewId {}) {
    LOG_F(ERROR,
      "Renderer: cannot update clustered-lighting state for invalid view id");
    return;
  }

  per_view_runtime_state_[view_id].light_culling = config;
  if (!RepublishCurrentViewBindings(
        render_context, ViewBindingRepublishMode::kLightingOnly)) {
    LOG_F(ERROR,
      "Renderer: failed to republish current-view bindings after light "
      "culling update for view {}",
      view_id.get());
  }
}

auto Renderer::UpdateViewExposure(ViewId view_id, const scene::Scene& scene,
  const SyntheticSunData& sun_state) -> float
{
  CHECK_F(sun_state.enabled == 0U || std::isfinite(sun_state.cos_zenith),
    "Renderer::UpdateViewExposure invalid sun_state.cos_zenith for view {}",
    view_id.get());

  namespace env = scene::environment;

  float exposure = 1.0F;
  float exposure_key = 1.0F;
  float raw_exposure_key = 1.0F;
  float compensation_ev = 0.0F;
  std::optional<float> used_ev {};
  engine::ExposureMode exposure_mode = engine::ExposureMode::kManual;
  std::optional<float> camera_ev {};
  bool exposure_enabled = true;
  std::optional<float> manual_ev_read {};

  if (const auto resolved_it = resolved_views_.find(view_id);
    resolved_it != resolved_views_.end()) {
    camera_ev = resolved_it->second.CameraEv();
  }

  // Manual and auto exposure use the post-process volume.
  if (const auto env = scene.GetEnvironment()) {
    if (const auto pp = env->TryGetSystem<env::PostProcessVolume>();
      pp && pp->IsEnabled()) {
      exposure_enabled = pp->GetExposureEnabled();
      if (!exposure_enabled) {
        LOG_F(WARNING,
          "Exposure not enabled for view {}; using default exposure={}",
          view_id.get(), exposure);
        return exposure;
      }
      raw_exposure_key = pp->GetExposureKey();
      exposure_key = std::max(1e-4F, raw_exposure_key);
      compensation_ev = pp->GetExposureCompensationEv();
      const auto mode = pp->GetExposureMode();
      exposure_mode = mode;
      manual_ev_read = pp->GetManualExposureEv();

      if (mode == engine::ExposureMode::kManual
        || mode == engine::ExposureMode::kManualCamera
        || mode == engine::ExposureMode::kAuto) {
        // Auto mode must not derive baseline EV from camera/sun model every
        // frame. That path can pin twilight to daylight-like EV values and
        // fight histogram adaptation. Use the authored manual EV as seed;
        // AutoExposurePass performs runtime adaptation.
        const float ev = (mode == engine::ExposureMode::kManualCamera)
          ? camera_ev.value_or(*manual_ev_read)
          : *manual_ev_read;
        used_ev = ev;

        // Physically calibrated manual exposure (ISO 2720 reflected-light
        // calibration constant K = 12.5).
        // For ExposureMode::kAuto, this value serves as a physically-aligned
        // baseline/seed before the histogram-based adaptation pass takes over.
        exposure = (1.0F / 12.5F) * std::exp2(compensation_ev - ev);

        if (mode == engine::ExposureMode::kAuto) {
          DLOG_F(3,
            "View {} in auto exposure mode, will use baseline exposure={:.4f}",
            view_id, exposure);
        }
      }
    }
  }

  exposure *= exposure_key;
  return exposure;
}

auto Renderer::ExecuteRenderGraphForView(ViewId view_id,
  const RenderGraphFactory& factory, RenderContext& render_context,
  graphics::CommandRecorder& recorder) -> co::Co<bool>
{
  DLOG_SCOPE_FUNCTION(3);

  try {
    co_await factory(view_id, render_context, recorder);
    co_return true;
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "RenderGraph execution for view {} failed: {}", view_id,
      ex.what());
    co_return false;
  } catch (...) {
    LOG_F(ERROR, "RenderGraph execution for view {} failed: unknown error",
      view_id.get());
    co_return false;
  }
}

auto Renderer::GetOrCreateSkyAtmosphereLutManagerForView(const ViewId view_id)
  -> observer_ptr<internal::SkyAtmosphereLutManager>
{
  auto it = per_view_atmo_luts_.find(view_id);
  if (it != per_view_atmo_luts_.end()) {
    return observer_ptr { it->second.get() };
  }

  auto graphics_ptr = gfx_weak_.lock();
  if (!graphics_ptr) {
    return nullptr;
  }

  auto lut = std::make_unique<internal::SkyAtmosphereLutManager>(
    observer_ptr { graphics_ptr.get() }, observer_ptr { uploader_.get() },
    observer_ptr { upload_staging_provider_.get() });
  auto* lut_ptr = lut.get();
  per_view_atmo_luts_.insert_or_assign(view_id, std::move(lut));
  return observer_ptr { lut_ptr };
}

auto Renderer::EvictInactivePerViewState(
  const frame::SequenceNumber current_seq,
  const std::unordered_set<ViewId>& active_views) -> void
{
  constexpr std::uint64_t kEvictionWindowFrames = 120ULL;
  std::vector<ViewId> to_evict;
  for (const auto& [view_id, last_seen] : last_seen_view_frame_seq_) {
    if (active_views.contains(view_id)) {
      continue;
    }
    const auto age = current_seq.get() - last_seen.get();
    if (age > kEvictionWindowFrames) {
      to_evict.push_back(view_id);
    }
  }

  for (const auto view_id : to_evict) {
    per_view_atmo_luts_.erase(view_id);
    last_atmo_generation_.erase(view_id);
    last_seen_view_frame_seq_.erase(view_id);
    if (env_static_manager_) {
      env_static_manager_->EraseViewState(view_id);
    }
    if (sky_capture_pass_) {
      sky_capture_pass_->EraseViewState(view_id);
    }
    if (ibl_manager_) {
      ibl_manager_->EraseViewState(view_id);
    }
  }
}

auto Renderer::RunScenePrep(ViewId view_id, const ResolvedView& view,
  const FrameContext& frame_context, bool run_frame_phase,
  bool single_view_mode) -> std::size_t
{
  DLOG_SCOPE_FUNCTION(3);

  auto scene_ptr = frame_context.GetScene();
  CHECK_NOTNULL_F(scene_ptr, "FrameContext.scene is null in RunScenePrep");
  auto& scene = *scene_ptr;

  // Get or create the prepared frame for this specific view
  auto& prepared_frame = prepared_frames_[view_id];

  auto frame_seq = frame_context.GetFrameSequenceNumber();
  ::oxygen::observer_ptr<const ::oxygen::ResolvedView> view_ptr(&view);

  enum class PrepStep {
    kFrameCollect,
    kFrameFinalize,
    kViewCollectCached,
    kViewCollectSingle,
    kViewFinalizeAndCapture,
  };

  std::array<PrepStep, 5> plan {};
  std::size_t plan_size = 0;

  if (single_view_mode) {
    plan[0] = PrepStep::kViewCollectSingle;
    plan[1] = PrepStep::kViewFinalizeAndCapture;
    plan_size = 2;
  } else if (run_frame_phase) {
    plan[0] = PrepStep::kFrameCollect;
    plan[1] = PrepStep::kFrameFinalize;
    plan[2] = PrepStep::kViewCollectCached;
    plan[3] = PrepStep::kViewFinalizeAndCapture;
    plan_size = 4;
  } else {
    plan[0] = PrepStep::kViewCollectCached;
    plan[1] = PrepStep::kViewFinalizeAndCapture;
    plan_size = 2;
  }

  for (std::size_t i = 0; i < plan_size; ++i) {
    switch (plan[i]) {
    case PrepStep::kFrameCollect: {
      DLOG_SCOPE_F(3,
        fmt::format("frame-phase for frame seq {}", nostd::to_string(frame_seq))
          .c_str());
      scene_prep_->Collect(
        scene, std::nullopt, frame_seq, *scene_prep_state_, true);
      break;
    }
    case PrepStep::kFrameFinalize:
      scene_prep_->Finalize();
      break;
    case PrepStep::kViewCollectCached: {
      DLOG_SCOPE_F(3,
        fmt::format("view-phase for view {}", nostd::to_string(view_id))
          .c_str());
      scene_prep_->Collect(scene,
        std::optional<::oxygen::observer_ptr<const ::oxygen::ResolvedView>>(
          view_ptr),
        frame_seq, *scene_prep_state_,
        run_frame_phase); // Only reset on first view
      break;
    }
    case PrepStep::kViewCollectSingle: {
      DLOG_SCOPE_F(3,
        fmt::format("single-view phase for view {}", nostd::to_string(view_id))
          .c_str());
      scene_prep_->CollectSingleView(
        scene, view_ptr, frame_seq, *scene_prep_state_, run_frame_phase);
      break;
    }
    case PrepStep::kViewFinalizeAndCapture: {
      scene_prep_->Finalize();

      // CRITICAL: Capture bindless SRV indices IMMEDIATELY after Finalize
      // These indices are valid only for THIS view's finalization and will be
      // overwritten when the next view calls Finalize. Store them in THIS
      // view's prepared_frame so OnRender can use the correct indices.
      if (const auto transforms = scene_prep_state_->GetTransformUploader()) {
        prepared_frame.bindless_worlds_slot = transforms->GetWorldsSrvIndex();
        DLOG_F(3, " captured worlds: {}", prepared_frame.bindless_worlds_slot);
        prepared_frame.bindless_normals_slot = transforms->GetNormalsSrvIndex();
        DLOG_F(3, "captured normals: {}", prepared_frame.bindless_normals_slot);
      }
      if (const auto materials = scene_prep_state_->GetMaterialBinder()) {
        prepared_frame.bindless_material_shading_slot
          = materials->GetMaterialShadingSrvIndex();
      }
      if (auto emitter = scene_prep_state_->GetDrawMetadataEmitter()) {
        prepared_frame.bindless_draw_metadata_slot
          = emitter->GetDrawMetadataSrvIndex();
        prepared_frame.bindless_draw_bounds_slot
          = emitter->GetDrawBoundingSpheresSrvIndex();
        prepared_frame.bindless_instance_data_slot
          = emitter->GetInstanceDataSrvIndex();
      }

      auto& runtime_state = per_view_runtime_state_[view_id];
      runtime_state = PerViewRuntimeState {};

      const oxygen::observer_ptr<renderer::LightManager> light_mgr
        = scene_prep_state_->GetLightManager();
      if (light_mgr) {
        const auto dir_lights = light_mgr->GetDirectionalLights();
        runtime_state.sun = internal::ResolveSunForView(scene, dir_lights);

        std::size_t sun_tagged_count = 0;
        std::size_t env_contrib_count = 0;
        for (const auto& dl : dir_lights) {
          const auto flags = static_cast<DirectionalLightFlags>(dl.flags);
          if ((flags & DirectionalLightFlags::kSunLight)
            != DirectionalLightFlags::kNone) {
            ++sun_tagged_count;
          }
          if ((flags & DirectionalLightFlags::kEnvironmentContribution)
            != DirectionalLightFlags::kNone) {
            ++env_contrib_count;
          }
        }

        if (runtime_state.sun.enabled == 0U
          && (sun_tagged_count > 0 || env_contrib_count > 0)) {
          LOG_F(WARNING,
            "Renderer: resolved sun is disabled but directional light set "
            "contains sun/environment contributors "
            "(view={} total={} sun_tagged={} env_contrib={})",
            nostd::to_string(view_id), dir_lights.size(), sun_tagged_count,
            env_contrib_count);
        }
      }

      prepared_frame.exposure
        = UpdateViewExposure(view_id, scene, runtime_state.sun);

      // Populate SkyAtmosphere per-view context. Defaults stay conservative
      // until LUT precompute is wired; analytic fallback stays enabled.
      float aerial_distance_scale = 1.0F;
      float aerial_scattering_strength = 1.0F;
      // Planet center positioned below Z=0 ground plane so camera at Z>=0
      // is on/above surface. Default radius places center at Z=-6360km.
      float planet_radius_m = 6'360'000.0F;
      glm::vec3 planet_center_ws { 0.0F, 0.0F, -planet_radius_m };
      glm::vec3 planet_up_ws { 0.0F, 0.0F, 1.0F };
      float camera_altitude_m = 0.0F;
      float sky_view_lut_slice = 0.0F;
      float planet_to_sun_cos_zenith = 0.0F;

      namespace env = scene::environment;

      if (auto env = scene.GetEnvironment()) {
        if (const auto atmo = env->TryGetSystem<env::SkyAtmosphere>();
          atmo && atmo->IsEnabled()) {
          aerial_distance_scale = atmo->GetAerialPerspectiveDistanceScale();
          aerial_scattering_strength = atmo->GetAerialScatteringStrength();
          planet_radius_m = atmo->GetPlanetRadiusMeters();

          // Update planet center to keep Z=0 as ground level.
          planet_center_ws = glm::vec3(0.0F, 0.0F, -planet_radius_m);

          // LUT availability is checked later when merging with debug
          // flags. The debug UI controls whether aerial perspective is
          // enabled.
          const auto camera_pos = view.CameraPosition();
          camera_altitude_m = glm::max(
            glm::length(camera_pos - planet_center_ws) - planet_radius_m, 0.0F);
          planet_to_sun_cos_zenith = (runtime_state.sun.enabled != 0U)
            ? runtime_state.sun.cos_zenith
            : 0.0F;
        }
      }

      runtime_state.environment_view = EnvironmentViewData {
        .flags = 0U,
        .sky_view_lut_slice = sky_view_lut_slice,
        .planet_to_sun_cos_zenith = planet_to_sun_cos_zenith,
        .aerial_perspective_distance_scale = aerial_distance_scale,
        .aerial_scattering_strength = aerial_scattering_strength,
        .planet_center_ws_pad = glm::vec4(planet_center_ws, 0.0F),
        .planet_up_ws_camera_altitude_m
        = glm::vec4(planet_up_ws, camera_altitude_m),
      };

      const bool allow_atmosphere
        = frame_context.GetViewContext(view_id).metadata.with_atmosphere;
      bool atmo_enabled = false;
      if (const auto scene_env = scene.GetEnvironment();
        allow_atmosphere && scene_env) {
        if (const auto atmo = scene_env->TryGetSystem<env::SkyAtmosphere>();
          atmo && atmo->IsEnabled()) {
          atmo_enabled = true;
        }
      }
      if (atmo_enabled) {
        if (const auto lut_mgr
          = GetOrCreateSkyAtmosphereLutManagerForView(view_id)) {
          lut_mgr->UpdateSunState(runtime_state.sun);
          if (const auto scene_env = scene.GetEnvironment()) {
            if (const auto params
              = BuildSkyAtmosphereParamsFromEnvironment(*scene_env, *lut_mgr);
              params.has_value()) {
              lut_mgr->UpdateParameters(*params);
            }
          }
        }
      }
      break;
    }
    }
  }

  PublishPreparedFrameSpans(view_id, prepared_frame);
  UpdateViewConstantsFromView(view);

  const auto draw_count
    = prepared_frame.draw_metadata_bytes.size() / sizeof(DrawMetadata);

  DLOG_F(3, "draw count: {}", draw_count);
  return draw_count;
}

auto Renderer::PublishPreparedFrameSpans(
  ViewId view_id, PreparedSceneFrame& prepared_frame) -> void
{
  DLOG_SCOPE_FUNCTION(3);

  // Ensure per-view backing storage exists
  auto& storage = per_view_storage_[view_id];

  const auto transforms = scene_prep_state_->GetTransformUploader();
  const auto world_span = transforms->GetWorldMatrices();

  // Copy matrix floats into per-view storage so spans stay valid
  storage.world_matrix_storage.assign(
    reinterpret_cast<const float*>(world_span.data()),
    reinterpret_cast<const float*>(world_span.data())
      + world_span.size() * 16u);

  prepared_frame.world_matrices = std::span<const float>(
    storage.world_matrix_storage.data(), storage.world_matrix_storage.size());

  const auto normal_span = transforms->GetNormalMatrices();
  storage.normal_matrix_storage.assign(
    reinterpret_cast<const float*>(normal_span.data()),
    reinterpret_cast<const float*>(normal_span.data())
      + normal_span.size() * 16u);

  prepared_frame.normal_matrices = std::span<const float>(
    storage.normal_matrix_storage.data(), storage.normal_matrix_storage.size());

  // Publish draw metadata bytes and partitions from emitter accessors
  if (auto emitter = scene_prep_state_->GetDrawMetadataEmitter()) {
    const auto src_bytes = emitter->GetDrawMetadataBytes();
    storage.draw_metadata_storage.assign(src_bytes.begin(), src_bytes.end());

    prepared_frame.draw_metadata_bytes
      = std::span<const std::byte>(storage.draw_metadata_storage.data(),
        storage.draw_metadata_storage.size());

    using PR = oxygen::engine::PreparedSceneFrame::PartitionRange;
    const auto parts = emitter->GetPartitions();
    storage.partition_storage.assign(parts.begin(), parts.end());

    prepared_frame.partitions = std::span<const PR>(
      storage.partition_storage.data(), storage.partition_storage.size());

    const auto draw_bounds = emitter->GetDrawBoundingSpheres();
    storage.draw_bounding_sphere_storage.assign(
      draw_bounds.begin(), draw_bounds.end());
    prepared_frame.draw_bounding_spheres = std::span<const glm::vec4>(
      storage.draw_bounding_sphere_storage.data(),
      storage.draw_bounding_sphere_storage.size());
  } else {
    // No emitter -> empty spans
    prepared_frame.draw_metadata_bytes = {};
    prepared_frame.partitions = {};
    prepared_frame.draw_bounding_spheres = {};
  }

  storage.shadow_caster_bounds_storage.clear();
  storage.visible_receiver_bounds_storage.clear();
  std::size_t collected_item_count = 0U;
  std::size_t zero_radius_item_count = 0U;
  std::size_t cast_shadow_item_count = 0U;
  std::size_t receive_shadow_item_count = 0U;
  std::size_t main_view_visible_item_count = 0U;
  if (scene_prep_state_ != nullptr) {
    const auto items = scene_prep_state_->CollectedItems();
    collected_item_count = items.size();
    storage.shadow_caster_bounds_storage.reserve(items.size());
    storage.visible_receiver_bounds_storage.reserve(items.size());
    std::size_t zero_radius_log_count = 0U;
    for (const auto& item : items) {
      if (item.world_bounding_sphere.w <= 0.0F) {
        ++zero_radius_item_count;
        if (zero_radius_log_count < 4U) {
          const auto mesh_sphere = item.geometry.mesh != nullptr
            ? item.geometry.mesh->BoundingSphere()
            : glm::vec4 { 0.0F, 0.0F, 0.0F, 0.0F };
          const auto mesh_bounds_min = item.geometry.mesh != nullptr
            ? item.geometry.mesh->BoundingBoxMin()
            : glm::vec3 { 0.0F, 0.0F, 0.0F };
          const auto mesh_bounds_max = item.geometry.mesh != nullptr
            ? item.geometry.mesh->BoundingBoxMax()
            : glm::vec3 { 0.0F, 0.0F, 0.0F };
          LOG_F(INFO,
            "Renderer: view={} zero-radius item asset={} lod={} cast={} "
            "receive={} visible={} world_sphere=({}, {}, {}, {}) "
            "mesh_sphere=({}, {}, {}, {}) "
            "mesh_bbox_min=({}, {}, {}) mesh_bbox_max=({}, {}, {})",
            view_id.get(), oxygen::data::to_string(item.geometry.asset_key),
            item.geometry.lod_index, item.cast_shadows, item.receive_shadows,
            item.main_view_visible, item.world_bounding_sphere.x,
            item.world_bounding_sphere.y, item.world_bounding_sphere.z,
            item.world_bounding_sphere.w, mesh_sphere.x, mesh_sphere.y,
            mesh_sphere.z, mesh_sphere.w, mesh_bounds_min.x, mesh_bounds_min.y,
            mesh_bounds_min.z, mesh_bounds_max.x, mesh_bounds_max.y,
            mesh_bounds_max.z);
          ++zero_radius_log_count;
        }
      }
      if (item.cast_shadows) {
        ++cast_shadow_item_count;
      }
      if (item.receive_shadows) {
        ++receive_shadow_item_count;
      }
      if (item.main_view_visible) {
        ++main_view_visible_item_count;
      }
      if (!item.cast_shadows || item.world_bounding_sphere.w <= 0.0F) {
      } else {
        storage.shadow_caster_bounds_storage.push_back(
          item.world_bounding_sphere);
      }
      if (!item.main_view_visible || !item.receive_shadows
        || item.world_bounding_sphere.w <= 0.0F) {
        continue;
      }
      storage.visible_receiver_bounds_storage.push_back(
        item.world_bounding_sphere);
    }
  }
  prepared_frame.shadow_caster_bounding_spheres
    = std::span<const glm::vec4>(storage.shadow_caster_bounds_storage.data(),
      storage.shadow_caster_bounds_storage.size());
  prepared_frame.visible_receiver_bounding_spheres
    = std::span<const glm::vec4>(storage.visible_receiver_bounds_storage.data(),
      storage.visible_receiver_bounds_storage.size());

  LOG_F(INFO,
    "Renderer: view={} prepared shadow bounds collected={} retained={} "
    "cast_items={} receive_items={} visible_items={} zero_radius={} "
    "caster_bounds={} receiver_bounds={}",
    view_id.get(), collected_item_count,
    scene_prep_state_ != nullptr ? scene_prep_state_->RetainedCount() : 0U,
    cast_shadow_item_count, receive_shadow_item_count,
    main_view_visible_item_count, zero_radius_item_count,
    storage.shadow_caster_bounds_storage.size(),
    storage.visible_receiver_bounds_storage.size());
}

auto Renderer::UpdateViewConstantsFromView(const ResolvedView& view) -> void
{
  // Update ViewConstants from the provided view snapshot.
  view_const_cpu_.SetViewMatrix(view.ViewMatrix())
    .SetProjectionMatrix(view.ProjectionMatrix())
    .SetStableProjectionMatrix(view.StableProjectionMatrix())
    .SetCameraPosition(view.CameraPosition());
}

auto Renderer::OnFrameStart(observer_ptr<FrameContext> context) -> void
{
  DLOG_SCOPE_FUNCTION(2);

  resolved_views_.clear();
  per_view_runtime_state_.clear();

  {
    std::lock_guard lock(composition_mutex_);
    composition_submission_.reset();
    composition_surface_.reset();
  }

  if (!scene_prep_state_ || !texture_binder_) {
    LOG_F(
      ERROR, "Renderer OnFrameStart called before OnAttached initialization");
    return;
  }

  auto tag = oxygen::renderer::internal::RendererTagFactory::Get();
  auto frame_slot = context->GetFrameSlot();
  auto frame_sequence = context->GetFrameSequenceNumber();
  frame_budget_stats_ = context->GetBudgetStats();
  if (frame_budget_stats_.cpu_budget.count() <= 0) {
    frame_budget_stats_.cpu_budget = std::chrono::milliseconds(16);
  }
  if (frame_budget_stats_.gpu_budget.count() <= 0) {
    frame_budget_stats_.gpu_budget = std::chrono::milliseconds(16);
  }

  // Store frame lifecycle state for RenderContext propagation
  frame_slot_ = frame_slot;
  frame_seq_num = frame_sequence;

  // Initialize Upload Coordinator and its staging providers for the new frame
  // slot BEFORE any uploaders start allocating from them.
  inline_transfers_->OnFrameStart(tag, frame_slot);
  uploader_->OnFrameStart(tag, frame_slot);
  // then uploaders and the ViewConstants manager
  texture_binder_->OnFrameStart();
  view_const_manager_->OnFrameStart(frame_slot);
  view_frame_bindings_publisher_->OnFrameStart(frame_sequence, frame_slot);
  draw_frame_bindings_publisher_->OnFrameStart(frame_sequence, frame_slot);
  view_color_data_publisher_->OnFrameStart(frame_sequence, frame_slot);
  debug_frame_bindings_publisher_->OnFrameStart(frame_sequence, frame_slot);
  lighting_frame_bindings_publisher_->OnFrameStart(frame_sequence, frame_slot);
  if (shadow_manager_) {
    shadow_manager_->OnFrameStart(tag, frame_sequence, frame_slot);
  }
  shadow_frame_bindings_publisher_->OnFrameStart(frame_sequence, frame_slot);
  environment_view_data_publisher_->OnFrameStart(frame_sequence, frame_slot);
  if (environment_frame_bindings_publisher_) {
    environment_frame_bindings_publisher_->OnFrameStart(
      frame_sequence, frame_slot);
  }
  if (env_static_manager_) {
    env_static_manager_->OnFrameStart(tag, frame_slot);
    env_static_manager_->SetBlueNoiseEnabled(atmosphere_blue_noise_enabled_);
  }
  scene_prep_state_->GetTransformUploader()->OnFrameStart(
    tag, frame_sequence, frame_slot);
  scene_prep_state_->GetGeometryUploader()->OnFrameStart(tag, frame_slot);
  scene_prep_state_->GetMaterialBinder()->OnFrameStart(tag, frame_slot);
  if (auto emitter = scene_prep_state_->GetDrawMetadataEmitter()) {
    emitter->OnFrameStart(tag, frame_sequence, frame_slot);
  }
  if (auto light_manager = scene_prep_state_->GetLightManager()) {
    light_manager->OnFrameStart(tag, frame_sequence, frame_slot);
  }
}

/*!
 Executes the scene transform propagation phase.

 Flow
 1. Acquire non-owning scene pointer from the frame context.
 2. If absent: early return (benign no-op, keeps frame deterministic).
 3. Call Scene::Update() which performs:
    - Pass 1: Dense linear scan processing dirty node flags (non-transform).
    - Pass 2: Pre-order filtered traversal (DirtyTransformFilter) resolving
      world transforms only along dirty chains (parent first).
 4. Return; no extra state retained by this module.

 Invariants / Guarantees
 - Invoked exactly once per frame in kTransformPropagation phase.
 - Parent world matrix valid before any child transform recompute.
 - Clean descendants of a dirty ancestor incur only an early-out check.
 - kIgnoreParentTransform subtrees intentionally skipped per design.
 - No scene graph structural mutation occurs here.
 - No GPU resource mutation or uploads here (CPU authoritative only).

 Never Do
 - Do not reparent / create / destroy nodes here.
 - Do not call Scene::Update() more than once per frame.
 - Do not cache raw pointers across frames.
 - Do not allocate large transient buffers (Scene owns traversal memory).
 - Do not introduce side-effects dependent on sibling visitation order.

 Performance Characteristics
 - Time: O(F + T) where F = processed dirty flags, T = visited transform
   chain nodes (<= total nodes, typically sparse).
 - Memory: No steady-state allocations.
 - Optimization: Early-exit for clean transforms; dense flag pass for cache
 locality.

 Future Improvement (Parallel Chains)
 - The scene's root hierarchies are independent for transform propagation.
 - A future optimization can collect the subset of root hierarchies that have
   at least one dirty descendant and dispatch each qualifying root subtree to
   a worker task (parent-first order preserved inside each task, no sharing).
 - Synchronize (join) all tasks before proceeding to later phases to maintain
   frame determinism. Skip parallel dispatch below a configurable dirty-node
   threshold to avoid overhead on small scenes.
 - This preserves all existing invariants (no graph mutation, parent-first,
   single update per node) while offering scalable speedups on large scenes.

 @note Dirty flag semantics, traversal filtering, and no-mutation policy are
       deliberate and should be preserved.
 @see oxygen::scene::Scene::Update
 @see oxygen::scene::SceneTraversal::UpdateTransforms
 @see oxygen::scene::DirtyTransformFilter
*/
auto Renderer::OnTransformPropagation(observer_ptr<FrameContext> context)
  -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  // Acquire scene pointer (non-owning). If absent, log once per frame in debug.
  auto scene_ptr = context->GetScene();
  if (!scene_ptr) {
    DLOG_F(WARNING,
      "No active scene set in FrameContext; skipping transform propagation");
    co_return; // Nothing to update
  }

  // Perform hierarchy propagation & world matrix updates.
  scene_ptr->Update();

  co_return;
}
