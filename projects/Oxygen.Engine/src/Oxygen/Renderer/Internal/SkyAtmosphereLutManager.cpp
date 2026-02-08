//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Base/Types/Geometry.h"
#include <algorithm>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Internal/BlueNoiseData.h>
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/Types/EnvironmentStaticData.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

namespace oxygen::engine::internal {

namespace resources = ::oxygen::engine::resources;
namespace upload = ::oxygen::engine::upload;

using graphics::TextureDesc;

using oxygen::TextureType;

SkyAtmosphereLutManager::SkyAtmosphereLutManager(observer_ptr<Graphics> gfx,
  observer_ptr<upload::UploadCoordinator> uploader,
  observer_ptr<upload::StagingProvider> staging_provider, Config config)
  : gfx_(gfx)
  , uploader_(uploader)
  , staging_(staging_provider)
  , config_(config)
{
}

SkyAtmosphereLutManager::~SkyAtmosphereLutManager() { CleanupResources(); }

auto SkyAtmosphereLutManager::UpdateParameters(
  const GpuSkyAtmosphereParams& params) -> void
{
  const auto new_params = ExtractCachedParams(params);

  if (new_params == cached_params_) {
    return;
  }

  cached_params_ = new_params;
  dirty_ = true;
  ++generation_;

  LOG_F(INFO,
    "SkyAtmosphereLutManager: parameters changed, marking dirty "
    "(sun_disk_enabled={}, sun_disk_radius={})",
    cached_params_.sun_disk_enabled,
    cached_params_.sun_disk_angular_radius_radians);
}

auto SkyAtmosphereLutManager::UpdateSunState(const SunState& sun) noexcept
  -> void
{
  const bool elevation_changed = sun_state_.ElevationDiffers(sun);
  const bool enabled_changed = sun_state_.enabled != sun.enabled;

  if (elevation_changed || enabled_changed) {
    dirty_ = true;
    ++generation_;
  }

  sun_state_ = sun;
}

auto SkyAtmosphereLutManager::IsDirty() const noexcept -> bool
{
  return dirty_;
}

auto SkyAtmosphereLutManager::MarkClean() noexcept -> void { dirty_ = false; }

auto SkyAtmosphereLutManager::MarkDirty() noexcept -> void { dirty_ = true; }

auto SkyAtmosphereLutManager::SetAtmosphereFlags(uint32_t flags) noexcept
  -> void
{
  if (atmosphere_flags_ != flags) {
    atmosphere_flags_ = flags;
    dirty_ = true;
    ++generation_;
  }
}

auto SkyAtmosphereLutManager::HasBeenGenerated() const noexcept -> bool
{
  return luts_generated_;
}

auto SkyAtmosphereLutManager::MarkGenerated() noexcept -> void
{
  luts_generated_ = true;
}

auto SkyAtmosphereLutManager::GetTransmittanceLutSlot() const noexcept
  -> ShaderVisibleIndex
{
  return transmittance_lut_.srv_index;
}

auto SkyAtmosphereLutManager::GetSkyViewLutSlot() const noexcept
  -> ShaderVisibleIndex
{
  return sky_view_lut_.srv_index;
}

auto SkyAtmosphereLutManager::GetMultiScatLutSlot() const noexcept
  -> ShaderVisibleIndex
{
  return multi_scat_lut_.srv_index;
}

auto SkyAtmosphereLutManager::GetCameraVolumeLutSlot() const noexcept
  -> ShaderVisibleIndex
{
  return camera_volume_lut_.srv_index;
}

auto SkyAtmosphereLutManager::GetBlueNoiseSlot() const noexcept
  -> ShaderVisibleIndex
{
  if (blue_noise_ready_) {
    return blue_noise_lut_.srv_index;
  }

  // Poll for upload completion
  if (blue_noise_upload_ticket_.has_value()) {
    if (uploader_
      && uploader_->IsComplete(blue_noise_upload_ticket_.value())
        .value_or(false)) {
      blue_noise_ready_ = true;
      blue_noise_upload_ticket_.reset();
      // Increment generation to trigger binding update in EnvironmentStaticData
      ++generation_;
      return blue_noise_lut_.srv_index;
    }
  }

  return kInvalidShaderVisibleIndex;
}

auto SkyAtmosphereLutManager::GetBlueNoiseSize() const noexcept
  -> std::tuple<uint32_t, uint32_t, uint32_t>
{
  return { resources::kBlueNoiseSize, resources::kBlueNoiseSize,
    resources::kBlueNoiseSlices };
}

auto SkyAtmosphereLutManager::GetTransmittanceLutTexture() const noexcept
  -> observer_ptr<graphics::Texture>
{
  return transmittance_lut_.texture
    ? observer_ptr(transmittance_lut_.texture.get())
    : nullptr;
}

auto SkyAtmosphereLutManager::GetSkyViewLutTexture() const noexcept
  -> observer_ptr<graphics::Texture>
{
  return sky_view_lut_.texture ? observer_ptr(sky_view_lut_.texture.get())
                               : nullptr;
}

auto SkyAtmosphereLutManager::GetMultiScatLutTexture() const noexcept
  -> observer_ptr<graphics::Texture>
{
  return multi_scat_lut_.texture ? observer_ptr(multi_scat_lut_.texture.get())
                                 : nullptr;
}

auto SkyAtmosphereLutManager::GetCameraVolumeLutTexture() const noexcept
  -> observer_ptr<graphics::Texture>
{
  return camera_volume_lut_.texture
    ? observer_ptr(camera_volume_lut_.texture.get())
    : nullptr;
}

auto SkyAtmosphereLutManager::GetBlueNoiseTexture() const noexcept
  -> observer_ptr<graphics::Texture>
{
  return blue_noise_lut_.texture ? observer_ptr(blue_noise_lut_.texture.get())
                                 : nullptr;
}

auto SkyAtmosphereLutManager::GetTransmittanceLutUavSlot() const noexcept
  -> ShaderVisibleIndex
{
  return transmittance_lut_.uav_index;
}

auto SkyAtmosphereLutManager::GetSkyViewLutUavSlot() const noexcept
  -> ShaderVisibleIndex
{
  return sky_view_lut_.uav_index;
}

auto SkyAtmosphereLutManager::GetMultiScatLutUavSlot() const noexcept
  -> ShaderVisibleIndex
{
  return multi_scat_lut_.uav_index;
}

auto SkyAtmosphereLutManager::GetCameraVolumeLutUavSlot() const noexcept
  -> ShaderVisibleIndex
{
  return camera_volume_lut_.uav_index;
}

auto SkyAtmosphereLutManager::EnsureResourcesCreated() -> bool
{
  if (resources_created_) {
    return true;
  }

  if (!gfx_) {
    LOG_F(ERROR, "SkyAtmosphereLutManager: Graphics not available");
    return false;
  }

  // Create transmittance LUT (RGBA16F - optical depth for Rayleigh/Mie/Abs)
  transmittance_lut_.texture
    = CreateTransmittanceLutTexture({ .width = config_.transmittance_width,
      .height = config_.transmittance_height });
  if (!transmittance_lut_.texture) {
    return false;
  }

  if (!CreateLutViews(transmittance_lut_, 1, true)) {
    CleanupResources();
    return false;
  }

  // Create sky-view LUT as a 2D texture array with altitude slices [P1].
  // Each slice is (sky_view_width x sky_view_height); array_size = slices.
  sky_view_lut_.texture = CreateSkyViewLutTexture(
    { .width = config_.sky_view_width, .height = config_.sky_view_height },
    config_.sky_view_slices);
  if (!sky_view_lut_.texture) {
    CleanupResources();
    return false;
  }

  // SRV/UAV must use kTexture2DArray dimension to see all slices [P2].
  if (!CreateLutViews(sky_view_lut_, config_.sky_view_slices, true)) {
    CleanupResources();
    return false;
  }

  // Create multiple scattering LUT (RGBA16F - total escaped radiance)
  multi_scat_lut_.texture = CreateMultiScatLutTexture(config_.multi_scat_size);
  if (!multi_scat_lut_.texture) {
    CleanupResources();
    return false;
  }

  if (!CreateLutViews(multi_scat_lut_, 1, true)) {
    CleanupResources();
    return false;
  }

  // Create camera volume LUT as a 3D texture (froxel grid)
  camera_volume_lut_.texture = CreateCameraVolumeLutTexture(
    {
      .width = config_.camera_volume_width,
      .height = config_.camera_volume_height,
    },
    config_.camera_volume_depth);
  if (!camera_volume_lut_.texture) {
    CleanupResources();
    return false;
  }

  // Camera volume needs special 3D texture view handling
  if (!CreateLutViews(camera_volume_lut_, config_.camera_volume_depth, true)) {
    CleanupResources();
    return false;
  }

  // Create blue noise texture as a 3D volume (R8_UNORM) [Phase 3]
  blue_noise_lut_.texture = CreateBlueNoiseTexture();
  if (!blue_noise_lut_.texture) {
    CleanupResources();
    return false;
  }

  // Blue Noise only needs an SRV (read-only)
  if (!CreateLutViews(blue_noise_lut_, resources::kBlueNoiseSlices, false)) {
    CleanupResources();
    return false;
  }

  // Upload initial Blue Noise data. This is a one-time operation.
  UploadBlueNoiseData();

  resources_created_ = true;

  LOG_F(INFO,
    "SkyAtmosphereLutManager: created LUTs (transmittance={}x{}, "
    "sky_view={}x{}x{} slices, multi_scat={}x{}, camera_volume={}x{}x{}, "
    "blue_noise={}x{}x{})",
    config_.transmittance_width, config_.transmittance_height,
    config_.sky_view_width, config_.sky_view_height, config_.sky_view_slices,
    config_.multi_scat_size, config_.multi_scat_size,
    config_.camera_volume_width, config_.camera_volume_height,
    config_.camera_volume_depth, resources::kBlueNoiseSize,
    resources::kBlueNoiseSize, resources::kBlueNoiseSlices);

  return true;
}

// Common implementation for creating LUT textures
auto SkyAtmosphereLutManager::CreateLutTexture(Extent<uint32_t> extent,
  uint32_t depth_or_array_size, bool is_rgba, const char* debug_name,
  TextureType texture_type) -> std::shared_ptr<graphics::Texture>
{
  TextureDesc desc;
  desc.width = extent.width;
  desc.height = extent.height;
  desc.mip_levels = 1U;
  desc.sample_count = 1U;
  desc.format = is_rgba ? Format::kRGBA16Float : Format::kRG16Float;
  desc.debug_name = debug_name;
  desc.is_shader_resource = true;
  desc.is_uav = true;
  desc.is_render_target = false;
  desc.initial_state = graphics::ResourceStates::kUnorderedAccess;
  desc.texture_type = texture_type;

  // Set depth or array_size based on texture type
  if (texture_type == TextureType::kTexture3D) {
    desc.depth = depth_or_array_size;
  } else if (texture_type == TextureType::kTexture2DArray) {
    desc.array_size = depth_or_array_size;
  }
  // For Texture2D, depth_or_array_size is ignored

  auto texture = gfx_->CreateTexture(desc);
  if (!texture) {
    LOG_F(ERROR, "SkyAtmosphereLutManager: failed to create texture '{}'",
      debug_name);
    return nullptr;
  }

  texture->SetName(desc.debug_name);
  gfx_->GetResourceRegistry().Register(texture);

  return texture;
}

// Domain-specific LUT texture creation methods

auto SkyAtmosphereLutManager::CreateTransmittanceLutTexture(
  Extent<uint32_t> extent) -> std::shared_ptr<graphics::Texture>
{
  return CreateLutTexture(
    extent, 1, true, "Atmo_TransmittanceLUT", TextureType::kTexture2D);
}

auto SkyAtmosphereLutManager::CreateSkyViewLutTexture(Extent<uint32_t> extent,
  uint32_t num_slices) -> std::shared_ptr<graphics::Texture>
{
  return CreateLutTexture(
    extent, num_slices, true, "Atmo_SkyViewLUT", TextureType::kTexture2DArray);
}

auto SkyAtmosphereLutManager::CreateMultiScatLutTexture(uint32_t size)
  -> std::shared_ptr<graphics::Texture>
{
  return CreateLutTexture({ .width = size, .height = size }, 1, true,
    "Atmo_MultiScatLUT", TextureType::kTexture2D);
}

auto SkyAtmosphereLutManager::CreateCameraVolumeLutTexture(
  Extent<uint32_t> extent, uint32_t depth) -> std::shared_ptr<graphics::Texture>
{
  return CreateLutTexture(
    extent, depth, true, "Atmo_CameraVolumeLUT", TextureType::kTexture3D);
}

auto SkyAtmosphereLutManager::UploadBlueNoiseData() -> void
{
  if (!uploader_ || !staging_) {
    return;
  }

  const uint32_t size = resources::kBlueNoiseSize;
  const uint32_t slices = resources::kBlueNoiseSlices;
  const uint32_t row_pitch = size; // 1 byte per texel
  const uint32_t slice_pitch = size * size;

  upload::UploadTextureSourceView src_view;
  src_view.subresources.push_back(upload::UploadTextureSourceSubresource {
    .bytes = std::span<const std::byte>(
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      reinterpret_cast<const std::byte*>(resources::TextureData_BlueNoise),
      resources::kBlueNoiseDataSize),
    .row_pitch = row_pitch,
    .slice_pitch = slice_pitch,
  });

  upload::UploadRequest request {
    .kind = upload::UploadKind::kTexture3D,
    .priority = upload::Priority { 0 },
    .debug_name = "BlueNoise_Upload",
    .desc = upload::UploadTextureDesc {
      .dst = blue_noise_lut_.texture,
      .width = size,
      .height = size,
      .depth = slices,
      .format = Format::kR8UNorm,
    },
    .subresources = {
      upload::UploadSubresource {
        .mip = 0,
        .array_slice = 0,
        .x = 0, .y = 0, .z = 0,
        .width = size,
        .height = size,
        .depth = slices,
      },
    },
    .data = src_view,
  };

  if (auto result = uploader_->Submit(request, *staging_); !result) {
    const std::error_code ec = result.error();
    LOG_F(ERROR,
      "SkyAtmosphereLutManager: failed to submit Blue Noise upload: {}",
      ec.message());
  } else {
    blue_noise_upload_ticket_ = *result;
    blue_noise_ready_ = false;
  }
}

auto SkyAtmosphereLutManager::CreateBlueNoiseTexture()
  -> std::shared_ptr<graphics::Texture>
{
  TextureDesc desc;
  desc.width = resources::kBlueNoiseSize;
  desc.height = resources::kBlueNoiseSize;
  desc.depth = resources::kBlueNoiseSlices;
  desc.mip_levels = 1U;
  desc.sample_count = 1U;
  desc.format = Format::kR8UNorm;
  desc.debug_name = "Atmo_BlueNoiseVolume";
  desc.is_shader_resource = true;
  desc.is_uav = false;
  desc.is_render_target = false;
  desc.initial_state = graphics::ResourceStates::kCommon;
  desc.texture_type = TextureType::kTexture3D;

  auto texture = gfx_->CreateTexture(desc);
  if (!texture) {
    LOG_F(
      ERROR, "SkyAtmosphereLutManager: failed to create Blue Noise texture");
    return nullptr;
  }

  texture->SetName(desc.debug_name);
  gfx_->GetResourceRegistry().Register(texture);

  return texture;
}

auto SkyAtmosphereLutManager::CreateLutViews(
  LutResources& lut, uint32_t depth_or_array_size, bool is_rgba) -> bool
{
  auto& allocator = gfx_->GetDescriptorAllocator();
  auto& registry = gfx_->GetResourceRegistry();

  const auto& tex_desc = lut.texture->GetDescriptor();
  const auto view_dimension = tex_desc.texture_type;

  // Create SRV for shader sampling
  auto srv_handle = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!srv_handle.IsValid()) {
    LOG_F(ERROR, "SkyAtmosphereLutManager: failed to allocate SRV descriptor");
    return false;
  }

  graphics::TextureViewDescription srv_desc;
  srv_desc.view_type = graphics::ResourceViewType::kTexture_SRV;
  srv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  srv_desc.format
    = is_rgba ? Format::kRGBA16Float : (lut.texture->GetDescriptor().format);
  srv_desc.dimension = view_dimension;

  // Appropriately set sub-resource range for array textures [P2].
  if (view_dimension == TextureType::kTexture2DArray) {
    srv_desc.sub_resources.base_array_slice = 0U;
    srv_desc.sub_resources.num_array_slices = depth_or_array_size;
  }

  lut.srv_index = allocator.GetShaderVisibleIndex(srv_handle);
  lut.srv_view
    = registry.RegisterView(*lut.texture, std::move(srv_handle), srv_desc);

  // Create UAV for compute shader writes only if supported [P20]
  if (tex_desc.is_uav) {
    auto uav_handle
      = allocator.Allocate(graphics::ResourceViewType::kTexture_UAV,
        graphics::DescriptorVisibility::kShaderVisible);
    if (!uav_handle.IsValid()) {
      LOG_F(
        ERROR, "SkyAtmosphereLutManager: failed to allocate UAV descriptor");
      return false;
    }

    graphics::TextureViewDescription uav_desc;
    uav_desc.view_type = graphics::ResourceViewType::kTexture_UAV;
    uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    uav_desc.format
      = is_rgba ? Format::kRGBA16Float : (lut.texture->GetDescriptor().format);
    uav_desc.dimension = view_dimension;

    // Appropriately set sub-resource range for array textures [P2].
    if (view_dimension == TextureType::kTexture2DArray) {
      uav_desc.sub_resources.base_array_slice = 0U;
      uav_desc.sub_resources.num_array_slices = depth_or_array_size;
    }

    lut.uav_index = allocator.GetShaderVisibleIndex(uav_handle);
    lut.uav_view
      = registry.RegisterView(*lut.texture, std::move(uav_handle), uav_desc);
  } else {
    lut.uav_index = kInvalidShaderVisibleIndex;
    lut.uav_view = {};
  }

  return true;
}

auto SkyAtmosphereLutManager::CleanupResources() -> void
{
  if (!gfx_) {
    return;
  }

  auto& registry = gfx_->GetResourceRegistry();

  auto cleanup_lut = [&registry](LutResources& lut) {
    if (!lut.texture) {
      return;
    }
    if (registry.Contains(*lut.texture)) {
      if (lut.srv_view.get().IsValid()) {
        registry.UnRegisterView(*lut.texture, lut.srv_view);
      }
      if (lut.uav_view.get().IsValid()) {
        registry.UnRegisterView(*lut.texture, lut.uav_view);
      }
      registry.UnRegisterResource(*lut.texture);
    }
    lut.texture.reset();
    lut.srv_view = {};
    lut.uav_view = {};
    lut.srv_index = kInvalidShaderVisibleIndex;
    lut.uav_index = kInvalidShaderVisibleIndex;
  };

  cleanup_lut(transmittance_lut_);
  cleanup_lut(sky_view_lut_);
  cleanup_lut(multi_scat_lut_);
  cleanup_lut(camera_volume_lut_);
  cleanup_lut(blue_noise_lut_);

  blue_noise_upload_ticket_.reset();
  blue_noise_ready_ = false;

  resources_created_ = false;
}

auto SkyAtmosphereLutManager::ExtractCachedParams(
  const GpuSkyAtmosphereParams& params) -> CachedParams
{
  return CachedParams {
    .planet_radius_m = params.planet_radius_m,
    .atmosphere_height_m = params.atmosphere_height_m,
    .rayleigh_scale_height_m = params.rayleigh_scale_height_m,
    .mie_scale_height_m = params.mie_scale_height_m,
    .mie_g = params.mie_g,
    .multi_scattering_factor = params.multi_scattering_factor,
    .rayleigh_r = params.rayleigh_scattering_rgb.x,
    .rayleigh_g = params.rayleigh_scattering_rgb.y,
    .rayleigh_b = params.rayleigh_scattering_rgb.z,
    .mie_r = params.mie_scattering_rgb.x,
    .mie_g_val = params.mie_scattering_rgb.y,
    .mie_b = params.mie_scattering_rgb.z,
    .absorption_r = params.absorption_rgb.x,
    .absorption_g = params.absorption_rgb.y,
    .absorption_b = params.absorption_rgb.z,
    .ground_albedo_r = params.ground_albedo_rgb.x,
    .ground_albedo_g = params.ground_albedo_rgb.y,
    .ground_albedo_b = params.ground_albedo_rgb.z,
    .absorption_density = params.absorption_density,
    .sky_view_slices = params.sky_view_lut_slices,
    .sky_view_alt_mapping_mode = params.sky_view_alt_mapping_mode,
    .sun_disk_enabled = params.sun_disk_enabled,
    .sun_disk_angular_radius_radians = params.sun_disk_angular_radius_radians,
    .aerial_perspective_distance_scale
    = params.aerial_perspective_distance_scale,
    .enabled = params.enabled,
  };
}

auto SkyAtmosphereLutManager::SetSkyViewLutSlices(uint32_t slices) -> void
{
  constexpr uint32_t kMinSlices = 4U;
  constexpr uint32_t kMaxSlices = 32U;

  slices = std::clamp(slices, kMinSlices, kMaxSlices);
  if (config_.sky_view_slices == slices) {
    return;
  }

  config_.sky_view_slices = slices;

  // Changing slice count requires destroying and recreating the sky-view
  // texture because D3D12 array_size is immutable after creation [P16].
  // We must immediately recreate the resources to ensure that a valid
  // SRV slot is available for the upcoming frame's EnvironmentStaticData
  // population. Leaving it destroyed causes a gap where the slot is invalid,
  // leading to black artifacts in sky capture/reflection passes.
  if (resources_created_) {
    CleanupResources();
    EnsureResourcesCreated();
    // Force regeneration since texture is fresh
    luts_generated_ = false;
  }

  dirty_ = true;
  ++generation_;
  LOG_F(INFO, "SkyAtmosphereLutManager: sky_view_slices changed to {}", slices);
}

auto SkyAtmosphereLutManager::SetAltMappingMode(uint32_t mode) -> void
{
  if (config_.sky_view_alt_mapping_mode == mode) {
    return;
  }

  config_.sky_view_alt_mapping_mode = mode;
  dirty_ = true;
  ++generation_;
  LOG_F(INFO, "SkyAtmosphereLutManager: alt_mapping_mode changed to {}", mode);
}

} // namespace oxygen::engine::internal
