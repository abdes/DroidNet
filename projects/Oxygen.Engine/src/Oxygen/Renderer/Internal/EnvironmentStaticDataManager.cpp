//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "EnvironmentStaticDataManager.h"

#include <cmath>
#include <cstring>
#include <limits>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/Internal/BrdfLutManager.h>
#include <Oxygen/Renderer/Internal/IblManager.h>
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Environment/VolumetricClouds.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::engine::internal {

using graphics::BufferDesc;
using graphics::BufferMemory;
using graphics::BufferUsage;

namespace {

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

  auto ToGpuToneMapper(const scene::environment::ToneMapper mapper) noexcept
    -> ToneMapper
  {
    switch (mapper) {
    case scene::environment::ToneMapper::kAcesFitted:
      return ToneMapper::kAcesFitted;
    case scene::environment::ToneMapper::kReinhard:
      return ToneMapper::kReinhard;
    case scene::environment::ToneMapper::kNone:
      return ToneMapper::kNone;
    }
    return ToneMapper::kAcesFitted;
  }

  auto ToGpuExposureMode(const scene::environment::ExposureMode mode) noexcept
    -> ExposureMode
  {
    switch (mode) {
    case scene::environment::ExposureMode::kManual:
      return ExposureMode::kManual;
    case scene::environment::ExposureMode::kAuto:
      return ExposureMode::kAuto;
    }
    return ExposureMode::kManual;
  }

} // namespace

EnvironmentStaticDataManager::EnvironmentStaticDataManager(
  observer_ptr<Graphics> gfx,
  observer_ptr<renderer::resources::IResourceBinder> texture_binder,
  observer_ptr<IBrdfLutProvider> brdf_lut_provider,
  observer_ptr<IblManager> ibl_manager,
  observer_ptr<ISkyAtmosphereLutProvider> sky_atmo_lut_provider)
  : gfx_(gfx)
  , texture_binder_(texture_binder)
  , brdf_lut_provider_(brdf_lut_provider)
  , ibl_manager_(ibl_manager)
  , sky_atmo_lut_provider_(sky_atmo_lut_provider)
{
  slot_needs_upload_.fill(true);
}

EnvironmentStaticDataManager::~EnvironmentStaticDataManager()
{
  if (gfx_ && buffer_) {
    auto& registry = gfx_->GetResourceRegistry();
    if (registry.Contains(*buffer_)) {
      if (srv_view_.get().IsValid()) {
        registry.UnRegisterView(*buffer_, srv_view_);
      }
      registry.UnRegisterResource(*buffer_);
    }

    srv_view_ = {};
    srv_index_ = kInvalidShaderVisibleIndex;
  }

  if (buffer_ && mapped_ptr_) {
    buffer_->UnMap();
    mapped_ptr_ = nullptr;
  }

  buffer_.reset();
}

auto EnvironmentStaticDataManager::OnFrameStart(frame::Slot slot) -> void
{
  current_slot_ = slot;
}

auto EnvironmentStaticDataManager::UpdateIfNeeded(const RenderContext& context)
  -> void
{
  observer_ptr<const scene::SceneEnvironment> env = nullptr;
  if (const auto scene_ptr = context.GetScene()) {
    env = scene_ptr->GetEnvironment();
    BuildFromSceneEnvironment(env);
  }

  UploadIfNeeded();
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

auto EnvironmentStaticDataManager::BuildFromSceneEnvironment(
  observer_ptr<const scene::SceneEnvironment> env) -> void
{
  EnvironmentStaticData next {};

  // Capture publish decisions so we can log only on transitions.
  // Defaults represent "not publishing".
  std::uint32_t published_skylight_source
    = (std::numeric_limits<std::uint32_t>::max)();
  std::uint32_t published_skylight_cubemap_slot = kInvalidDescriptorSlot;
  bool published_skylight_cubemap_ready = false;

  std::uint32_t published_skysphere_source
    = (std::numeric_limits<std::uint32_t>::max)();
  std::uint32_t published_skysphere_cubemap_slot = kInvalidDescriptorSlot;
  bool published_skysphere_cubemap_ready = false;

  bool published_ibl_has_source = false;
  std::uint32_t published_ibl_source_slot = kInvalidDescriptorSlot;
  bool published_ibl_is_dirty = false;
  bool published_ibl_outputs = false;

  if (brdf_lut_provider_) {
    const auto [tex, slot] = brdf_lut_provider_->GetOrCreateLut();
    if (slot != brdf_lut_slot_) {
      brdf_lut_slot_ = slot;
      brdf_lut_texture_ = tex;
      brdf_lut_transitioned_ = false;
      MarkAllSlotsDirty();
    }
  }

  if (env) {
    if (const auto fog = env->TryGetSystem<scene::environment::Fog>();
      fog && fog->IsEnabled()) {
      next.fog.enabled = 1u;
      next.fog.model = ToGpuFogModel(fog->GetModel());
      next.fog.density = fog->GetDensity();
      next.fog.height_falloff = fog->GetHeightFalloff();
      next.fog.height_offset_m = fog->GetHeightOffsetMeters();
      next.fog.start_distance_m = fog->GetStartDistanceMeters();
      next.fog.max_opacity = fog->GetMaxOpacity();
      next.fog.albedo_rgb = fog->GetAlbedoRgb();
      next.fog.anisotropy_g = fog->GetAnisotropy();
      next.fog.scattering_intensity = fog->GetScatteringIntensity();
    }

    if (const auto atmo
      = env->TryGetSystem<scene::environment::SkyAtmosphere>();
      atmo && atmo->IsEnabled()) {
      next.atmosphere.enabled = 1u;
      next.atmosphere.planet_radius_m = atmo->GetPlanetRadiusMeters();
      next.atmosphere.atmosphere_height_m = atmo->GetAtmosphereHeightMeters();
      next.atmosphere.ground_albedo_rgb = atmo->GetGroundAlbedoRgb();
      next.atmosphere.rayleigh_scattering_rgb
        = atmo->GetRayleighScatteringRgb();
      next.atmosphere.rayleigh_scale_height_m
        = atmo->GetRayleighScaleHeightMeters();
      next.atmosphere.mie_scattering_rgb = atmo->GetMieScatteringRgb();
      next.atmosphere.mie_scale_height_m = atmo->GetMieScaleHeightMeters();
      next.atmosphere.mie_g = atmo->GetMieAnisotropy();
      next.atmosphere.absorption_rgb = atmo->GetAbsorptionRgb();
      next.atmosphere.absorption_scale_height_m
        = atmo->GetAbsorptionScaleHeightMeters();
      next.atmosphere.multi_scattering_factor
        = atmo->GetMultiScatteringFactor();
      next.atmosphere.sun_disk_enabled = atmo->GetSunDiskEnabled() ? 1u : 0u;
      next.atmosphere.sun_disk_angular_radius_radians
        = atmo->GetSunDiskAngularRadiusRadians();
      next.atmosphere.aerial_perspective_distance_scale
        = atmo->GetAerialPerspectiveDistanceScale();

      // Populate LUT slots from the sky atmosphere LUT provider.
      if (sky_atmo_lut_provider_) {
        const auto transmittance_slot
          = sky_atmo_lut_provider_->GetTransmittanceLutSlot();
        const auto sky_view_slot = sky_atmo_lut_provider_->GetSkyViewLutSlot();

        next.atmosphere.transmittance_lut_slot
          = transmittance_slot != kInvalidShaderVisibleIndex
          ? transmittance_slot.get()
          : kInvalidDescriptorSlot;
        next.atmosphere.sky_view_lut_slot
          = sky_view_slot != kInvalidShaderVisibleIndex
          ? sky_view_slot.get()
          : kInvalidDescriptorSlot;

        const auto [trans_w, trans_h]
          = sky_atmo_lut_provider_->GetTransmittanceLutSize();
        const auto [sky_w, sky_h] = sky_atmo_lut_provider_->GetSkyViewLutSize();

        next.atmosphere.transmittance_lut_width = static_cast<float>(trans_w);
        next.atmosphere.transmittance_lut_height = static_cast<float>(trans_h);
        next.atmosphere.sky_view_lut_width = static_cast<float>(sky_w);
        next.atmosphere.sky_view_lut_height = static_cast<float>(sky_h);

        // Update the LUT manager's parameters to trigger dirty tracking.
        sky_atmo_lut_provider_->UpdateParameters(next.atmosphere);
      }
    }

    if (const auto sky_light
      = env->TryGetSystem<scene::environment::SkyLight>();
      sky_light && sky_light->IsEnabled()) {
      next.sky_light.enabled = 1u;
      next.sky_light.source = ToGpuSkyLightSource(sky_light->GetSource());
      published_skylight_source
        = static_cast<std::uint32_t>(next.sky_light.source);
      next.sky_light.intensity = sky_light->GetIntensity();
      next.sky_light.tint_rgb = sky_light->GetTintRgb();
      next.sky_light.diffuse_intensity = sky_light->GetDiffuseIntensity();
      next.sky_light.specular_intensity = sky_light->GetSpecularIntensity();
      next.sky_light.brdf_lut_slot
        = brdf_lut_slot_ != kInvalidShaderVisibleIndex ? brdf_lut_slot_.get()
                                                       : kInvalidDescriptorSlot;

      // Resolve cubemap ResourceKey to shader-visible index via TextureBinder.
      if (texture_binder_
        && sky_light->GetSource()
          == scene::environment::SkyLightSource::kSpecifiedCubemap
        && !sky_light->GetCubemapResource().IsPlaceholder()) {
        const auto key = sky_light->GetCubemapResource();
        const auto slot = texture_binder_->GetOrAllocate(key);
        published_skylight_cubemap_ready
          = texture_binder_->IsResourceReady(key);
        published_skylight_cubemap_slot = slot.get();
        if (published_skylight_cubemap_ready) {
          next.sky_light.cubemap_slot = slot.get();
        } else {
          next.sky_light.cubemap_slot = kInvalidDescriptorSlot;
        }
      } else {
        next.sky_light.cubemap_slot = kInvalidDescriptorSlot;
      }
    }

    if (const auto sky_sphere
      = env->TryGetSystem<scene::environment::SkySphere>();
      sky_sphere && sky_sphere->IsEnabled()) {
      // Warn if both SkyAtmosphere and SkySphere are enabled (mutually
      // exclusive; SkyAtmosphere takes priority in the shader).
      if (next.atmosphere.enabled != 0U) {
        DLOG_F(WARNING,
          "Both SkyAtmosphere and SkySphere are enabled. They are mutually "
          "exclusive; SkyAtmosphere will take priority for sky rendering.");
      }

      next.sky_sphere.enabled = 1u;
      next.sky_sphere.source = ToGpuSkySphereSource(sky_sphere->GetSource());
      published_skysphere_source
        = static_cast<std::uint32_t>(next.sky_sphere.source);
      next.sky_sphere.solid_color_rgb = sky_sphere->GetSolidColorRgb();
      next.sky_sphere.intensity = sky_sphere->GetIntensity();
      next.sky_sphere.rotation_radians = sky_sphere->GetRotationRadians();
      next.sky_sphere.tint_rgb = sky_sphere->GetTintRgb();

      // Resolve cubemap ResourceKey to shader-visible index via TextureBinder.
      if (texture_binder_
        && sky_sphere->GetSource()
          == scene::environment::SkySphereSource::kCubemap
        && !sky_sphere->GetCubemapResource().IsPlaceholder()) {
        const auto key = sky_sphere->GetCubemapResource();
        const auto slot = texture_binder_->GetOrAllocate(key);
        published_skysphere_cubemap_ready
          = texture_binder_->IsResourceReady(key);
        published_skysphere_cubemap_slot = slot.get();
        if (published_skysphere_cubemap_ready) {
          next.sky_sphere.cubemap_slot = slot.get();
        } else {
          next.sky_sphere.cubemap_slot = kInvalidDescriptorSlot;
        }
      } else {
        next.sky_sphere.cubemap_slot = kInvalidDescriptorSlot;
      }
    }

    // Bind IBL resources when SkyLight is enabled and we have a valid
    // environment cubemap source to filter.
    //
    // Source selection for filtering is done by IblComputePass. Here, we only
    // publish the output map slots when inputs are valid.
    if (next.sky_light.enabled != 0U && ibl_manager_) {
      const bool has_source
        = next.sky_light.cubemap_slot != kInvalidDescriptorSlot
        || (next.sky_sphere.enabled != 0U
          && next.sky_sphere.cubemap_slot != kInvalidDescriptorSlot);

      published_ibl_has_source = has_source;

      if (has_source && ibl_manager_->EnsureResourcesCreated()) {
        const ShaderVisibleIndex source_slot
          = (next.sky_light.cubemap_slot != kInvalidDescriptorSlot)
          ? ShaderVisibleIndex { next.sky_light.cubemap_slot }
          : ShaderVisibleIndex { next.sky_sphere.cubemap_slot };

        published_ibl_source_slot = source_slot.get();
        published_ibl_is_dirty = ibl_manager_->IsDirty(source_slot);

        // Only publish the output slots once the IBL maps are known to be
        // generated for the currently selected source cubemap.
        //
        // This prevents sampling from uninitialized UAV-initial-state
        // textures (which would appear black), especially visible when the
        // demo focuses on IBL specular and disables competing terms.
        if (!published_ibl_is_dirty) {
          next.sky_light.irradiance_map_slot
            = ibl_manager_->GetIrradianceMapSlot().get();
          next.sky_light.prefilter_map_slot
            = ibl_manager_->GetPrefilterMapSlot().get();
          published_ibl_outputs = true;
        } else {
          next.sky_light.irradiance_map_slot = kInvalidDescriptorSlot;
          next.sky_light.prefilter_map_slot = kInvalidDescriptorSlot;
          published_ibl_outputs = false;
        }
      } else {
        next.sky_light.irradiance_map_slot = kInvalidDescriptorSlot;
        next.sky_light.prefilter_map_slot = kInvalidDescriptorSlot;
        published_ibl_outputs = false;
      }
    }

    if (const auto clouds
      = env->TryGetSystem<scene::environment::VolumetricClouds>();
      clouds && clouds->IsEnabled()) {
      next.clouds.enabled = 1u;
      next.clouds.base_altitude_m = clouds->GetBaseAltitudeMeters();
      next.clouds.layer_thickness_m = clouds->GetLayerThicknessMeters();
      next.clouds.coverage = clouds->GetCoverage();
      next.clouds.density = clouds->GetDensity();
      next.clouds.albedo_rgb = clouds->GetAlbedoRgb();
      next.clouds.extinction_scale = clouds->GetExtinctionScale();
      next.clouds.phase_g = clouds->GetPhaseAnisotropy();
      next.clouds.wind_dir_ws = clouds->GetWindDirectionWs();
      next.clouds.wind_speed_mps = clouds->GetWindSpeedMps();
      next.clouds.shadow_strength = clouds->GetShadowStrength();
    }

    if (const auto pp
      = env->TryGetSystem<scene::environment::PostProcessVolume>();
      pp && pp->IsEnabled()) {
      next.post_process.enabled = 1u;
      next.post_process.tone_mapper = ToGpuToneMapper(pp->GetToneMapper());
      next.post_process.exposure_mode
        = ToGpuExposureMode(pp->GetExposureMode());

      // Scene authoring stores EV; GPU expects a multiplier.
      next.post_process.exposure_compensation
        = std::exp2(pp->GetExposureCompensationEv());

      next.post_process.auto_exposure_min_ev = pp->GetAutoExposureMinEv();
      next.post_process.auto_exposure_max_ev = pp->GetAutoExposureMaxEv();
      next.post_process.auto_exposure_speed_up = pp->GetAutoExposureSpeedUp();
      next.post_process.auto_exposure_speed_down
        = pp->GetAutoExposureSpeedDown();

      next.post_process.bloom_intensity = pp->GetBloomIntensity();
      next.post_process.bloom_threshold = pp->GetBloomThreshold();
      next.post_process.saturation = pp->GetSaturation();
      next.post_process.contrast = pp->GetContrast();
      next.post_process.vignette_intensity = pp->GetVignetteIntensity();
    }
  }

  if (std::memcmp(&next, &cpu_snapshot_, sizeof(EnvironmentStaticData)) != 0) {
    // Useful diagnostics for IBL troubleshooting.
    LOG_F(2,
      "EnvironmentStaticData changed: "
      "SkyLight(en={}, src={}, cube={}, irr={}, pref={}, brdf={}, I={}, D={}, "
      "S={}) SkySphere(en={}, src={}, cube={})",
      next.sky_light.enabled, static_cast<uint32_t>(next.sky_light.source),
      next.sky_light.cubemap_slot, next.sky_light.irradiance_map_slot,
      next.sky_light.prefilter_map_slot, next.sky_light.brdf_lut_slot,
      next.sky_light.intensity, next.sky_light.diffuse_intensity,
      next.sky_light.specular_intensity, next.sky_sphere.enabled,
      static_cast<uint32_t>(next.sky_sphere.source),
      next.sky_sphere.cubemap_slot);

    cpu_snapshot_ = next;
    MarkAllSlotsDirty();
  }

  // Emit publish-decision logs when the manager's output policy changes.
  // This avoids guessing when a slot is being withheld vs published.
  const bool diag_changed = !publish_diag_initialized_
    || last_published_skylight_source_ != published_skylight_source
    || last_published_skylight_cubemap_slot_ != published_skylight_cubemap_slot
    || last_published_skylight_cubemap_ready_
      != published_skylight_cubemap_ready
    || last_published_skysphere_source_ != published_skysphere_source
    || last_published_skysphere_cubemap_slot_
      != published_skysphere_cubemap_slot
    || last_published_skysphere_cubemap_ready_
      != published_skysphere_cubemap_ready
    || last_published_ibl_has_source_ != published_ibl_has_source
    || last_published_ibl_source_slot_ != published_ibl_source_slot
    || last_published_ibl_is_dirty_ != published_ibl_is_dirty
    || last_published_ibl_outputs_ != published_ibl_outputs;

  if (diag_changed) {
    LOG_F(2,
      "EnvStatic publish policy: SkyLight(src={}, requested_slot={}, ready={}, "
      "published_slot={}, I={}, D={}, S={}) "
      "SkySphere(src={}, requested_slot={}, ready={}, published_slot={}) "
      "IBL(has_source={}, src_slot={}, dirty={}, "
      "publish_outputs={}, irr={}, pref={})",
      published_skylight_source, published_skylight_cubemap_slot,
      published_skylight_cubemap_ready, next.sky_light.cubemap_slot,
      next.sky_light.intensity, next.sky_light.diffuse_intensity,
      next.sky_light.specular_intensity, published_skysphere_source,
      published_skysphere_cubemap_slot, published_skysphere_cubemap_ready,
      next.sky_sphere.cubemap_slot, published_ibl_has_source,
      published_ibl_source_slot, published_ibl_is_dirty, published_ibl_outputs,
      next.sky_light.irradiance_map_slot, next.sky_light.prefilter_map_slot);

    publish_diag_initialized_ = true;
    last_published_skylight_source_ = published_skylight_source;
    last_published_skylight_cubemap_slot_ = published_skylight_cubemap_slot;
    last_published_skylight_cubemap_ready_ = published_skylight_cubemap_ready;
    last_published_skysphere_source_ = published_skysphere_source;
    last_published_skysphere_cubemap_slot_ = published_skysphere_cubemap_slot;
    last_published_skysphere_cubemap_ready_ = published_skysphere_cubemap_ready;
    last_published_ibl_has_source_ = published_ibl_has_source;
    last_published_ibl_source_slot_ = published_ibl_source_slot;
    last_published_ibl_is_dirty_ = published_ibl_is_dirty;
    last_published_ibl_outputs_ = published_ibl_outputs;
  }
}

auto EnvironmentStaticDataManager::UploadIfNeeded() -> void
{
  if (current_slot_ == frame::kInvalidSlot) {
    LOG_F(ERROR,
      "EnvironmentStaticDataManager::UploadIfNeeded called without valid "
      "frame slot");
    return;
  }

  EnsureResourcesCreated();
  if (!buffer_ || !mapped_ptr_ || srv_index_ == kInvalidShaderVisibleIndex) {
    return;
  }

  const auto slot_index = CurrentSlotIndex();
  if (slot_index >= slot_needs_upload_.size()) {
    LOG_F(
      ERROR, "EnvironmentStaticDataManager: invalid slot index {}", slot_index);
    return;
  }

  if (!slot_needs_upload_[slot_index]) {
    return;
  }

  LOG_F(2,
    "EnvStatic upload: frame_slot={} skylight(cube={}, irr={}, pref={}, "
    "brdf={}) "
    "skysphere(cube={})",
    slot_index, cpu_snapshot_.sky_light.cubemap_slot,
    cpu_snapshot_.sky_light.irradiance_map_slot,
    cpu_snapshot_.sky_light.prefilter_map_slot,
    cpu_snapshot_.sky_light.brdf_lut_slot,
    cpu_snapshot_.sky_sphere.cubemap_slot);

  const auto offset_bytes
    = static_cast<std::size_t>(slot_index) * sizeof(EnvironmentStaticData);
  std::memcpy(static_cast<std::byte*>(mapped_ptr_) + offset_bytes,
    &cpu_snapshot_, sizeof(EnvironmentStaticData));
  slot_needs_upload_[slot_index] = false;
}

auto EnvironmentStaticDataManager::EnsureResourcesCreated() -> void
{
  if (buffer_) {
    return;
  }

  const auto total_bytes = static_cast<std::uint64_t>(kStrideBytes)
    * static_cast<std::uint64_t>(frame::kFramesInFlight.get());

  const BufferDesc desc {
    .size_bytes = total_bytes,
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kUpload,
    .debug_name = "EnvironmentStaticData",
  };

  buffer_ = gfx_->CreateBuffer(desc);
  if (!buffer_) {
    LOG_F(ERROR, "EnvironmentStaticDataManager: failed to create buffer");
    return;
  }

  buffer_->SetName(desc.debug_name);

  gfx_->GetResourceRegistry().Register(buffer_);

  mapped_ptr_ = buffer_->Map();
  if (!mapped_ptr_) {
    LOG_F(ERROR, "EnvironmentStaticDataManager: failed to map buffer");
    buffer_.reset();
    return;
  }

  auto& allocator = gfx_->GetDescriptorAllocator();
  auto handle
    = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!handle.IsValid()) {
    LOG_F(ERROR, "EnvironmentStaticDataManager: descriptor allocation failed");
    return;
  }

  graphics::BufferViewDescription view_desc;
  view_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_SRV;
  view_desc.range = { 0u, total_bytes };
  view_desc.stride = kStrideBytes;
  view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;

  DCHECK_F(gfx_->GetResourceRegistry().Contains(*buffer_),
    "EnvironmentStaticData buffer not registered in ResourceRegistry");

  const auto srv_index = allocator.GetShaderVisibleIndex(handle);
  srv_view_ = gfx_->GetResourceRegistry().RegisterView(
    *buffer_, std::move(handle), view_desc);

  srv_index_ = srv_index;

  LOG_F(INFO,
    "EnvStatic resources created: srv_index={}, stride_bytes={}, "
    "total_bytes={}",
    srv_index_.get(), kStrideBytes, total_bytes);
  MarkAllSlotsDirty();
}

auto EnvironmentStaticDataManager::MarkAllSlotsDirty() -> void
{
  slot_needs_upload_.fill(true);
}

} // namespace oxygen::engine::internal
