//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "IblManager.h"

#include <cmath>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/TextureType.h> // For TextureType enum
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

namespace oxygen::engine::internal {

using graphics::TextureDesc;
using oxygen::TextureType;

IblManager::IblManager(observer_ptr<Graphics> gfx, Config config)
  : gfx_(gfx)
  , config_(config)
{
}

IblManager::~IblManager() { CleanupResources(); }

auto IblManager::CleanupResources() -> void
{
  if (!gfx_)
    return;
  auto& registry = gfx_->GetResourceRegistry();

  auto cleanup_map = [&](MapResources& map) {
    if (!map.texture)
      return;
    if (registry.Contains(*map.texture)) {
      if (map.srv_view.get().IsValid()) {
        registry.UnRegisterView(*map.texture, map.srv_view);
      }
      for (auto& uav : map.uav_views) {
        if (uav.get().IsValid()) {
          registry.UnRegisterView(*map.texture, uav);
        }
      }
      registry.UnRegisterResource(*map.texture);
    }
    map.texture.reset();
    map.srv_view = {};
    map.srv_index = kInvalidShaderVisibleIndex;
    map.uav_views.clear();
    map.uav_indices.clear();
  };

  cleanup_map(irradiance_map_);
  cleanup_map(prefilter_map_);

  resources_created_ = false;
  last_source_cubemap_slot_ = kInvalidShaderVisibleIndex;
}

auto IblManager::EnsureResourcesCreated() -> bool
{
  if (resources_created_)
    return true;
  if (!gfx_)
    return false;

  if (config_.irradiance_size == 0U || config_.prefilter_size == 0U) {
    LOG_F(ERROR,
      "IblManager: invalid config (irradiance_size={}, prefilter_size={})",
      config_.irradiance_size, config_.prefilter_size);
    return false;
  }

  // Irradiance Map: small cubemap, 1 mip
  irradiance_map_.texture
    = CreateMapTexture(config_.irradiance_size, 1, "IBL_IrradianceMap");
  if (!irradiance_map_.texture)
    return false;

  if (!CreateViews(irradiance_map_)) {
    CleanupResources();
    return false;
  }

  // Prefilter Map: larger cubemap, full mip chain
  // Valid mip levels: log2(size) + 1
  // e.g. 256 -> 9 mips
  // We want roughly 5-6 levels of roughness.
  // Standard split sum often uses 5 levels.
  // Let's use full chain for now.
  const uint32_t prefilter_mips
    = static_cast<uint32_t>(std::floor(std::log2(config_.prefilter_size))) + 1;

  prefilter_map_.texture = CreateMapTexture(
    config_.prefilter_size, prefilter_mips, "IBL_PrefilterMap");
  if (!prefilter_map_.texture) {
    CleanupResources();
    return false;
  }

  if (!CreateViews(prefilter_map_)) {
    CleanupResources();
    return false;
  }

  resources_created_ = true;
  LOG_F(INFO, "IblManager: Created resources (Err={}, Pref={})",
    config_.irradiance_size, config_.prefilter_size);
  return true;
}

auto IblManager::CreateMapTexture(uint32_t size, uint32_t mip_levels,
  const char* name) -> std::shared_ptr<graphics::Texture>
{
  TextureDesc desc;
  desc.width = size;
  desc.height = size;
  desc.depth = 1;
  desc.array_size = 6; // Cubemap
  desc.mip_levels = mip_levels;
  desc.sample_count = 1;
  desc.format = Format::kRGBA16Float; // HDR needed
  desc.texture_type = TextureType::kTextureCube;
  desc.debug_name = name;
  desc.is_shader_resource = true;
  desc.is_uav = true;
  desc.is_render_target = false; // We use Compute
  desc.initial_state = graphics::ResourceStates::kUnorderedAccess;

  auto texture = gfx_->CreateTexture(desc);
  if (!texture) {
    LOG_F(ERROR, "IblManager: Failed to create texture '{}'", name);
    return nullptr;
  }
  texture->SetName(name);
  gfx_->GetResourceRegistry().Register(texture);
  return texture;
}

auto IblManager::CreateViews(MapResources& map) -> bool
{
  auto& allocator = gfx_->GetDescriptorAllocator();
  auto& registry = gfx_->GetResourceRegistry();

  // 1. SRV (Cubemap view)
  {
    auto handle = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
    if (!handle.IsValid())
      return false;

    graphics::TextureViewDescription srv_desc;
    srv_desc.view_type = graphics::ResourceViewType::kTexture_SRV;
    srv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    srv_desc.format = map.texture->GetDescriptor().format;
    srv_desc.dimension = TextureType::kTextureCube;
    srv_desc.sub_resources.base_mip_level = 0;
    srv_desc.sub_resources.num_mip_levels
      = map.texture->GetDescriptor().mip_levels;
    srv_desc.sub_resources.base_array_slice = 0;
    srv_desc.sub_resources.num_array_slices = 6;

    map.srv_index = allocator.GetShaderVisibleIndex(handle);
    map.srv_view
      = registry.RegisterView(*map.texture, std::move(handle), srv_desc);
  }

  // 2. UAVs (Texture2DArray view, one per mip level)
  // We cannot write to a "Cubemap" UAV directly in HLSL easily as a single
  // target in some cases, but typically we treat it as Texture2DArray[6].
  const uint32_t mips = map.texture->GetDescriptor().mip_levels;
  map.uav_views.resize(mips);
  map.uav_indices.resize(mips);

  for (uint32_t i = 0; i < mips; ++i) {
    auto handle = allocator.Allocate(graphics::ResourceViewType::kTexture_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
    if (!handle.IsValid())
      return false;

    graphics::TextureViewDescription uav_desc;
    uav_desc.view_type = graphics::ResourceViewType::kTexture_UAV;
    uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    uav_desc.format = map.texture->GetDescriptor().format;
    uav_desc.dimension
      = TextureType::kTexture2DArray; // Treat faces as array slices
    uav_desc.sub_resources.base_mip_level = i;
    uav_desc.sub_resources.num_mip_levels = 1;
    uav_desc.sub_resources.base_array_slice = 0;
    uav_desc.sub_resources.num_array_slices = 6;

    map.uav_indices[i] = allocator.GetShaderVisibleIndex(handle);
    map.uav_views[i]
      = registry.RegisterView(*map.texture, std::move(handle), uav_desc);
  }

  return true;
}

auto IblManager::GetPrefilterMapUavSlot(
  IblPassTag /*tag*/, uint32_t mip_level) const noexcept -> ShaderVisibleIndex
{
  if (mip_level < prefilter_map_.uav_indices.size()) {
    return prefilter_map_.uav_indices[mip_level];
  }
  return kInvalidShaderVisibleIndex;
}

auto IblManager::GetIrradianceMapUavSlot(IblPassTag /*tag*/) const noexcept
  -> ShaderVisibleIndex
{
  if (!irradiance_map_.uav_indices.empty()) {
    return irradiance_map_.uav_indices[0];
  }
  return kInvalidShaderVisibleIndex;
}

auto IblManager::GetIrradianceMap(IblPassTag /*tag*/) const noexcept
  -> observer_ptr<graphics::Texture>
{
  return observer_ptr(irradiance_map_.texture.get());
}

auto IblManager::GetPrefilterMap(IblPassTag /*tag*/) const noexcept
  -> observer_ptr<graphics::Texture>
{
  return observer_ptr(prefilter_map_.texture.get());
}

auto IblManager::MarkGenerated(
  IblPassTag /*tag*/, ShaderVisibleIndex source_slot) -> void
{
  last_source_cubemap_slot_ = source_slot;
  generation_.fetch_add(1, std::memory_order_acq_rel);
}

auto IblManager::QueryOutputsFor(ShaderVisibleIndex source_slot) const noexcept
  -> IIblProvider::OutputMaps
{
  IIblProvider::OutputMaps out {};
  out.generation = generation_.load(std::memory_order_acquire);

  if (!resources_created_)
    return out;

  if (source_slot == kInvalidShaderVisibleIndex)
    return out;

  // Only publish outputs when they were generated for the requested source.
  if (last_source_cubemap_slot_ != source_slot)
    return out;

  out.irradiance = irradiance_map_.srv_index;
  out.prefilter = prefilter_map_.srv_index;
  if (prefilter_map_.texture) {
    out.prefilter_mip_levels
      = prefilter_map_.texture->GetDescriptor().mip_levels;
  }
  return out;
}

} // namespace oxygen::engine::internal
