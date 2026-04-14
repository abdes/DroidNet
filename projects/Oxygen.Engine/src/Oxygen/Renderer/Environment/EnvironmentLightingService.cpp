//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Environment/EnvironmentLightingService.h>

#include <algorithm>
#include <chrono>
#include <optional>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Renderer/Internal/BrdfLutManager.h>
#include <Oxygen/Renderer/Internal/EnvironmentStaticDataManager.h>
#include <Oxygen/Renderer/Internal/IblManager.h>
#include <Oxygen/Renderer/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/Passes/IblComputePass.h>
#include <Oxygen/Renderer/Passes/SkyAtmosphereLutComputePass.h>
#include <Oxygen/Renderer/Passes/SkyCapturePass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Resources/TextureBinder.h>
#include <Oxygen/Renderer/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/Sun.h>
#include <Oxygen/Scene/Scene.h>

namespace {

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

namespace oxygen::engine {

EnvironmentLightingService::EnvironmentLightingService(
  const observer_ptr<Graphics> gfx, const RendererConfig& config,
  const observer_ptr<upload::UploadCoordinator> uploader,
  const observer_ptr<upload::StagingProvider> upload_staging_provider,
  const observer_ptr<upload::InlineTransfersCoordinator> inline_transfers,
  const observer_ptr<upload::StagingProvider> inline_staging_provider,
  const observer_ptr<renderer::resources::TextureBinder>
    texture_binder) noexcept
  : gfx_(gfx)
  , uploader_(uploader)
  , upload_staging_provider_(upload_staging_provider)
  , inline_transfers_(inline_transfers)
  , inline_staging_provider_(inline_staging_provider)
  , texture_binder_(texture_binder)
  , config_(&config)
{
}

EnvironmentLightingService::~EnvironmentLightingService() = default;

auto EnvironmentLightingService::Initialize() -> void
{
  if (!gfx_ || !inline_staging_provider_ || !inline_transfers_ || !uploader_
    || !upload_staging_provider_ || !texture_binder_) {
    return;
  }

  environment_view_data_publisher_ = std::make_unique<
    internal::PerViewStructuredPublisher<EnvironmentViewData>>(
    gfx_, *inline_staging_provider_, inline_transfers_, "EnvironmentViewData");
  environment_frame_bindings_publisher_ = std::make_unique<
    internal::PerViewStructuredPublisher<EnvironmentFrameBindings>>(gfx_,
    *inline_staging_provider_, inline_transfers_, "EnvironmentFrameBindings");

  brdf_lut_manager_ = std::make_unique<internal::BrdfLutManager>(
    gfx_, uploader_, upload_staging_provider_);

  sky_capture_pass_config_ = std::make_shared<SkyCapturePassConfig>();
  sky_capture_pass_config_->resolution = 128u;
  sky_capture_pass_
    = std::make_unique<SkyCapturePass>(gfx_, sky_capture_pass_config_);

  sky_atmo_lut_compute_pass_config_
    = std::make_shared<SkyAtmosphereLutComputePassConfig>();
  sky_atmo_lut_compute_pass_ = std::make_unique<SkyAtmosphereLutComputePass>(
    gfx_, sky_atmo_lut_compute_pass_config_);

  ibl_manager_ = std::make_unique<internal::IblManager>(gfx_);

  env_static_manager_
    = std::make_unique<internal::EnvironmentStaticDataManager>(gfx_,
      texture_binder_, observer_ptr { brdf_lut_manager_.get() },
      observer_ptr { ibl_manager_.get() },
      observer_ptr { sky_capture_pass_.get() });

  ibl_compute_pass_ = std::make_unique<IblComputePass>("IblComputePass");
}

auto EnvironmentLightingService::OnFrameStart(
  const frame::SequenceNumber frame_sequence, const frame::Slot frame_slot)
  -> void
{
  const auto tag = oxygen::renderer::internal::RendererTagFactory::Get();

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
}

auto EnvironmentLightingService::UpdatePreparedViewState(const ViewId view_id,
  const scene::Scene& scene, const ResolvedView& view,
  const bool allow_atmosphere, const SyntheticSunData& sun,
  EnvironmentViewData& environment_view) -> void
{
  float aerial_distance_scale = 1.0F;
  float aerial_scattering_strength = 1.0F;
  float planet_radius_m = 6'360'000.0F;
  glm::vec3 planet_center_ws { 0.0F, 0.0F, -planet_radius_m };
  glm::vec3 planet_up_ws { 0.0F, 0.0F, 1.0F };
  float camera_altitude_m = 0.0F;
  float sky_view_lut_slice = 0.0F;
  float planet_to_sun_cos_zenith = 0.0F;

  namespace env = scene::environment;

  if (auto scene_environment = scene.GetEnvironment()) {
    if (const auto atmo = scene_environment->TryGetSystem<env::SkyAtmosphere>();
      atmo && atmo->IsEnabled()) {
      aerial_distance_scale = atmo->GetAerialPerspectiveDistanceScale();
      aerial_scattering_strength = atmo->GetAerialScatteringStrength();
      planet_radius_m = atmo->GetPlanetRadiusMeters();
      planet_center_ws = glm::vec3(0.0F, 0.0F, -planet_radius_m);

      const auto camera_pos = view.CameraPosition();
      camera_altitude_m = glm::max(
        glm::length(camera_pos - planet_center_ws) - planet_radius_m, 0.0F);
      planet_to_sun_cos_zenith = (sun.enabled != 0U) ? sun.cos_zenith : 0.0F;
    }
  }

  environment_view = EnvironmentViewData {
    .flags = 0U,
    .sky_view_lut_slice = sky_view_lut_slice,
    .planet_to_sun_cos_zenith = planet_to_sun_cos_zenith,
    .aerial_perspective_distance_scale = aerial_distance_scale,
    .aerial_scattering_strength = aerial_scattering_strength,
    .planet_center_ws_pad = glm::vec4(planet_center_ws, 0.0F),
    .planet_up_ws_camera_altitude_m
    = glm::vec4(planet_up_ws, camera_altitude_m),
  };

  bool atmo_enabled = false;
  if (const auto scene_env = scene.GetEnvironment();
    allow_atmosphere && scene_env) {
    if (const auto atmo = scene_env->TryGetSystem<env::SkyAtmosphere>();
      atmo && atmo->IsEnabled()) {
      atmo_enabled = true;
    }
  }
  if (!atmo_enabled) {
    return;
  }

  if (const auto lut_mgr = GetOrCreateSkyAtmosphereLutManagerForView(view_id)) {
    lut_mgr->UpdateSunState(sun);
    if (const auto scene_env = scene.GetEnvironment()) {
      if (const auto params
        = BuildSkyAtmosphereParamsFromEnvironment(*scene_env, *lut_mgr);
        params.has_value()) {
        lut_mgr->UpdateParameters(*params);
      }
    }
  }
}

auto EnvironmentLightingService::PrepareCurrentView(const ViewId view_id,
  RenderContext& render_context, const bool allow_atmosphere) -> void
{
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

  if (env_static_manager_) {
    if (allow_atmosphere) {
      const auto tag = oxygen::renderer::internal::RendererTagFactory::Get();
      env_static_manager_->UpdateIfNeeded(tag, render_context, view_id);
    } else {
      env_static_manager_->EraseViewState(view_id);
    }
  }
}

auto EnvironmentLightingService::ExecutePerViewPasses(const ViewId view_id,
  RenderContext& render_context, graphics::CommandRecorder& recorder,
  const bool allow_atmosphere) -> co::Co<>
{
  auto atmo_lut_manager = render_context.current_view.atmo_lut_manager;
  if (!allow_atmosphere) {
    if (env_static_manager_) {
      env_static_manager_->EraseViewState(view_id);
    }
    co_return;
  }

  if (sky_atmo_lut_compute_pass_ && atmo_lut_manager) {
    const auto swap_count_before = atmo_lut_manager->GetSwapCount();
    if (atmo_lut_manager->IsDirty() || !atmo_lut_manager->HasBeenGenerated()) {
      try {
        graphics::GpuEventScope lut_scope(recorder,
          "Environment.AtmosphereLutCompute",
          profiling::ProfileGranularity::kDiagnostic,
          profiling::ProfileCategory::kCompute);
        co_await sky_atmo_lut_compute_pass_->PrepareResources(
          render_context, recorder);
        co_await sky_atmo_lut_compute_pass_->Execute(render_context, recorder);
      } catch (const std::exception& ex) {
        LOG_F(ERROR, "SkyAtmosphereLutComputePass failed: {}", ex.what());
      }
    }
    if (env_static_manager_
      && atmo_lut_manager->GetSwapCount() != swap_count_before) {
      const auto tag = oxygen::renderer::internal::RendererTagFactory::Get();
      env_static_manager_->UpdateIfNeeded(tag, render_context, view_id);
    }
  }

  if (sky_capture_pass_) {
    const bool capture_requested
      = sky_capture_requested_ || !sky_capture_pass_->IsCaptured(view_id);
    bool needs_capture = capture_requested;
    const auto capture_gen_before
      = sky_capture_pass_->GetCaptureGeneration(view_id);
    bool atmo_gen_changed = false;
    bool atmo_stable_for_capture = true;
    if (atmo_lut_manager) {
      const auto current_atmo_gen = atmo_lut_manager->GetGeneration();
      atmo_stable_for_capture
        = atmo_lut_manager->HasBeenGenerated() && !atmo_lut_manager->IsDirty();
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
        graphics::GpuEventScope capture_scope(recorder,
          "Environment.SkyCapture", profiling::ProfileGranularity::kDiagnostic,
          profiling::ProfileCategory::kPass);
        co_await sky_capture_pass_->PrepareResources(render_context, recorder);
        co_await sky_capture_pass_->Execute(render_context, recorder);
        const auto capture_gen_after
          = sky_capture_pass_->GetCaptureGeneration(view_id);
        if (env_static_manager_ && capture_gen_after != capture_gen_before) {
          const auto tag
            = oxygen::renderer::internal::RendererTagFactory::Get();
          env_static_manager_->UpdateIfNeeded(tag, render_context, view_id);
          env_static_manager_->RequestIblRegeneration(view_id);
        }
        if (atmo_lut_manager) {
          last_atmo_generation_[view_id] = atmo_lut_manager->GetGeneration();
        }
      } catch (const std::exception& ex) {
        LOG_F(ERROR, "SkyCapturePass failed: {}", ex.what());
      }
    }
  }

  if (ibl_compute_pass_) {
    if (env_static_manager_
      && env_static_manager_->IsIblRegenerationRequested(view_id)) {
      ibl_compute_pass_->RequestRegenerationOnce();
      env_static_manager_->MarkIblRegenerationClean(view_id);
    }
    try {
      graphics::GpuEventScope ibl_scope(recorder, "Environment.IblCompute",
        profiling::ProfileGranularity::kDiagnostic,
        profiling::ProfileCategory::kCompute);
      co_await ibl_compute_pass_->PrepareResources(render_context, recorder);
      co_await ibl_compute_pass_->Execute(render_context, recorder);
    } catch (const std::exception& ex) {
      LOG_F(ERROR, "IblComputePass failed: {}", ex.what());
    }
  }
}

auto EnvironmentLightingService::PublishForView(const ViewId view_id,
  const ShaderVisibleIndex environment_static_slot,
  const EnvironmentViewData& environment_view,
  const bool can_reuse_cached_view_bindings, ViewFrameBindings& view_bindings)
  -> void
{
  if (can_reuse_cached_view_bindings
    || !environment_frame_bindings_publisher_) {
    return;
  }

  auto environment_view_slot = kInvalidShaderVisibleIndex;
  if (environment_view_data_publisher_) {
    environment_view_slot
      = environment_view_data_publisher_->Publish(view_id, environment_view);
  }

  const EnvironmentFrameBindings environment_bindings {
    .environment_static_slot = environment_static_slot,
    .environment_view_slot = environment_view_slot,
  };
  view_bindings.environment_frame_slot
    = environment_frame_bindings_publisher_->Publish(
      view_id, environment_bindings);
}

auto EnvironmentLightingService::NoteViewSeen(
  const ViewId view_id, const frame::SequenceNumber frame_sequence) -> void
{
  last_seen_view_frame_seq_[view_id] = frame_sequence;
}

auto EnvironmentLightingService::OnFrameComplete() noexcept -> void
{
  sky_capture_requested_ = false;
}

auto EnvironmentLightingService::EvictViewProducts(const ViewId view_id) -> void
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

auto EnvironmentLightingService::EvictInactiveViewProducts(
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
    EvictViewProducts(view_id);
  }
}

auto EnvironmentLightingService::Shutdown() noexcept -> void
{
  per_view_atmo_luts_.clear();
  last_atmo_generation_.clear();
  last_seen_view_frame_seq_.clear();

  sky_capture_pass_.reset();
  sky_capture_pass_config_.reset();
  sky_atmo_lut_compute_pass_.reset();
  sky_atmo_lut_compute_pass_config_.reset();
  ibl_compute_pass_.reset();
  env_static_manager_.reset();
  ibl_manager_.reset();
  brdf_lut_manager_.reset();
  environment_view_data_publisher_.reset();
  environment_frame_bindings_publisher_.reset();
}

auto EnvironmentLightingService::GetSkyAtmosphereLutManagerForView(
  const ViewId view_id) const noexcept
  -> observer_ptr<internal::SkyAtmosphereLutManager>
{
  if (const auto it = per_view_atmo_luts_.find(view_id);
    it != per_view_atmo_luts_.end()) {
    return observer_ptr { it->second.get() };
  }
  return nullptr;
}

auto EnvironmentLightingService::GetOrCreateSkyAtmosphereLutManagerForView(
  const ViewId view_id) -> observer_ptr<internal::SkyAtmosphereLutManager>
{
  if (const auto it = per_view_atmo_luts_.find(view_id);
    it != per_view_atmo_luts_.end()) {
    return observer_ptr { it->second.get() };
  }

  if (!gfx_ || !uploader_ || !upload_staging_provider_) {
    return nullptr;
  }

  auto lut = std::make_unique<internal::SkyAtmosphereLutManager>(
    gfx_, uploader_, upload_staging_provider_);
  auto* lut_ptr = lut.get();
  per_view_atmo_luts_.insert_or_assign(view_id, std::move(lut));
  return observer_ptr { lut_ptr };
}

auto EnvironmentLightingService::GetEnvironmentStaticDataManager()
  const noexcept -> observer_ptr<internal::EnvironmentStaticDataManager>
{
  return observer_ptr { env_static_manager_.get() };
}

auto EnvironmentLightingService::GetIblManager() const noexcept
  -> observer_ptr<internal::IblManager>
{
  return observer_ptr { ibl_manager_.get() };
}

auto EnvironmentLightingService::GetIblComputePass() const noexcept
  -> observer_ptr<IblComputePass>
{
  return observer_ptr { ibl_compute_pass_.get() };
}

auto EnvironmentLightingService::GetEnvironmentStaticSlot(
  const ViewId view_id) const noexcept -> ShaderVisibleIndex
{
  return env_static_manager_ ? env_static_manager_->GetSrvIndex(view_id)
                             : kInvalidShaderVisibleIndex;
}

auto EnvironmentLightingService::RequestIblRegeneration() noexcept -> void
{
  if (ibl_compute_pass_) {
    ibl_compute_pass_->RequestRegenerationOnce();
  }
}

auto EnvironmentLightingService::RequestSkyCapture(
  const std::span<const ViewId> known_view_ids) noexcept -> void
{
  sky_capture_requested_ = true;
  if (sky_capture_pass_) {
    for (const auto view_id : known_view_ids) {
      sky_capture_pass_->MarkDirty(view_id);
    }
  }
}

auto EnvironmentLightingService::SetAtmosphereBlueNoiseEnabled(
  const bool enabled) noexcept -> void
{
  atmosphere_blue_noise_enabled_ = enabled;
  if (env_static_manager_) {
    env_static_manager_->SetBlueNoiseEnabled(enabled);
  }
}

} // namespace oxygen::engine
