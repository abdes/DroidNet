//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "EnvironmentStaticDataManager.h"

#include <cmath>
#include <cstring>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
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
  observer_ptr<Graphics> gfx)
  : gfx_(gfx)
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
  }

  UploadIfNeeded();
}

auto EnvironmentStaticDataManager::BuildFromSceneEnvironment(
  observer_ptr<const scene::SceneEnvironment> env) -> void
{
  EnvironmentStaticData next {};

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
    }

    if (const auto sky_light
      = env->TryGetSystem<scene::environment::SkyLight>();
      sky_light && sky_light->IsEnabled()) {
      next.sky_light.enabled = 1u;
      next.sky_light.source = ToGpuSkyLightSource(sky_light->GetSource());
      next.sky_light.intensity = sky_light->GetIntensity();
      next.sky_light.tint_rgb = sky_light->GetTintRgb();
      next.sky_light.diffuse_intensity = sky_light->GetDiffuseIntensity();
      next.sky_light.specular_intensity = sky_light->GetSpecularIntensity();

      // Asset binding not implemented in Phase 1.5.
      next.sky_light.cubemap_slot = kInvalidDescriptorSlot;
    }

    if (const auto sky_sphere
      = env->TryGetSystem<scene::environment::SkySphere>();
      sky_sphere && sky_sphere->IsEnabled()) {
      next.sky_sphere.enabled = 1u;
      next.sky_sphere.source = ToGpuSkySphereSource(sky_sphere->GetSource());
      next.sky_sphere.solid_color_rgb = sky_sphere->GetSolidColorRgb();
      next.sky_sphere.intensity = sky_sphere->GetIntensity();
      next.sky_sphere.rotation_radians = sky_sphere->GetRotationRadians();
      next.sky_sphere.tint_rgb = sky_sphere->GetTintRgb();

      // Asset binding not implemented in Phase 1.5.
      next.sky_sphere.cubemap_slot = kInvalidDescriptorSlot;
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
    cpu_snapshot_ = next;
    MarkAllSlotsDirty();
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
  MarkAllSlotsDirty();
}

auto EnvironmentStaticDataManager::MarkAllSlotsDirty() -> void
{
  slot_needs_upload_.fill(true);
}

} // namespace oxygen::engine::internal
