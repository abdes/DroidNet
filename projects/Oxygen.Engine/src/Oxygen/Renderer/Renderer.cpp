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
#include <cstring>
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
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/GpuEventScope.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/ImGui/GpuTimelinePanel.h>
#include <Oxygen/Renderer/ImGui/ImGuiModule.h>
#include <Oxygen/Renderer/Internal/BrdfLutManager.h>
#include <Oxygen/Renderer/Internal/ConventionalShadowDrawRecordBuilder.h>
#include <Oxygen/Renderer/Internal/EnvironmentStaticDataManager.h>
#include <Oxygen/Renderer/Internal/GpuDebugManager.h>
#include <Oxygen/Renderer/Internal/GpuTimelineProfiler.h>
#include <Oxygen/Renderer/Internal/IblManager.h>
#include <Oxygen/Renderer/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Renderer/Internal/RenderContextMaterializer.h>
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/Internal/SunResolver.h>
#include <Oxygen/Renderer/Internal/ViewConstantsManager.h>
#include <Oxygen/Renderer/LightManager.h>
#include <Oxygen/Renderer/Passes/CompositingPass.h>
#include <Oxygen/Renderer/Passes/IblComputePass.h>
#include <Oxygen/Renderer/Passes/SkyAtmosphereLutComputePass.h>
#include <Oxygen/Renderer/Passes/SkyCapturePass.h>
#include <Oxygen/Renderer/Pipeline/ForwardPipeline.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/RenderContextPool.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Resources/DrawMetadataEmitter.h>
#include <Oxygen/Renderer/Resources/GeometryUploader.h>
#include <Oxygen/Renderer/Resources/IResourceBinder.h>
#include <Oxygen/Renderer/Resources/MaterialBinder.h>
#include <Oxygen/Renderer/Resources/TextureBinder.h>
#include <Oxygen/Renderer/Resources/TransformUploader.h>
#include <Oxygen/Renderer/SceneCameraViewResolver.h>
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
#include <Oxygen/Renderer/Upload/RingBufferStaging.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/Upload/UploadPolicy.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/Sun.h>
#include <Oxygen/Scene/Scene.h>

namespace {
constexpr std::string_view kCVarRendererTextureDumpTopN
  = "rndr.texture_dump_top_n";
constexpr std::string_view kCVarRendererLastFrameStats
  = "rndr.last_frame_stats";
constexpr std::string_view kCVarRendererGpuTimestamps = "rndr.gpu_timestamps";
constexpr std::string_view kCVarRendererGpuTimestampMaxScopes
  = "rndr.gpu_timestamps.max_scopes";
constexpr std::string_view kCVarRendererGpuTimestampExportNextFrame
  = "rndr.gpu_timestamps.export_next_frame";
constexpr std::string_view kCVarRendererGpuTimestampViewer
  = "rndr.gpu_timestamps.viewer";
constexpr std::string_view kCVarRendererShadowCsmDistanceScale
  = "rndr.shadow.csm.distance_scale";
constexpr std::string_view kCVarRendererShadowCsmTransitionScale
  = "rndr.shadow.csm.transition_scale";
constexpr std::string_view kCVarRendererShadowCsmMaxCascades
  = "rndr.shadow.csm.max_cascades";
constexpr std::string_view kCVarRendererShadowCsmMaxResolution
  = "rndr.shadow.csm.max_resolution";
constexpr std::string_view kCommandRendererDumpTextureMemory
  = "rndr.dump_texture_memory";
constexpr std::string_view kCommandRendererDumpStats = "rndr.dump_stats";
constexpr int64_t kDefaultTextureDumpTopN = 20;
constexpr int64_t kMinTextureDumpTopN = 1;
constexpr int64_t kMaxTextureDumpTopN = 500;
constexpr int64_t kDefaultGpuTimestampMaxScopes = 4096;
constexpr int64_t kMinGpuTimestampMaxScopes = 1;
constexpr int64_t kMaxGpuTimestampMaxScopes = 65536;
constexpr double kDefaultShadowCsmDistanceScale = 1.0;
constexpr double kDefaultShadowCsmTransitionScale = 1.0;
constexpr int64_t kDefaultShadowCsmMaxCascades = 4;
constexpr int64_t kDefaultShadowCsmMaxResolution = 0;
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

struct AlphaTestShadowCasterMaterialStateSignature {
  std::uint64_t hash { 0U };
  std::uint32_t alpha_test_material_count { 0U };
  std::uint32_t pending_alpha_test_material_count { 0U };
  std::uint32_t alpha_test_domain_mismatch_count { 0U };
};

auto HashShadowCasterMaterialState(
  const std::span<const oxygen::engine::sceneprep::RenderItemData> items,
  const oxygen::renderer::resources::IResourceBinder* texture_binder)
  -> AlphaTestShadowCasterMaterialStateSignature
{
  AlphaTestShadowCasterMaterialStateSignature signature {};
  if (texture_binder == nullptr || items.empty()) {
    return signature;
  }

  std::vector<std::uint64_t> material_hashes {};
  material_hashes.reserve(items.size());
  for (const auto& item : items) {
    if (!item.cast_shadows || !item.material.IsValid()
      || item.material.resolved_asset == nullptr
      || item.world_bounding_sphere.w <= 0.0F) {
      continue;
    }

    const auto& material = *item.material.resolved_asset;
    const auto material_flags = material.GetFlags();
    const bool alpha_test_enabled
      = (material_flags & oxygen::data::pak::render::kMaterialFlag_AlphaTest)
      != 0U;
    if (!alpha_test_enabled) {
      continue;
    }

    const auto no_texture_sampling
      = (material_flags
          & oxygen::data::pak::render::kMaterialFlag_NoTextureSampling)
      != 0U;

    ++signature.alpha_test_material_count;
    if (material.GetMaterialDomain() != oxygen::data::MaterialDomain::kMasked) {
      ++signature.alpha_test_domain_mismatch_count;
    }

    const auto base_color_key = material.GetBaseColorTextureKey();
    const auto base_color_key_value = base_color_key.get();
    const bool is_ready = base_color_key_value == 0U
      || texture_binder->IsResourceReady(base_color_key);
    if (!is_ready) {
      ++signature.pending_alpha_test_material_count;
    }

    auto material_hash
      = HashBytes(&base_color_key_value, sizeof(base_color_key_value));
    material_hash = HashBytes(&is_ready, sizeof(is_ready), material_hash);
    const auto alpha_cutoff = material.GetAlphaCutoff();
    material_hash
      = HashBytes(&alpha_cutoff, sizeof(alpha_cutoff), material_hash);
    const auto base_color = material.GetBaseColor();
    const auto base_alpha = base_color[3];
    material_hash = HashBytes(&base_alpha, sizeof(base_alpha), material_hash);
    material_hash = HashBytes(
      &no_texture_sampling, sizeof(no_texture_sampling), material_hash);
    material_hashes.push_back(material_hash);
  }

  if (material_hashes.empty()) {
    return signature;
  }

  std::ranges::sort(material_hashes);
  std::uint64_t hash = kFnvOffsetBasis;
  const auto hashed_materials
    = static_cast<std::uint32_t>(material_hashes.size());
  hash = HashBytes(&hashed_materials, sizeof(hashed_materials), hash);
  hash = HashBytes(material_hashes.data(),
    material_hashes.size() * sizeof(material_hashes.front()), hash);
  signature.hash = hash;
  return signature;
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

auto FormatCompositingTaskScopeLabel(
  const oxygen::engine::CompositingTask& task) -> std::string
{
  using oxygen::engine::CompositingTaskType;

  switch (task.type) {
  case CompositingTaskType::kCopy:
    return fmt::format(
      "Composite Copy View {}", task.copy.source_view_id.get());
  case CompositingTaskType::kBlend:
    return fmt::format("Composite Blend View {} (alpha {:.2f})",
      task.blend.source_view_id.get(), task.blend.alpha);
  case CompositingTaskType::kBlendTexture:
    if (task.texture_blend.source_texture) {
      const auto& name
        = task.texture_blend.source_texture->GetDescriptor().debug_name;
      if (!name.empty()) {
        return fmt::format("Composite Blend Texture {} (alpha {:.2f})", name,
          task.texture_blend.alpha);
      }
    }
    return fmt::format(
      "Composite Blend Texture (alpha {:.2f})", task.texture_blend.alpha);
  case CompositingTaskType::kTaa:
    return fmt::format("Composite TAA (jitter {:.2f})", task.taa.jitter_scale);
  }

  return "Composite Task";
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

auto TrackCompositionSourceTexture(oxygen::graphics::ResourceRegistry& registry,
  oxygen::graphics::CommandRecorder& recorder,
  const oxygen::graphics::Texture& texture) -> void
{
  CHECK_F(registry.Contains(texture),
    "Renderer: composition source texture '{}' must be registered in "
    "ResourceRegistry before compositing",
    texture.GetDescriptor().debug_name);
  if (recorder.IsResourceTracked(texture)) {
    return;
  }

  auto initial = texture.GetDescriptor().initial_state;
  CHECK_F(initial != oxygen::graphics::ResourceStates::kUnknown
      && initial != oxygen::graphics::ResourceStates::kUndefined,
    "Renderer: composition source texture '{}' must either already have "
    "resource-state tracking or declare a valid initial_state",
    texture.GetDescriptor().debug_name);
  recorder.BeginTrackingResourceState(texture, initial, true);
}

auto CopyTextureToRegion(oxygen::graphics::CommandRecorder& recorder,
  oxygen::graphics::Texture& source, oxygen::graphics::Texture& backbuffer,
  const oxygen::ViewPort& viewport) -> void
{
  CHECK_F(recorder.IsResourceTracked(source),
    "Renderer: copy source texture '{}' must already be tracked for "
    "compositing",
    source.GetDescriptor().debug_name);
  CHECK_F(recorder.IsResourceTracked(backbuffer),
    "Renderer: compositing backbuffer '{}' must already be tracked",
    backbuffer.GetDescriptor().debug_name);

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

  recorder.RequireResourceState(
    source, oxygen::graphics::ResourceStates::kCopySource);
  recorder.RequireResourceState(
    backbuffer, oxygen::graphics::ResourceStates::kCopyDest);
  recorder.FlushBarriers();

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

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal

namespace {

class OffscreenForwardPipeline final
  : public oxygen::renderer::ForwardPipeline {
public:
  using oxygen::renderer::ForwardPipeline::ForwardPipeline;

  [[nodiscard]] auto GetCapabilityRequirements() const
    -> oxygen::renderer::PipelineCapabilityRequirements override
  {
    return oxygen::renderer::PipelineCapabilityRequirements {
      .required = oxygen::renderer::RendererCapabilityFamily::kScenePreparation
        | oxygen::renderer::RendererCapabilityFamily::kGpuUploadAndAssetBinding,
      .optional = oxygen::renderer::RendererCapabilityFamily::kLightingData
        | oxygen::renderer::RendererCapabilityFamily::kShadowing
        | oxygen::renderer::RendererCapabilityFamily::kEnvironmentLighting
        | oxygen::renderer::RendererCapabilityFamily::kFinalOutputComposition
        | oxygen::renderer::RendererCapabilityFamily::kDiagnosticsAndProfiling,
    };
  }
};

} // namespace

//===----------------------------------------------------------------------===//
// Renderer Implementation
//===----------------------------------------------------------------------===//

Renderer::Renderer(std::weak_ptr<Graphics> graphics, RendererConfig config,
  const renderer::CapabilitySet capability_families)
  : gfx_weak_(std::move(graphics))
  , config_(config)
  , capability_families_(capability_families)
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
  // Inline staging is retired by InlineTransfersCoordinator, not by the upload
  // queue fence path. Keep it out of UploadCoordinator provider retirement so
  // its partition reuse bookkeeping stays on the correct fence timeline.
  inline_staging_provider_ = std::make_shared<upload::RingBufferStaging>(
    upload::internal::UploaderTagFactory::Get(), observer_ptr { gfx.get() },
    frame::kFramesInFlight, 16, upload::kDefaultRingBufferStagingSlack,
    "Renderer.InlineStaging");
  inline_transfers_->RegisterProvider(inline_staging_provider_);

  // Initialize the render-context pool helper used to claim per-frame
  // render contexts during PreRender/Render phases.
  render_context_pool_ = std::make_unique<RenderContextPool>();
}

Renderer::Renderer(std::weak_ptr<Graphics> graphics, RendererConfig config)
  : Renderer(std::move(graphics), std::move(config),
      renderer::kPhase1DefaultRuntimeCapabilityFamilies)
{
}

Renderer::~Renderer()
{
  const auto stats = GetStats();
  LogRendererPerformanceStats(stats, loguru::Verbosity_INFO);

  OnShutdown();
  scene_prep_.reset();
}

Renderer::OffscreenFrameSession::OffscreenFrameSession(
  Renderer& renderer, const OffscreenFrameConfig config)
  : renderer_(&renderer)
  , active_(true)
{
  renderer_->WireContext(render_context_, {});
  render_context_.scene = config.scene;
}

Renderer::OffscreenFrameSession::~OffscreenFrameSession() { Release(); }

Renderer::OffscreenFrameSession::OffscreenFrameSession(
  OffscreenFrameSession&& other) noexcept
  : renderer_(std::exchange(other.renderer_, nullptr))
  , render_context_(std::move(other.render_context_))
  , current_resolved_view_(std::move(other.current_resolved_view_))
  , current_prepared_frame_(std::move(other.current_prepared_frame_))
  , active_(std::exchange(other.active_, false))
{
  RebindCurrentViewPointers();
}

auto Renderer::OffscreenFrameSession::operator=(
  OffscreenFrameSession&& other) noexcept -> OffscreenFrameSession&
{
  if (this == &other) {
    return *this;
  }

  Release();
  renderer_ = std::exchange(other.renderer_, nullptr);
  render_context_ = std::move(other.render_context_);
  current_resolved_view_ = std::move(other.current_resolved_view_);
  current_prepared_frame_ = std::move(other.current_prepared_frame_);
  active_ = std::exchange(other.active_, false);
  RebindCurrentViewPointers();
  return *this;
}

auto Renderer::OffscreenFrameSession::SetCurrentView(const ViewId view_id,
  const ResolvedView& resolved_view, const PreparedSceneFrame& prepared_frame)
  -> void
{
  CHECK_F(active_ && renderer_ != nullptr,
    "OffscreenFrameSession::SetCurrentView called on an inactive session");
  CHECK_NOTNULL_F(renderer_->view_const_manager_.get(),
    "Renderer offscreen frame requires ViewConstantsManager");

  current_resolved_view_ = resolved_view;
  current_prepared_frame_ = prepared_frame;

  render_context_.current_view = {};
  render_context_.current_view.view_id = view_id;
  render_context_.current_view.exposure_view_id = view_id;
  render_context_.current_view.resolved_view.reset(&*current_resolved_view_);
  render_context_.current_view.prepared_frame.reset(&*current_prepared_frame_);

  renderer_->UpdateViewConstantsFromView(resolved_view);
  renderer_->view_const_cpu_
    .SetTimeSeconds(renderer_->last_frame_dt_seconds_, ViewConstants::kRenderer)
    .SetFrameSlot(renderer_->frame_slot_, ViewConstants::kRenderer)
    .SetFrameSequenceNumber(renderer_->frame_seq_num, ViewConstants::kRenderer)
    .SetBindlessViewFrameBindingsSlot(
      BindlessViewFrameBindingsSlot {}, ViewConstants::kRenderer);

  const auto& snapshot = renderer_->view_const_cpu_.GetSnapshot();
  auto buffer_info = renderer_->view_const_manager_->WriteViewConstants(
    view_id, &snapshot, sizeof(snapshot));
  CHECK_F(buffer_info.buffer != nullptr,
    "Renderer offscreen frame failed to write ViewConstants for view {}",
    view_id.get());

  renderer_->WireContext(render_context_, buffer_info.buffer);
}

auto Renderer::OffscreenFrameSession::SetCurrentView(const ViewId view_id,
  const ResolvedView& resolved_view, const PreparedSceneFrame& prepared_frame,
  const ViewConstants& view_constants) -> void
{
  SetCurrentView(view_id, resolved_view, prepared_frame);

  CHECK_F(active_ && renderer_ != nullptr,
    "OffscreenFrameSession::SetCurrentView called on an inactive session");
  CHECK_NOTNULL_F(renderer_->view_const_manager_.get(),
    "Renderer offscreen frame requires ViewConstantsManager");

  auto override_constants = view_constants;
  override_constants
    .SetTimeSeconds(renderer_->last_frame_dt_seconds_, ViewConstants::kRenderer)
    .SetFrameSlot(renderer_->frame_slot_, ViewConstants::kRenderer)
    .SetFrameSequenceNumber(renderer_->frame_seq_num, ViewConstants::kRenderer);

  const auto& snapshot = override_constants.GetSnapshot();
  auto buffer_info = renderer_->view_const_manager_->WriteViewConstants(
    view_id, &snapshot, sizeof(snapshot));
  CHECK_F(buffer_info.buffer != nullptr,
    "Renderer offscreen frame failed to override ViewConstants for view {}",
    view_id.get());

  renderer_->WireContext(render_context_, buffer_info.buffer);
}

auto Renderer::OffscreenFrameSession::RebindCurrentViewPointers() noexcept
  -> void
{
  if (!current_resolved_view_.has_value()) {
    render_context_.current_view.resolved_view.reset();
  } else {
    render_context_.current_view.resolved_view.reset(&*current_resolved_view_);
  }

  if (!current_prepared_frame_.has_value()) {
    render_context_.current_view.prepared_frame.reset();
  } else {
    render_context_.current_view.prepared_frame.reset(
      &*current_prepared_frame_);
  }
}

auto Renderer::OffscreenFrameSession::Release() noexcept -> void
{
  if (!active_) {
    return;
  }
  if (renderer_ != nullptr) {
    renderer_->EndOffscreenFrame();
  }
  render_context_.current_view = {};
  current_resolved_view_.reset();
  current_prepared_frame_.reset();
  renderer_ = nullptr;
  active_ = false;
}

Renderer::SinglePassHarnessFacade::SinglePassHarnessFacade(
  Renderer& renderer) noexcept
  : renderer_(observer_ptr { &renderer })
{
}

auto Renderer::SinglePassHarnessFacade::SetFrameSession(
  FrameSessionInput session) -> SinglePassHarnessFacade&
{
  frame_session_ = std::move(session);
  return *this;
}

auto Renderer::SinglePassHarnessFacade::SetOutputTarget(
  OutputTargetInput target) -> SinglePassHarnessFacade&
{
  output_target_ = std::move(target);
  return *this;
}

auto Renderer::SinglePassHarnessFacade::SetResolvedView(ResolvedViewInput view)
  -> SinglePassHarnessFacade&
{
  resolved_view_ = std::move(view);
  return *this;
}

auto Renderer::SinglePassHarnessFacade::SetPreparedFrame(
  PreparedFrameInput frame) -> SinglePassHarnessFacade&
{
  prepared_frame_ = std::move(frame);
  return *this;
}

auto Renderer::SinglePassHarnessFacade::SetCoreShaderInputs(
  CoreShaderInputsInput inputs) -> SinglePassHarnessFacade&
{
  core_shader_inputs_ = std::move(inputs);
  return *this;
}

auto Renderer::SinglePassHarnessFacade::CanFinalize() const -> bool
{
  return frame_session_.has_value() && output_target_.has_value()
    && (resolved_view_.has_value() || core_shader_inputs_.has_value());
}

auto Renderer::SinglePassHarnessFacade::Validate() const -> ValidationReport
{
  auto materializer = internal::RenderContextMaterializer(*renderer_);
  return materializer.ValidateSinglePass(internal::SinglePassHarnessStaging {
    .frame_session = frame_session_,
    .output_target = output_target_,
    .resolved_view = resolved_view_,
    .prepared_frame = prepared_frame_,
    .core_shader_inputs = core_shader_inputs_,
  });
}

auto Renderer::SinglePassHarnessFacade::Finalize()
  -> std::expected<ValidatedSinglePassHarnessContext, ValidationReport>
{
  auto materializer = internal::RenderContextMaterializer(*renderer_);
  return materializer.MaterializeSinglePass(internal::SinglePassHarnessStaging {
    .frame_session = frame_session_,
    .output_target = output_target_,
    .resolved_view = resolved_view_,
    .prepared_frame = prepared_frame_,
    .core_shader_inputs = core_shader_inputs_,
  });
}

auto Renderer::ForSinglePassHarness() -> SinglePassHarnessFacade
{
  return SinglePassHarnessFacade(*this);
}

Renderer::RenderGraphHarnessFacade::RenderGraphHarnessFacade(
  Renderer& renderer) noexcept
  : renderer_(observer_ptr { &renderer })
{
}

auto Renderer::RenderGraphHarnessFacade::SetFrameSession(
  FrameSessionInput session) -> RenderGraphHarnessFacade&
{
  frame_session_ = std::move(session);
  return *this;
}

auto Renderer::RenderGraphHarnessFacade::SetOutputTarget(
  OutputTargetInput target) -> RenderGraphHarnessFacade&
{
  output_target_ = std::move(target);
  return *this;
}

auto Renderer::RenderGraphHarnessFacade::SetResolvedView(ResolvedViewInput view)
  -> RenderGraphHarnessFacade&
{
  resolved_view_ = std::move(view);
  return *this;
}

auto Renderer::RenderGraphHarnessFacade::SetPreparedFrame(
  PreparedFrameInput frame) -> RenderGraphHarnessFacade&
{
  prepared_frame_ = std::move(frame);
  return *this;
}

auto Renderer::RenderGraphHarnessFacade::SetCoreShaderInputs(
  CoreShaderInputsInput inputs) -> RenderGraphHarnessFacade&
{
  core_shader_inputs_ = std::move(inputs);
  return *this;
}

auto Renderer::RenderGraphHarnessFacade::SetRenderGraph(
  RenderGraphHarnessInput graph) -> RenderGraphHarnessFacade&
{
  render_graph_ = std::move(graph);
  return *this;
}

auto Renderer::RenderGraphHarnessFacade::CanFinalize() const -> bool
{
  return frame_session_.has_value() && output_target_.has_value()
    && (resolved_view_.has_value() || core_shader_inputs_.has_value())
    && render_graph_.has_value();
}

auto Renderer::RenderGraphHarnessFacade::Validate() const -> ValidationReport
{
  auto materializer = internal::RenderContextMaterializer(*renderer_);
  auto report
    = materializer.ValidateSinglePass(internal::SinglePassHarnessStaging {
      .frame_session = frame_session_,
      .output_target = output_target_,
      .resolved_view = resolved_view_,
      .prepared_frame = prepared_frame_,
      .core_shader_inputs = core_shader_inputs_,
    });

  if (!render_graph_.has_value()) {
    report.issues.push_back(ValidationIssue {
      .code = "render_graph.missing",
      .message = "Render-graph harness requires a caller-authored render graph",
    });
  }

  return report;
}

auto Renderer::RenderGraphHarnessFacade::Finalize()
  -> std::expected<ValidatedRenderGraphHarness, ValidationReport>
{
  auto report = Validate();
  if (!report.Ok()) {
    return std::unexpected(std::move(report));
  }

  auto materializer = internal::RenderContextMaterializer(*renderer_);
  auto materialized
    = materializer.MaterializeSinglePass(internal::SinglePassHarnessStaging {
      .frame_session = frame_session_,
      .output_target = output_target_,
      .resolved_view = resolved_view_,
      .prepared_frame = prepared_frame_,
      .core_shader_inputs = core_shader_inputs_,
    });
  if (!materialized.has_value()) {
    return std::unexpected(std::move(materialized.error()));
  }

  const auto view_id = resolved_view_.has_value()
    ? resolved_view_->view_id
    : core_shader_inputs_->view_id;
  return ValidatedRenderGraphHarness(
    std::move(materialized.value()), *render_graph_, view_id);
}

auto Renderer::ForRenderGraphHarness() -> RenderGraphHarnessFacade
{
  return RenderGraphHarnessFacade(*this);
}

Renderer::OffscreenSceneViewInput::OffscreenSceneViewInput()
{
  composition_view_ = renderer::CompositionView::ForScene(
    kInvalidViewId, View {}, scene::SceneNode {});
  SyncName();
}

auto Renderer::OffscreenSceneViewInput::FromCamera(std::string name,
  const ViewId view_id, const View& view, const scene::SceneNode& camera)
  -> OffscreenSceneViewInput
{
  auto input = OffscreenSceneViewInput {};
  input.name_storage_ = std::move(name);
  input.composition_view_
    = renderer::CompositionView::ForScene(view_id, view, camera);
  input.SyncName();
  return input;
}

auto Renderer::OffscreenSceneViewInput::SetWithAtmosphere(const bool enabled)
  -> OffscreenSceneViewInput&
{
  composition_view_.with_atmosphere = enabled;
  return *this;
}

auto Renderer::OffscreenSceneViewInput::SetClearColor(
  const graphics::Color& clear_color) -> OffscreenSceneViewInput&
{
  composition_view_.clear_color = clear_color;
  return *this;
}

auto Renderer::OffscreenSceneViewInput::SetForceWireframe(const bool enabled)
  -> OffscreenSceneViewInput&
{
  composition_view_.force_wireframe = enabled;
  return *this;
}

auto Renderer::OffscreenSceneViewInput::SetExposureSourceViewId(
  const ViewId view_id) -> OffscreenSceneViewInput&
{
  composition_view_.exposure_source_view_id = view_id;
  return *this;
}

auto Renderer::OffscreenSceneViewInput::SyncName() noexcept -> void
{
  composition_view_.name = name_storage_;
}

Renderer::ValidatedOffscreenSceneSession::ValidatedOffscreenSceneSession(
  Renderer& renderer, FrameSessionInput frame_session,
  SceneSourceInput scene_source, OffscreenSceneViewInput view_intent,
  OutputTargetInput output_target, OffscreenPipelineInput pipeline)
  : renderer_(observer_ptr { &renderer })
  , frame_session_(std::move(frame_session))
  , scene_source_(std::move(scene_source))
  , view_intent_(std::move(view_intent))
  , output_target_(std::move(output_target))
  , pipeline_(pipeline.borrowed_pipeline)
  , owned_pipeline_(std::move(pipeline.owned_pipeline))
{
  if (owned_pipeline_) {
    pipeline_.reset(owned_pipeline_.get());
  }
  if (view_intent_.ViewIntent().id == kInvalidViewId) {
    auto normalized = renderer::CompositionView::ForScene(ViewId { 1U },
      view_intent_.ViewIntent().view, *view_intent_.ViewIntent().camera);
    normalized.with_atmosphere = view_intent_.ViewIntent().with_atmosphere;
    normalized.clear_color = view_intent_.ViewIntent().clear_color;
    normalized.force_wireframe = view_intent_.ViewIntent().force_wireframe;
    normalized.exposure_source_view_id
      = view_intent_.ViewIntent().exposure_source_view_id;
    view_intent_ = OffscreenSceneViewInput::FromCamera(
      std::string(view_intent_.ViewIntent().name), ViewId { 1U },
      normalized.view, *normalized.camera);
    view_intent_.SetWithAtmosphere(normalized.with_atmosphere)
      .SetClearColor(normalized.clear_color)
      .SetForceWireframe(normalized.force_wireframe)
      .SetExposureSourceViewId(normalized.exposure_source_view_id);
  }
}

auto Renderer::ValidatedOffscreenSceneSession::MakeCompositionView() const
  -> renderer::CompositionView
{
  return view_intent_.ViewIntent();
}

auto Renderer::ValidatedOffscreenSceneSession::Execute() -> co::Co<void>
{
  CHECK_NOTNULL_F(
    renderer_.get(), "ValidatedOffscreenSceneSession requires a live renderer");
  CHECK_NOTNULL_F(scene_source_.scene.get(),
    "ValidatedOffscreenSceneSession requires a live scene");
  CHECK_NOTNULL_F(output_target_.framebuffer.get(),
    "ValidatedOffscreenSceneSession requires an output target");
  CHECK_NOTNULL_F(
    pipeline_.get(), "ValidatedOffscreenSceneSession requires a pipeline");

  auto graphics = renderer_->GetGraphics();
  const auto make_tag
    = []() { return engine::internal::EngineTagFactory::Get(); };

  auto frame_context = FrameContext {};
  frame_context.SetGraphicsBackend(
    std::weak_ptr<Graphics>(graphics), make_tag());
  frame_context.SetFrameSequenceNumber(
    frame_session_.frame_sequence, make_tag());
  frame_context.SetFrameSlot(frame_session_.frame_slot, make_tag());
  frame_context.SetFrameStartTime(std::chrono::steady_clock::now(), make_tag());
  frame_context.SetScene(scene_source_.scene);

  auto timing = ModuleTimingData {};
  timing.game_delta_time = time::CanonicalDuration {
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<float>(frame_session_.delta_time_seconds)),
  };
  timing.fixed_delta_time = timing.game_delta_time;
  timing.current_fps = frame_session_.delta_time_seconds > 0.0F
    ? 1.0F / frame_session_.delta_time_seconds
    : 0.0F;
  frame_context.SetModuleTimingData(timing, make_tag());
  frame_context.SetBudgetStats(
    FrameContext::BudgetStats { .cpu_budget = std::chrono::milliseconds(16),
      .gpu_budget = std::chrono::milliseconds(16) },
    make_tag());

  const auto target_fb = std::shared_ptr<graphics::Framebuffer>(
    const_cast<graphics::Framebuffer*>(output_target_.framebuffer.get()),
    [](graphics::Framebuffer*) { });
  const auto active_view = MakeCompositionView();
  const std::array views { active_view };

  auto finish_frame = [&]() -> void {
    frame_context.SetCurrentPhase(core::PhaseId::kFrameEnd, make_tag());
    renderer_->OnFrameEnd(observer_ptr<FrameContext> { &frame_context });
    graphics->EndFrame(
      frame_session_.frame_sequence, frame_session_.frame_slot);
  };

  graphics->BeginFrame(
    frame_session_.frame_sequence, frame_session_.frame_slot);
  try {
    frame_context.SetCurrentPhase(core::PhaseId::kFrameStart, make_tag());
    renderer_->OnFrameStart(observer_ptr<FrameContext> { &frame_context });
    pipeline_->OnFrameStart(
      observer_ptr<FrameContext> { &frame_context }, *renderer_);

    frame_context.SetCurrentPhase(
      core::PhaseId::kTransformPropagation, make_tag());
    co_await renderer_->OnTransformPropagation(
      observer_ptr<FrameContext> { &frame_context });

    frame_context.SetCurrentPhase(core::PhaseId::kPublishViews, make_tag());
    co_await pipeline_->OnPublishViews(
      observer_ptr<FrameContext> { &frame_context }, *renderer_,
      *scene_source_.scene,
      std::span<const renderer::CompositionView>(views.data(), views.size()),
      target_fb.get());

    frame_context.SetCurrentPhase(core::PhaseId::kPreRender, make_tag());
    co_await pipeline_->OnPreRender(
      observer_ptr<FrameContext> { &frame_context }, *renderer_,
      std::span<const renderer::CompositionView>(views.data(), views.size()));
    co_await renderer_->OnPreRender(
      observer_ptr<FrameContext> { &frame_context });

    frame_context.SetCurrentPhase(core::PhaseId::kRender, make_tag());
    co_await renderer_->OnRender(observer_ptr<FrameContext> { &frame_context });

    frame_context.SetCurrentPhase(core::PhaseId::kCompositing, make_tag());
    auto submission = co_await pipeline_->OnCompositing(
      observer_ptr<FrameContext> { &frame_context }, target_fb);
    if (!submission.tasks.empty() && submission.composite_target) {
      renderer_->RegisterComposition(std::move(submission), nullptr);
    }
    co_await renderer_->OnCompositing(
      observer_ptr<FrameContext> { &frame_context });

    finish_frame();
    co_return;
  } catch (...) {
    finish_frame();
    throw;
  }
}

Renderer::OffscreenSceneFacade::OffscreenSceneFacade(
  Renderer& renderer) noexcept
  : renderer_(observer_ptr { &renderer })
{
}

auto Renderer::OffscreenSceneFacade::SetFrameSession(FrameSessionInput session)
  -> OffscreenSceneFacade&
{
  frame_session_ = std::move(session);
  return *this;
}

auto Renderer::OffscreenSceneFacade::SetSceneSource(SceneSourceInput scene)
  -> OffscreenSceneFacade&
{
  scene_source_ = std::move(scene);
  return *this;
}

auto Renderer::OffscreenSceneFacade::SetViewIntent(OffscreenSceneViewInput view)
  -> OffscreenSceneFacade&
{
  view_intent_ = std::move(view);
  return *this;
}

auto Renderer::OffscreenSceneFacade::SetOutputTarget(OutputTargetInput target)
  -> OffscreenSceneFacade&
{
  output_target_ = std::move(target);
  return *this;
}

auto Renderer::OffscreenSceneFacade::SetPipeline(
  OffscreenPipelineInput pipeline) -> OffscreenSceneFacade&
{
  pipeline_ = std::move(pipeline);
  return *this;
}

auto Renderer::OffscreenSceneFacade::CanFinalize() const -> bool
{
  return frame_session_.has_value() && scene_source_.has_value()
    && view_intent_.has_value() && output_target_.has_value();
}

auto Renderer::OffscreenSceneFacade::Validate() const -> ValidationReport
{
  auto report = ValidationReport {};

  if (!frame_session_.has_value()) {
    report.issues.push_back(ValidationIssue { .code = "frame_session.missing",
      .message = "Offscreen scene requires a frame session" });
  } else if (frame_session_->frame_slot == frame::kInvalidSlot) {
    report.issues.push_back(
      ValidationIssue { .code = "frame_session.invalid_slot",
        .message = "Offscreen scene requires a valid frame slot" });
  } else if (frame_session_->scene != nullptr) {
    report.issues.push_back(
      ValidationIssue { .code = "frame_session.scene_not_allowed",
        .message = "Offscreen scene uses SetSceneSource as the sole scene "
                   "authority; FrameSessionInput.scene must remain null" });
  } else if (!(frame_session_->frame_slot < frame::kMaxSlot)) {
    report.issues.push_back(
      ValidationIssue { .code = "frame_session.out_of_bounds_slot",
        .message = "Offscreen scene requires a frame slot inside the "
                   "frames-in-flight range" });
  } else if (!std::isfinite(frame_session_->delta_time_seconds)
    || frame_session_->delta_time_seconds <= 0.0F) {
    report.issues.push_back(
      ValidationIssue { .code = "frame_session.invalid_delta_time",
        .message = "Offscreen scene requires a finite positive delta time" });
  }

  if (!scene_source_.has_value() || scene_source_->scene == nullptr) {
    report.issues.push_back(ValidationIssue { .code = "scene_source.missing",
      .message = "Offscreen scene requires a live scene source" });
  }

  if (!view_intent_.has_value()) {
    report.issues.push_back(ValidationIssue { .code = "view_intent.missing",
      .message = "Offscreen scene requires a view intent" });
  } else {
    const auto& view_intent = view_intent_->ViewIntent();
    auto camera = view_intent.camera.value_or(scene::SceneNode {});
    if (!camera.IsAlive() || !camera.HasCamera()) {
      report.issues.push_back(
        ValidationIssue { .code = "view_intent.invalid_camera",
          .message = "Offscreen scene requires a live camera node" });
    } else if (scene_source_.has_value() && scene_source_->scene != nullptr
      && !scene_source_->scene->Contains(camera)) {
      report.issues.push_back(
        ValidationIssue { .code = "view_intent.camera_not_in_scene",
          .message
          = "Offscreen scene camera must belong to the staged scene source" });
    }
  }

  if (!output_target_.has_value() || output_target_->framebuffer == nullptr) {
    report.issues.push_back(ValidationIssue { .code = "output_target.missing",
      .message = "Offscreen scene requires an output target framebuffer" });
  }

  if (renderer_->engine_ == nullptr || renderer_->scene_prep_state_ == nullptr
    || renderer_->texture_binder_ == nullptr) {
    report.issues.push_back(ValidationIssue { .code = "renderer.not_attached",
      .message = "Offscreen scene requires a renderer attached to an "
                 "engine-initialized runtime substrate" });
  }

  if ((!pipeline_.has_value()
        || (pipeline_->borrowed_pipeline == nullptr
          && pipeline_->owned_pipeline == nullptr))
    && renderer_->engine_ == nullptr) {
    report.issues.push_back(
      ValidationIssue { .code = "pipeline.default_unavailable",
        .message = "Default offscreen pipeline requires the renderer to be "
                   "attached to an engine" });
  }

  auto pipeline = observer_ptr<renderer::RenderingPipeline> {};
  if (pipeline_.has_value()) {
    pipeline = pipeline_->borrowed_pipeline;
    if (pipeline == nullptr && pipeline_->owned_pipeline != nullptr) {
      pipeline.reset(pipeline_->owned_pipeline.get());
    }
    if (pipeline_->borrowed_pipeline != nullptr
      && pipeline_->owned_pipeline != nullptr) {
      report.issues.push_back(
        ValidationIssue { .code = "pipeline.ambiguous_ownership",
          .message = "Offscreen pipeline input must be either borrowed or "
                     "owned, but not both" });
    }
  }
  if (pipeline == nullptr && renderer_->engine_ != nullptr) {
    auto preview = OffscreenForwardPipeline(renderer_->engine_);
    const auto validation = renderer_->ValidateCapabilityRequirements(
      preview.GetCapabilityRequirements());
    if (!validation.Ok()) {
      report.issues.push_back(
        ValidationIssue { .code = "pipeline.missing_required_capabilities",
          .message = "Default offscreen pipeline requires unavailable renderer "
                     "capability families" });
    }
  } else if (pipeline != nullptr) {
    const auto validation = renderer_->ValidateCapabilityRequirements(
      pipeline->GetCapabilityRequirements());
    if (!validation.Ok()) {
      report.issues.push_back(
        ValidationIssue { .code = "pipeline.missing_required_capabilities",
          .message = "Selected offscreen pipeline requires unavailable "
                     "renderer capability families" });
    }
  }

  return report;
}

auto Renderer::OffscreenSceneFacade::Finalize()
  -> std::expected<ValidatedOffscreenSceneSession, ValidationReport>
{
  auto report = Validate();
  if (!report.Ok()) {
    return std::unexpected(std::move(report));
  }

  auto pipeline_input
    = pipeline_.has_value() ? std::move(*pipeline_) : OffscreenPipelineInput {};
  auto pipeline = pipeline_input.borrowed_pipeline;
  if (pipeline == nullptr && pipeline_input.owned_pipeline != nullptr) {
    pipeline.reset(pipeline_input.owned_pipeline.get());
  }
  if (pipeline == nullptr) {
    pipeline_input.owned_pipeline
      = std::make_unique<OffscreenForwardPipeline>(renderer_->engine_);
    pipeline.reset(pipeline_input.owned_pipeline.get());
  }
  pipeline->BindToRenderer(*renderer_);

  auto view_intent = *view_intent_;
  if (view_intent.ViewIntent().id == kInvalidViewId) {
    view_intent = OffscreenSceneViewInput::FromCamera(
      std::string(view_intent.ViewIntent().name), ViewId { 1U },
      view_intent.ViewIntent().view, *view_intent.ViewIntent().camera);
    view_intent.SetWithAtmosphere(view_intent_->ViewIntent().with_atmosphere)
      .SetClearColor(view_intent_->ViewIntent().clear_color)
      .SetForceWireframe(view_intent_->ViewIntent().force_wireframe)
      .SetExposureSourceViewId(
        view_intent_->ViewIntent().exposure_source_view_id);
  }

  return ValidatedOffscreenSceneSession(*renderer_, *frame_session_,
    *scene_source_, std::move(view_intent), *output_target_,
    std::move(pipeline_input));
}

auto Renderer::ForOffscreenScene() -> OffscreenSceneFacade
{
  return OffscreenSceneFacade(*this);
}

auto Renderer::EnsureOffscreenFrameServicesInitialized() -> void
{
  auto gfx = gfx_weak_.lock();
  CHECK_F(gfx != nullptr,
    "Renderer::BeginOffscreenFrame requires a live Graphics backend");
  CHECK_NOTNULL_F(uploader_.get(),
    "Renderer::BeginOffscreenFrame requires UploadCoordinator");
  CHECK_NOTNULL_F(inline_transfers_.get(),
    "Renderer::BeginOffscreenFrame requires InlineTransfersCoordinator");
  CHECK_NOTNULL_F(inline_staging_provider_.get(),
    "Renderer::BeginOffscreenFrame requires inline staging provider");

  if (!scene_prep_state_) {
    LOG_F(INFO,
      "Renderer: initializing minimal ScenePrepState for offscreen frame use");
    auto light_manager
      = std::make_unique<renderer::LightManager>(observer_ptr { gfx.get() },
        observer_ptr { inline_staging_provider_.get() },
        observer_ptr { inline_transfers_.get() });
    scene_prep_state_ = std::make_unique<sceneprep::ScenePrepState>(
      nullptr, nullptr, nullptr, nullptr, std::move(light_manager));
  }

  if (!view_const_manager_) {
    LOG_F(INFO,
      "Renderer: initializing ViewConstantsManager for offscreen frame use");
    view_const_manager_ = std::make_unique<internal::ViewConstantsManager>(
      observer_ptr { gfx.get() },
      static_cast<std::uint32_t>(sizeof(ViewConstants::GpuData)));
  }

  if (!view_frame_bindings_publisher_) {
    view_frame_bindings_publisher_ = std::make_unique<
      internal::PerViewStructuredPublisher<ViewFrameBindings>>(
      observer_ptr { gfx.get() }, *inline_staging_provider_,
      observer_ptr { inline_transfers_.get() }, "ViewFrameBindings");
  }

  if (!lighting_frame_bindings_publisher_) {
    lighting_frame_bindings_publisher_ = std::make_unique<
      internal::PerViewStructuredPublisher<LightingFrameBindings>>(
      observer_ptr { gfx.get() }, *inline_staging_provider_,
      observer_ptr { inline_transfers_.get() }, "LightingFrameBindings");
  }

  if (!vsm_frame_bindings_publisher_) {
    vsm_frame_bindings_publisher_ = std::make_unique<
      internal::PerViewStructuredPublisher<VsmFrameBindings>>(
      observer_ptr { gfx.get() }, *inline_staging_provider_,
      observer_ptr { inline_transfers_.get() }, "VsmFrameBindings");
  }

  EnsureConventionalShadowDrawRecordBufferInitialized(
    observer_ptr { gfx.get() });
  EnsureShadowServicesInitialized(observer_ptr { gfx.get() });
}

auto Renderer::EnsureShadowServicesInitialized(observer_ptr<Graphics> gfx)
  -> void
{
  CHECK_NOTNULL_F(gfx.get(),
    "Renderer::EnsureShadowServicesInitialized requires a live Graphics "
    "backend");
  CHECK_NOTNULL_F(inline_transfers_.get(),
    "Renderer::EnsureShadowServicesInitialized requires "
    "InlineTransfersCoordinator");
  CHECK_NOTNULL_F(inline_staging_provider_.get(),
    "Renderer::EnsureShadowServicesInitialized requires inline staging "
    "provider");

  if (!shadow_manager_) {
    shadow_manager_
      = std::make_unique<renderer::ShadowManager>(observer_ptr { gfx.get() },
        observer_ptr { inline_staging_provider_.get() },
        observer_ptr { inline_transfers_.get() }, config_.shadow_quality_tier,
        config_.directional_shadow_policy);
  }

  if (!shadow_frame_bindings_publisher_) {
    shadow_frame_bindings_publisher_ = std::make_unique<
      internal::PerViewStructuredPublisher<ShadowFrameBindings>>(
      observer_ptr { gfx.get() }, *inline_staging_provider_,
      observer_ptr { inline_transfers_.get() }, "ShadowFrameBindings");
  }
}

auto Renderer::EnsureConventionalShadowDrawRecordBufferInitialized(
  observer_ptr<Graphics> gfx) -> void
{
  CHECK_NOTNULL_F(gfx.get(),
    "Renderer::EnsureConventionalShadowDrawRecordBufferInitialized requires a "
    "live Graphics backend");
  CHECK_NOTNULL_F(inline_transfers_.get(),
    "Renderer::EnsureConventionalShadowDrawRecordBufferInitialized requires "
    "InlineTransfersCoordinator");
  CHECK_NOTNULL_F(inline_staging_provider_.get(),
    "Renderer::EnsureConventionalShadowDrawRecordBufferInitialized requires "
    "inline staging provider");

  if (!conventional_shadow_draw_record_buffer_) {
    conventional_shadow_draw_record_buffer_
      = std::make_unique<upload::TransientStructuredBuffer>(
        observer_ptr { gfx.get() }, *inline_staging_provider_,
        static_cast<std::uint32_t>(
          sizeof(renderer::ConventionalShadowDrawRecord)),
        observer_ptr { inline_transfers_.get() },
        "ConventionalShadowDrawRecords");
  }
}

auto Renderer::BeginFrameServices(const frame::Slot frame_slot,
  const frame::SequenceNumber frame_sequence) -> void
{
  CHECK_F(frame_slot != frame::kInvalidSlot,
    "Renderer::BeginFrameServices requires a valid frame slot");

  auto tag = oxygen::renderer::internal::RendererTagFactory::Get();
  frame_slot_ = frame_slot;
  frame_seq_num = frame_sequence;

  if (gpu_timeline_profiler_) {
    gpu_timeline_profiler_->OnFrameStart(frame_sequence);
  }

  inline_transfers_->OnFrameStart(tag, frame_slot);
  uploader_->OnFrameStart(tag, frame_slot);
  if (texture_binder_) {
    texture_binder_->OnFrameStart();
  }
  if (view_const_manager_) {
    view_const_manager_->OnFrameStart(frame_slot);
  }
  if (view_frame_bindings_publisher_) {
    view_frame_bindings_publisher_->OnFrameStart(frame_sequence, frame_slot);
  }
  if (draw_frame_bindings_publisher_) {
    draw_frame_bindings_publisher_->OnFrameStart(frame_sequence, frame_slot);
  }
  if (view_color_data_publisher_) {
    view_color_data_publisher_->OnFrameStart(frame_sequence, frame_slot);
  }
  if (debug_frame_bindings_publisher_) {
    debug_frame_bindings_publisher_->OnFrameStart(frame_sequence, frame_slot);
  }
  if (conventional_shadow_draw_record_buffer_) {
    conventional_shadow_draw_record_buffer_->OnFrameStart(
      frame_sequence, frame_slot);
  }
  if (lighting_frame_bindings_publisher_) {
    lighting_frame_bindings_publisher_->OnFrameStart(
      frame_sequence, frame_slot);
  }
  if (vsm_frame_bindings_publisher_) {
    vsm_frame_bindings_publisher_->OnFrameStart(frame_sequence, frame_slot);
  }
  if (shadow_manager_) {
    shadow_manager_->OnFrameStart(tag, frame_sequence, frame_slot);
  }
  if (shadow_frame_bindings_publisher_) {
    shadow_frame_bindings_publisher_->OnFrameStart(frame_sequence, frame_slot);
  }
  if (environment_view_data_publisher_) {
    environment_view_data_publisher_->OnFrameStart(frame_sequence, frame_slot);
  }
  if (environment_frame_bindings_publisher_) {
    environment_frame_bindings_publisher_->OnFrameStart(
      frame_sequence, frame_slot);
  }
  if (env_static_manager_) {
    env_static_manager_->OnFrameStart(tag, frame_slot);
    env_static_manager_->SetBlueNoiseEnabled(atmosphere_blue_noise_enabled_);
  }

  if (scene_prep_state_ != nullptr) {
    if (const auto transforms = scene_prep_state_->GetTransformUploader()) {
      transforms->OnFrameStart(tag, frame_sequence, frame_slot);
    }
    if (const auto geometry = scene_prep_state_->GetGeometryUploader()) {
      geometry->OnFrameStart(tag, frame_slot);
    }
    if (const auto materials = scene_prep_state_->GetMaterialBinder()) {
      materials->OnFrameStart(tag, frame_slot);
    }
    if (const auto emitter = scene_prep_state_->GetDrawMetadataEmitter()) {
      emitter->OnFrameStart(tag, frame_sequence, frame_slot);
    }
    if (const auto light_manager = scene_prep_state_->GetLightManager()) {
      light_manager->OnFrameStart(tag, frame_sequence, frame_slot);
    }
  }
}

auto Renderer::EndOffscreenFrame() noexcept -> void
{
  if (auto gfx = gfx_weak_.lock();
    gfx != nullptr && frame_slot_ != frame::kInvalidSlot) {
    gfx->EndFrame(frame_seq_num, frame_slot_);
  }
  offscreen_frame_active_ = false;
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
  CHECK_F(!offscreen_frame_used_,
    "Renderer::OnAttached cannot attach a renderer after it has already been "
    "used for offscreen rendering");
  engine_ = engine;

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
    EnsureShadowServicesInitialized(observer_ptr { gfx.get() });

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
    EnsureConventionalShadowDrawRecordBufferInitialized(
      observer_ptr { gfx.get() });
    lighting_frame_bindings_publisher_ = std::make_unique<
      internal::PerViewStructuredPublisher<LightingFrameBindings>>(
      observer_ptr { gfx.get() }, *inline_staging_provider_,
      observer_ptr { inline_transfers_.get() }, "LightingFrameBindings");
    vsm_frame_bindings_publisher_ = std::make_unique<
      internal::PerViewStructuredPublisher<VsmFrameBindings>>(
      observer_ptr { gfx.get() }, *inline_staging_provider_,
      observer_ptr { inline_transfers_.get() }, "VsmFrameBindings");
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
    gpu_timeline_profiler_ = std::make_unique<internal::GpuTimelineProfiler>(
      observer_ptr { gfx.get() });
  }

  if (!gpu_timeline_panel_ && gpu_timeline_profiler_) {
    gpu_timeline_panel_ = std::make_unique<engine::imgui::GpuTimelinePanel>(
      observer_ptr { gpu_timeline_profiler_.get() });
  }

  imgui_module_subscription_ = engine_->SubscribeModuleAttached(
    [this](const engine::ModuleEvent& event) {
      if (event.type_id == engine::imgui::ImGuiModule::ClassTypeId()
        && event.module != nullptr) {
        AttachGpuTimelinePanelDrawer(
          static_cast<engine::imgui::ImGuiModule&>(*event.module));
      }
    },
    true);

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
    .name = std::string(kCVarRendererGpuTimestamps),
    .help = "Enable GPU timestamp profiling",
    .default_value = false,
    .flags = console::CVarFlags::kArchive,
    .min_value = std::nullopt,
    .max_value = std::nullopt,
  });

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarRendererGpuTimestampMaxScopes),
    .help = "Maximum GPU timestamp scopes recorded per frame",
    .default_value = kDefaultGpuTimestampMaxScopes,
    .flags = console::CVarFlags::kArchive,
    .min_value = static_cast<double>(kMinGpuTimestampMaxScopes),
    .max_value = static_cast<double>(kMaxGpuTimestampMaxScopes),
  });

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarRendererGpuTimestampExportNextFrame),
    .help = "One-shot GPU timestamp export path (.json or .csv)",
    .default_value = std::string {},
    .flags = console::CVarFlags::kDevOnly,
    .min_value = std::nullopt,
    .max_value = std::nullopt,
  });

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarRendererGpuTimestampViewer),
    .help = "Show the GPU timestamp viewer in ImGui",
    .default_value = false,
    .flags = console::CVarFlags::kArchive,
    .min_value = std::nullopt,
    .max_value = std::nullopt,
  });

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarRendererShadowCsmDistanceScale),
    .help = "Runtime scale applied to authored classic CSM max distance",
    .default_value = kDefaultShadowCsmDistanceScale,
    .flags = console::CVarFlags::kArchive,
    .min_value = 0.0,
    .max_value = 2.0,
  });

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarRendererShadowCsmTransitionScale),
    .help = "Runtime scale applied to authored classic CSM transition widths",
    .default_value = kDefaultShadowCsmTransitionScale,
    .flags = console::CVarFlags::kArchive,
    .min_value = 0.0,
    .max_value = 2.0,
  });

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarRendererShadowCsmMaxCascades),
    .help = "Runtime cap for active classic CSM cascade count",
    .default_value = kDefaultShadowCsmMaxCascades,
    .flags = console::CVarFlags::kArchive,
    .min_value = 1.0,
    .max_value = 4.0,
  });

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarRendererShadowCsmMaxResolution),
    .help
    = "Optional hard ceiling for classic CSM raster resolution (0 disables)",
    .default_value = kDefaultShadowCsmMaxResolution,
    .flags = console::CVarFlags::kArchive,
    .min_value = 0.0,
    .max_value = 4096.0,
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

  bool gpu_timestamps_enabled = false;
  if (console->TryGetCVarValue<bool>(
        kCVarRendererGpuTimestamps, gpu_timestamps_enabled)
    && gpu_timeline_profiler_) {
    gpu_timeline_profiler_->SetEnabled(gpu_timestamps_enabled);
  }

  int64_t gpu_timestamp_max_scopes = kDefaultGpuTimestampMaxScopes;
  if (console->TryGetCVarValue<int64_t>(
        kCVarRendererGpuTimestampMaxScopes, gpu_timestamp_max_scopes)
    && gpu_timeline_profiler_) {
    gpu_timeline_profiler_->SetMaxScopesPerFrame(
      static_cast<uint32_t>(gpu_timestamp_max_scopes));
  }

  std::string gpu_timestamp_export_path;
  if (console->TryGetCVarValue<std::string>(
        kCVarRendererGpuTimestampExportNextFrame, gpu_timestamp_export_path)
    && !gpu_timestamp_export_path.empty() && gpu_timeline_profiler_) {
    gpu_timeline_profiler_->RequestOneShotExport(gpu_timestamp_export_path);
    if (console_ != nullptr) {
      (void)console_->SetCVarFromText(
        { .name = std::string(kCVarRendererGpuTimestampExportNextFrame),
          .text = {} },
        { .source = console::CommandSource::kAutomation,
          .shipping_build = false,
          .record_history = false });
    }
  }

  bool gpu_timestamp_viewer_enabled = false;
  if (console->TryGetCVarValue<bool>(
        kCVarRendererGpuTimestampViewer, gpu_timestamp_viewer_enabled)
    && gpu_timeline_profiler_ && gpu_timeline_panel_) {
    gpu_timeline_panel_->SetVisible(gpu_timestamp_viewer_enabled);
    gpu_timeline_profiler_->SetRetainLatestFrame(gpu_timestamp_viewer_enabled);
  }

  renderer::DirectionalCsmRuntimeSettings csm_runtime_settings {};
  double distance_scale = kDefaultShadowCsmDistanceScale;
  if (console->TryGetCVarValue<double>(
        kCVarRendererShadowCsmDistanceScale, distance_scale)) {
    csm_runtime_settings.distance_scale = static_cast<float>(distance_scale);
  }

  double transition_scale = kDefaultShadowCsmTransitionScale;
  if (console->TryGetCVarValue<double>(
        kCVarRendererShadowCsmTransitionScale, transition_scale)) {
    csm_runtime_settings.transition_scale
      = static_cast<float>(transition_scale);
  }

  int64_t max_cascades = kDefaultShadowCsmMaxCascades;
  if (console->TryGetCVarValue<int64_t>(
        kCVarRendererShadowCsmMaxCascades, max_cascades)) {
    csm_runtime_settings.max_cascades
      = static_cast<std::uint32_t>(std::max<int64_t>(1, max_cascades));
  }

  int64_t max_resolution = kDefaultShadowCsmMaxResolution;
  if (console->TryGetCVarValue<int64_t>(
        kCVarRendererShadowCsmMaxResolution, max_resolution)) {
    csm_runtime_settings.max_resolution
      = max_resolution <= 0 ? 0U : static_cast<std::uint32_t>(max_resolution);
  }

  if (shadow_manager_) {
    shadow_manager_->SetDirectionalCsmRuntimeSettings(csm_runtime_settings);
  }
}

auto Renderer::OnShutdown() noexcept -> void
{
  DetachGpuTimelinePanelDrawer();
  imgui_module_subscription_.Cancel();

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
  gpu_timeline_panel_.reset();
  engine_ = nullptr;

  {
    std::unique_lock registration_lock(view_registration_mutex_);
    render_graphs_.clear();
  }

  {
    std::unique_lock state_lock(view_state_mutex_);
    view_ready_states_.clear();
    published_runtime_views_by_intent_.clear();
  }

  {
    std::lock_guard lock(composition_mutex_);
    pending_compositions_.clear();
    next_composition_sequence_in_frame_ = 0;
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
  gpu_timeline_profiler_.reset();
  scene_prep_state_.reset();
  env_static_manager_.reset();
  ibl_manager_.reset();
  brdf_lut_manager_.reset();

  view_frame_bindings_publisher_.reset();
  draw_frame_bindings_publisher_.reset();
  view_color_data_publisher_.reset();
  debug_frame_bindings_publisher_.reset();
  conventional_shadow_draw_record_buffer_.reset();
  lighting_frame_bindings_publisher_.reset();
  vsm_frame_bindings_publisher_.reset();
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
  offscreen_frame_active_ = false;

  if (auto gfx = gfx_weak_.lock()) {
    gfx->GetDeferredReclaimer().OnRendererShutdown();
  }
}

auto Renderer::AttachGpuTimelinePanelDrawer(
  engine::imgui::ImGuiModule& imgui_module) -> void
{
  if (!gpu_timeline_panel_ || gpu_timeline_panel_drawer_token_ != 0U) {
    return;
  }

  gpu_timeline_panel_drawer_token_
    = imgui_module.RegisterOverlayDrawer("Renderer.GpuTimeline", [this] {
        if (gpu_timeline_panel_) {
          gpu_timeline_panel_->Draw();
        }
      });
}

auto Renderer::DetachGpuTimelinePanelDrawer() -> void
{
  if (gpu_timeline_panel_drawer_token_ == 0U || engine_ == nullptr) {
    gpu_timeline_panel_drawer_token_ = 0U;
    return;
  }

  if (auto imgui_module = engine_->GetModule<engine::imgui::ImGuiModule>()) {
    imgui_module->get().UnregisterOverlayDrawer(
      gpu_timeline_panel_drawer_token_);
  }
  gpu_timeline_panel_drawer_token_ = 0U;
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

auto Renderer::GetConventionalShadowDepthTexture() const noexcept
  -> std::shared_ptr<graphics::Texture>
{
  if (!shadow_manager_) {
    return {};
  }
  return shadow_manager_->GetConventionalShadowDepthTexture();
}

auto Renderer::ExecuteCurrentViewVirtualShadowShell(
  const RenderContext& render_context, graphics::CommandRecorder& recorder,
  const observer_ptr<const graphics::Texture> scene_depth_texture) -> co::Co<>
{
  const auto clear_virtual_shadow_bindings = [&]() {
    UpdateCurrentViewDynamicBindings(render_context,
      CurrentViewDynamicBindingsUpdate {
        .virtual_shadow = VsmFrameBindings {},
      });
  };

  if (!shadow_manager_) {
    clear_virtual_shadow_bindings();
    co_return;
  }

  const auto vsm_shadow_renderer = shadow_manager_->GetVirtualShadowRenderer();
  if (!vsm_shadow_renderer) {
    clear_virtual_shadow_bindings();
    co_return;
  }

  co_await vsm_shadow_renderer->ExecutePreparedViewShell(
    render_context, recorder, scene_depth_texture);
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

auto Renderer::MakeGpuEventScopeOptions() const
  -> graphics::GpuEventScopeOptions
{
  if (!gpu_timeline_profiler_) {
    return {};
  }
  return gpu_timeline_profiler_->MakeScopeOptions();
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

auto Renderer::UpsertPublishedRuntimeView(FrameContext& frame_context,
  const ViewId intent_view_id, ViewContext view) -> ViewId
{
  CHECK_F(intent_view_id != kInvalidViewId,
    "Renderer::UpsertPublishedRuntimeView requires a valid intent view id");

  std::unique_lock state_lock(view_state_mutex_);
  if (const auto it = published_runtime_views_by_intent_.find(intent_view_id);
    it != published_runtime_views_by_intent_.end()) {
    frame_context.UpdateView(it->second.published_view_id, std::move(view));
    it->second.last_seen_frame = frame_context.GetFrameSequenceNumber();
    return it->second.published_view_id;
  }

  const auto published_view_id = frame_context.RegisterView(std::move(view));
  published_runtime_views_by_intent_.insert_or_assign(intent_view_id,
    PublishedRuntimeViewState {
      .published_view_id = published_view_id,
      .last_seen_frame = frame_context.GetFrameSequenceNumber(),
    });
  return published_view_id;
}

auto Renderer::ResolvePublishedRuntimeViewId(
  const ViewId intent_view_id) const noexcept -> ViewId
{
  if (intent_view_id == kInvalidViewId) {
    return kInvalidViewId;
  }

  std::shared_lock state_lock(view_state_mutex_);
  if (const auto it = published_runtime_views_by_intent_.find(intent_view_id);
    it != published_runtime_views_by_intent_.end()) {
    return it->second.published_view_id;
  }
  return kInvalidViewId;
}

auto Renderer::RemovePublishedRuntimeView(
  FrameContext& frame_context, const ViewId intent_view_id) -> void
{
  if (intent_view_id == kInvalidViewId) {
    return;
  }

  ViewId published_view_id { kInvalidViewId };
  {
    std::unique_lock state_lock(view_state_mutex_);
    if (const auto it = published_runtime_views_by_intent_.find(intent_view_id);
      it != published_runtime_views_by_intent_.end()) {
      published_view_id = it->second.published_view_id;
      published_runtime_views_by_intent_.erase(it);
    }
  }

  if (published_view_id == kInvalidViewId) {
    return;
  }

  frame_context.RemoveView(published_view_id);
  UnregisterViewRenderGraph(published_view_id);
}

auto Renderer::PruneStalePublishedRuntimeViews(FrameContext& frame_context)
  -> std::vector<ViewId>
{
  const auto current_frame = frame_context.GetFrameSequenceNumber();
  auto stale_intent_ids = std::vector<ViewId> {};
  auto stale_published_view_ids = std::vector<ViewId> {};

  {
    std::unique_lock state_lock(view_state_mutex_);
    for (auto it = published_runtime_views_by_intent_.begin();
      it != published_runtime_views_by_intent_.end();) {
      if (current_frame - it->second.last_seen_frame
        > kPublishedRuntimeViewMaxIdleFrames) {
        stale_intent_ids.push_back(it->first);
        stale_published_view_ids.push_back(it->second.published_view_id);
        it = published_runtime_views_by_intent_.erase(it);
      } else {
        ++it;
      }
    }
  }

  for (const auto published_view_id : stale_published_view_ids) {
    frame_context.RemoveView(published_view_id);
    UnregisterViewRenderGraph(published_view_id);
    EvictPerViewCachedProducts(published_view_id);
  }

  return stale_intent_ids;
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
    for (auto it = published_runtime_views_by_intent_.begin();
      it != published_runtime_views_by_intent_.end();) {
      if (it->second.published_view_id == view_id) {
        it = published_runtime_views_by_intent_.erase(it);
      } else {
        ++it;
      }
    }
  }
}

auto Renderer::RegisterComposition(CompositionSubmission submission,
  std::shared_ptr<graphics::Surface> target_surface) -> void
{
  std::lock_guard lock(composition_mutex_);
  if (!pending_compositions_.empty()) {
    const auto& anchor = pending_compositions_.front();
    const bool same_composite_target = anchor.submission.composite_target.get()
      == submission.composite_target.get();
    const bool same_target_surface
      = anchor.target_surface.get() == target_surface.get();
    CHECK_F(same_composite_target && same_target_surface,
      "Phase-1 composition queue requires a single target per frame");
  }
  pending_compositions_.push_back(PendingComposition {
    .submission = std::move(submission),
    .target_surface = std::move(target_surface),
    .sequence_in_frame = next_composition_sequence_in_frame_++,
  });
}

auto Renderer::BeginOffscreenFrame(OffscreenFrameConfig config)
  -> OffscreenFrameSession
{
  CHECK_F(!offscreen_frame_active_,
    "Renderer::BeginOffscreenFrame does not support nested offscreen frames");
  CHECK_F(render_context_ == nullptr,
    "Renderer::BeginOffscreenFrame cannot run while the main renderer frame "
    "context is active");
  CHECK_F(config.frame_slot != frame::kInvalidSlot,
    "Renderer::BeginOffscreenFrame requires a valid frame slot");
  CHECK_F(std::isfinite(config.delta_time_seconds)
      && config.delta_time_seconds > 0.0F,
    "Renderer::BeginOffscreenFrame requires a finite positive delta time");

  EnsureOffscreenFrameServicesInitialized();

  resolved_views_.clear();
  per_view_runtime_state_.clear();
  {
    std::lock_guard lock(composition_mutex_);
    pending_compositions_.clear();
    next_composition_sequence_in_frame_ = 0;
  }

  last_frame_dt_seconds_ = config.delta_time_seconds;
  frame_budget_stats_.cpu_budget = std::chrono::milliseconds(16);
  frame_budget_stats_.gpu_budget = std::chrono::milliseconds(16);

  const auto current_scene = config.scene.get();
  if (current_scene != last_scene_identity_) {
    LOG_F(INFO,
      "Renderer: offscreen scene identity changed (old={} new={}); resetting "
      "shadow cache state",
      fmt::ptr(last_scene_identity_), fmt::ptr(current_scene));
    last_scene_identity_ = current_scene;
    if (shadow_manager_) {
      shadow_manager_->ResetCachedState();
    }
  }

  auto gfx = gfx_weak_.lock();
  CHECK_F(gfx != nullptr,
    "Renderer::BeginOffscreenFrame requires a live Graphics backend");

  // Offscreen frames must advance the graphics frame lifecycle too. Without
  // this, readback retirement and deferred reclamation never move forward for
  // explicit test/tool frames, which can turn timing into hangs.
  gfx->BeginFrame(config.frame_sequence, config.frame_slot);
  BeginFrameServices(config.frame_slot, config.frame_sequence);
  offscreen_frame_used_ = true;
  offscreen_frame_active_ = true;
  return OffscreenFrameSession(*this, config);
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

  render_context_->scene = observer_ptr<scene::Scene> {
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

  if (shadow_manager_ && scene_prep_state_ != nullptr) {
    if (const auto light_manager = scene_prep_state_->GetLightManager()) {
      std::uint32_t active_scene_view_count = 0U;
      for (const auto& [view_id, _] : graphs_snapshot) {
        const auto& view_ctx = context->GetViewContext(view_id);
        if (view_ctx.metadata.is_scene_view
          && resolved_views_.contains(view_id)) {
          ++active_scene_view_count;
        }
      }
      shadow_manager_->ReserveFrameResources(
        active_scene_view_count, *light_manager);
    }
  }

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

      // Pass instances live on the pipeline, but pass registrations are
      // per-view state and must not leak across view graph executions.
      render_context_->ClearRegisteredPasses();

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
      const auto scope_options = MakeGpuEventScopeOptions();
      const auto view_scope_name = view_ctx.metadata.name.empty()
        ? fmt::format("View {}", view_id.get())
        : fmt::format("View: {}", view_ctx.metadata.name);

      graphics::GpuEventScope view_scope(
        *recorder, view_scope_name, scope_options);

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
        render_context_->current_view.exposure_view_id
          = view_ctx.metadata.exposure_view_id != kInvalidViewId
          ? view_ctx.metadata.exposure_view_id
          : view_id;
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
            auto marker_scope_options = scope_options;
            marker_scope_options.timestamp_enabled = false;
            graphics::GpuEventScope lut_scope(
              *recorder, "Atmosphere LUT Compute", marker_scope_options);
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
            auto marker_scope_options = scope_options;
            marker_scope_options.timestamp_enabled = false;
            graphics::GpuEventScope capture_scope(
              *recorder, "Sky Capture", marker_scope_options);
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
          auto marker_scope_options = scope_options;
          marker_scope_options.timestamp_enabled = false;
          graphics::GpuEventScope ibl_scope(
            *recorder, "IBL Compute", marker_scope_options);
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
      if (is_scene_view
        && !RepublishCurrentViewBindings(
          *render_context_, ViewBindingRepublishMode::kDynamicSystemBindings)) {
        LOG_F(ERROR,
          "Failed to republish scene depth/view bindings for view {}; "
          "skipping",
          view_id.get());
        update_view_state(view_id, false);
        record_view_timing();
        continue;
      }

      // --- STEP 4: Execute RenderGraph ---
      const auto render_graph_begin = std::chrono::steady_clock::now();
      graphics::GpuEventScope graph_scope(
        *recorder, "RenderGraph", scope_options);
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
  auto finalize_gpu_timestamps = [this]() -> void {
    if (gpu_timeline_profiler_) {
      gpu_timeline_profiler_->OnFrameRecordTailResolve();
    }
  };
  std::vector<PendingComposition> submissions;
  {
    std::lock_guard lock(composition_mutex_);
    if (pending_compositions_.empty()) {
      compositing_last_frame_ns_ = 0;
      finalize_gpu_timestamps();
      co_return;
    }
    submissions = std::move(pending_compositions_);
    pending_compositions_.clear();
  }

  const auto& anchor = submissions.front();
  for (const auto& pending : submissions) {
    const bool same_composite_target = anchor.submission.composite_target.get()
      == pending.submission.composite_target.get();
    const bool same_target_surface
      = anchor.target_surface.get() == pending.target_surface.get();
    CHECK_F(same_composite_target && same_target_surface,
      "Phase-1 composition queue requires a single target per frame");
  }

  std::ranges::stable_sort(submissions,
    [](const PendingComposition& lhs, const PendingComposition& rhs) {
      return lhs.sequence_in_frame < rhs.sequence_in_frame;
    });
  auto gfx = GetGraphics();
  CHECK_F(static_cast<bool>(gfx), "Graphics required for compositing");

  if (!compositing_pass_) {
    compositing_pass_config_ = std::make_shared<CompositingPassConfig>();
    compositing_pass_config_->debug_name = "CompositingPass";
    compositing_pass_
      = std::make_shared<CompositingPass>(compositing_pass_config_);
  }

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  bool had_tasks = false;
  for (const auto& pending : submissions) {
    auto payload = pending.submission;
    if (payload.tasks.empty()) {
      continue;
    }
    had_tasks = true;

    CHECK_F(static_cast<bool>(payload.composite_target),
      "Compositing requires a target framebuffer");

    auto recorder_ptr = gfx->AcquireCommandRecorder(queue_key,
      fmt::format("Renderer Compositing {}", pending.sequence_in_frame));
    CHECK_F(static_cast<bool>(recorder_ptr),
      "Compositing recorder acquisition failed");
    if (gpu_timeline_profiler_) {
      recorder_ptr->SetProfileScopeHandler(
        observer_ptr<graphics::IGpuProfileScopeHandler>(
          gpu_timeline_profiler_.get()));
    }
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
      "Log compositing target ptr={} size={}x{} fmt={} samples={} name={} "
      "queue_sequence={}",
      fmt::ptr(&backbuffer), backbuffer.GetDescriptor().width,
      backbuffer.GetDescriptor().height, backbuffer.GetDescriptor().format,
      backbuffer.GetDescriptor().sample_count,
      backbuffer.GetDescriptor().debug_name, pending.sequence_in_frame);

    RenderContext comp_context {};
    comp_context.SetRenderer(this, gfx.get());
    comp_context.pass_target = observer_ptr { payload.composite_target.get() };
    comp_context.frame_slot = frame_slot_;
    comp_context.frame_sequence = frame_seq_num;

    for (const auto& task : payload.tasks) {
      const auto scope_options = MakeGpuEventScopeOptions();
      const auto task_scope_label = FormatCompositingTaskScopeLabel(task);
      graphics::GpuEventScope task_scope(
        recorder, task_scope_label, scope_options);

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
        TrackCompositionSourceTexture(
          gfx->GetResourceRegistry(), recorder, *source);
        if (source->GetDescriptor().format
          != backbuffer.GetDescriptor().format) {
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
          DLOG_F(1, "Skip blend: view {} not ready",
            task.blend.source_view_id.get());
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

        TrackCompositionSourceTexture(
          gfx->GetResourceRegistry(), recorder, *source);
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

        TrackCompositionSourceTexture(gfx->GetResourceRegistry(), recorder,
          *task.texture_blend.source_texture);
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

    if (pending.target_surface) {
      const auto surfaces = context->GetSurfaces();
      for (size_t i = 0; i < surfaces.size(); ++i) {
        if (surfaces[i].get() == pending.target_surface.get()) {
          context->SetSurfacePresentable(i, true);
          break;
        }
      }
    }
  }

  if (!had_tasks) {
    compositing_last_frame_ns_ = 0;
    finalize_gpu_timestamps();
    co_return;
  }

  const auto compositing_end = std::chrono::steady_clock::now();
  const auto compositing_ns = static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      compositing_end - compositing_begin)
      .count());
  ++compositing_profile_frames_;
  compositing_profile_total_ns_ += compositing_ns;
  compositing_last_frame_ns_ = compositing_ns;

  finalize_gpu_timestamps();
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
  auto recorder = gfx.AcquireCommandRecorder(
    queue_key, std::string("View_") + std::to_string(view_id.get()));
  if (recorder && gpu_timeline_profiler_) {
    recorder->SetProfileScopeHandler(
      observer_ptr<graphics::IGpuProfileScopeHandler>(
        gpu_timeline_profiler_.get()));
  }
  return std::shared_ptr<oxygen::graphics::CommandRecorder>(
    std::move(recorder));
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

  const auto& view_ctx = frame_context.GetViewContext(view_id);
  // Populate render_context.current_view before EnvStatic update.
  render_context.current_view.view_id = view_id;
  render_context.current_view.exposure_view_id
    = view_ctx.metadata.exposure_view_id != kInvalidViewId
    ? view_ctx.metadata.exposure_view_id
    : view_id;
  render_context.current_view.resolved_view.reset(&resolved_it->second);
  render_context.current_view.prepared_frame.reset(&prepared_it->second);
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

auto Renderer::RepublishCurrentViewBindings(const RenderContext& render_context,
  const ViewBindingRepublishMode mode) -> bool
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
  const bool dynamic_system_bindings_only
    = mode == ViewBindingRepublishMode::kDynamicSystemBindings;
  const bool can_reuse_cached_view_bindings
    = dynamic_system_bindings_only && runtime_state.has_published_view_bindings;

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

    if (!PublishBaselineViewBindings(view_id, render_context, prepared,
          runtime_state, can_reuse_cached_view_bindings, view_bindings,
          view_constants)) {
      return false;
    }

    PublishOptionalFamilyViewBindings(view_id, render_context, resolved,
      prepared, environment_static_slot, runtime_state,
      can_reuse_cached_view_bindings, view_bindings, view_constants);

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
      "view_frame_slot={} shadow_frame_slot={} lighting_frame_slot={} "
      "virtual_shadow_frame_slot={} directional_shadow_mask_slot={} "
      "screen_shadow_mask_slot={}",
      render_context.frame_sequence.get(), view_id.get(),
      dynamic_system_bindings_only ? "dynamic-system-bindings" : "full",
      can_reuse_cached_view_bindings, view_bindings_slot,
      view_bindings.shadow_frame_slot.get(),
      view_bindings.lighting_frame_slot.get(),
      view_bindings.virtual_shadow_frame_slot.get(),
      runtime_state.virtual_shadow.directional_shadow_mask_slot.get(),
      runtime_state.virtual_shadow.screen_shadow_mask_slot.get());
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

auto Renderer::PublishBaselineViewBindings(const ViewId view_id,
  const RenderContext& render_context, const PreparedSceneFrame& prepared,
  PerViewRuntimeState& runtime_state, const bool can_reuse_cached_view_bindings,
  ViewFrameBindings& view_bindings, ViewConstants& view_constants) -> bool
{
  if (const auto* pass_target = render_context.pass_target.get();
    pass_target != nullptr
    && pass_target->GetDescriptor().depth_attachment.texture != nullptr) {
    view_bindings.scene_depth_slot = EnsureSceneDepthTextureSrv(
      runtime_state, *pass_target->GetDescriptor().depth_attachment.texture);
  } else {
    runtime_state.scene_depth_srv = kInvalidShaderVisibleIndex;
    runtime_state.scene_depth_texture_owner = nullptr;
    runtime_state.owns_scene_depth_srv = false;
    view_bindings.scene_depth_slot = kInvalidShaderVisibleIndex;
  }

  if (!can_reuse_cached_view_bindings && draw_frame_bindings_publisher_) {
    DLOG_F(3, "   worlds: {}", prepared.bindless_worlds_slot);
    DLOG_F(3, "  normals: {}", prepared.bindless_normals_slot);
    DLOG_F(3, "material shading: {}", prepared.bindless_material_shading_slot);
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

  if (!can_reuse_cached_view_bindings) {
    const auto provisional_view_bindings_slot
      = view_frame_bindings_publisher_->Publish(view_id, view_bindings);
    if (provisional_view_bindings_slot == kInvalidShaderVisibleIndex) {
      LOG_F(ERROR,
        "Renderer: failed to publish provisional view bindings for view {}",
        view_id.get());
      return false;
    }
    view_constants.SetBindlessViewFrameBindingsSlot(
      BindlessViewFrameBindingsSlot(provisional_view_bindings_slot),
      ViewConstants::kRenderer);
  }

  return true;
}

auto Renderer::PublishOptionalFamilyViewBindings(const ViewId view_id,
  const RenderContext& render_context, const ResolvedView& resolved,
  const PreparedSceneFrame& prepared,
  const ShaderVisibleIndex environment_static_slot,
  PerViewRuntimeState& runtime_state, const bool can_reuse_cached_view_bindings,
  ViewFrameBindings& view_bindings, ViewConstants& view_constants) -> void
{
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
      = lighting_frame_bindings_publisher_->Publish(view_id, lighting_bindings);
    LOG_F(INFO,
      "Renderer: frame={} view={} lighting publication dir_slot={} "
      "pos_slot={} "
      "lighting_frame_slot={} sun_enabled={} sun_illuminance={:.3f} "
      "sun_dir=({:.6f}, {:.6f}, {:.6f})",
      render_context.frame_sequence.get(), nostd::to_string(view_id),
      directional_lights_slot, positional_lights_slot,
      view_bindings.lighting_frame_slot, lighting_bindings.sun.enabled,
      lighting_bindings.sun.direction_ws_illuminance.w,
      lighting_bindings.sun.direction_ws_illuminance.x,
      lighting_bindings.sun.direction_ws_illuminance.y,
      lighting_bindings.sun.direction_ws_illuminance.z);
  }

  if (vsm_frame_bindings_publisher_) {
    view_bindings.virtual_shadow_frame_slot
      = vsm_frame_bindings_publisher_->Publish(
        view_id, runtime_state.virtual_shadow);
  }

  if (!can_reuse_cached_view_bindings && shadow_frame_bindings_publisher_
    && shadow_manager_) {
    auto shadow_instance_metadata_slot = kInvalidShaderVisibleIndex;
    auto directional_shadow_metadata_slot = kInvalidShaderVisibleIndex;
    auto sun_shadow_index = 0xFFFFFFFFU;
    if (const auto light_manager = scene_prep_state_->GetLightManager()) {
      const auto prepared_it = prepared_frames_.find(view_id);
      const auto rendered_items = prepared_it != prepared_frames_.end()
        ? prepared_it->second.render_items
        : std::span<const sceneprep::RenderItemData> {};
      const auto shadow_caster_bounds = prepared_it != prepared_frames_.end()
        ? prepared_it->second.shadow_caster_bounding_spheres
        : std::span<const glm::vec4> {};
      const auto visible_receiver_bounds = prepared_it != prepared_frames_.end()
        ? prepared_it->second.visible_receiver_bounding_spheres
        : std::span<const glm::vec4> {};
      auto shadow_caster_content_hash
        = HashPreparedShadowCasterContent(prepared);
      const auto alpha_test_material_state
        = HashShadowCasterMaterialState(rendered_items, texture_binder_.get());
      if (alpha_test_material_state.hash != 0U) {
        shadow_caster_content_hash = HashBytes(&alpha_test_material_state.hash,
          sizeof(alpha_test_material_state.hash), shadow_caster_content_hash);
      }
      LOG_F(INFO,
        "Renderer: view={} shadow content hash={} "
        "alpha_test_shadow_materials={} "
        "pending_alpha_test_shadow_materials={} "
        "alpha_test_domain_mismatches={}",
        view_id.get(), shadow_caster_content_hash,
        alpha_test_material_state.alpha_test_material_count,
        alpha_test_material_state.pending_alpha_test_material_count,
        alpha_test_material_state.alpha_test_domain_mismatch_count);
      const auto shadow_view = shadow_manager_->PublishForView(view_id,
        view_constants, *light_manager, render_context.GetSceneMutable(),
        std::max(1.0F, resolved.Viewport().width), rendered_items,
        shadow_caster_bounds, visible_receiver_bounds,
        frame_budget_stats_.gpu_budget, shadow_caster_content_hash);
      shadow_instance_metadata_slot = shadow_view.shadow_instance_metadata_srv;
      directional_shadow_metadata_slot
        = shadow_view.directional_shadow_metadata_srv;
      const auto directional_shadow_texture_slot
        = shadow_view.directional_shadow_texture_srv;
      sun_shadow_index = shadow_view.sun_shadow_index;

      const auto* shadow_instance = shadow_manager_ != nullptr
        ? shadow_manager_->TryGetShadowInstanceMetadata(view_id)
        : nullptr;

      const ShadowFrameBindings shadow_bindings {
        .shadow_instance_metadata_slot = shadow_instance_metadata_slot,
        .directional_shadow_metadata_slot = directional_shadow_metadata_slot,
        .directional_shadow_texture_slot = directional_shadow_texture_slot,
        .sun_shadow_index = sun_shadow_index,
      };
      LOG_F(INFO,
        "Renderer: frame={} view={} shadow publication shadow_meta={} "
        "dir_meta={} dir_tex={} sun_shadow_index={}",
        render_context.frame_sequence.get(), view_id.get(),
        shadow_instance_metadata_slot.get(),
        directional_shadow_metadata_slot.get(),
        directional_shadow_texture_slot.get(), sun_shadow_index);
      if (shadow_manager_ != nullptr) {
        if (shadow_instance != nullptr) {
          LOG_F(INFO,
            "Renderer: frame={} view={} shadow instance detail "
            "shadow_meta_srv={} sun_shadow_index={} light_index={} "
            "payload_index={} domain={} implementation={} flags=0x{:08x}",
            render_context.frame_sequence.get(), view_id.get(),
            shadow_instance_metadata_slot.get(), sun_shadow_index,
            shadow_instance->light_index, shadow_instance->payload_index,
            shadow_instance->domain, shadow_instance->implementation_kind,
            shadow_instance->flags);
        } else {
          LOG_F(INFO,
            "Renderer: frame={} view={} shadow instance detail unavailable "
            "shadow_meta_srv={} sun_shadow_index={}",
            render_context.frame_sequence.get(), view_id.get(),
            shadow_instance_metadata_slot.get(), sun_shadow_index);
        }
      }
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

  if (!can_reuse_cached_view_bindings
    && environment_frame_bindings_publisher_) {
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
}

auto Renderer::EnsureSceneDepthTextureSrv(PerViewRuntimeState& runtime_state,
  const graphics::Texture& depth_texture) -> ShaderVisibleIndex
{
  const auto graphics_ptr = gfx_weak_.lock();
  if (!graphics_ptr) {
    return kInvalidShaderVisibleIndex;
  }

  auto& allocator = graphics_ptr->GetDescriptorAllocator();
  auto& registry = graphics_ptr->GetResourceRegistry();

  const auto depth_format = depth_texture.GetDescriptor().format;
  Format srv_format = Format::kR32Float;
  switch (depth_format) {
  case Format::kDepth32:
  case Format::kDepth32Stencil8:
  case Format::kDepth24Stencil8:
    srv_format = Format::kR32Float;
    break;
  case Format::kDepth16:
    srv_format = Format::kR16UNorm;
    break;
  default:
    srv_format = depth_format;
    break;
  }

  graphics::TextureViewDescription srv_desc {
    .view_type = graphics::ResourceViewType::kTexture_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = srv_format,
    .dimension = depth_texture.GetDescriptor().texture_type,
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    .is_read_only_dsv = false,
  };

  if (const auto existing_index
    = registry.FindShaderVisibleIndex(depth_texture, srv_desc);
    existing_index.has_value()) {
    runtime_state.scene_depth_srv = *existing_index;
    runtime_state.scene_depth_texture_owner = &depth_texture;
    runtime_state.owns_scene_depth_srv = false;
    return runtime_state.scene_depth_srv;
  }

  if (runtime_state.scene_depth_srv.IsValid()
    && runtime_state.scene_depth_texture_owner == &depth_texture
    && runtime_state.owns_scene_depth_srv
    && registry.Contains(depth_texture, srv_desc)) {
    return runtime_state.scene_depth_srv;
  }

  auto register_new_srv = [&]() -> ShaderVisibleIndex {
    auto srv_handle
      = allocator.AllocateRaw(graphics::ResourceViewType::kTexture_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!srv_handle.IsValid()) {
      return kInvalidShaderVisibleIndex;
    }
    runtime_state.scene_depth_srv = allocator.GetShaderVisibleIndex(srv_handle);
    auto native_view
      = registry.RegisterView(const_cast<graphics::Texture&>(depth_texture),
        std::move(srv_handle), srv_desc);
    if (!native_view->IsValid()) {
      runtime_state.scene_depth_srv = kInvalidShaderVisibleIndex;
      runtime_state.scene_depth_texture_owner = nullptr;
      runtime_state.owns_scene_depth_srv = false;
      return kInvalidShaderVisibleIndex;
    }
    runtime_state.scene_depth_texture_owner = &depth_texture;
    runtime_state.owns_scene_depth_srv = true;
    return runtime_state.scene_depth_srv;
  };

  if (!runtime_state.scene_depth_srv.IsValid()
    || !runtime_state.owns_scene_depth_srv) {
    return register_new_srv();
  }
  runtime_state.scene_depth_srv = kInvalidShaderVisibleIndex;
  runtime_state.scene_depth_texture_owner = nullptr;
  runtime_state.owns_scene_depth_srv = false;
  return register_new_srv();
}

auto Renderer::UpdateCurrentViewDynamicBindings(
  const RenderContext& render_context,
  const CurrentViewDynamicBindingsUpdate& update) -> void
{
  const auto view_id = render_context.current_view.view_id;
  if (view_id == ViewId {}) {
    LOG_F(
      ERROR, "Renderer: cannot update dynamic bindings for invalid view id");
    return;
  }

  auto& runtime_state = per_view_runtime_state_[view_id];
  bool changed = false;
  if (update.light_culling.has_value()) {
    runtime_state.light_culling = *update.light_culling;
    changed = true;
  }
  if (update.virtual_shadow.has_value()) {
    runtime_state.virtual_shadow = *update.virtual_shadow;
    changed = true;
  }
  if (!changed) {
    return;
  }

  if (!RepublishCurrentViewBindings(
        render_context, ViewBindingRepublishMode::kDynamicSystemBindings)) {
    LOG_F(ERROR,
      "Renderer: failed to republish current-view bindings after dynamic "
      "binding update for view {}",
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
  DLOG_F(2,
    "Renderer: view={} exposure update mode={} exposure_enabled={} "
    "camera_ev={} manual_ev={} compensation_ev={:.6f} raw_key={:.6f} "
    "seed_exposure={:.6f} sun_enabled={} sun_cos_zenith={:.6f}",
    view_id.get(), static_cast<std::uint32_t>(exposure_mode), exposure_enabled,
    camera_ev.has_value() ? camera_ev.value() : -9999.0F,
    manual_ev_read.has_value() ? manual_ev_read.value() : -9999.0F,
    compensation_ev, raw_exposure_key, exposure, sun_state.enabled,
    sun_state.cos_zenith);
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

auto Renderer::EvictPerViewCachedProducts(const ViewId view_id) -> void
{
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
    EvictPerViewCachedProducts(view_id);
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
        true); // Reset per-view transient data for every view.
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
        LOG_F(INFO,
          "Renderer: frame={} view={} lighting resolve dir_count={} "
          "sun_enabled={} sun_dir=({:.6f}, {:.6f}, {:.6f}) "
          "sun_illuminance={:.3f} sun_color=({:.6f}, {:.6f}, {:.6f})",
          frame_seq, nostd::to_string(view_id), dir_lights.size(),
          runtime_state.sun.enabled,
          runtime_state.sun.direction_ws_illuminance.x,
          runtime_state.sun.direction_ws_illuminance.y,
          runtime_state.sun.direction_ws_illuminance.z,
          runtime_state.sun.direction_ws_illuminance.w,
          runtime_state.sun.color_rgb_intensity.x,
          runtime_state.sun.color_rgb_intensity.y,
          runtime_state.sun.color_rgb_intensity.z);

        std::size_t sun_tagged_count = 0;
        std::size_t env_contrib_count = 0;
        std::size_t shadowed_dir_count = 0;
        std::size_t shadowed_sun_count = 0;
        std::uint32_t first_dir_shadow_index = 0xFFFFFFFFU;
        std::uint32_t first_dir_flags = 0U;
        for (const auto& dl : dir_lights) {
          const auto flags = static_cast<DirectionalLightFlags>(dl.flags);
          if (first_dir_shadow_index == 0xFFFFFFFFU) {
            first_dir_shadow_index = dl.shadow_index;
            first_dir_flags = dl.flags;
          }
          if ((flags & DirectionalLightFlags::kSunLight)
            != DirectionalLightFlags::kNone) {
            ++sun_tagged_count;
          }
          if ((flags & DirectionalLightFlags::kEnvironmentContribution)
            != DirectionalLightFlags::kNone) {
            ++env_contrib_count;
          }
          if (dl.shadow_index != 0xFFFFFFFFU) {
            ++shadowed_dir_count;
            if ((flags & DirectionalLightFlags::kSunLight)
              != DirectionalLightFlags::kNone) {
              ++shadowed_sun_count;
            }
          }
        }
        LOG_F(INFO,
          "Renderer: frame={} view={} directional light summary total={} "
          "sun_tagged={} env_contrib={} shadowed_total={} shadowed_sun={} "
          "first_shadow_index={} first_flags=0x{:08x}",
          frame_seq, nostd::to_string(view_id), dir_lights.size(),
          sun_tagged_count, env_contrib_count, shadowed_dir_count,
          shadowed_sun_count, first_dir_shadow_index, first_dir_flags);

        CHECK_F(runtime_state.sun.enabled == 0U || sun_tagged_count == 1U,
          "Renderer: resolved sun must be backed by exactly one sun-tagged "
          "directional light frame={} view={} sun_enabled={} "
          "sun_tagged_count={}",
          frame_seq, nostd::to_string(view_id), runtime_state.sun.enabled,
          sun_tagged_count);

        if (const auto sun_tagged_light
          = internal::FindSunTaggedDirectionalLight(dir_lights);
          runtime_state.sun.enabled != 0U && sun_tagged_light.has_value()) {
          const float light_dir_len_sq = glm::dot(
            sun_tagged_light->direction_ws, sun_tagged_light->direction_ws);
          const glm::vec3 resolved_direction = runtime_state.sun.GetDirection();
          const glm::vec3 expected_direction_to_sun = light_dir_len_sq > 0.0F
            ? -glm::normalize(sun_tagged_light->direction_ws)
            : glm::vec3(0.0F);
          const float resolved_illuminance = runtime_state.sun.GetIlluminance();
          const float expected_illuminance = sun_tagged_light->intensity_lux;
          LOG_F(INFO,
            "Renderer: frame={} view={} sun contract resolved_dir=({:.6f}, "
            "{:.6f}, {:.6f}) expected_dir=({:.6f}, {:.6f}, {:.6f}) "
            "resolved_illuminance={:.3f} expected_illuminance={:.3f}",
            frame_seq, nostd::to_string(view_id), resolved_direction.x,
            resolved_direction.y, resolved_direction.z,
            expected_direction_to_sun.x, expected_direction_to_sun.y,
            expected_direction_to_sun.z, resolved_illuminance,
            expected_illuminance);
          CHECK_F(internal::ResolvedSunMatchesDirectionalLight(
                    runtime_state.sun, *sun_tagged_light),
            "Resolved sun payload diverged from sun-tagged directional "
            "light frame={} view={} resolved_dir=({:.6f}, {:.6f}, {:.6f}) "
            "expected_dir=({:.6f}, {:.6f}, {:.6f}) "
            "resolved_illuminance={:.3f} expected_illuminance={:.3f} "
            "light_flags=0x{:08x} shadow_index={}",
            frame_seq, nostd::to_string(view_id), resolved_direction.x,
            resolved_direction.y, resolved_direction.z,
            expected_direction_to_sun.x, expected_direction_to_sun.y,
            expected_direction_to_sun.z, resolved_illuminance,
            expected_illuminance, sun_tagged_light->flags,
            sun_tagged_light->shadow_index);
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
      DLOG_F(2, "Renderer: frame={} view={} exposure={:.6f}", frame_seq,
        nostd::to_string(view_id), prepared_frame.exposure);

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
    prepared_frame.draw_bounding_spheres
      = std::span<const glm::vec4>(storage.draw_bounding_sphere_storage.data(),
        storage.draw_bounding_sphere_storage.size());
  } else {
    // No emitter -> empty spans
    prepared_frame.draw_metadata_bytes = {};
    prepared_frame.partitions = {};
    prepared_frame.draw_bounding_spheres = {};
  }

  PublishConventionalShadowDrawRecords(view_id, prepared_frame);

  storage.render_item_storage.clear();
  storage.shadow_caster_bounds_storage.clear();
  storage.visible_receiver_bounds_storage.clear();
  std::size_t collected_item_count = 0U;
  std::size_t zero_radius_item_count = 0U;
  std::size_t cast_shadow_item_count = 0U;
  std::size_t receive_shadow_item_count = 0U;
  std::size_t main_view_visible_item_count = 0U;
  prepared_frame.render_items = {};
  if (scene_prep_state_ != nullptr) {
    const auto items = scene_prep_state_->CollectedItems();
    collected_item_count = items.size();
    storage.render_item_storage.assign(items.begin(), items.end());
    prepared_frame.render_items = std::span<const sceneprep::RenderItemData>(
      storage.render_item_storage.data(), storage.render_item_storage.size());
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
  } else {
    storage.render_item_storage.clear();
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

auto Renderer::PublishConventionalShadowDrawRecords(
  ViewId view_id, PreparedSceneFrame& prepared_frame) -> void
{
  auto& storage = per_view_storage_[view_id];
  storage.conventional_shadow_draw_record_storage.clear();
  prepared_frame.conventional_shadow_draw_records = {};
  prepared_frame.bindless_conventional_shadow_draw_records_slot
    = kInvalidShaderVisibleIndex;

  internal::BuildConventionalShadowDrawRecords(
    prepared_frame, storage.conventional_shadow_draw_record_storage);

  prepared_frame.conventional_shadow_draw_records
    = std::span<const renderer::ConventionalShadowDrawRecord>(
      storage.conventional_shadow_draw_record_storage.data(),
      storage.conventional_shadow_draw_record_storage.size());

  std::uint32_t static_record_count = 0U;
  std::uint32_t shadow_only_record_count = 0U;
  std::uint32_t opaque_record_count = 0U;
  std::uint32_t masked_record_count = 0U;
  std::vector<std::uint32_t> partition_counts(
    prepared_frame.partitions.size(), 0U);
  for (const auto& record : storage.conventional_shadow_draw_record_storage) {
    if (renderer::IsStaticShadowCaster(record)) {
      ++static_record_count;
    }
    if (!renderer::IsMainViewVisible(record)) {
      ++shadow_only_record_count;
    }
    if (record.partition_pass_mask.IsSet(PassMaskBit::kOpaque)) {
      ++opaque_record_count;
    }
    if (record.partition_pass_mask.IsSet(PassMaskBit::kMasked)) {
      ++masked_record_count;
    }
    if (record.partition_index < partition_counts.size()) {
      ++partition_counts[record.partition_index];
    }
  }

  if (!storage.conventional_shadow_draw_record_storage.empty()) {
    if (conventional_shadow_draw_record_buffer_ == nullptr) {
      LOG_F(ERROR,
        "Renderer: view={} shadow draw record buffer is unavailable; records "
        "will not be published to the GPU",
        view_id.get());
    } else if (const auto allocation
      = conventional_shadow_draw_record_buffer_->Allocate(
        static_cast<std::uint32_t>(
          storage.conventional_shadow_draw_record_storage.size()));
      allocation) {
      if (allocation->mapped_ptr != nullptr) {
        std::memcpy(allocation->mapped_ptr,
          storage.conventional_shadow_draw_record_storage.data(),
          storage.conventional_shadow_draw_record_storage.size()
            * sizeof(renderer::ConventionalShadowDrawRecord));
        prepared_frame.bindless_conventional_shadow_draw_records_slot
          = allocation->srv;
      }
    } else {
      LOG_F(ERROR,
        "Renderer: view={} failed to publish {} conventional shadow draw "
        "records: {}",
        view_id.get(), storage.conventional_shadow_draw_record_storage.size(),
        allocation.error().message());
    }
  }

  std::string partition_distribution {};
  for (std::uint32_t partition_index = 0U;
    partition_index < prepared_frame.partitions.size(); ++partition_index) {
    const auto& partition = prepared_frame.partitions[partition_index];
    if (!internal::IsConventionalShadowRasterPartition(partition.pass_mask)) {
      continue;
    }
    if (!partition_distribution.empty()) {
      partition_distribution += ", ";
    }
    partition_distribution += fmt::format("#{}:{}@[{},{})={}", partition_index,
      to_string(partition.pass_mask), partition.begin, partition.end,
      partition_counts[partition_index]);
  }
  if (partition_distribution.empty()) {
    partition_distribution = "none";
  }

  LOG_F(INFO,
    "Renderer: view={} conventional shadow draw records={} static={} "
    "dynamic={} "
    "shadow_only={} opaque={} masked={} slot={} partition_distribution={}",
    view_id.get(), storage.conventional_shadow_draw_record_storage.size(),
    static_record_count,
    static_cast<std::uint32_t>(
      storage.conventional_shadow_draw_record_storage.size())
      - static_record_count,
    shadow_only_record_count, opaque_record_count, masked_record_count,
    prepared_frame.bindless_conventional_shadow_draw_records_slot.get(),
    partition_distribution);
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
    pending_compositions_.clear();
    next_composition_sequence_in_frame_ = 0;
  }

  if (!scene_prep_state_ || !texture_binder_) {
    LOG_F(
      ERROR, "Renderer OnFrameStart called before OnAttached initialization");
    return;
  }

  auto frame_slot = context->GetFrameSlot();
  auto frame_sequence = context->GetFrameSequenceNumber();
  frame_budget_stats_ = context->GetBudgetStats();
  if (frame_budget_stats_.cpu_budget.count() <= 0) {
    frame_budget_stats_.cpu_budget = std::chrono::milliseconds(16);
  }
  if (frame_budget_stats_.gpu_budget.count() <= 0) {
    frame_budget_stats_.gpu_budget = std::chrono::milliseconds(16);
  }

  const auto current_scene = context->GetScene().get();
  if (current_scene != last_scene_identity_) {
    LOG_F(INFO,
      "Renderer: scene identity changed (old={} new={}); resetting shadow "
      "cache state",
      fmt::ptr(last_scene_identity_), fmt::ptr(current_scene));
    last_scene_identity_ = current_scene;
    if (shadow_manager_) {
      shadow_manager_->ResetCachedState();
    }
  }

  BeginFrameServices(frame_slot, frame_sequence);
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
