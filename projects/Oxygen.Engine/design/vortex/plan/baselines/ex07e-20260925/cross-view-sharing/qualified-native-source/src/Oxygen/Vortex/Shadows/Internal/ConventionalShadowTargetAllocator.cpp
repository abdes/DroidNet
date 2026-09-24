//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/AllocationBudget.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Shadows/Internal/ConventionalShadowTargetAllocator.h>
#include <Oxygen/Vortex/Shadows/Internal/ShadowEligibility.h>
#include <Oxygen/Vortex/Shadows/Types/CubeLocalShadowRecord.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/LightingIndices.h>

namespace oxygen::vortex::shadows::internal {

namespace {

  constexpr auto kDirectionalRequestedResolutions
    = std::array { 1024U, 2048U, 3072U, 4096U };
  constexpr auto kLocalRequestedResolutions
    = std::array { 512U, 1024U, 2048U, 2048U };

  auto ResolveDirectionalResolutionRequest(
    const scene::ShadowResolutionHint hint) -> std::uint32_t
  {
    const auto index = static_cast<std::size_t>(hint);
    return index < kDirectionalRequestedResolutions.size()
      ? kDirectionalRequestedResolutions.at(index)
      : kDirectionalRequestedResolutions.front();
  }

  auto ResolveLocalResolutionRequest(const scene::ShadowResolutionHint hint)
    -> std::uint32_t
  {
    const auto index = static_cast<std::size_t>(hint);
    return index < kLocalRequestedResolutions.size()
      ? kLocalRequestedResolutions.at(index)
      : kLocalRequestedResolutions.at(
          static_cast<std::size_t>(scene::ShadowResolutionHint::kMedium));
  }

  auto ResolveDirectionalResolutionBudget(const ShadowQualityTier quality_tier)
    -> std::uint32_t
  {
    switch (quality_tier) {
    case ShadowQualityTier::kLow:
      return 1024U;
    case ShadowQualityTier::kMedium:
      return 2048U;
    case ShadowQualityTier::kHigh:
      return 3072U;
    case ShadowQualityTier::kUltra:
      return 4096U;
    default:
      return 2048U;
    }
  }

  auto ResolveDirectionalResolution(
    const scene::ShadowResolutionHint resolution_hint,
    const ShadowQualityTier quality_tier) -> std::uint32_t
  {
    return (std::min)(ResolveDirectionalResolutionRequest(resolution_hint),
      ResolveDirectionalResolutionBudget(quality_tier));
  }

  auto ResolveDepthSrvFormat(const Format format) -> Format
  {
    return format == Format::kDepth32 ? Format::kR32Float : format;
  }

} // namespace

ConventionalShadowTargetAllocator::ConventionalShadowTargetAllocator(
  Renderer& renderer)
  : renderer_(renderer)

{
}

ConventionalShadowTargetAllocator::~ConventionalShadowTargetAllocator()
{
  for (auto& [id, allocations] : views_) {
    Retire(allocations);
  }
  {
    std::lock_guard lock(local_pool_->mutex);
    local_pool_->closed = true;
    local_pool_->reuse.Close();
  }
  local_content_.clear();
  local_chunks_.clear();
}

auto ConventionalShadowTargetAllocator::Retire(SurfaceAllocation& allocation)
  -> void
{
  if (!allocation.surface) {
    return;
  }
  auto gfx = renderer_.GetGraphics();
  if (!gfx) {
    allocation = {};
    return;
  }
  gfx->GetDeferredReclaimer().RegisterDeferredAction(
    [owner = gfx.get(),
      surface = std::move(allocation.surface)] mutable -> void {
      owner->ForgetKnownResourceState(*surface);
      auto& registry = owner->GetResourceRegistry();
      if (registry.Contains(*surface)) {
        registry.UnRegisterResource(*surface);
      }
      surface.reset();
    });
  allocation = {};
}

auto ConventionalShadowTargetAllocator::Retire(ViewAllocations& allocations)
  -> void
{
  allocations.local_owners.clear();
  for (auto& [id, surface] : allocations.directional) {
    Retire(surface);
  }
}

auto ConventionalShadowTargetAllocator::OnFrameStart(
  frame::SequenceNumber sequence, frame::Slot slot) -> void
{
  if (sequence == current_sequence_) {
    return;
  }
  (void)slot;
  requested_content_.clear();
  for (auto it = views_.begin(); it != views_.end();) {
    if (it->second.last_used != current_sequence_) {
      Retire(it->second);
      it = views_.erase(it);
    } else {
      ++it;
    }
  }
  std::erase_if(
    local_content_, [](const auto& entry) { return entry.second.expired(); });
  PruneLocalChunks();
  std::erase_if(observed_backings_,
    [](const auto& item) { return item.texture.expired(); });
  std::erase_if(
    observed_versions_, [](const auto& item) { return item.expired(); });
  current_sequence_ = sequence;
}

auto ConventionalShadowTargetAllocator::RetainLocalSources(ViewId view_id,
  std::uint64_t scene_generation,
  std::span<const FrameLocalLightSelection> lights) -> void
{
  auto& view = Touch(view_id);
  auto active = std::unordered_set<scene::NodeHandle> {};
  for (const auto& light : lights) {
    // Camera rejection never ends a light's ownership.
    if (HasLocalShadowInfluence(light)) {
      active.insert(light.source_node);
    }
  }
  std::erase_if(view.local_owners, [&](const auto& owner) {
    if (view.scene_generation != scene_generation
      || !active.contains(owner.first)) {
      return true;
    }
    return false;
  });
  view.scene_generation = scene_generation;
}

auto ConventionalShadowTargetAllocator::Touch(ViewId view_id)
  -> ViewAllocations&
{
  auto& view = views_[view_id];
  view.last_used = current_sequence_;
  return view;
}

auto ConventionalShadowTargetAllocator::RetainDirectionalSurfaces(
  const std::span<const LightSelectionIndex> selections) -> void
{
  const auto active = std::unordered_set<LightSelectionIndex>(
    selections.begin(), selections.end());
  for (auto& [view_id, view] : views_) {
    for (auto it = view.directional.begin(); it != view.directional.end();) {
      if (!active.contains(it->first)) {
        Retire(it->second);
        it = view.directional.erase(it);
      } else {
        ++it;
      }
    }
  }
}

auto ConventionalShadowTargetAllocator::AcquireSurface(
  SurfaceAllocation& current, std::uint32_t layers, std::uint32_t resolution,
  bool cube, const char* name, std::uint32_t capacity_layers) -> bool
{
  if (layers == 0U || resolution == 0U) {
    return false;
  }
  if (current.surface && current.srv.IsValid() && current.layers >= layers
    && current.resolution == glm::uvec2 { resolution, resolution }) {
    current.last_used = current_sequence_;
    return true;
  }
  auto gfx = renderer_.GetGraphics();
  if (!gfx) {
    return false;
  }
  graphics::TextureDesc desc {};
  desc.width = desc.height = resolution;
  desc.array_size = (std::max)(layers, capacity_layers);
  desc.format = Format::kDepth32;
  desc.texture_type
    = cube ? TextureType::kTextureCubeArray : TextureType::kTexture2DArray;
  desc.debug_name = name;
  desc.is_shader_resource = desc.is_render_target = desc.is_typeless = true;
  desc.use_clear_value = true;
  desc.clear_value = graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F };
  desc.initial_state = graphics::ResourceStates::kDepthWrite;
  desc.allocation_budget = { .owner = renderer_.GetLightingAllocationBudget() };
  auto surface = std::shared_ptr<graphics::Texture> {};
  try {
    surface = gfx->CreateTexture(desc);
  } catch (const graphics::AllocationBudgetExceeded&) {
    if (desc.array_size == layers) {
      throw;
    }
    // Spare chunk capacity must not reject otherwise admissible shadows.
    desc.array_size = layers;
    surface = gfx->CreateTexture(desc);
  }
  if (!surface) {
    return false;
  }
  auto& registry = gfx->GetResourceRegistry();
  registry.Register(surface);
  const auto view_desc = graphics::TextureViewDescription {
    .view_type = graphics::ResourceViewType::kTexture_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = ResolveDepthSrvFormat(desc.format),
    .dimension = desc.texture_type,
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
  };
  auto& descriptors = gfx->GetDescriptorAllocator();
  auto handle
    = descriptors.AllocateRaw(view_desc.view_type, view_desc.visibility);
  if (!handle.IsValid()) {
    registry.UnRegisterResource(*surface);
    return false;
  }
  const auto srv = descriptors.GetShaderVisibleIndex(handle);
  try {
    const auto view
      = registry.RegisterView(*surface, std::move(handle), view_desc);
    if (!view->IsValid()) {
      registry.UnRegisterResource(*surface);
      return false;
    }
  } catch (...) {
    registry.UnRegisterResource(*surface);
    throw;
  }
  // Commit only after the replacement and descriptor are complete. The old
  // backing remains charged until deferred retirement and all leases release.
  Retire(current);
  current = {
    .surface = std::move(surface),
    .srv = srv,
    .resolution = { resolution, resolution },
    .layers = desc.array_size,
    .last_used = current_sequence_,
  };
  return true;
}

auto ConventionalShadowTargetAllocator::AcquireDirectionalSurface(
  ViewId view_id, LightSelectionIndex selection_index,
  std::uint32_t cascade_count, scene::ShadowResolutionHint hint)
  -> DirectionalAllocation
{
  if (cascade_count == 0U
    || cascade_count > kFrameDirectionalLightMaxCascades) {
    return {};
  }
  auto& surface = Touch(view_id).directional[selection_index];
  if (!AcquireSurface(surface, cascade_count,
        ResolveDirectionalResolution(hint, renderer_.GetShadowQualityTier()),
        false, "Vortex.DirectionalShadowSurface")) {
    return {};
  }
  return {
    .surface = surface.surface,
    .surface_srv = surface.srv,
    .resolution = surface.resolution,
    .cascade_count = cascade_count,
  };
}

ConventionalShadowTargetAllocator::LocalAcquisition::~LocalAcquisition()
{
  if (committed || !in_place || !previous || !owner) {
    return;
  }
  auto& backing = *owner->version->slot->backing;
  std::lock_guard lock(backing.mutex);
  if (!backing.quarantined
    && owner->version->state != ShadowMapVersion::State::kSubmitted) {
    previous->version->accepting = true;
  }
}
auto ConventionalShadowTargetAllocator::LocalAcquisition::Commit() noexcept
  -> void
{
  assert(owner && alias);
  *alias = owner;
  committed = true;
}
auto ConventionalShadowTargetAllocator::PrepareLocalFamily(
  std::span<const LocalShadowRequest* const> requests) -> void
{
  requested_content_.clear();
  requested_content_.reserve(requests.size());
  for (const auto* request : requests) {
    if (request->content.resolution != 0 && request->content.reusable) {
      requested_content_.push_back(&request->content);
    }
  }
  std::ranges::sort(
    requested_content_, {}, [](const auto* key) { return key->hash; });
}
auto ConventionalShadowTargetAllocator::IsRequested(
  const LocalShadowContentKey& key) const -> bool
{
  if (!key.reusable) {
    return false;
  }
  auto first = std::ranges::lower_bound(requested_content_, key.hash, {},
    [](const auto* item) { return item->hash; });
  for (; first != requested_content_.end() && (*first)->hash == key.hash;
    ++first) {
    if (**first == key) {
      return true;
    }
  }
  return false;
}
auto ConventionalShadowTargetAllocator::CreateLocalBacking(
  uint32_t resolution, bool cube) -> std::shared_ptr<SharedShadowBacking>
{
  auto gfx = renderer_.GetGraphics();
  if (!gfx) {
    throw std::logic_error("Graphics backend is unavailable");
  }
  auto backing = std::make_shared<SharedShadowBacking>();
  const auto faces = cube ? 6U : 1U;
  graphics::TextureDesc desc;
  desc.width = desc.height = resolution;
  desc.array_size = LocalChunkCapacity(resolution, cube) * faces;
  desc.format = Format::kDepth32;
  desc.texture_type
    = cube ? TextureType::kTextureCubeArray : TextureType::kTexture2DArray;
  desc.is_shader_resource = desc.is_render_target = desc.is_typeless = true;
  desc.use_clear_value = true;
  desc.clear_value = graphics::Color { 0, 0, 0, 0 };
  desc.initial_state = graphics::ResourceStates::kDepthWrite;
  desc.debug_name
    = cube ? "Vortex.SharedPointShadows" : "Vortex.SharedSpotShadows";
  desc.allocation_budget = { .owner = renderer_.GetLightingAllocationBudget() };
  try {
    backing->texture = gfx->CreateTexture(desc);
  } catch (const graphics::AllocationBudgetExceeded&) {
    if (desc.array_size == faces) {
      throw;
    }
    desc.array_size = faces;
    backing->texture = gfx->CreateTexture(desc);
  }
  if (!backing->texture) {
    throw std::runtime_error("Local shadow backing allocation failed");
  }
  auto& registry = gfx->GetResourceRegistry();
  auto lease = registry.RegisterManaged(backing->texture);
  if (!lease) {
    throw std::runtime_error("Local shadow registration failed");
  }
  const auto srv = registry.AcquireManagedView<graphics::Texture>(*lease,
    graphics::TextureViewDescription {
      .view_type = graphics::ResourceViewType::kTexture_SRV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = Format::kR32Float,
      .dimension = desc.texture_type });
  if (!srv) {
    throw std::runtime_error("Local shadow SRV creation failed");
  }
  backing->srv = srv->shader_visible_index;
  backing->registration = lease->AllocationOwner();
  backing->resolution = resolution;
  backing->cube = cube;
  backing->map_capacity = desc.array_size / faces;
  backing->dsvs.resize(desc.array_size);
  backing->queues.reserve(4);
  observed_backings_.push_back({ backing, backing->texture });
  return backing;
}
auto ConventionalShadowTargetAllocator::EnsureSlotViews(
  const ShadowSlotCore& slot) -> void
{
  auto gfx = renderer_.GetGraphics();
  auto& registry = gfx->GetResourceRegistry();
  auto lease = registry.AcquireManaged(slot.backing->registration.Identity());
  if (!lease) {
    throw std::logic_error("Local shadow backing is retired");
  }
  const auto faces = slot.backing->cube ? 6U : 1U;
  for (uint32_t face = 0; face < faces; ++face) {
    const auto layer = slot.offset * faces + face;
    if (slot.backing->dsvs[layer]->IsValid()) {
      continue;
    }
    const auto view = registry.AcquireManagedView<graphics::Texture>(*lease,
      graphics::TextureViewDescription {
        .view_type = graphics::ResourceViewType::kTexture_DSV,
        .visibility = graphics::DescriptorVisibility::kCpuOnly,
        .format = Format::kDepth32,
        .dimension = TextureType::kTexture2DArray,
        .sub_resources = { .base_mip_level = 0,
          .num_mip_levels = 1,
          .base_array_slice = layer,
          .num_array_slices = 1 } });
    if (!view) {
      throw std::runtime_error("Local shadow DSV creation failed");
    }
    slot.backing->dsvs[layer] = view->view;
  }
}
auto ConventionalShadowTargetAllocator::AcquirePhysicalSlot(
  uint32_t resolution, bool cube) -> std::shared_ptr<ShadowSlotCore>
{
  std::shared_ptr<SharedShadowBacking> backing;
  uint32_t offset = 0;
  for (const auto& candidate : local_chunks_) {
    if (candidate->resolution != resolution || candidate->cube != cube
      || !CanWriteBacking(*candidate)) {
      continue;
    }
    std::array<bool, 64> occupied {};
    for (const auto& weak : local_pool_->slots) {
      if (const auto slot = weak.lock(); slot && slot->backing == candidate) {
        std::lock_guard lock(slot->mutex);
        if (!slot->finalized) {
          occupied[slot->offset] = true;
        }
      }
    }
    for (offset = 0; offset < candidate->map_capacity && occupied[offset];
      ++offset) { }
    if (offset < candidate->map_capacity) {
      backing = candidate;
      break;
    }
  }
  const bool new_chunk = !backing;
  if (new_chunk) {
    if (local_chunks_.size() == local_chunks_.capacity()) {
      local_chunks_.reserve(
        (std::max)(size_t { 4 }, local_chunks_.capacity() * 2));
    }
    backing = CreateLocalBacking(resolution, cube);
    offset = 0;
  }
  auto slot = std::make_shared<ShadowSlotCore>();
  slot->backing = std::move(backing);
  slot->offset = offset;
  ShadowSlotIndex index;
  {
    std::lock_guard lock(local_pool_->mutex);
    if (local_pool_->closed) {
      throw std::logic_error("Shadow slot pool is closed");
    }
    if (!local_pool_->free.empty()) {
      index = local_pool_->free.back();
      local_pool_->free.pop_back();
    } else {
      index
        = ShadowSlotIndex { static_cast<uint32_t>(local_pool_->slots.size()) };
      if (local_pool_->free.capacity() <= local_pool_->slots.size()) {
        local_pool_->free.reserve((std::max)(local_pool_->slots.size() + 1,
          local_pool_->free.capacity() * 2));
      }
      local_pool_->slots.emplace_back();
    }
  }
  try {
    slot->handle = local_pool_->reuse.ActivateSlot(index);
  } catch (...) {
    std::lock_guard lock(local_pool_->mutex);
    local_pool_->free.push_back(index);
    throw;
  }
  slot->pool = local_pool_;
  local_pool_->slots[index.get()] = slot;
  EnsureSlotViews(*slot);
  if (new_chunk) {
    local_chunks_.push_back(slot->backing);
  }
  return slot;
}
auto ConventionalShadowTargetAllocator::PruneLocalChunks() -> void
{
  std::erase_if(local_chunks_, [&](const auto& backing) {
    for (const auto& weak : local_pool_->slots) {
      if (const auto slot = weak.lock(); slot && slot->backing == backing) {
        std::lock_guard lock(slot->mutex);
        if (!slot->finalized) {
          return false;
        }
      }
    }
    return true;
  });
}
auto ConventionalShadowTargetAllocator::AcquireLocalMap(
  ViewId view_id, const LocalShadowRequest& request) -> LocalAcquisition
{
  if (!request.content.light.IsValid() || request.content.resolution == 0) {
    throw std::invalid_argument(
      "Local shadows require a light identity and resolution");
  }
  auto& aliases = Touch(view_id).local_owners;
  auto& alias = aliases.try_emplace(request.content.light).first->second;
  LocalAcquisition acquired;
  acquired.alias = &alias;
  acquired.previous = alias;
  if (request.content.reusable) {
    const auto [first, last] = local_content_.equal_range(request.content.hash);
    for (auto it = first; it != last; ++it) {
      auto owner = it->second.lock();
      if (!owner) {
        continue;
      }
      auto& version = *owner->version;
      std::lock_guard lock(version.slot->backing->mutex);
      if (version.accepting
        && version.state == ShadowMapVersion::State::kSubmitted
        && !version.slot->backing->quarantined
        && version.content == request.content) {
        acquired.owner = std::move(owner);
        acquired.reused = true;
        ++decisions_.cache_hits;
        return acquired;
      }
    }
  }
  try {
    ++decisions_.cache_misses;
    std::shared_ptr<ShadowSlotCore> slot;
    const bool same_shape = alias
      && alias->version->slot->backing->resolution == request.content.resolution
      && alias->version->slot->backing->cube == request.IsCube();
    const bool family_needs_previous
      = same_shape && IsRequested(alias->version->content);
    if (same_shape && !family_needs_previous
      && CanReplaceVersion(*alias->version)) {
      slot = alias->version->slot;
      acquired.in_place = true;
      ++decisions_.in_place_updates;
    } else {
      slot = AcquirePhysicalSlot(request.content.resolution, request.IsCube());
      if (!alias) {
        ++decisions_.first_allocations;
      } else if (!same_shape) {
        ++decisions_.copy_on_write_shape;
      } else if (family_needs_previous) {
        ++decisions_.copy_on_write_family;
      } else {
        ++decisions_.copy_on_write_reader;
      }
    }
    acquired.owner = std::make_shared<ShadowMapOwner>(
      std::make_shared<ShadowMapVersion>(std::move(slot), request.content));
    observed_versions_.push_back(acquired.owner->version);
    if (request.content.reusable) {
      local_content_.emplace(request.content.hash, acquired.owner);
    }
    if (acquired.in_place) {
      std::lock_guard lock(alias->version->slot->backing->mutex);
      alias->version->accepting = false;
    }
    return acquired;
  } catch (...) {
    acquired.owner.reset();
    PruneLocalChunks();
    if (auto gfx = renderer_.GetGraphics()) {
      gfx->PollCompletedUses();
    }
    throw;
  }
}

auto ConventionalShadowTargetAllocator::InspectLocalSharing() const
  -> ShadowSharingDiagnostics
{
  auto result = decisions_;
  for (const auto& [id, view] : views_) {
    for (const auto& [light, owner] : view.local_owners) {
      result.aliases += owner ? 1U : 0U;
    }
  }
  for (const auto& weak : observed_versions_) {
    if (const auto version = weak.lock()) {
      ++result.live_versions;
      result.version_payload_bytes += sizeof(ShadowMapVersion)
        + version->content.casters.capacity()
          * sizeof(std::shared_ptr<const ShadowCasterRecord>);
    }
  }
  for (const auto& item : observed_backings_) {
    auto texture = item.texture.lock();
    if (!texture) {
      continue;
    }
    auto backing = item.backing.lock();
    std::uint64_t spare = 0;
    bool closing = true;
    if (backing) {
      std::uint64_t occupied = 0;
      for (const auto& weak : local_pool_->slots) {
        if (const auto slot = weak.lock(); slot && slot->backing == backing) {
          std::lock_guard lock(slot->mutex);
          occupied += slot->finalized ? 0U : 1U;
          closing = closing && slot->owners == 0;
        }
      }
      spare = (backing->map_capacity - occupied) * backing->resolution
        * backing->resolution * sizeof(float) * (backing->cube ? 6U : 1U);
    }
    result.backings.push_back({ std::move(texture), spare, closing });
  }
  return result;
}

auto ConventionalShadowTargetAllocator::ResolveLocalResolution(
  const scene::ShadowResolutionHint hint) const -> std::uint32_t
{
  return (std::min)(ResolveLocalResolutionRequest(hint),
    ResolveDirectionalResolutionBudget(renderer_.GetShadowQualityTier()));
}

auto ConventionalShadowTargetAllocator::LocalChunkCapacity(
  const std::uint32_t resolution, const bool cube) -> std::uint32_t
{
  // Append bounded chunks instead of replacing a multi-gigabyte array when
  // one more light becomes relevant. Existing chunks keep their allocations.
  constexpr std::uint64_t kTargetChunkBytes = 64ULL * 1024ULL * 1024ULL;
  const auto bytes_per_light = static_cast<std::uint64_t>(resolution)
    * resolution * sizeof(float)
    * (cube ? CubeLocalShadowRecord::kFaceCount : 1U);
  if (bytes_per_light == 0U) {
    return 0U;
  }
  return static_cast<std::uint32_t>(
    std::clamp<std::uint64_t>(kTargetChunkBytes / bytes_per_light, 1U, 64U));
}

} // namespace oxygen::vortex::shadows::internal
