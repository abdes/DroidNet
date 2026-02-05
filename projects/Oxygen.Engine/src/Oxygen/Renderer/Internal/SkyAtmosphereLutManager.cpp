//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

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
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/Types/EnvironmentStaticData.h>

namespace oxygen::engine::internal {

using graphics::TextureDesc;
using oxygen::TextureType;

SkyAtmosphereLutManager::SkyAtmosphereLutManager(
  observer_ptr<Graphics> gfx, Config config)
  : gfx_(gfx)
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

  LOG_F(2, "SkyAtmosphereLutManager: parameters changed, marking dirty");
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

auto SkyAtmosphereLutManager::GetTransmittanceLutTexture() const noexcept
  -> observer_ptr<graphics::Texture>
{
  return observer_ptr(transmittance_lut_.texture.get());
}

auto SkyAtmosphereLutManager::GetSkyViewLutTexture() const noexcept
  -> observer_ptr<graphics::Texture>
{
  return observer_ptr(sky_view_lut_.texture.get());
}

auto SkyAtmosphereLutManager::GetMultiScatLutTexture() const noexcept
  -> observer_ptr<graphics::Texture>
{
  return observer_ptr(multi_scat_lut_.texture.get());
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
  transmittance_lut_.texture = CreateLutTexture(config_.transmittance_width,
    config_.transmittance_height, true, "Atmo_TransmittanceLUT");
  if (!transmittance_lut_.texture) {
    return false;
  }

  if (!CreateLutViews(transmittance_lut_, true)) {
    CleanupResources();
    return false;
  }

  // Create sky-view LUT (RGBA16F - inscatter RGB + transmittance)
  sky_view_lut_.texture = CreateLutTexture(
    config_.sky_view_width, config_.sky_view_height, true, "Atmo_SkyViewLUT");
  if (!sky_view_lut_.texture) {
    CleanupResources();
    return false;
  }

  if (!CreateLutViews(sky_view_lut_, true)) {
    CleanupResources();
    return false;
  }

  // Create multiple scattering LUT (RGBA16F - total escaped radiance)
  multi_scat_lut_.texture = CreateLutTexture(config_.multi_scat_size,
    config_.multi_scat_size, true, "Atmo_MultiScatLUT");
  if (!multi_scat_lut_.texture) {
    CleanupResources();
    return false;
  }

  if (!CreateLutViews(multi_scat_lut_, true)) {
    CleanupResources();
    return false;
  }

  resources_created_ = true;

  LOG_F(INFO,
    "SkyAtmosphereLutManager: created LUTs (transmittance={}x{}, "
    "sky_view={}x{}, multi_scat={}x{})",
    config_.transmittance_width, config_.transmittance_height,
    config_.sky_view_width, config_.sky_view_height, config_.multi_scat_size,
    config_.multi_scat_size);

  return true;
}

auto SkyAtmosphereLutManager::CreateLutTexture(uint32_t width, uint32_t height,
  bool is_rgba, const char* debug_name) -> std::shared_ptr<graphics::Texture>
{
  TextureDesc desc;
  desc.width = width;
  desc.height = height;
  desc.mip_levels = 1u;
  desc.sample_count = 1u;
  desc.format = is_rgba ? Format::kRGBA16Float : Format::kRG16Float;
  desc.texture_type = TextureType::kTexture2D;
  desc.debug_name = debug_name;
  desc.is_shader_resource = true;
  desc.is_uav = true;
  desc.is_render_target = false;
  desc.initial_state = graphics::ResourceStates::kUnorderedAccess;

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

auto SkyAtmosphereLutManager::CreateLutViews(LutResources& lut, bool is_rgba)
  -> bool
{
  auto& allocator = gfx_->GetDescriptorAllocator();
  auto& registry = gfx_->GetResourceRegistry();

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
  srv_desc.format = is_rgba ? Format::kRGBA16Float : Format::kRG16Float;
  srv_desc.dimension = TextureType::kTexture2D;

  lut.srv_index = allocator.GetShaderVisibleIndex(srv_handle);
  lut.srv_view
    = registry.RegisterView(*lut.texture, std::move(srv_handle), srv_desc);

  // Create UAV for compute shader writes
  auto uav_handle = allocator.Allocate(graphics::ResourceViewType::kTexture_UAV,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!uav_handle.IsValid()) {
    LOG_F(ERROR, "SkyAtmosphereLutManager: failed to allocate UAV descriptor");
    return false;
  }

  graphics::TextureViewDescription uav_desc;
  uav_desc.view_type = graphics::ResourceViewType::kTexture_UAV;
  uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  uav_desc.format = is_rgba ? Format::kRGBA16Float : Format::kRG16Float;
  uav_desc.dimension = TextureType::kTexture2D;

  lut.uav_index = allocator.GetShaderVisibleIndex(uav_handle);
  lut.uav_view
    = registry.RegisterView(*lut.texture, std::move(uav_handle), uav_desc);

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
    .absorption_scale_height_m = params.absorption_scale_height_m,
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
  };
}

} // namespace oxygen::engine::internal
