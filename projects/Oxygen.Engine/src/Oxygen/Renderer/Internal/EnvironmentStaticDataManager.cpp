//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstring>

#include <Oxygen/Base/Logging.h>
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
    case scene::environment::ExposureMode::kManualCamera:
      return ExposureMode::kManualCamera;
    }
    return ExposureMode::kManual;
  }

} // namespace

EnvironmentStaticDataManager::EnvironmentStaticDataManager(
  observer_ptr<Graphics> gfx,
  observer_ptr<renderer::resources::IResourceBinder> texture_binder,
  observer_ptr<IBrdfLutProvider> brdf_lut_provider,
  observer_ptr<IIblProvider> ibl_manager,
  observer_ptr<ISkyAtmosphereLutProvider> sky_atmo_lut_provider,
  observer_ptr<ISkyCaptureProvider> sky_capture_provider)
  : gfx_(gfx)
  , texture_binder_(texture_binder)
  , brdf_lut_provider_(brdf_lut_provider)
  , ibl_provider_(ibl_manager)
  , sky_lut_provider_(sky_atmo_lut_provider)
  , sky_capture_provider_(sky_capture_provider)
{
  // These are required dependencies, not guaranteeing them and not guaranteeing
  // that they will survive for the lifetime of the BRDF LUT Manager is a logic
  // error that will abort.
  CHECK_NOTNULL_F(gfx_, "expecting a valid Graphics instance");
  CHECK_NOTNULL_F(texture_binder_, "expecting a valid resource binder");
  CHECK_NOTNULL_F(brdf_lut_provider_, "expecting a valid BRDF LUT provider");
  CHECK_NOTNULL_F(ibl_provider_, "expecting a valid IBL provider");
  CHECK_NOTNULL_F(sky_lut_provider_, "expecting a valid sky LUT provider");
  CHECK_NOTNULL_F(
    sky_capture_provider_, "expecting a valid sky capture provider");

  // Ensure uploaded ids are zero so the initial snapshot (id=1)
  // will be considered not-yet-uploaded for all slots.
  slot_uploaded_id_.fill(0);
}

EnvironmentStaticDataManager::~EnvironmentStaticDataManager()
{
  if (buffer_ == nullptr) {
    return;
  }

  auto& registry = gfx_->GetResourceRegistry();
  if (registry.Contains(*buffer_)) {
    // Unregister the buffer and all its views.
    registry.UnRegisterResource(*buffer_);
  }

  srv_view_ = {};
  srv_index_ = kInvalidShaderVisibleIndex;

  if (mapped_ptr_) {
    buffer_->UnMap();
    mapped_ptr_ = nullptr;
  }

  buffer_.reset();
}

auto EnvironmentStaticDataManager::OnFrameStart(
  renderer::RendererTag /*tag*/, frame::Slot slot) -> void
{
  current_slot_ = slot;
}

auto EnvironmentStaticDataManager::UpdateIfNeeded(
  renderer::RendererTag /*tag*/, const RenderContext& context) -> void
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

auto EnvironmentStaticDataManager::RequestIblRegeneration() noexcept -> void
{
  ibl_regeneration_requested_ = true;
}

auto EnvironmentStaticDataManager::BuildFromSceneEnvironment(
  observer_ptr<const scene::SceneEnvironment> env) -> void
{
  EnvironmentStaticData next {};

  // Handle BRDF LUT provider and possible changes to the LUT slot.
  ProcessBrdfLut();

  if (env) {
    PopulateFog(env, next);
    PopulateAtmosphere(env, next);
    PopulateSkyLight(env, next);
    PopulateSkySphere(env, next);
    PopulateSkyCapture(next);
    PopulateIbl(next);
    PopulateClouds(env, next);
    PopulatePostProcess(env, next);
  }

  if (std::memcmp(&next, &cpu_snapshot_, sizeof(EnvironmentStaticData)) != 0) {
    // If authored intensity or other filtering params changed, flag for IBL
    // re-filter.
    if (next.sky_light.intensity != cpu_snapshot_.sky_light.intensity
      || next.sky_light.tint_rgb != cpu_snapshot_.sky_light.tint_rgb) {
      RequestIblRegeneration();
    }

    cpu_snapshot_ = next;
    MarkAllSlotsDirty();
  }
}

auto EnvironmentStaticDataManager::ProcessBrdfLut() -> void
{
  if (brdf_lut_provider_) {
    const auto [tex, slot] = brdf_lut_provider_->GetOrCreateLut();
    if (slot != brdf_lut_slot_) {
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
}

auto EnvironmentStaticDataManager::PopulateAtmosphere(
  observer_ptr<const scene::SceneEnvironment> env, EnvironmentStaticData& next)
  -> void
{
  if (const auto atmo = env->TryGetSystem<scene::environment::SkyAtmosphere>();
    atmo && atmo->IsEnabled()) {
    next.atmosphere.enabled = 1u;
    next.atmosphere.planet_radius_m = atmo->GetPlanetRadiusMeters();
    next.atmosphere.atmosphere_height_m = atmo->GetAtmosphereHeightMeters();
    next.atmosphere.ground_albedo_rgb = atmo->GetGroundAlbedoRgb();
    next.atmosphere.rayleigh_scattering_rgb = atmo->GetRayleighScatteringRgb();
    next.atmosphere.rayleigh_scale_height_m
      = atmo->GetRayleighScaleHeightMeters();
    next.atmosphere.mie_scattering_rgb = atmo->GetMieScatteringRgb();
    next.atmosphere.mie_scale_height_m = atmo->GetMieScaleHeightMeters();
    next.atmosphere.mie_g = atmo->GetMieAnisotropy();
    next.atmosphere.absorption_rgb = atmo->GetAbsorptionRgb();
    next.atmosphere.absorption_scale_height_m
      = atmo->GetAbsorptionScaleHeightMeters();
    next.atmosphere.multi_scattering_factor = atmo->GetMultiScatteringFactor();

    if (const auto sun = env->TryGetSystem<scene::environment::Sun>();
      sun && sun->IsEnabled()) {
      const float sun_disk_radius = sun->GetDiskAngularRadiusRadians();
      next.atmosphere.sun_disk_enabled = sun_disk_radius > 0.0F ? 1u : 0u;
      next.atmosphere.sun_disk_angular_radius_radians = sun_disk_radius;
    } else {
      next.atmosphere.sun_disk_enabled = atmo->GetSunDiskEnabled() ? 1u : 0u;
      next.atmosphere.sun_disk_angular_radius_radians
        = atmo->GetSunDiskAngularRadiusRadians();
    }
    next.atmosphere.aerial_perspective_distance_scale
      = atmo->GetAerialPerspectiveDistanceScale();

    if (sky_lut_provider_) {
      const auto transmittance_slot
        = sky_lut_provider_->GetTransmittanceLutSlot();
      const auto sky_view_slot = sky_lut_provider_->GetSkyViewLutSlot();

      next.atmosphere.transmittance_lut_slot
        = TransmittanceLutSlot { transmittance_slot };

      next.atmosphere.sky_view_lut_slot = SkyViewLutSlot { sky_view_slot };

      const auto [trans_w, trans_h]
        = sky_lut_provider_->GetTransmittanceLutSize();
      const auto [sky_w, sky_h] = sky_lut_provider_->GetSkyViewLutSize();

      next.atmosphere.transmittance_lut_width = static_cast<float>(trans_w);
      next.atmosphere.transmittance_lut_height = static_cast<float>(trans_h);
      next.atmosphere.sky_view_lut_width = static_cast<float>(sky_w);
      next.atmosphere.sky_view_lut_height = static_cast<float>(sky_h);

      sky_lut_provider_->UpdateParameters(next.atmosphere);
    }
  }
}

auto EnvironmentStaticDataManager::PopulateSkyLight(
  observer_ptr<const scene::SceneEnvironment> env, EnvironmentStaticData& next)
  -> void
{
  if (const auto sky_light = env->TryGetSystem<scene::environment::SkyLight>();
    sky_light && sky_light->IsEnabled()) {
    next.sky_light.enabled = 1u;
    next.sky_light.source = ToGpuSkyLightSource(sky_light->GetSource());

    // Intensity is a direct multiplier. For non-physical sources (cubemaps),
    // we bridge the unit gap by assuming 1.0 Intensity = 5000 Nits (Standard
    // Sky). Procedural atmosphere remains at its native physical scale.
    const float intensity = sky_light->GetIntensity();
    const float unit_bridge
      = (next.sky_light.source == SkyLightSource::kSpecifiedCubemap) ? 5000.0F
                                                                     : 1.0F;
    next.sky_light.intensity = intensity * unit_bridge;

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
      DLOG_F(WARNING,
        "Both SkyAtmosphere and SkySphere are enabled. They are mutually "
        "exclusive; SkyAtmosphere will take priority for sky rendering.");
    }

    next.sky_sphere.enabled = 1u;
    next.sky_sphere.source = ToGpuSkySphereSource(sky_sphere->GetSource());
    next.sky_sphere.solid_color_rgb = sky_sphere->GetSolidColorRgb();

    // Bridging non-physical assets to 5000 Nit physical baseline.
    const float intensity = sky_sphere->GetIntensity();
    const float unit_bridge
      = (next.sky_sphere.source == SkySphereSource::kCubemap
          || next.sky_sphere.source == SkySphereSource::kSolidColor)
      ? 5000.0F
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
    const auto capture_gen = sky_capture_provider_->GetCaptureGeneration();
    if (capture_gen != last_capture_generation_) {
      last_capture_generation_ = capture_gen;
      MarkAllSlotsDirty();
    }

    // If SkyLight source is kCapturedScene, we provide the captured cubemap
    // slot. This is used by IblComputePass to decide which source to filter.
    if (next.sky_light.enabled != 0U
      && next.sky_light.source == SkyLightSource::kCapturedScene) {
      if (sky_capture_provider_->IsCaptured()) {
        next.sky_light.cubemap_slot
          = CubeMapSlot { sky_capture_provider_->GetCapturedCubemapSlot() };
      } else {
        next.sky_light.cubemap_slot
          = CubeMapSlot { kInvalidShaderVisibleIndex };
      }
    }
  }
}

auto EnvironmentStaticDataManager::PopulateIbl(EnvironmentStaticData& next)
  -> void
{
  if (next.sky_light.enabled == 0U || !ibl_provider_) {
    return;
  }

  const bool has_source = next.sky_light.cubemap_slot.IsValid()
    || (next.sky_sphere.enabled != 0U
      && next.sky_sphere.cubemap_slot.IsValid());

  if (!has_source) {
    next.sky_light.irradiance_map_slot = IrradianceMapSlot {};
    next.sky_light.prefilter_map_slot = PrefilterMapSlot {};
    next.sky_light.prefilter_max_mip = 0U;
    return;
  }

  const auto source_slot
    = ShaderVisibleIndex { next.sky_light.cubemap_slot.IsValid()
          ? next.sky_light.cubemap_slot
          : next.sky_sphere.cubemap_slot };

  const auto outputs = ibl_provider_->QueryOutputsFor(source_slot);

  next.sky_light.irradiance_map_slot = IrradianceMapSlot { outputs.irradiance };
  next.sky_light.prefilter_map_slot = PrefilterMapSlot { outputs.prefilter };
  next.sky_light.ibl_generation = static_cast<uint32_t>(outputs.generation);

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
}

auto EnvironmentStaticDataManager::PopulatePostProcess(
  observer_ptr<const scene::SceneEnvironment> env, EnvironmentStaticData& next)
  -> void
{
  if (const auto pp
    = env->TryGetSystem<scene::environment::PostProcessVolume>();
    pp && pp->IsEnabled()) {
    next.post_process.enabled = 1u;
    next.post_process.tone_mapper = ToGpuToneMapper(pp->GetToneMapper());
    next.post_process.exposure_mode = ToGpuExposureMode(pp->GetExposureMode());

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
  }
}

auto EnvironmentStaticDataManager::UploadIfNeeded() -> void
{
  DCHECK_F(current_slot_ != frame::kInvalidSlot,
    "proper use of the environment static data manager requires calling its "
    "OnFrameStart() method every frame, and before any use");

  EnsureResourcesCreated();
  if (!buffer_ || !mapped_ptr_ || srv_index_ == kInvalidShaderVisibleIndex) {
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

  DLOG_SCOPE_F(2, "Uploading environment static data");
  auto format_slot =
    []<typename T>
    requires requires(const T& v) {
      { v.IsValid() } -> std::convertible_to<bool>;
      { static_cast<std::uint32_t>(v) };
    }(const T& slot)
  -> std::string {
      if (!slot.IsValid()) {
        return "not ready";
      }

      return nostd::to_string(
        ShaderVisibleIndex { static_cast<std::uint32_t>(slot) });
    };
  // clang-format off
  DLOG_F(2, "frame_slot = {}", slot_index);
  {
    DLOG_SCOPE_F(2, "skylight");
    DLOG_F(2, "      cube = {}", format_slot(cpu_snapshot_.sky_light.cubemap_slot));
    DLOG_F(2, "irradiance = {}", format_slot(cpu_snapshot_.sky_light.irradiance_map_slot));
    DLOG_F(2, " prefilter = {}", format_slot(cpu_snapshot_.sky_light.prefilter_map_slot));
    DLOG_F(2, "      brdf = {}", format_slot(cpu_snapshot_.sky_light.brdf_lut_slot));
  }
  DLOG_F(2, "skysphere cube = {}", format_slot(cpu_snapshot_.sky_sphere.cubemap_slot));
  // clang-format on

  const auto offset_bytes
    = static_cast<std::size_t>(slot_index) * sizeof(EnvironmentStaticData);
  std::memcpy(static_cast<std::byte*>(mapped_ptr_) + offset_bytes,
    &cpu_snapshot_, sizeof(EnvironmentStaticData));
  slot_uploaded_id_[slot_index] = snapshot_id_;
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
  if (!mapped_ptr_) {
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
    .range = { 0u, total_bytes },
    .stride = kStrideBytes,
  };

  const auto srv_index = allocator.GetShaderVisibleIndex(handle);
  srv_view_ = gfx_->GetResourceRegistry().RegisterView(
    *buffer_, std::move(handle), view_desc);

  srv_index_ = srv_index;

  DLOG_F(1, "srv index = {}", srv_index_.get());
  DLOG_F(1, "   stride = {} (bytes)", kStrideBytes);
  DLOG_F(1, "    total = {} (bytes)", total_bytes);

  MarkAllSlotsDirty();
}

auto EnvironmentStaticDataManager::MarkAllSlotsDirty() -> void
{
  DLOG_F(2, "Marking all slots as needing upload (invalidating snapshot)");
  ++snapshot_id_;
}

} // namespace oxygen::engine::internal
