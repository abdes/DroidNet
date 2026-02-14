//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstring>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/Atmosphere.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/Internal/EnvironmentStaticDataManager.h>
#include <Oxygen/Renderer/Internal/IBrdfLutProvider.h>
#include <Oxygen/Renderer/Internal/IIblProvider.h>
#include <Oxygen/Renderer/Internal/ISkyAtmosphereLutProvider.h>
#include <Oxygen/Renderer/Internal/ISkyCaptureProvider.h>
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Environment/Sun.h>
#include <Oxygen/Scene/Environment/VolumetricClouds.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::engine::internal {

using graphics::BufferDesc;
using graphics::BufferMemory;
using graphics::BufferUsage;

namespace {
  constexpr bool kDisablePostProcessVolumeForTesting = true;

  auto ToGpuFogModel(const scene::environment::FogModel model) noexcept
    -> FogModel
  {
    switch (model) {
    case scene::environment::FogModel::kExponentialHeight:
      return FogModel::kExponentialHeight;
    case scene::environment::FogModel::kVolumetric:
      return FogModel::kVolumetric;
    }
    return FogModel::kExponentialHeight;
  }

  auto ToGpuSkySphereSource(
    const scene::environment::SkySphereSource source) noexcept
    -> SkySphereSource
  {
    switch (source) {
    case scene::environment::SkySphereSource::kCubemap:
      return SkySphereSource::kCubemap;
    case scene::environment::SkySphereSource::kSolidColor:
      return SkySphereSource::kSolidColor;
    }
    return SkySphereSource::kCubemap;
  }

  auto ToGpuSkyLightSource(
    const scene::environment::SkyLightSource source) noexcept -> SkyLightSource
  {
    switch (source) {
    case scene::environment::SkyLightSource::kCapturedScene:
      return SkyLightSource::kCapturedScene;
    case scene::environment::SkyLightSource::kSpecifiedCubemap:
      return SkyLightSource::kSpecifiedCubemap;
    }
    return SkyLightSource::kCapturedScene;
  }

} // namespace

EnvironmentStaticDataManager::EnvironmentStaticDataManager(
  observer_ptr<Graphics> gfx,
  observer_ptr<renderer::resources::IResourceBinder> texture_binder,
  observer_ptr<IBrdfLutProvider> brdf_lut_provider,
  observer_ptr<IIblProvider> ibl_manager,
  observer_ptr<ISkyCaptureProvider> sky_capture_provider)
  : gfx_(gfx)
  , texture_binder_(texture_binder)
  , brdf_lut_provider_(brdf_lut_provider)
  , ibl_provider_(ibl_manager)
  , sky_capture_provider_(sky_capture_provider)
{
  // These are required dependencies, not guaranteeing them and not guaranteeing
  // that they will survive for the lifetime of the BRDF LUT Manager is a logic
  // error that will abort.
  CHECK_NOTNULL_F(gfx_, "expecting a valid Graphics instance");
  CHECK_NOTNULL_F(texture_binder_, "expecting a valid resource binder");
  CHECK_NOTNULL_F(brdf_lut_provider_, "expecting a valid BRDF LUT provider");
  CHECK_NOTNULL_F(ibl_provider_, "expecting a valid IBL provider");
  CHECK_NOTNULL_F(
    sky_capture_provider_, "expecting a valid sky capture provider");

  slot_uploaded_id_.fill(0);
}

EnvironmentStaticDataManager::~EnvironmentStaticDataManager()
{
  auto& registry = gfx_->GetResourceRegistry();
  for (auto& [_, state] : view_states_) {
    if (state.buffer != nullptr) {
      if (registry.Contains(*state.buffer)) {
        registry.UnRegisterResource(*state.buffer);
      }
      if (state.mapped_ptr != nullptr) {
        state.buffer->UnMap();
        state.mapped_ptr = nullptr;
      }
      state.buffer.reset();
      state.srv_view = {};
      state.srv_index = kInvalidShaderVisibleIndex;
    }
  }
}

auto EnvironmentStaticDataManager::OnFrameStart(
  renderer::RendererTag /*tag*/, frame::Slot slot) -> void
{
  current_slot_ = slot;
}

auto EnvironmentStaticDataManager::UpdateIfNeeded(renderer::RendererTag /*tag*/,
  const RenderContext& context, const ViewId view_id) -> void
{
  LoadViewState(view_id);
  active_view_id_ = view_id;
  const auto sky_lut_provider = context.current_view.atmo_lut_manager
    ? observer_ptr<ISkyAtmosphereLutProvider>(
        context.current_view.atmo_lut_manager.get())
    : nullptr;

  last_update_frame_slot_ = context.frame_slot;
  last_update_frame_sequence_ = context.frame_sequence;

  if (current_slot_ != frame::kInvalidSlot
    && context.frame_slot != frame::kInvalidSlot
    && current_slot_ != context.frame_slot) {
    static frame::SequenceNumber last_logged_mismatch_seq { 0 };
    if (last_logged_mismatch_seq != context.frame_sequence) {
      LOG_F(ERROR,
        "EnvStatic: frame slot mismatch (current_slot={} ctx_slot={} "
        "frame_seq={})",
        current_slot_.get(), context.frame_slot.get(),
        context.frame_sequence.get());
      last_logged_mismatch_seq = context.frame_sequence;
    }
  }

  observer_ptr<const scene::SceneEnvironment> env = nullptr;
  if (const auto scene_ptr = context.GetScene()) {
    env = scene_ptr->GetEnvironment();
    BuildFromSceneEnvironment(env, sky_lut_provider);
  }

  RefreshCoherentSnapshotState();
  UploadIfNeeded();
  StoreViewState(view_id);
}

auto EnvironmentStaticDataManager::EnforceBarriers(
  graphics::CommandRecorder& recorder) -> void
{
  if (brdf_lut_texture_) {
    // If not yet transitioned, start from kCommon (upload/decay state)
    // and transition to kShaderResource.
    // If already transitioned, start from kShaderResource.
    // NEVER restore to initial state (keep_initial_state = false) so it stays
    // in kShaderResource on the Graphics queue.
    const auto initial_state = brdf_lut_transitioned_
      ? graphics::ResourceStates::kShaderResource
      : graphics::ResourceStates::kCommon;

    // IMPORTANT: force_submit=true. If the resource is already tracked in
    // kShaderResource, we still want to ensure this tracking intent is
    // registered to the command recorder so it can validate the state.
    if (!recorder.IsResourceTracked(*brdf_lut_texture_)) {
      recorder.BeginTrackingResourceState(
        *brdf_lut_texture_, initial_state, false);
    }
    recorder.RequireResourceState(
      *brdf_lut_texture_, graphics::ResourceStates::kShaderResource);

    recorder.FlushBarriers();
    brdf_lut_transitioned_ = true;
  }
}

auto EnvironmentStaticDataManager::GetOrCreateViewState(const ViewId view_id)
  -> ViewState&
{
  auto [it, inserted] = view_states_.try_emplace(view_id);
  if (inserted) {
    it->second.slot_uploaded_id.fill(0);
  }
  return it->second;
}

auto EnvironmentStaticDataManager::LoadViewState(const ViewId view_id) -> void
{
  auto& state = GetOrCreateViewState(view_id);
  cpu_snapshot_ = state.cpu_snapshot;
  published_snapshot_ = state.published_snapshot;
  has_published_snapshot_ = state.has_published_snapshot;
  snapshot_id_ = state.snapshot_id;
  slot_uploaded_id_ = state.slot_uploaded_id;
  last_capture_generation_ = state.last_capture_generation;
  last_published_atmo_content_version_
    = state.last_published_atmo_content_version;
  last_warned_capture_missing_source_generation_
    = state.last_warned_capture_missing_source_generation;
  last_warned_capture_outputs_not_ready_generation_
    = state.last_warned_capture_outputs_not_ready_generation;
  last_warned_capture_stale_ibl_generation_
    = state.last_warned_capture_stale_ibl_generation;
  last_observed_ibl_source_content_version_
    = state.last_observed_ibl_source_content_version;
  last_coherent_snapshot_ = state.last_coherent_snapshot;
  has_last_coherent_snapshot_ = state.has_last_coherent_snapshot;
  incoherent_frame_count_ = state.incoherent_frame_count;
  last_incoherent_logged_sequence_ = state.last_incoherent_logged_sequence;
  ibl_matches_capture_content_ = state.ibl_matches_capture_content;
  use_last_coherent_fallback_ = state.use_last_coherent_fallback;
  coherence_threshold_crossed_ = state.coherence_threshold_crossed;
  ibl_regeneration_requested_ = state.ibl_regeneration_requested;
  buffer_ = state.buffer;
  mapped_ptr_ = state.mapped_ptr;
  srv_view_ = state.srv_view;
  srv_index_ = state.srv_index;
}

auto EnvironmentStaticDataManager::StoreViewState(const ViewId view_id) -> void
{
  auto& state = GetOrCreateViewState(view_id);
  state.cpu_snapshot = cpu_snapshot_;
  state.published_snapshot = published_snapshot_;
  state.has_published_snapshot = has_published_snapshot_;
  state.snapshot_id = snapshot_id_;
  state.slot_uploaded_id = slot_uploaded_id_;
  state.last_capture_generation = last_capture_generation_;
  state.last_published_atmo_content_version
    = last_published_atmo_content_version_;
  state.last_warned_capture_missing_source_generation
    = last_warned_capture_missing_source_generation_;
  state.last_warned_capture_outputs_not_ready_generation
    = last_warned_capture_outputs_not_ready_generation_;
  state.last_warned_capture_stale_ibl_generation
    = last_warned_capture_stale_ibl_generation_;
  state.last_observed_ibl_source_content_version
    = last_observed_ibl_source_content_version_;
  state.last_coherent_snapshot = last_coherent_snapshot_;
  state.has_last_coherent_snapshot = has_last_coherent_snapshot_;
  state.incoherent_frame_count = incoherent_frame_count_;
  state.last_incoherent_logged_sequence = last_incoherent_logged_sequence_;
  state.ibl_matches_capture_content = ibl_matches_capture_content_;
  state.use_last_coherent_fallback = use_last_coherent_fallback_;
  state.coherence_threshold_crossed = coherence_threshold_crossed_;
  state.ibl_regeneration_requested = ibl_regeneration_requested_;
  state.buffer = buffer_;
  state.mapped_ptr = mapped_ptr_;
  state.srv_view = srv_view_;
  state.srv_index = srv_index_;
}

auto EnvironmentStaticDataManager::RequestIblRegeneration(
  const ViewId view_id) noexcept -> void
{
  GetOrCreateViewState(view_id).ibl_regeneration_requested = true;
}

auto EnvironmentStaticDataManager::IsIblRegenerationRequested(
  const ViewId view_id) const noexcept -> bool
{
  if (const auto it = view_states_.find(view_id); it != view_states_.end()) {
    return it->second.ibl_regeneration_requested;
  }
  return false;
}

auto EnvironmentStaticDataManager::MarkIblRegenerationClean(
  const ViewId view_id) noexcept -> void
{
  if (const auto it = view_states_.find(view_id); it != view_states_.end()) {
    it->second.ibl_regeneration_requested = false;
  }
}

auto EnvironmentStaticDataManager::GetSrvIndex(
  const ViewId view_id) const noexcept -> ShaderVisibleIndex
{
  if (const auto it = view_states_.find(view_id); it != view_states_.end()) {
    return it->second.srv_index;
  }
  return kInvalidShaderVisibleIndex;
}

auto EnvironmentStaticDataManager::GetSkyLightCubemapSlot(
  const ViewId view_id) const noexcept -> ShaderVisibleIndex
{
  if (const auto it = view_states_.find(view_id); it != view_states_.end()) {
    const auto& snapshot = it->second.published_snapshot;
    if (it->second.has_published_snapshot && (snapshot.sky_light.enabled != 0U)
      && snapshot.sky_light.cubemap_slot.IsValid()) {
      return ShaderVisibleIndex { snapshot.sky_light.cubemap_slot.value };
    }
  }
  return kInvalidShaderVisibleIndex;
}

auto EnvironmentStaticDataManager::GetSkyLightCubemapSlot() const noexcept
  -> ShaderVisibleIndex
{
  if (active_view_id_ == kInvalidViewId) {
    return kInvalidShaderVisibleIndex;
  }
  return GetSkyLightCubemapSlot(active_view_id_);
}

auto EnvironmentStaticDataManager::IsSkyLightCapturedSceneSource(
  const ViewId view_id) const noexcept -> bool
{
  if (const auto it = view_states_.find(view_id); it != view_states_.end()) {
    const auto& snapshot = it->second.published_snapshot;
    return it->second.has_published_snapshot && snapshot.sky_light.enabled != 0U
      && snapshot.sky_light.source == SkyLightSource::kCapturedScene;
  }
  return false;
}

auto EnvironmentStaticDataManager::IsSkyLightCapturedSceneSource()
  const noexcept -> bool
{
  if (active_view_id_ == kInvalidViewId) {
    return false;
  }
  return IsSkyLightCapturedSceneSource(active_view_id_);
}

auto EnvironmentStaticDataManager::GetSkySphereCubemapSlot(
  const ViewId view_id) const noexcept -> ShaderVisibleIndex
{
  if (const auto it = view_states_.find(view_id); it != view_states_.end()) {
    const auto& snapshot = it->second.published_snapshot;
    if (it->second.has_published_snapshot && (snapshot.sky_sphere.enabled != 0U)
      && snapshot.sky_sphere.cubemap_slot.IsValid()) {
      return ShaderVisibleIndex { snapshot.sky_sphere.cubemap_slot.value };
    }
  }
  return kInvalidShaderVisibleIndex;
}

auto EnvironmentStaticDataManager::GetSkySphereCubemapSlot() const noexcept
  -> ShaderVisibleIndex
{
  if (active_view_id_ == kInvalidViewId) {
    return kInvalidShaderVisibleIndex;
  }
  return GetSkySphereCubemapSlot(active_view_id_);
}

auto EnvironmentStaticDataManager::EraseViewState(const ViewId view_id) -> void
{
  if (const auto it = view_states_.find(view_id); it != view_states_.end()) {
    auto& state = it->second;
    if (state.buffer != nullptr) {
      auto& registry = gfx_->GetResourceRegistry();
      if (registry.Contains(*state.buffer)) {
        registry.UnRegisterResource(*state.buffer);
      }
      if (state.mapped_ptr != nullptr) {
        state.buffer->UnMap();
      }
    }
    view_states_.erase(it);
  }
  if (active_view_id_ == view_id) {
    active_view_id_ = kInvalidViewId;
  }
}

auto EnvironmentStaticDataManager::BuildFromSceneEnvironment(
  observer_ptr<const scene::SceneEnvironment> env,
  observer_ptr<ISkyAtmosphereLutProvider> sky_lut_provider) -> void
{
  EnvironmentStaticData next {};

  // Handle BRDF LUT provider and possible changes to the LUT slot.
  ProcessBrdfLut();

  if (env) {
    PopulateFog(env, next);
    PopulateAtmosphere(env, next, sky_lut_provider);
    PopulateSkyLight(env, next);
    PopulateSkySphere(env, next);
    PopulateSkyCapture(next);
    PopulateIbl(next);
    PopulateClouds(env, next);
    PopulatePostProcess(env, next);
  }

  if (std::memcmp(&next, &cpu_snapshot_, sizeof(EnvironmentStaticData)) != 0) {
    auto format_slot =
      []<typename T>
      requires requires(const T& v) {
        { v.IsValid() } -> std::convertible_to<bool>;
        { v.value } -> std::convertible_to<ShaderVisibleIndex>;
      }(const T& slot)
    -> std::string {
        if (!slot.IsValid()) {
          return "not ready";
        }

        return nostd::to_string(slot.value);
      };

    const auto old_snapshot_id = snapshot_id_;
    const auto u_slot_index = CurrentSlotIndex();
    const auto old_uploaded_id = u_slot_index < slot_uploaded_id_.size()
      ? slot_uploaded_id_[u_slot_index]
      : 0ULL;

    const auto& old_sl = cpu_snapshot_.sky_light;
    const auto& old_ss = cpu_snapshot_.sky_sphere;
    const auto& old_pp = cpu_snapshot_.post_process;
    const auto& old_atmo = cpu_snapshot_.atmosphere;
    const auto& next_sl = next.sky_light;
    const auto& next_ss = next.sky_sphere;
    const auto& next_pp = next.post_process;
    const auto& next_atmo = next.atmosphere;

    LOG_F(INFO,
      "EnvStatic: snapshot changed (snapshot_id={} slot={} srv={} "
      "uploaded_id={}) "
      "skylight(en:{}->{} src:{}->{} cube:{}->{} ) "
      "skysphere(en:{}->{} src:{}->{} cube:{}->{} ) "
      "pp(en:{}->{} mapper:{}->{} exp_mode:{}->{})",
      old_snapshot_id, u_slot_index, srv_index_.get(), old_uploaded_id,
      old_sl.enabled, next_sl.enabled, static_cast<uint32_t>(old_sl.source),
      static_cast<uint32_t>(next_sl.source), format_slot(old_sl.cubemap_slot),
      format_slot(next_sl.cubemap_slot), old_ss.enabled, next_ss.enabled,
      static_cast<uint32_t>(old_ss.source),
      static_cast<uint32_t>(next_ss.source), format_slot(old_ss.cubemap_slot),
      format_slot(next_ss.cubemap_slot), old_pp.enabled, next_pp.enabled,
      static_cast<uint32_t>(old_pp.tone_mapper),
      static_cast<uint32_t>(next_pp.tone_mapper),
      static_cast<uint32_t>(old_pp.exposure_mode),
      static_cast<uint32_t>(next_pp.exposure_mode));

    if (std::memcmp(&old_atmo, &next_atmo, sizeof(old_atmo)) != 0) {
      LOG_F(INFO,
        "EnvStatic: atmosphere changed (enabled:{}->{} trans:{}->{} sky:{}->{} "
        "ms:{}->{} irr:{}->{} cv:{}->{} bn:{}->{})",
        old_atmo.enabled, next_atmo.enabled,
        format_slot(old_atmo.transmittance_lut_slot),
        format_slot(next_atmo.transmittance_lut_slot),
        format_slot(old_atmo.sky_view_lut_slot),
        format_slot(next_atmo.sky_view_lut_slot),
        format_slot(old_atmo.multi_scat_lut_slot),
        format_slot(next_atmo.multi_scat_lut_slot),
        format_slot(old_atmo.sky_irradiance_lut_slot),
        format_slot(next_atmo.sky_irradiance_lut_slot),
        format_slot(old_atmo.camera_volume_lut_slot),
        format_slot(next_atmo.camera_volume_lut_slot),
        format_slot(old_atmo.blue_noise_slot),
        format_slot(next_atmo.blue_noise_slot));

      if (next_sl.enabled != 0U
        && next_sl.source == SkyLightSource::kCapturedScene
        && old_sl.ibl_generation == next_sl.ibl_generation) {
        const auto content_lag
          = last_capture_generation_ > last_observed_ibl_source_content_version_
          ? last_capture_generation_ - last_observed_ibl_source_content_version_
          : 0ULL;
        if (content_lag > 1ULL) {
          LOG_F(WARNING,
            "EnvStatic: Atmosphere LUTs changed but SkyLight IBL content is "
            "stale "
            "(ibl_gen={} capture_gen={} ibl_src_ver={} lag={} view={})",
            next_sl.ibl_generation, last_capture_generation_,
            last_observed_ibl_source_content_version_, content_lag,
            active_view_id_.get());
        }
      }
    }

    if (old_sl.irradiance_map_slot != next_sl.irradiance_map_slot
      || old_sl.prefilter_map_slot != next_sl.prefilter_map_slot
      || old_sl.ibl_generation != next_sl.ibl_generation) {
      LOG_F(INFO,
        "EnvStatic: skylight IBL outputs changed (gen:{}->{} irr:{}->{} "
        "pref:{}->{} max_mip:{}->{})",
        old_sl.ibl_generation, next_sl.ibl_generation,
        format_slot(old_sl.irradiance_map_slot),
        format_slot(next_sl.irradiance_map_slot),
        format_slot(old_sl.prefilter_map_slot),
        format_slot(next_sl.prefilter_map_slot), old_sl.prefilter_max_mip,
        next_sl.prefilter_max_mip);
    }

    cpu_snapshot_ = next;
    MarkAllSlotsDirty();

    LOG_F(INFO, "EnvStatic: snapshot invalidated (snapshot_id {}->{} slot={})",
      old_snapshot_id, snapshot_id_, u_slot_index);
  }
}

auto EnvironmentStaticDataManager::ProcessBrdfLut() -> void
{
  if (brdf_lut_provider_) {
    const auto [tex, slot] = brdf_lut_provider_->GetOrCreateLut();
    if (slot != brdf_lut_slot_) {
      LOG_F(INFO, "EnvStatic: BRDF LUT slot changed ({} -> {})",
        brdf_lut_slot_.get(), slot.get());
      brdf_lut_slot_ = slot;
      brdf_lut_texture_ = tex;
      brdf_lut_transitioned_ = false;
      MarkAllSlotsDirty();
    }
  }
}

auto EnvironmentStaticDataManager::PopulateFog(
  observer_ptr<const scene::SceneEnvironment> env, EnvironmentStaticData& next)
  -> void
{
  if (const auto fog = env->TryGetSystem<scene::environment::Fog>();
    fog && fog->IsEnabled()) {
    next.fog.enabled = 1U;
    next.fog.model = ToGpuFogModel(fog->GetModel());
    next.fog.extinction_sigma_t_per_m = fog->GetExtinctionSigmaTPerMeter();
    next.fog.height_falloff_per_m = fog->GetHeightFalloffPerMeter();
    next.fog.height_offset_m = fog->GetHeightOffsetMeters();
    next.fog.start_distance_m = fog->GetStartDistanceMeters();
    next.fog.max_opacity = fog->GetMaxOpacity();
    next.fog.single_scattering_albedo_rgb = fog->GetSingleScatteringAlbedoRgb();
    next.fog.anisotropy_g = fog->GetAnisotropy();
  }
}

auto EnvironmentStaticDataManager::PopulateAtmosphere(
  observer_ptr<const scene::SceneEnvironment> env, EnvironmentStaticData& next,
  observer_ptr<ISkyAtmosphereLutProvider> sky_lut_provider) -> void
{
  if (const auto atmo = env->TryGetSystem<scene::environment::SkyAtmosphere>();
    atmo && atmo->IsEnabled()) {
    next.atmosphere.enabled = 1U;
    next.atmosphere.planet_radius_m = atmo->GetPlanetRadiusMeters();
    next.atmosphere.atmosphere_height_m = atmo->GetAtmosphereHeightMeters();
    next.atmosphere.ground_albedo_rgb = atmo->GetGroundAlbedoRgb();
    next.atmosphere.rayleigh_scattering_rgb = atmo->GetRayleighScatteringRgb();
    next.atmosphere.rayleigh_scale_height_m
      = atmo->GetRayleighScaleHeightMeters();
    next.atmosphere.mie_scattering_rgb = atmo->GetMieScatteringRgb();
    next.atmosphere.mie_extinction_rgb
      = next.atmosphere.mie_scattering_rgb + atmo->GetMieAbsorptionRgb();
    next.atmosphere.mie_scale_height_m = atmo->GetMieScaleHeightMeters();
    next.atmosphere.mie_g = atmo->GetMieAnisotropy();
    next.atmosphere.absorption_rgb = atmo->GetAbsorptionRgb();
    next.atmosphere.absorption_density = atmo->GetOzoneDensityProfile();
    next.atmosphere.multi_scattering_factor = atmo->GetMultiScatteringFactor();

    const bool atmo_disk_enabled = atmo->GetSunDiskEnabled();
    float sun_disk_radius
      = scene::environment::Sun::kDefaultDiskAngularRadiusRad;
    if (const auto sun = env->TryGetSystem<scene::environment::Sun>(); sun) {
      sun_disk_radius = sun->GetDiskAngularRadiusRadians();
    }
    // Even if Sun system is present, respect the Atmosphere's explicit sun
    // disk toggle. This allows UI to hide the sun disk without disabling the
    // sun light.
    next.atmosphere.sun_disk_enabled
      = (atmo_disk_enabled && sun_disk_radius > 0.0F) ? 1U : 0U;
    next.atmosphere.sun_disk_angular_radius_radians = sun_disk_radius;
    DLOG_F(3,
      "PopulateAtmosphere: sun disk (enabled={}, radius={}, atmo_toggle={})",
      next.atmosphere.sun_disk_enabled,
      next.atmosphere.sun_disk_angular_radius_radians, atmo_disk_enabled);
    next.atmosphere.aerial_perspective_distance_scale
      = atmo->GetAerialPerspectiveDistanceScale();

    if (sky_lut_provider) {
      // Slots are populated later, conditional on generation state, to prevent
      // exposing uninitialized textures which cause black artifacts.

      const auto [trans_w, trans_h]
        = sky_lut_provider->GetTransmittanceLutSize();
      const auto [sky_w, sky_h] = sky_lut_provider->GetSkyViewLutSize();
      const auto [sky_irr_w, sky_irr_h]
        = sky_lut_provider->GetSkyIrradianceLutSize();

      next.atmosphere.transmittance_lut_width = static_cast<float>(trans_w);
      next.atmosphere.transmittance_lut_height = static_cast<float>(trans_h);
      next.atmosphere.sky_view_lut_width = static_cast<float>(sky_w);
      next.atmosphere.sky_view_lut_height = static_cast<float>(sky_h);
      next.atmosphere.sky_irradiance_lut_width = static_cast<float>(sky_irr_w);
      next.atmosphere.sky_irradiance_lut_height = static_cast<float>(sky_irr_h);

      // Populate altitude-slice fields from the LUT provider [T3].
      next.atmosphere.sky_view_lut_slices
        = sky_lut_provider->GetSkyViewLutSlices();
      next.atmosphere.sky_view_alt_mapping_mode
        = sky_lut_provider->GetAltMappingMode();

      const auto copy_latched_slots = [this, &next]() -> void {
        next.atmosphere.transmittance_lut_slot
          = cpu_snapshot_.atmosphere.transmittance_lut_slot;
        next.atmosphere.sky_view_lut_slot
          = cpu_snapshot_.atmosphere.sky_view_lut_slot;
        next.atmosphere.multi_scat_lut_slot
          = cpu_snapshot_.atmosphere.multi_scat_lut_slot;
        next.atmosphere.sky_irradiance_lut_slot
          = cpu_snapshot_.atmosphere.sky_irradiance_lut_slot;
        next.atmosphere.camera_volume_lut_slot
          = cpu_snapshot_.atmosphere.camera_volume_lut_slot;
        next.atmosphere.blue_noise_slot = blue_noise_enabled_
          ? cpu_snapshot_.atmosphere.blue_noise_slot
          : BlueNoiseSlot { kInvalidShaderVisibleIndex };
      };

      const bool has_latched_slots
        = cpu_snapshot_.atmosphere.transmittance_lut_slot.IsValid()
        && cpu_snapshot_.atmosphere.sky_view_lut_slot.IsValid()
        && cpu_snapshot_.atmosphere.multi_scat_lut_slot.IsValid()
        && cpu_snapshot_.atmosphere.sky_irradiance_lut_slot.IsValid()
        && cpu_snapshot_.atmosphere.camera_volume_lut_slot.IsValid();

      const bool generated = sky_lut_provider->HasBeenGenerated();
      const auto content_version = sky_lut_provider->GetContentVersion();
      const auto trans_slot = sky_lut_provider->GetTransmittanceLutSlot();
      const auto sky_slot = sky_lut_provider->GetSkyViewLutSlot();
      const auto ms_slot = sky_lut_provider->GetMultiScatLutSlot();
      const auto irr_slot = sky_lut_provider->GetSkyIrradianceLutSlot();
      const auto cv_slot = sky_lut_provider->GetCameraVolumeLutSlot();
      const auto bn_slot = sky_lut_provider->GetBlueNoiseSlot();
      const bool all_required_slots_valid = trans_slot.IsValid()
        && sky_slot.IsValid() && ms_slot.IsValid() && irr_slot.IsValid()
        && cv_slot.IsValid();

      if (generated && content_version > last_published_atmo_content_version_
        && all_required_slots_valid) {
        next.atmosphere.transmittance_lut_slot
          = TransmittanceLutSlot { trans_slot };
        next.atmosphere.sky_view_lut_slot = SkyViewLutSlot { sky_slot };
        next.atmosphere.multi_scat_lut_slot = MultiScatLutSlot { ms_slot };
        next.atmosphere.sky_irradiance_lut_slot
          = SkyIrradianceLutSlot { irr_slot };
        next.atmosphere.camera_volume_lut_slot
          = CameraVolumeLutSlot { cv_slot };
        next.atmosphere.blue_noise_slot = blue_noise_enabled_
          ? BlueNoiseSlot { bn_slot }
          : BlueNoiseSlot { kInvalidShaderVisibleIndex };
        last_published_atmo_content_version_ = content_version;
      } else if (has_latched_slots) {
        copy_latched_slots();
      }
    }
  } else {
    last_published_atmo_content_version_ = 0;
  }
}

auto EnvironmentStaticDataManager::PopulateSkyLight(
  observer_ptr<const scene::SceneEnvironment> env, EnvironmentStaticData& next)
  -> void
{
  if (const auto sky_light = env->TryGetSystem<scene::environment::SkyLight>();
    sky_light && sky_light->IsEnabled()) {
    next.sky_light.enabled = 1U;
    next.sky_light.source = ToGpuSkyLightSource(sky_light->GetSource());

    // `intensity_mul` is authored as a unitless multiplier.
    // For non-physical sources (cubemaps), we bridge the unit gap by assuming
    // 1.0 intensity_mul = 5000 Nits (Standard Sky). Procedural atmosphere
    // remains at its native physical scale.
    const float intensity_mul = sky_light->GetIntensityMul();
    const float unit_bridge
      = (next.sky_light.source == SkyLightSource::kSpecifiedCubemap)
      ? atmos::kStandardSkyLuminance
      : 1.0F;
    next.sky_light.radiance_scale = intensity_mul * unit_bridge;

    next.sky_light.tint_rgb = sky_light->GetTintRgb();
    next.sky_light.diffuse_intensity = sky_light->GetDiffuseIntensity();
    next.sky_light.specular_intensity = sky_light->GetSpecularIntensity();
    next.sky_light.brdf_lut_slot = BrdfLutSlot { brdf_lut_slot_ };

    next.sky_light.cubemap_max_mip = 0U;
    next.sky_light.prefilter_max_mip = 0U;

    if (texture_binder_
      && sky_light->GetSource()
        == scene::environment::SkyLightSource::kSpecifiedCubemap
      && !sky_light->GetCubemapResource().IsPlaceholder()) {
      const auto key = sky_light->GetCubemapResource();
      const auto slot = texture_binder_->GetOrAllocate(key);
      const bool cubemap_ready = texture_binder_->IsResourceReady(key);
      next.sky_light.cubemap_slot
        = CubeMapSlot { cubemap_ready ? slot : kInvalidShaderVisibleIndex };
      if (cubemap_ready) {
        if (const auto mips = texture_binder_->TryGetMipLevels(key);
          mips.has_value() && *mips > 0U) {
          next.sky_light.cubemap_max_mip = *mips - 1U;
        }
      }
    } else {
      next.sky_light.cubemap_slot = CubeMapSlot { kInvalidShaderVisibleIndex };
    }
  }
}

auto EnvironmentStaticDataManager::PopulateSkySphere(
  observer_ptr<const scene::SceneEnvironment> env, EnvironmentStaticData& next)
  -> void
{
  if (const auto sky_sphere
    = env->TryGetSystem<scene::environment::SkySphere>();
    sky_sphere && sky_sphere->IsEnabled()) {
    if (next.atmosphere.enabled != 0U) {
      LOG_F(WARNING,
        "Both SkyAtmosphere and SkySphere are enabled. They are mutually "
        "exclusive; SkyAtmosphere will take priority for sky rendering.");
    }

    next.sky_sphere.enabled = 1U;
    next.sky_sphere.source = ToGpuSkySphereSource(sky_sphere->GetSource());
    next.sky_sphere.solid_color_rgb = sky_sphere->GetSolidColorRgb();

    // Bridging non-physical assets to 5000 Nit physical baseline.
    const float intensity = sky_sphere->GetIntensity();
    const float unit_bridge
      = (next.sky_sphere.source == SkySphereSource::kCubemap
          || next.sky_sphere.source == SkySphereSource::kSolidColor)
      ? atmos::kStandardSkyLuminance
      : 1.0F;
    next.sky_sphere.intensity = intensity * unit_bridge;

    next.sky_sphere.rotation_radians = sky_sphere->GetRotationRadians();
    next.sky_sphere.tint_rgb = sky_sphere->GetTintRgb();

    next.sky_sphere.cubemap_max_mip = 0U;

    if (texture_binder_
      && sky_sphere->GetSource()
        == scene::environment::SkySphereSource::kCubemap
      && !sky_sphere->GetCubemapResource().IsPlaceholder()) {
      const auto key = sky_sphere->GetCubemapResource();
      const auto slot = texture_binder_->GetOrAllocate(key);
      const bool cubemap_ready = texture_binder_->IsResourceReady(key);
      next.sky_sphere.cubemap_slot
        = CubeMapSlot { cubemap_ready ? slot : kInvalidShaderVisibleIndex };
      if (cubemap_ready) {
        if (const auto mips = texture_binder_->TryGetMipLevels(key);
          mips.has_value() && *mips > 0U) {
          next.sky_sphere.cubemap_max_mip = *mips - 1U;
        }
      }
    } else {
      next.sky_sphere.cubemap_slot = CubeMapSlot { kInvalidShaderVisibleIndex };
    }
  }
}

auto EnvironmentStaticDataManager::PopulateSkyCapture(
  EnvironmentStaticData& next) -> void
{
  if (sky_capture_provider_) {
    const auto capture_gen
      = sky_capture_provider_->GetCaptureGeneration(active_view_id_);
    if (capture_gen != last_capture_generation_) {
      LOG_F(INFO, "EnvStatic: sky capture generation changed ({} -> {})",
        last_capture_generation_, capture_gen);
      last_capture_generation_ = capture_gen;
      MarkAllSlotsDirty();
    }

    // If SkyLight source is kCapturedScene, we provide the captured cubemap
    // slot. This is used by IblComputePass to decide which source to filter.
    if (next.sky_light.enabled != 0U
      && next.sky_light.source == SkyLightSource::kCapturedScene) {
      // Keep publishing the captured cubemap slot even when a re-capture is
      // pending (IsCaptured()==false). This avoids transient black IBL while
      // UI interactions (e.g., sun elevation dragging) continuously mark the
      // capture dirty.
      const auto captured_slot
        = sky_capture_provider_->GetCapturedCubemapSlot(active_view_id_);
      next.sky_light.cubemap_slot = CubeMapSlot { captured_slot };
    }
  } else {
    LOG_F(INFO, "PopulateSkyCapture: Provider not available");
  }
}

auto EnvironmentStaticDataManager::PopulateIbl(EnvironmentStaticData& next)
  -> void
{
  ibl_matches_capture_content_ = true;

  if (next.sky_light.enabled == 0U || !ibl_provider_) {
    return;
  }

  const bool captured_scene_source
    = next.sky_light.source == SkyLightSource::kCapturedScene;
  const std::uint64_t capture_gen = sky_capture_provider_
    ? sky_capture_provider_->GetCaptureGeneration(active_view_id_)
    : 0U;

  const bool has_source = next.sky_light.cubemap_slot.IsValid()
    || (next.sky_sphere.enabled != 0U
      && next.sky_sphere.cubemap_slot.IsValid());

  if (!has_source) {
    if (captured_scene_source) {
      ibl_matches_capture_content_ = false;
    }
    if (captured_scene_source
      && capture_gen != last_warned_capture_missing_source_generation_) {
      LOG_F(WARNING,
        "EnvStatic: captured-scene SkyLight has no valid source cubemap "
        "(view={} capture_gen={} ibl_gen={} atmo_T={} atmo_V={})",
        active_view_id_.get(), capture_gen,
        cpu_snapshot_.sky_light.ibl_generation,
        cpu_snapshot_.atmosphere.transmittance_lut_slot.value.get(),
        cpu_snapshot_.atmosphere.sky_view_lut_slot.value.get());
      last_warned_capture_missing_source_generation_ = capture_gen;
    }

    // During sky-capture transitions (e.g. atmosphere slider updates), the
    // cubemap source can be temporarily unavailable. Avoid flashing by keeping
    // the last known valid IBL outputs until a new source and its filtered
    // outputs become available.
    if (cpu_snapshot_.sky_light.irradiance_map_slot.IsValid()
      && cpu_snapshot_.sky_light.prefilter_map_slot.IsValid()) {
      next.sky_light.irradiance_map_slot
        = cpu_snapshot_.sky_light.irradiance_map_slot;
      next.sky_light.prefilter_map_slot
        = cpu_snapshot_.sky_light.prefilter_map_slot;
      next.sky_light.prefilter_max_mip
        = cpu_snapshot_.sky_light.prefilter_max_mip;
      next.sky_light.ibl_generation = cpu_snapshot_.sky_light.ibl_generation;
    } else {
      next.sky_light.irradiance_map_slot = IrradianceMapSlot {};
      next.sky_light.prefilter_map_slot = PrefilterMapSlot {};
      next.sky_light.prefilter_max_mip = 0U;
      next.sky_light.ibl_generation = 0U;
    }
    return;
  }

  const auto source_slot
    = ShaderVisibleIndex { next.sky_light.cubemap_slot.IsValid()
          ? next.sky_light.cubemap_slot.value
          : next.sky_sphere.cubemap_slot.value };

  const auto outputs
    = ibl_provider_->QueryOutputsFor(active_view_id_, source_slot);
  last_observed_ibl_source_content_version_ = outputs.source_content_version;

  if (!outputs.irradiance.IsValid() || !outputs.prefilter.IsValid()) {
    if (captured_scene_source) {
      ibl_matches_capture_content_ = false;
    }
    if (captured_scene_source
      && capture_gen != last_warned_capture_outputs_not_ready_generation_) {
      LOG_F(WARNING,
        "EnvStatic: captured-scene SkyLight IBL outputs not ready "
        "(view={} capture_gen={} source_slot={} prev_ibl_gen={})",
        active_view_id_.get(), capture_gen, source_slot.get(),
        cpu_snapshot_.sky_light.ibl_generation);
      last_warned_capture_outputs_not_ready_generation_ = capture_gen;
    }

    // Source is available, but filtered outputs are not ready yet (compute
    // pass will generate them). Keep previous valid IBL to avoid transient
    // black frames.
    if (cpu_snapshot_.sky_light.irradiance_map_slot.IsValid()
      && cpu_snapshot_.sky_light.prefilter_map_slot.IsValid()) {
      next.sky_light.irradiance_map_slot
        = cpu_snapshot_.sky_light.irradiance_map_slot;
      next.sky_light.prefilter_map_slot
        = cpu_snapshot_.sky_light.prefilter_map_slot;
      next.sky_light.prefilter_max_mip
        = cpu_snapshot_.sky_light.prefilter_max_mip;
      next.sky_light.ibl_generation = cpu_snapshot_.sky_light.ibl_generation;
    }
    return;
  }

  next.sky_light.irradiance_map_slot = IrradianceMapSlot { outputs.irradiance };
  next.sky_light.prefilter_map_slot = PrefilterMapSlot { outputs.prefilter };
  next.sky_light.ibl_generation = static_cast<uint32_t>(outputs.generation);

  const auto capture_to_ibl_content_lag
    = capture_gen > outputs.source_content_version
    ? capture_gen - outputs.source_content_version
    : 0ULL;
  if (captured_scene_source && capture_to_ibl_content_lag > 1ULL
    && capture_gen != last_warned_capture_stale_ibl_generation_) {
    LOG_F(ERROR,
      "EnvStatic: captured-scene SkyLight using stale IBL generation "
      "(view={} capture_gen={} ibl_gen={} ibl_src_ver={} lag={} "
      "source_slot={})",
      active_view_id_.get(), capture_gen, outputs.generation,
      outputs.source_content_version, capture_to_ibl_content_lag,
      source_slot.get());
    last_warned_capture_stale_ibl_generation_ = capture_gen;
  }

  if (captured_scene_source) {
    // Allow one-generation lag because capture and IBL run sequentially in the
    // frame; this avoids false incoherence while work is in flight.
    ibl_matches_capture_content_ = capture_to_ibl_content_lag <= 1ULL;
  }

  if (captured_scene_source && capture_to_ibl_content_lag <= 1ULL) {
    last_warned_capture_stale_ibl_generation_ = 0U;
    last_warned_capture_outputs_not_ready_generation_ = 0U;
    last_warned_capture_missing_source_generation_ = 0U;
  }

  if (outputs.prefilter_mip_levels > 0U) {
    next.sky_light.prefilter_max_mip = outputs.prefilter_mip_levels - 1U;
  } else {
    next.sky_light.prefilter_max_mip = 0U;
  }
}

auto EnvironmentStaticDataManager::PopulateClouds(
  observer_ptr<const scene::SceneEnvironment> env, EnvironmentStaticData& next)
  -> void
{
  if (const auto clouds
    = env->TryGetSystem<scene::environment::VolumetricClouds>();
    clouds && clouds->IsEnabled()) {
    next.clouds.enabled = 1U;
    next.clouds.base_altitude_m = clouds->GetBaseAltitudeMeters();
    next.clouds.layer_thickness_m = clouds->GetLayerThicknessMeters();
    next.clouds.coverage = clouds->GetCoverage();
    next.clouds.extinction_sigma_t_per_m
      = clouds->GetExtinctionSigmaTPerMeter();
    next.clouds.single_scattering_albedo_rgb
      = clouds->GetSingleScatteringAlbedoRgb();
    next.clouds.phase_g = clouds->GetPhaseAnisotropy();
    next.clouds.wind_dir_ws = clouds->GetWindDirectionWs();
    next.clouds.wind_speed_mps = clouds->GetWindSpeedMps();
    next.clouds.shadow_strength = clouds->GetShadowStrength();
  }
}

auto EnvironmentStaticDataManager::PopulatePostProcess(
  observer_ptr<const scene::SceneEnvironment> env, EnvironmentStaticData& next)
  -> void
{
  if (kDisablePostProcessVolumeForTesting) {
    (void)env;
    (void)next;
    return;
  }

  if (const auto pp
    = env->TryGetSystem<scene::environment::PostProcessVolume>();
    pp && pp->IsEnabled()) {
    const auto prev = cpu_snapshot_.post_process;

    next.post_process.enabled = 1U;
    next.post_process.tone_mapper = pp->GetToneMapper();
    next.post_process.exposure_mode = pp->GetExposureMode();

    next.post_process.exposure_compensation
      = std::exp2(pp->GetExposureCompensationEv());

    next.post_process.auto_exposure_min_ev = pp->GetAutoExposureMinEv();
    next.post_process.auto_exposure_max_ev = pp->GetAutoExposureMaxEv();
    next.post_process.auto_exposure_speed_up = pp->GetAutoExposureSpeedUp();
    next.post_process.auto_exposure_speed_down = pp->GetAutoExposureSpeedDown();

    next.post_process.bloom_intensity = pp->GetBloomIntensity();
    next.post_process.bloom_threshold = pp->GetBloomThreshold();
    next.post_process.saturation = pp->GetSaturation();
    next.post_process.contrast = pp->GetContrast();
    next.post_process.vignette_intensity = pp->GetVignetteIntensity();

    if (std::memcmp(&prev, &next.post_process, sizeof(prev)) != 0) {
      LOG_F(INFO,
        "EnvStatic: PostProcessVolume changed (pp_enabled={}, exp_enabled={}, "
        "mode={}, comp_ev={:.3f}, key={:.6f}, tone_mapper={})",
        pp->IsEnabled(), pp->GetExposureEnabled(),
        static_cast<uint32_t>(pp->GetExposureMode()),
        pp->GetExposureCompensationEv(), pp->GetExposureKey(),
        static_cast<uint32_t>(next.post_process.tone_mapper));
    }
  }
}

auto EnvironmentStaticDataManager::RefreshCoherentSnapshotState() -> void
{
  constexpr uint32_t kFallbackWindowFrames = 3;
  constexpr uint32_t kPeriodicBlockedLogFrames = 30;

  bool coherent = true;
  const auto& sl = cpu_snapshot_.sky_light;
  const auto& atmo = cpu_snapshot_.atmosphere;
  const bool captured_scene_source
    = sl.enabled != 0U && sl.source == SkyLightSource::kCapturedScene;

  if (captured_scene_source) {
    const bool captured_ready = sky_capture_provider_ != nullptr
      && sky_capture_provider_->IsCaptured(active_view_id_);
    if (!captured_ready || !sl.cubemap_slot.IsValid()) {
      coherent = false;
    }
    if (atmo.enabled != 0U) {
      const bool atmo_slots_valid = atmo.transmittance_lut_slot.IsValid()
        && atmo.sky_view_lut_slot.IsValid()
        && atmo.multi_scat_lut_slot.IsValid()
        && atmo.sky_irradiance_lut_slot.IsValid()
        && atmo.camera_volume_lut_slot.IsValid();
      if (!atmo_slots_valid) {
        coherent = false;
      }
    }
    if (!sl.irradiance_map_slot.IsValid() || !sl.prefilter_map_slot.IsValid()) {
      coherent = false;
    }
    if (!ibl_matches_capture_content_) {
      coherent = false;
    }
  }

  current_snapshot_coherent_ = coherent;
  if (coherent) {
    last_coherent_snapshot_ = cpu_snapshot_;
    has_last_coherent_snapshot_ = true;
    use_last_coherent_fallback_ = false;
    coherence_threshold_crossed_ = false;
    incoherent_frame_count_ = 0;
    last_incoherent_logged_sequence_ = frame::SequenceNumber { 0 };
    return;
  }

  use_last_coherent_fallback_ = has_last_coherent_snapshot_
    && last_coherent_snapshot_.sky_light.enabled
      == cpu_snapshot_.sky_light.enabled
    && last_coherent_snapshot_.sky_light.source
      == cpu_snapshot_.sky_light.source
    && last_coherent_snapshot_.atmosphere.enabled
      == cpu_snapshot_.atmosphere.enabled
    && incoherent_frame_count_ <= kFallbackWindowFrames;

  ++incoherent_frame_count_;
  const bool should_warn = incoherent_frame_count_ == 1
    || (incoherent_frame_count_ % kPeriodicBlockedLogFrames) == 0;
  if (should_warn
    && last_incoherent_logged_sequence_ != last_update_frame_sequence_) {
    LOG_F(WARNING,
      "EnvStatic: coherence gate blocking publication "
      "(view={} frame_seq={} blocked_frames={} capture_gen={} ibl_gen={} "
      "ibl_src_ver={} fallback={} atmo_T={} atmo_V={} "
      "sl_cube={} ibl_irr={} ibl_pref={})",
      active_view_id_.get(), last_update_frame_sequence_.get(),
      incoherent_frame_count_, last_capture_generation_, sl.ibl_generation,
      last_observed_ibl_source_content_version_, use_last_coherent_fallback_,
      atmo.transmittance_lut_slot.value.get(),
      atmo.sky_view_lut_slot.value.get(), sl.cubemap_slot.value.get(),
      sl.irradiance_map_slot.value.get(), sl.prefilter_map_slot.value.get());
    if (incoherent_frame_count_ >= 8 && !coherence_threshold_crossed_) {
      LOG_F(ERROR,
        "EnvStatic: coherence gate threshold crossed; publishing current "
        "snapshot despite incoherence (blocked_frames={} view={} "
        "capture_gen={} ibl_gen={} ibl_src_ver={})",
        incoherent_frame_count_, active_view_id_.get(),
        last_capture_generation_, sl.ibl_generation,
        last_observed_ibl_source_content_version_);
      coherence_threshold_crossed_ = true;
    }
    last_incoherent_logged_sequence_ = last_update_frame_sequence_;
  }
}

auto EnvironmentStaticDataManager::UploadIfNeeded() -> void
{
  DCHECK_F(current_slot_ != frame::kInvalidSlot,
    "proper use of the environment static data manager requires calling its "
    "OnFrameStart() method every frame, and before any use");

  EnsureResourcesCreated();
  if (!buffer_ || (mapped_ptr_ == nullptr)
    || srv_index_ == kInvalidShaderVisibleIndex) {
    return;
  }

  const auto slot_index = CurrentSlotIndex();
  if (slot_index >= slot_uploaded_id_.size()) {
    LOG_F(ERROR, "Slot index {} is out of range (must be < {})", slot_index,
      slot_uploaded_id_.size());
    return;
  }

  // If this slot already uploaded the current snapshot, nothing to do.
  if (slot_uploaded_id_[slot_index] == snapshot_id_) {
    return;
  }

  const auto prev_uploaded_id = slot_uploaded_id_[slot_index];

  DLOG_SCOPE_F(2, "Uploading environment static data");
  auto format_slot =
    []<typename T>
    requires requires(const T& v) {
      { v.IsValid() } -> std::convertible_to<bool>;
      { v.value } -> std::convertible_to<ShaderVisibleIndex>;
    }(const T& slot)
  -> std::string {
      if (!slot.IsValid()) {
        return "not ready";
      }

      return nostd::to_string(slot.value);
    };

#ifndef NDEBUG
  // clang-format off
  DLOG_F(2, "frame_slot = {}", slot_index);
  {
    DLOG_SCOPE_F(2, "skylight");
    const auto& sl = cpu_snapshot_.sky_light;
    DLOG_F(2, "      cube = {}", format_slot(sl.cubemap_slot));
    DLOG_F(2, "irradiance = {}", format_slot(sl.irradiance_map_slot));
    DLOG_F(2, " prefilter = {}", format_slot(sl.prefilter_map_slot));
    DLOG_F(2, "      brdf = {}", format_slot(sl.brdf_lut_slot));
  }
  DLOG_F(2, "skysphere cube = {}", format_slot(cpu_snapshot_.sky_sphere.cubemap_slot));
// clang-format on
#endif

  const auto& snapshot_to_upload
    = (current_snapshot_coherent_ || !use_last_coherent_fallback_)
    ? cpu_snapshot_
    : last_coherent_snapshot_;

  published_snapshot_ = snapshot_to_upload;
  has_published_snapshot_ = true;

  const auto offset_bytes
    = static_cast<std::size_t>(slot_index) * sizeof(EnvironmentStaticData);
  std::memcpy(static_cast<std::byte*>(mapped_ptr_) + offset_bytes,
    &snapshot_to_upload, sizeof(EnvironmentStaticData));
  slot_uploaded_id_[slot_index] = snapshot_id_;

  LOG_F(INFO,
    "EnvStatic: uploaded (slot={} srv={} snapshot_id={} prev_uploaded={}) "
    "ctx(slot={} seq={}) "
    "skylight(en={} src={} cube={}) skysphere(en={} src={} cube={}) "
    "pp(en={} mapper={} exp_mode={}) "
    "atmo(en={} T={} V={} M={} I={} C={} BN={}) "
    "ibl(gen={} irr={} pref={})",
    slot_index, srv_index_.get(), snapshot_id_, prev_uploaded_id,
    last_update_frame_slot_.get(), last_update_frame_sequence_.get(),
    snapshot_to_upload.sky_light.enabled,
    static_cast<uint32_t>(snapshot_to_upload.sky_light.source),
    format_slot(snapshot_to_upload.sky_light.cubemap_slot),
    snapshot_to_upload.sky_sphere.enabled,
    static_cast<uint32_t>(snapshot_to_upload.sky_sphere.source),
    format_slot(snapshot_to_upload.sky_sphere.cubemap_slot),
    snapshot_to_upload.post_process.enabled,
    static_cast<uint32_t>(snapshot_to_upload.post_process.tone_mapper),
    static_cast<uint32_t>(snapshot_to_upload.post_process.exposure_mode),
    snapshot_to_upload.atmosphere.enabled,
    format_slot(snapshot_to_upload.atmosphere.transmittance_lut_slot),
    format_slot(snapshot_to_upload.atmosphere.sky_view_lut_slot),
    format_slot(snapshot_to_upload.atmosphere.multi_scat_lut_slot),
    format_slot(snapshot_to_upload.atmosphere.sky_irradiance_lut_slot),
    format_slot(snapshot_to_upload.atmosphere.camera_volume_lut_slot),
    format_slot(snapshot_to_upload.atmosphere.blue_noise_slot),
    snapshot_to_upload.sky_light.ibl_generation,
    format_slot(snapshot_to_upload.sky_light.irradiance_map_slot),
    format_slot(snapshot_to_upload.sky_light.prefilter_map_slot));
}

auto EnvironmentStaticDataManager::EnsureResourcesCreated() -> void
{
  if (buffer_) {
    return;
  }

  DLOG_SCOPE_FUNCTION(1);

  const auto total_bytes = static_cast<std::uint64_t>(kStrideBytes)
    * static_cast<std::uint64_t>(frame::kFramesInFlight.get());

  const BufferDesc desc {
    .size_bytes = total_bytes,
    .usage = BufferUsage::kNone, // TODO: verify if we need any usage flags
    .memory = BufferMemory::kUpload,
    .debug_name = "EnvironmentStaticData",
  };

  buffer_ = gfx_->CreateBuffer(desc);
  if (!buffer_) {
    LOG_F(ERROR,
      "-failed-: could not create buffer for environment static data upload");
    return;
  }

  // Must register before creating views.
  gfx_->GetResourceRegistry().Register(buffer_);

  mapped_ptr_ = buffer_->Map();
  if (mapped_ptr_ == nullptr) {
    LOG_F(ERROR, "-failed-: map buffer for environment static data upload");
    buffer_.reset();
    return;
  }

  auto& allocator = gfx_->GetDescriptorAllocator();
  auto handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!handle.IsValid()) {
    LOG_F(ERROR, "-failed-: descriptor for environment static data SRV");
    return;
  }

  graphics::BufferViewDescription view_desc {
    .view_type = graphics::ResourceViewType::kStructuredBuffer_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = oxygen::Format::kUnknown, // TODO: verify if we need a format here
    .range = { 0U, total_bytes },
    .stride = kStrideBytes,
  };

  const auto srv_index = allocator.GetShaderVisibleIndex(handle);
  srv_view_ = gfx_->GetResourceRegistry().RegisterView(
    *buffer_, std::move(handle), view_desc);

  srv_index_ = srv_index;

  LOG_F(INFO,
    "EnvStatic: created upload buffer (srv={} stride_bytes={} total_bytes={} "
    "frames_in_flight={})",
    srv_index_.get(), kStrideBytes, total_bytes, frame::kFramesInFlight.get());

  MarkAllSlotsDirty();
}

auto EnvironmentStaticDataManager::MarkAllSlotsDirty() -> void
{
  const auto old = snapshot_id_;
  ++snapshot_id_;
  LOG_F(INFO,
    "EnvStatic: MarkAllSlotsDirty (snapshot_id {}->{} current_slot={})", old,
    snapshot_id_, CurrentSlotIndex());
}

} // namespace oxygen::engine::internal
