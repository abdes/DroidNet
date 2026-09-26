//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <new>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Internal/CapturedSkySource.h>
#include <Oxygen/Vortex/Environment/Internal/IblGpuValidation.h>
#include <Oxygen/Vortex/Environment/Internal/IblProcessor.h>
#include <Oxygen/Vortex/Environment/Internal/IblWorkBudget.h>
#include <Oxygen/Vortex/Environment/Passes/IblProbePass.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Resources/TextureBinder.h>

namespace oxygen::vortex::environment::internal {

namespace {
  auto MakePublishedState(const IblGpuProducts& product,
    const StaticSkyLightProductKey& key, const bool enabled)
    -> EnvironmentProbeState
  {
    auto result = EnvironmentProbeState {};
    result.probes = { .environment_map_srv = product.processed_srv,
      .diffuse_sh_srv = product.diffuse_sh_srv,
      .prefiltered_map_srv = product.specular_srv,
      .probe_revision = product.revision,
      .product_metadata_srv = product.metadata_srv };
    result.static_sky_light = { .key = key,
      .processed_cubemap_srv = product.processed_srv,
      .diffuse_irradiance_sh_srv = product.diffuse_sh_srv,
      .prefiltered_cubemap_srv = product.specular_srv,
      .processed_cubemap_max_mip = product.maximum_mip,
      .prefiltered_cubemap_max_mip = product.maximum_mip,
      .product_revision = product.revision,
      .status = enabled ? StaticSkyLightProductStatus::kValidCurrentKey
                        : StaticSkyLightProductStatus::kDisabled };
    result.valid = true;
    result.flags = kEnvironmentProbeStateFlagResourcesValid;
    return result;
  }
} // namespace

struct IblProcessor::Cache {
  struct SceneCache {
    std::weak_ptr<const scene::Scene> scene;
    std::unique_ptr<IblGpuProcessor> processor;
    std::unique_ptr<CapturedSkySource> captured;
    std::shared_ptr<const IblGpuProducts> published;
    StaticSkyLightProductKey key;
    StaticSkyLightProductKey desired_key;
    frame::SequenceNumber published_frame {};
    std::uint32_t candidate_revision {};
    std::uint32_t source {};
    bool enabled { false };
    bool empty_capture { false };
    double cpu_update_ms {};
    std::shared_ptr<IblGpuJob> candidate;
    StaticSkyLightProductKey candidate_key;
    frame::SequenceNumber candidate_frame {};
    std::uint64_t authoring_revision {};
    frame::SequenceNumber last_frame {};
    RefreshState frame_result;
    bool has_frame { false };
    bool retry_immediate { false };
    std::unique_ptr<IblGpuValidation> validation;
    bool validation_allocation_failed { false };
  };
  std::unordered_map<std::uint64_t, SceneCache> scenes;
  std::unique_ptr<IblBrdfResources> brdf;
  std::shared_ptr<const IblGpuProducts> published;
  std::uint32_t revision {};
  std::uint64_t active_scene_lifetime {};
  bool publication_invalidated { false };
  std::shared_ptr<IblWorkBudget> budget { std::make_shared<IblWorkBudget>() };
  bool timing_required { false };
  bool recorded_work { false };
  std::optional<frame::SequenceNumber> diagnostics_frame;
};

IblProcessor::IblProcessor(Renderer& renderer)
  : renderer_(renderer)
  , cache_(std::make_unique<Cache>())
{
  renderer_.GetDiagnosticsService().RegisterIblTimingSink(cache_->budget);
}
IblProcessor::~IblProcessor()
{
  cache_->budget->Close();
  if (cache_->timing_required)
    renderer_.GetDiagnosticsService().ReleaseIblTiming();
}

auto IblProcessor::OnFrameStart(const frame::SequenceNumber sequence) -> bool
{
  if (cache_->timing_required && !cache_->recorded_work) {
    renderer_.GetDiagnosticsService().ReleaseIblTiming();
    cache_->timing_required = false;
  }
  cache_->recorded_work = false;
  RetireExpiredScenes();
  // Embedded/offscreen views may start the same scene renderer more than once
  // in a frame. Poll and request metadata only on its first frame entry.
  if (cache_->diagnostics_frame == sequence)
    return std::exchange(cache_->publication_invalidated, false);
  cache_->diagnostics_frame = sequence;
  auto& diagnostics = renderer_.GetDiagnosticsService();
  const bool collect = HasAnyFeature(
    diagnostics.GetEnabledFeatures(), DiagnosticsFeature::kFrameLedger);
  for (auto& [lifetime, scene] : cache_->scenes) {
    try {
      // Drain already requested observations even when collection is disabled.
      if (scene.validation) {
        for (const auto& observed : scene.validation->Poll()) {
          if (!observed || observed->status == SkyLightGpuValidation::kValid)
            continue;
          const bool invalid
            = observed->status == SkyLightGpuValidation::kInvalid;
          diagnostics.ReportIssue(
            { .severity = invalid ? DiagnosticsSeverity::kError
                                  : DiagnosticsSeverity::kWarning,
              .code = invalid ? "ibl.gpu-invalid" : "ibl.readback-unavailable",
              .message = std::string(invalid
                             ? "Invalid GPU sky-light generation "
                             : "Readback unavailable for sky-light generation ")
                + std::to_string(observed->revision) + " in scene "
                + std::to_string(lifetime)
                + (invalid ? ". Its lighting contribution is zero." : "."),
              .pass_name = "Vortex.Diagnostics.IBL.Metadata",
              .product_name = "SkyLight" });
        }
      }
      if (!collect || !scene.published || !scene.processor
        || !scene.processor->IsOpen())
        continue;
      try {
        if (!scene.validation) {
          scene.validation_allocation_failed = true;
          scene.validation
            = std::make_unique<IblGpuValidation>(renderer_.GetGraphics());
          scene.validation_allocation_failed = false;
        }
        scene.validation->Request(*scene.published, diagnostics);
      } catch (const std::bad_alloc&) {
        // Diagnostic allocation does not alter the accepted lighting products.
      }
      if (!scene.validation
        || scene.validation->Inspect(*scene.published, collect).gpu_validation
          == SkyLightGpuValidation::kUnavailable)
        diagnostics.ReportIssue({ .severity = DiagnosticsSeverity::kWarning,
          .code = "ibl.readback-unavailable",
          .message = "GPU sky-light validation unavailable for scene "
            + std::to_string(lifetime),
          .pass_name = "Vortex.Diagnostics.IBL.Metadata",
          .product_name = "SkyLight" });
    } catch (const std::bad_alloc&) {
      // Reporting an unavailable diagnostic must not reject a healthy frame.
    }
  }
  // An execution-uncertain diagnostic submission closes the same Nexus pool
  // as any other reader. Invalidate that publication before view preparation.
  RetireExpiredScenes();
  return std::exchange(cache_->publication_invalidated, false);
}

auto IblProcessor::RetireExpiredScenes() -> void
{
  auto& cache = *cache_;
  if (cache.published && !GetPublishedProducts()) {
    cache.publication_invalidated = true;
    cache.published.reset();
  }
  std::erase_if(cache.scenes, [](const auto& entry) {
    return entry.first != 0U && entry.second.scene.expired();
  });
}

auto IblProcessor::RefreshPersistentProbes(
  const EnvironmentProbeState& current, const bool changed) -> RefreshState
{
  const auto result = IblProbePass {}.Refresh(current, changed);
  return { result.requested, result.refreshed, result.probe_state };
}

auto IblProcessor::GetPublishedProducts() const
  -> std::shared_ptr<const IblGpuProducts>
{
  const auto found = cache_->scenes.find(cache_->active_scene_lifetime);
  if (found == cache_->scenes.end()
    || (found->first != 0U && found->second.scene.expired())
    || (found->second.processor && !found->second.processor->IsOpen()))
    return {};
  return cache_->published;
}

auto IblProcessor::GetCachedSceneCount() const -> std::size_t
{
  return cache_->scenes.size();
}

auto IblProcessor::InspectState(const std::uint64_t scene_lifetime) const
  -> SkyLightRuntimeState
{
  const auto found = cache_->scenes.find(scene_lifetime);
  if (found == cache_->scenes.end()
    || (scene_lifetime != 0U && found->second.scene.expired()))
    return {};
  const auto& cached = found->second;
  const auto& state = cached.frame_result.probe_state;
  const bool open = cached.processor && cached.processor->IsOpen();
  auto result = SkyLightRuntimeState { .observed = cached.has_frame,
    .enabled = cached.enabled,
    .usable = state.valid && open && cached.published != nullptr,
    .empty_capture = cached.empty_capture,
    .source = cached.source,
    .source_cubemap = cached.desired_key.source_cubemap,
    .scene_lifetime = scene_lifetime,
    .frame_sequence = cached.last_frame.get(),
    .published_source_revision = cached.key.source_revision,
    .desired_source_revision = cached.desired_key.source_revision,
    .published_revision = cached.published ? cached.published->revision : 0U,
    .building_revision = cached.candidate ? cached.candidate_revision : 0U,
    .face_size = cached.published ? cached.key.output_face_size : 0U,
    .source_age_frames = cached.published && cached.key != cached.desired_key
      ? cached.last_frame.get() - cached.published_frame.get()
      : 0U,
    .cpu_update_ms = cached.cpu_update_ms,
    .status = state.static_sky_light.status,
    .unavailable_reason = state.static_sky_light.unavailable_reason };
  if (cached.processor && !open) {
    result.status = StaticSkyLightProductStatus::kUnavailable;
    result.unavailable_reason
      = StaticSkyLightUnavailableReason::kProcessingFailed;
    result.building_revision = 0U;
  }
  if (!result.usable) {
    result.published_revision = 0U;
    result.published_source_revision = 0U;
    result.face_size = 0U;
    result.source_age_frames = 0U;
  }
  if (result.usable) {
    const bool collect
      = HasAnyFeature(renderer_.GetDiagnosticsService().GetEnabledFeatures(),
        DiagnosticsFeature::kFrameLedger);
    const auto validation = cached.validation
      ? cached.validation->Inspect(*cached.published, collect)
      : SkyLightRuntimeState { .gpu_validation
          = cached.validation_allocation_failed
            ? SkyLightGpuValidation::kUnavailable
            : collect ? SkyLightGpuValidation::kPending
                      : SkyLightGpuValidation::kNotRequested };
    result.gpu_validation = validation.gpu_validation;
    result.validated_revision = validation.validated_revision;
    result.last_failed_gpu_revision = validation.last_failed_gpu_revision;
    result.source_radiance_scale = validation.source_radiance_scale;
    result.average_brightness = validation.average_brightness;
  }
  return result;
}

auto IblProcessor::GetTimingSampleCount() const -> std::uint64_t
{
  return cache_->budget->SampleCount();
}

auto IblProcessor::GetActivePoolStats() const -> IblGpuProcessor::Stats
{
  const auto found = cache_->scenes.find(cache_->active_scene_lifetime);
  return found != cache_->scenes.end() && found->second.processor
    ? found->second.processor->GetStats()
    : IblGpuProcessor::Stats {};
}

auto IblProcessor::AcquireCapture(
  const std::shared_ptr<const IblGpuProducts>& products)
  -> Result<IblCaptureLease, IblCaptureError>
{
  RetireExpiredScenes();
  if (!products)
    return Err(IblCaptureError::kUnavailable);
  return products->AcquireCapture();
}

auto IblProcessor::RefreshSkyLightProducts(const EnvironmentProbeState& current,
  RenderContext& ctx, const StableAtmosphereState& stable,
  const GpuFogParams& fog,
  const std::shared_ptr<resources::TextureBinder>& binder) -> RefreshState
try {
  auto& cache = *cache_;
  const auto scene = ctx.GetScene();
  const auto lifetime = scene ? scene->GetLifetimeId().get() : 0U;
  // No other scene's product can escape a failed current-scene update.
  cache.published.reset();
  // This refresh returns the replacement probe state to the caller.
  cache.publication_invalidated = false;
  RetireExpiredScenes();
  cache.active_scene_lifetime = lifetime;
  auto& scene_cache = cache.scenes[lifetime];
  if (scene)
    scene_cache.scene = scene->weak_from_this();
  const auto& light = stable.view_products.sky_light;
  auto next = EnvironmentProbeState {};
  next.probes.probe_revision = current.probes.probe_revision;
  const auto unavailable = [&](const StaticSkyLightUnavailableReason reason) {
    scene_cache.candidate.reset();
    scene_cache.retry_immediate = true;
    next.static_sky_light.status = StaticSkyLightProductStatus::kUnavailable;
    next.static_sky_light.unavailable_reason = reason;
    next.flags = kEnvironmentProbeStateFlagUnavailable;
    scene_cache.frame_result = { true, false, next };
    return scene_cache.frame_result;
  };
  if (scene_cache.processor && !scene_cache.processor->IsOpen()) {
    scene_cache.enabled = light.enabled;
    scene_cache.source = light.source;
    scene_cache.empty_capture = false;
    scene_cache.desired_key
      = { .source_cubemap = light.source == kSkyLightSourceSpecifiedCubemap
            ? light.cubemap_resource
            : content::ResourceKey {},
          .source_revision
          = light.enabled && light.source == kSkyLightSourceCapturedScene
            ? HashSkyCaptureInputs(stable)
            : 0U };
    scene_cache.last_frame = ctx.frame_sequence;
    scene_cache.has_frame = true;
    scene_cache.cpu_update_ms = 0.0;
    return unavailable(StaticSkyLightUnavailableReason::kProcessingFailed);
  }
  if (scene_cache.has_frame && scene_cache.last_frame == ctx.frame_sequence) {
    if (scene_cache.frame_result.probe_state.valid)
      cache.published = scene_cache.published;
    return { false, false, scene_cache.frame_result.probe_state };
  }
  const auto cpu_begin = std::chrono::steady_clock::now();
  const ScopeGuard cpu_time([&]() noexcept {
    scene_cache.cpu_update_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - cpu_begin)
                                  .count();
  });
  scene_cache.enabled = light.enabled;
  scene_cache.source = light.source;
  scene_cache.empty_capture = false;
  const bool resumed = scene_cache.has_frame
    && ctx.frame_sequence.get() != scene_cache.last_frame.get() + 1U;
  const bool authoring
    = stable.authoring_revision != scene_cache.authoring_revision;
  scene_cache.authoring_revision = stable.authoring_revision;
  scene_cache.last_frame = ctx.frame_sequence;
  scene_cache.has_frame = true;
  const bool retry_immediate = scene_cache.retry_immediate;
  // Until source resolution classifies this update, failure cannot reuse an
  // earlier source as the result of an authoring or structural replacement.
  scene_cache.retry_immediate = true;
  auto key = StaticSkyLightProductKey {
    .source_cubemap = light.source == kSkyLightSourceSpecifiedCubemap
      ? light.cubemap_resource
      : content::ResourceKey {},
    .source_revision = light.enabled ? HashSkyCaptureInputs(stable) : 0U,
    .output_face_size = 128U,
    .source_format_class = light.enabled ? light.source : 0xFFFFFFFFU,
    .source_rotation_radians
    = light.enabled && light.source == kSkyLightSourceSpecifiedCubemap
      ? light.source_cubemap_angle_radians
      : 0.0F,
    .lower_hemisphere_solid_color = light.lower_hemisphere_is_solid_color,
    .lower_hemisphere_color = light.lower_hemisphere_color,
    .lower_hemisphere_blend_alpha = light.lower_hemisphere_blend_alpha,
  };
  if (!light.enabled)
    key = { .output_face_size = 128U, .source_format_class = 0xFFFFFFFFU };
  scene_cache.desired_key = key;
  if (light.source == kSkyLightSourceSpecifiedCubemap)
    scene_cache.desired_key.source_revision = 0U;
  struct ResidentOwner {
    std::shared_ptr<resources::TextureBinder> binder;
    std::shared_ptr<const resources::TextureBinder::ReadyTexture> texture;
  };
  std::shared_ptr<const resources::TextureBinder::ReadyTexture> resident;
  if (light.enabled && light.source == kSkyLightSourceSpecifiedCubemap) {
    if (light.cubemap_resource.get() == 0U)
      return unavailable(StaticSkyLightUnavailableReason::kMissingCubemap);
    if (!binder)
      return unavailable(
        StaticSkyLightUnavailableReason::kResourceResolveFailed);
    const auto descriptor = binder->GetOrAllocate(light.cubemap_resource);
    if (binder->IsResourceReady(light.cubemap_resource)) {
      scene_cache.desired_key.source_revision
        = binder->GetContentRevision(descriptor);
      if (scene_cache.published
        && scene_cache.key.source_cubemap == key.source_cubemap
        && scene_cache.key.source_format_class == key.source_format_class
        && scene_cache.key.source_revision
          == scene_cache.desired_key.source_revision)
        scene_cache.desired_key.output_face_size
          = scene_cache.key.output_face_size;
    }
    // An unchanged resident descriptor also proves unchanged layout. Runtime
    // edits may retain its prior products if allocating the source lease fails.
    if (scene_cache.published && !authoring && !resumed && !retry_immediate
      && scene_cache.key.source_cubemap == key.source_cubemap
      && scene_cache.key.source_format_class == key.source_format_class
      && binder->IsResourceReady(light.cubemap_resource)
      && binder->GetContentRevision(descriptor)
        == scene_cache.key.source_revision)
      scene_cache.retry_immediate = false;
    resident = binder->AcquireReadyTexture(light.cubemap_resource);
    if (!resident)
      return unavailable(binder->HasResourceFailed(light.cubemap_resource)
          ? StaticSkyLightUnavailableReason::kResourceResolveFailed
          : StaticSkyLightUnavailableReason::kGpuProductsPending);
    const auto& source_desc = resident->texture->GetDescriptor();
    if (source_desc.texture_type != TextureType::kTextureCube
      || source_desc.array_size != 6U)
      return unavailable(StaticSkyLightUnavailableReason::kNotTextureCube);
    switch (source_desc.format) {
    case Format::kRGBA16Float:
    case Format::kRGBA32Float:
    case Format::kBC6HFloatU:
    case Format::kBC6HFloatS:
    case Format::kR11G11B10Float:
    case Format::kR9G9B9E5Float:
      break;
    default:
      return unavailable(StaticSkyLightUnavailableReason::kUnsupportedFormat);
    }
    key.source_revision = binder->GetContentRevision(resident->srv);
    key.output_face_size = std::bit_floor(source_desc.width);
  }
  const bool changed = !scene_cache.published || scene_cache.key != key;
  scene_cache.desired_key = key;
  constexpr auto capture_fog_flags = kGpuFogFlagEnabled
    | kGpuFogFlagHeightFogEnabled | kGpuFogFlagVisibleInRealTimeSkyCaptures;
  const bool empty_capture = light.source == kSkyLightSourceCapturedScene
    && !stable.view_products.atmosphere.enabled
    && ((fog.flags & capture_fog_flags) != capture_fog_flags
      || (fog.primary_density <= 0.0F && fog.secondary_density <= 0.0F)
      || fog.max_opacity <= 0.0F);
  const bool structural = scene_cache.key.source_cubemap != key.source_cubemap
    || scene_cache.key.source_format_class != key.source_format_class
    || scene_cache.key.output_face_size != key.output_face_size;
  scene_cache.empty_capture = empty_capture;
  const bool immediate = !scene_cache.published || authoring || resumed
    || structural || !light.enabled || empty_capture || retry_immediate;
  if (immediate)
    scene_cache.candidate.reset();
  scene_cache.retry_immediate = immediate && changed;
  const bool requested = changed || scene_cache.candidate != nullptr;
  if (requested) {
    cache.recorded_work = true;
    if (!cache.timing_required) {
      renderer_.GetDiagnosticsService().AcquireIblTiming();
      cache.timing_required = true;
    }
  }
  bool snapshot_started = false;
  bool refreshed = false;
  const auto publish = [&](const std::shared_ptr<const IblGpuProducts>& product,
                         const StaticSkyLightProductKey& product_key) {
    scene_cache.published = product;
    scene_cache.key = product_key;
    scene_cache.published_frame
      = immediate ? ctx.frame_sequence : scene_cache.candidate_frame;
    scene_cache.retry_immediate = false;
    refreshed = true;
  };
  const auto processing_failed = [&]() {
    if (immediate || !scene_cache.processor || !scene_cache.processor->IsOpen())
      return unavailable(StaticSkyLightUnavailableReason::kProcessingFailed);
    scene_cache.candidate.reset();
    cache.published = scene_cache.published;
    next = MakePublishedState(*cache.published, scene_cache.key, light.enabled);
    next.flags |= kEnvironmentProbeStateFlagStale;
    next.static_sky_light.status
      = StaticSkyLightProductStatus::kRegeneratingCurrentKey;
    next.static_sky_light.unavailable_reason
      = StaticSkyLightUnavailableReason::kProcessingFailed;
    scene_cache.frame_result = { true, false, next };
    return scene_cache.frame_result;
  };
  if (changed && !scene_cache.candidate) {
    auto graphics = renderer_.GetGraphics();
    if (!graphics)
      return processing_failed();
    if (!scene_cache.processor) {
      auto processor = std::make_unique<IblGpuProcessor>(renderer_);
      auto captured = std::make_unique<CapturedSkySource>(renderer_);
      scene_cache.processor = std::move(processor);
      scene_cache.captured = std::move(captured);
    }
    scene_cache.processor->SetTimingContext(cache.budget, ctx.frame_sequence);
    if (!cache.brdf)
      cache.brdf = std::make_unique<IblBrdfResources>(renderer_);
    const auto brdf = cache.brdf->Prepare();
    if (!brdf)
      return processing_failed();
    const auto settings = IblProcessSettings {
      .face_size = key.output_face_size,
      .source_rotation_radians = key.source_rotation_radians,
      .lower_hemisphere_solid_color = key.lower_hemisphere_solid_color,
      .lower_hemisphere_color = { key.lower_hemisphere_color.x,
        key.lower_hemisphere_color.y, key.lower_hemisphere_color.z },
      .lower_hemisphere_blend_alpha = key.lower_hemisphere_blend_alpha,
      .dispatch_tile_size = immediate ? 0U : 64U,
    };
    const auto revision
      = cache.revision == UINT32_MAX ? 1U : cache.revision + 1U;
    if (immediate) {
      const auto result = !light.enabled
        ? scene_cache.processor->ProcessSky({}, *brdf, settings, revision)
        : resident
        ? scene_cache.processor->ProcessCubeView(resident->texture,
            resident->srv,
            std::make_shared<ResidentOwner>(ResidentOwner { binder, resident }),
            *brdf, settings, revision)
        : scene_cache.captured->Process(ctx, stable, fog,
            *scene_cache.processor, *brdf, settings, revision);
      if (!result)
        return processing_failed();
      publish(*result, key);
      cache.revision = revision;
    } else {
      const auto job = resident
        ? scene_cache.processor->BeginCubeView(resident->texture, resident->srv,
            std::make_shared<ResidentOwner>(ResidentOwner { binder, resident }),
            *brdf, settings, revision)
        : scene_cache.captured->Begin(ctx, stable, fog, *scene_cache.processor,
            *brdf, settings, revision);
      if (job) {
        scene_cache.candidate = *job;
        scene_cache.candidate_key = key;
        scene_cache.candidate_frame = ctx.frame_sequence;
        scene_cache.candidate_revision = revision;
        snapshot_started = true;
        cache.revision = revision;
      } else
        return processing_failed();
    }
  }
  if (scene_cache.candidate) {
    scene_cache.processor->SetTimingContext(cache.budget, ctx.frame_sequence);
    const auto pending
      = scene_cache.processor->PendingDispatches(*scene_cache.candidate);
    const auto age
      = ctx.frame_sequence.get() - scene_cache.candidate_frame.get();
    const auto frames_left = age < 4U ? 4U - age : 1U;
    double remaining_ms = 0.0;
    for (const auto& step : pending)
      remaining_ms += cache.budget->Predict(step);
    // Balance remaining work against the completion deadline. A delayed timing
    // sample must not turn a short candidate into a long series of tiny
    // batches.
    const auto allowance = std::max(IblWorkBudget::kFrameBudgetMs
        - (snapshot_started ? IblWorkBudget::kSnapshotReserveMs : 0.0),
      remaining_ms / static_cast<double>(frames_left));
    const auto selection = cache.budget->Select(pending, allowance);
    const auto advanced = scene_cache.processor->Advance(scene_cache.candidate,
      frames_left == 1U ? static_cast<std::uint32_t>(pending.size())
                        : selection.count);
    if (advanced && advanced->products) {
      publish(advanced->products, scene_cache.candidate_key);
      scene_cache.candidate.reset();
    } else if (!advanced) {
      return processing_failed();
    }
  }
  cache.published = scene_cache.published;
  next = MakePublishedState(*cache.published, scene_cache.key, light.enabled);
  if (scene_cache.key != key) {
    next.flags |= kEnvironmentProbeStateFlagStale;
    next.static_sky_light.status
      = StaticSkyLightProductStatus::kRegeneratingCurrentKey;
  }
  scene_cache.frame_result = { requested, refreshed, next };
  return scene_cache.frame_result;
} catch (const std::bad_alloc&) {
  const auto found = cache_->scenes.find(cache_->active_scene_lifetime);
  if (found != cache_->scenes.end()) {
    auto& scene_cache = found->second;
    scene_cache.candidate.reset();
    if (scene_cache.published && !scene_cache.retry_immediate
      && scene_cache.processor && scene_cache.processor->IsOpen()) {
      cache_->published = scene_cache.published;
      auto retained
        = MakePublishedState(*scene_cache.published, scene_cache.key, true);
      retained.flags |= kEnvironmentProbeStateFlagStale;
      retained.static_sky_light.status
        = StaticSkyLightProductStatus::kRegeneratingCurrentKey;
      retained.static_sky_light.unavailable_reason
        = StaticSkyLightUnavailableReason::kProcessingFailed;
      scene_cache.frame_result = { true, false, retained };
      return scene_cache.frame_result;
    }
  }
  cache_->published.reset();
  auto unavailable = EnvironmentProbeState {};
  unavailable.probes.probe_revision = current.probes.probe_revision;
  unavailable.static_sky_light.status
    = StaticSkyLightProductStatus::kUnavailable;
  unavailable.static_sky_light.unavailable_reason
    = StaticSkyLightUnavailableReason::kProcessingFailed;
  unavailable.flags = kEnvironmentProbeStateFlagUnavailable;
  if (found != cache_->scenes.end()) {
    found->second.retry_immediate = true;
    found->second.frame_result = { true, false, unavailable };
  }
  return { true, false, unavailable };
}
} // namespace oxygen::vortex::environment::internal
