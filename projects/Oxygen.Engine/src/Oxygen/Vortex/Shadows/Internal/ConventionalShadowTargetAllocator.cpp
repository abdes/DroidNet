//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
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
  , slot_reuse_(slot_reclaimer_, [this](ShadowSlotIndex index, std::monostate) {
    slots_.at(index.get()).occupied = false;
    free_slots_.push_back(index);
  })
{
}

ConventionalShadowTargetAllocator::~ConventionalShadowTargetAllocator()
{
  for (auto& [id, allocations] : views_) {
    Retire(allocations);
  }
  // Nexus callbacks reference this owner. Drain before its storage is
  // destroyed.
  slot_reclaimer_.ProcessAllDeferredReleases();
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
  for (const auto& [source, slot] : allocations.local_owners) {
    slot_reuse_.Release(slot.handle);
  }
  allocations.local_owners.clear();
  for (auto& [id, surface] : allocations.directional) {
    Retire(surface);
  }
  for (auto& [resolution, surface] : allocations.spot) {
    Retire(surface);
  }
  for (auto& [resolution, surface] : allocations.point) {
    Retire(surface);
  }
}

auto ConventionalShadowTargetAllocator::OnFrameStart(
  frame::SequenceNumber sequence, frame::Slot slot) -> void
{
  if (sequence == current_sequence_) {
    return;
  }
  slot_reuse_.OnBeginFrame(slot);
  for (auto it = views_.begin(); it != views_.end();) {
    if (it->second.last_used != current_sequence_) {
      Retire(it->second);
      it = views_.erase(it);
    } else {
      const auto retire_unused = [this, view_id = it->first](
                                   auto& buckets, bool cube) {
        for (auto bucket = buckets.begin(); bucket != buckets.end();) {
          const auto [resolution, chunk] = bucket->first;
          const auto capacity = LocalChunkCapacity(resolution, cube);
          const auto occupied
            = std::ranges::any_of(slots_, [&](const auto& location) {
                return location.occupied && location.view_id == view_id
                  && location.resolution == resolution && location.cube == cube
                  && location.ordinal / capacity == chunk;
              });
          if (!occupied && bucket->second.last_used != current_sequence_) {
            Retire(bucket->second);
            bucket = buckets.erase(bucket);
          } else {
            ++bucket;
          }
        }
      };
      retire_unused(it->second.spot, false);
      retire_unused(it->second.point, true);
      ++it;
    }
  }
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
      slot_reuse_.Release(owner.second.handle);
      return true;
    }
    return false;
  });
  view.scene_generation = scene_generation;
}

auto ConventionalShadowTargetAllocator::AcquireLocalSlot(ViewId view_id,
  scene::NodeHandle source, std::uint32_t resolution, bool cube) -> LocalSlot
{
  if (!source.IsValid() || resolution == 0U) {
    throw std::invalid_argument(
      "Local shadow ownership requires a light identity and resolution");
  }
  auto& view = Touch(view_id);
  auto& owners = view.local_owners;
  if (auto found = owners.find(source); found != owners.end()) {
    if (found->second.resolution == resolution && found->second.cube == cube) {
      return found->second;
    }
    slot_reuse_.Release(found->second.handle);
    owners.erase(found);
  }
  auto occupied = std::unordered_set<std::uint32_t> {};
  for (const auto& location : slots_) {
    if (location.occupied && location.view_id == view_id
      && location.resolution == resolution && location.cube == cube) {
      occupied.insert(location.ordinal);
    }
  }
  const auto capacity = LocalChunkCapacity(resolution, cube);
  const auto& surfaces = cube ? view.point : view.spot;
  auto ordinal = 0U;
  for (;;) {
    if (occupied.contains(ordinal)) {
      ++ordinal;
      continue;
    }
    const auto surface = surfaces.find({ resolution, ordinal / capacity });
    if (surface != surfaces.end() && surface->second.surface
      && ordinal % capacity >= surface->second.layers / (cube ? 6U : 1U)) {
      // A budget-tight chunk may have been created without spare layers.
      // Append a new chunk instead of replacing its existing owners' surface.
      ordinal = (ordinal / capacity + 1U) * capacity;
      continue;
    }
    break;
  }
  auto index = ShadowSlotIndex { static_cast<std::uint32_t>(slots_.size()) };
  if (free_slots_.empty()) {
    slots_.emplace_back();
  } else {
    index = free_slots_.back();
    free_slots_.pop_back();
  }
  slots_[index.get()] = { view_id, resolution, ordinal, cube, true };
  auto slot = LocalSlot { slot_reuse_.ActivateSlot(index), resolution,
    ordinal / capacity, ordinal % capacity, cube };
  owners.emplace(source, slot);
  return slot;
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
    .dimension = TextureType::kTexture2DArray,
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

auto ConventionalShadowTargetAllocator::AcquireSpotSurface(ViewId view_id,
  std::uint32_t shadow_count, std::uint32_t resolution, std::uint32_t chunk)
  -> SpotAllocation
{
  const auto capacity = LocalChunkCapacity(resolution, false);
  if (shadow_count == 0U || shadow_count > capacity) {
    return {};
  }
  auto& surface = Touch(view_id).spot[{ resolution, chunk }];
  if (!AcquireSurface(surface, shadow_count, resolution, false,
        "Vortex.SpotShadowSurface", capacity)) {
    return {};
  }
  return {
    .surface = surface.surface,
    .surface_srv = surface.srv,
    .resolution = surface.resolution,
    .shadow_count = shadow_count,
  };
}

auto ConventionalShadowTargetAllocator::AcquirePointSurface(ViewId view_id,
  std::uint32_t shadow_count, std::uint32_t resolution, std::uint32_t chunk)
  -> PointAllocation
{
  constexpr auto faces = CubeLocalShadowRecord::kFaceCount;
  const auto capacity = LocalChunkCapacity(resolution, true);
  if (shadow_count == 0U || shadow_count > capacity) {
    return {};
  }
  auto& surface = Touch(view_id).point[{ resolution, chunk }];
  if (!AcquireSurface(surface, shadow_count * faces, resolution, true,
        "Vortex.PointShadowCubeSurface", capacity * faces)) {
    return {};
  }
  return {
    .surface = surface.surface,
    .surface_srv = surface.srv,
    .resolution = surface.resolution,
    .shadow_count = shadow_count,
  };
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
