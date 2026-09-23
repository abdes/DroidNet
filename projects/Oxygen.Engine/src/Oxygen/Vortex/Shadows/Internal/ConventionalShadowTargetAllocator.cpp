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
#include <unordered_set>
#include <utility>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
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

  auto ResolveSpotResolution(const scene::ShadowResolutionHint resolution_hint,
    const ShadowQualityTier quality_tier) -> std::uint32_t
  {
    return (std::min)(ResolveLocalResolutionRequest(resolution_hint),
      ResolveDirectionalResolutionBudget(quality_tier));
  }

  auto ResolvePointResolution(const scene::ShadowResolutionHint resolution_hint,
    const ShadowQualityTier quality_tier) -> std::uint32_t
  {
    return (std::min)(ResolveLocalResolutionRequest(resolution_hint),
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
  for (auto& [id, surface] : allocations.directional) {
    Retire(surface);
  }
  Retire(allocations.spot);
  Retire(allocations.point);
}

auto ConventionalShadowTargetAllocator::OnFrameStart(
  frame::SequenceNumber sequence) -> void
{
  if (sequence == current_sequence_) {
    return;
  }
  for (auto it = views_.begin(); it != views_.end();) {
    if (it->second.last_used != current_sequence_) {
      Retire(it->second);
      it = views_.erase(it);
    } else {
      ++it;
    }
  }
  current_sequence_ = sequence;
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
  bool cube, const char* name) -> bool
{
  if (layers == 0U || resolution == 0U) {
    return false;
  }
  if (current.surface && current.srv.IsValid() && current.layers == layers
    && current.resolution == glm::uvec2 { resolution, resolution }) {
    return true;
  }
  auto gfx = renderer_.GetGraphics();
  if (!gfx) {
    return false;
  }
  graphics::TextureDesc desc {};
  desc.width = desc.height = resolution;
  desc.array_size = layers;
  desc.format = Format::kDepth32Stencil8;
  desc.texture_type
    = cube ? TextureType::kTextureCubeArray : TextureType::kTexture2DArray;
  desc.debug_name = name;
  desc.is_shader_resource = desc.is_render_target = desc.is_typeless = true;
  desc.use_clear_value = true;
  desc.clear_value = graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F };
  desc.initial_state = graphics::ResourceStates::kDepthWrite;
  desc.allocation_budget = { .owner = renderer_.GetLightingAllocationBudget() };
  auto surface = gfx->CreateTexture(desc);
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
    .layers = layers,
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
    .cascade_count = surface.layers,
  };
}

auto ConventionalShadowTargetAllocator::AcquireSpotSurface(
  ViewId view_id, std::uint32_t shadow_count, scene::ShadowResolutionHint hint)
  -> SpotAllocation
{
  auto& surface = Touch(view_id).spot;
  if (!AcquireSurface(surface, shadow_count,
        ResolveSpotResolution(hint, renderer_.GetShadowQualityTier()), false,
        "Vortex.SpotShadowSurface")) {
    return {};
  }
  return {
    .surface = surface.surface,
    .surface_srv = surface.srv,
    .resolution = surface.resolution,
    .shadow_count = surface.layers,
  };
}

auto ConventionalShadowTargetAllocator::AcquirePointSurface(
  ViewId view_id, std::uint32_t shadow_count, scene::ShadowResolutionHint hint)
  -> PointAllocation
{
  constexpr auto faces = CubeLocalShadowRecord::kFaceCount;
  if (shadow_count > std::numeric_limits<std::uint32_t>::max() / faces) {
    return {};
  }
  auto& surface = Touch(view_id).point;
  if (!AcquireSurface(surface, shadow_count * faces,
        ResolvePointResolution(hint, renderer_.GetShadowQualityTier()), true,
        "Vortex.PointShadowCubeSurface")) {
    return {};
  }
  return {
    .surface = surface.surface,
    .surface_srv = surface.srv,
    .resolution = surface.resolution,
    .shadow_count = surface.layers / faces,
  };
}

} // namespace oxygen::vortex::shadows::internal
