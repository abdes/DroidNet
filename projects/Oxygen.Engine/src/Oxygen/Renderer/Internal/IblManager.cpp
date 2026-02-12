//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Internal/IblManager.h>

namespace oxygen::engine::internal {

using graphics::TextureDesc;
using oxygen::TextureType;

IblManager::IblManager(observer_ptr<Graphics> gfx)
  : IblManager(gfx, {})
{
}

IblManager::IblManager(observer_ptr<Graphics> gfx, Config config)
  : gfx_(gfx)
  , config_(config)
{
  DCHECK_NOTNULL_F(gfx_);
}

IblManager::~IblManager() { CleanupResources(); }

auto IblManager::CleanupViewResources(const ViewId view_id) -> void
{
  const auto it = view_states_.find(view_id);
  if (it == view_states_.end() || !it->second) {
    return;
  }

  auto& state = *it->second;
  auto& registry = gfx_->GetResourceRegistry();

  auto cleanup_map = [&](MapResources& map) {
    if (!map.texture) {
      return;
    }
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

  cleanup_map(state.irradiance_map);
  cleanup_map(state.prefilter_map);

  state.resources_created = false;
  state.last_source_cubemap_slot = kInvalidShaderVisibleIndex;
  state.last_source_content_version = 0ULL;
  state.generation.store(1ULL, std::memory_order_release);
}

auto IblManager::CleanupResources() -> void
{
  std::vector<ViewId> ids;
  ids.reserve(view_states_.size());
  for (const auto& [id, _] : view_states_) {
    ids.push_back(id);
  }
  for (const auto id : ids) {
    CleanupViewResources(id);
  }
  view_states_.clear();
}

auto IblManager::EnsureResourcesCreated() -> bool
{
  // View-specific allocation is required by this design.
  return true;
}

auto IblManager::EnsureResourcesCreatedForView(const ViewId view_id) -> bool
{
  return EnsureViewResourcesCreated(view_id);
}

auto IblManager::EnsureViewResourcesCreated(const ViewId view_id) -> bool
{
  auto& state_ptr = view_states_[view_id];
  if (!state_ptr) {
    state_ptr = std::make_unique<ViewState>();
  }
  auto& state = *state_ptr;

  if (state.resources_created) {
    return true;
  }

  if (config_.irradiance_size == 0U || config_.prefilter_size == 0U) {
    LOG_F(ERROR,
      "IblManager: invalid config (irradiance_size={}, prefilter_size={})",
      config_.irradiance_size, config_.prefilter_size);
    return false;
  }

  state.irradiance_map.texture
    = CreateMapTexture(config_.irradiance_size, 1, "IBL_IrradianceMap");
  if (!state.irradiance_map.texture) {
    return false;
  }

  if (!CreateViews(state.irradiance_map)) {
    CleanupViewResources(view_id);
    return false;
  }

  const uint32_t prefilter_mips
    = static_cast<uint32_t>(std::floor(std::log2(config_.prefilter_size))) + 1;

  state.prefilter_map.texture = CreateMapTexture(
    config_.prefilter_size, prefilter_mips, "IBL_PrefilterMap");
  if (!state.prefilter_map.texture) {
    CleanupViewResources(view_id);
    return false;
  }

  if (!CreateViews(state.prefilter_map)) {
    CleanupViewResources(view_id);
    return false;
  }

  state.resources_created = true;
  LOG_F(INFO, "IblManager: Created per-view resources (view={}, Err={}, Pref={})",
    view_id.get(), config_.irradiance_size, config_.prefilter_size);
  return true;
}

auto IblManager::CreateMapTexture(uint32_t size, uint32_t mip_levels,
  const char* name) -> std::shared_ptr<graphics::Texture>
{
  TextureDesc desc;
  desc.width = size;
  desc.height = size;
  desc.depth = 1;
  desc.array_size = 6;
  desc.mip_levels = mip_levels;
  desc.sample_count = 1;
  desc.format = Format::kRGBA16Float;
  desc.texture_type = TextureType::kTextureCube;
  desc.debug_name = name;
  desc.is_shader_resource = true;
  desc.is_uav = true;
  desc.is_render_target = false;
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

  {
    auto handle = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
    if (!handle.IsValid()) {
      return false;
    }

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

  const uint32_t mips = map.texture->GetDescriptor().mip_levels;
  map.uav_views.resize(mips);
  map.uav_indices.resize(mips);

  for (uint32_t i = 0; i < mips; ++i) {
    auto handle = allocator.Allocate(graphics::ResourceViewType::kTexture_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
    if (!handle.IsValid()) {
      return false;
    }

    graphics::TextureViewDescription uav_desc;
    uav_desc.view_type = graphics::ResourceViewType::kTexture_UAV;
    uav_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    uav_desc.format = map.texture->GetDescriptor().format;
    uav_desc.dimension = TextureType::kTexture2DArray;
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

auto IblManager::GetPrefilterMapUavSlot(IblPassTag /*tag*/, const ViewId view_id,
  const uint32_t mip_level) const noexcept -> ShaderVisibleIndex
{
  const auto it = view_states_.find(view_id);
  if (it == view_states_.end() || !it->second) {
    return kInvalidShaderVisibleIndex;
  }
  const auto& map = it->second->prefilter_map;
  if (mip_level < map.uav_indices.size()) {
    return map.uav_indices[mip_level];
  }
  return kInvalidShaderVisibleIndex;
}

auto IblManager::GetIrradianceMapUavSlot(
  IblPassTag /*tag*/, const ViewId view_id) const noexcept -> ShaderVisibleIndex
{
  const auto it = view_states_.find(view_id);
  if (it == view_states_.end() || !it->second) {
    return kInvalidShaderVisibleIndex;
  }
  const auto& map = it->second->irradiance_map;
  if (!map.uav_indices.empty()) {
    return map.uav_indices[0];
  }
  return kInvalidShaderVisibleIndex;
}

auto IblManager::GetIrradianceMap(
  IblPassTag /*tag*/, const ViewId view_id) const noexcept
  -> observer_ptr<graphics::Texture>
{
  const auto it = view_states_.find(view_id);
  if (it == view_states_.end() || !it->second) {
    return nullptr;
  }
  return observer_ptr(it->second->irradiance_map.texture.get());
}

auto IblManager::GetPrefilterMap(
  IblPassTag /*tag*/, const ViewId view_id) const noexcept
  -> observer_ptr<graphics::Texture>
{
  const auto it = view_states_.find(view_id);
  if (it == view_states_.end() || !it->second) {
    return nullptr;
  }
  return observer_ptr(it->second->prefilter_map.texture.get());
}

auto IblManager::MarkGenerated(IblPassTag /*tag*/, const ViewId view_id,
  const ShaderVisibleIndex source_slot,
  const std::uint64_t source_content_version) -> void
{
  auto& state_ptr = view_states_[view_id];
  if (!state_ptr) {
    state_ptr = std::make_unique<ViewState>();
  }
  auto& state = *state_ptr;
  state.last_source_cubemap_slot = source_slot;
  state.last_source_content_version = source_content_version;
  state.generation.fetch_add(1, std::memory_order_acq_rel);
}

auto IblManager::QueryOutputsFor(
  const ViewId view_id, const ShaderVisibleIndex source_slot) const noexcept
  -> IIblProvider::OutputMaps
{
  IIblProvider::OutputMaps out {};

  const auto it = view_states_.find(view_id);
  if (it == view_states_.end() || !it->second) {
    return out;
  }

  const auto& state = *it->second;
  out.generation = state.generation.load(std::memory_order_acquire);
  out.source_content_version = state.last_source_content_version;

  if (!state.resources_created) {
    return out;
  }

  if (source_slot == kInvalidShaderVisibleIndex) {
    return out;
  }

  if (state.last_source_cubemap_slot != source_slot) {
    return out;
  }

  out.irradiance = state.irradiance_map.srv_index;
  out.prefilter = state.prefilter_map.srv_index;
  if (state.prefilter_map.texture) {
    out.prefilter_mip_levels
      = state.prefilter_map.texture->GetDescriptor().mip_levels;
  }
  return out;
}

auto IblManager::EraseViewState(const ViewId view_id) -> void
{
  CleanupViewResources(view_id);
  view_states_.erase(view_id);
}

} // namespace oxygen::engine::internal
