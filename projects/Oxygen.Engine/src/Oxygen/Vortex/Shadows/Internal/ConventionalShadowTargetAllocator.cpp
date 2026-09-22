//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_set>
#include <utility>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
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
  = default;

auto ConventionalShadowTargetAllocator::OnFrameStart() -> void { }

auto ConventionalShadowTargetAllocator::RetainDirectionalSurfaces(
  const std::span<const LightSelectionIndex> selections) -> void
{
  const auto active = std::unordered_set<LightSelectionIndex>(
    selections.begin(), selections.end());
  std::erase_if(directional_allocations_,
    [&](const auto& entry) -> auto { return !active.contains(entry.first); });
}

auto ConventionalShadowTargetAllocator::AcquireDirectionalSurface(
  const LightSelectionIndex selection_index, const std::uint32_t cascade_count,
  const scene::ShadowResolutionHint resolution_hint) -> DirectionalAllocation
{
  EnsureDirectionalSurface(selection_index, cascade_count, resolution_hint);
  return directional_allocations_.at(selection_index);
}

auto ConventionalShadowTargetAllocator::AcquireSpotSurface(
  const std::uint32_t shadow_count,
  const scene::ShadowResolutionHint resolution_hint) -> SpotAllocation
{
  EnsureSpotSurface(shadow_count, resolution_hint);
  return {
    .surface = spot_surface_,
    .surface_srv = spot_surface_srv_,
    .resolution = spot_resolution_,
    .shadow_count = spot_array_size_,
  };
}

auto ConventionalShadowTargetAllocator::AcquirePointSurface(
  const std::uint32_t shadow_count,
  const scene::ShadowResolutionHint resolution_hint) -> PointAllocation
{
  EnsurePointSurface(shadow_count, resolution_hint);
  return {
    .surface = point_surface_,
    .surface_srv = point_surface_srv_,
    .resolution = point_resolution_,
    .shadow_count = point_shadow_count_,
  };
}

auto ConventionalShadowTargetAllocator::EnsureDirectionalSurface(
  const LightSelectionIndex selection_index, const std::uint32_t cascade_count,
  const scene::ShadowResolutionHint resolution_hint) -> void
{
  auto& allocation = directional_allocations_[selection_index];
  const auto array_size = (std::max)(1U, (std::min)(cascade_count, 4U));
  const auto resolved_resolution = ResolveDirectionalResolution(
    resolution_hint, renderer_.GetShadowQualityTier());
  const auto resolution
    = glm::uvec2 { resolved_resolution, resolved_resolution };
  const auto needs_reallocation = !allocation.surface
    || allocation.resolution != resolution
    || allocation.cascade_count != array_size;
  if (!needs_reallocation) {
    if (!allocation.surface_srv.IsValid()) {
      allocation.surface_srv
        = RegisterDirectionalSurfaceSrv(allocation.surface);
    }
    return;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    allocation.surface.reset();
    allocation.surface_srv = kInvalidShaderVisibleIndex;
    allocation.resolution = {};
    allocation.cascade_count = 0U;
    return;
  }

  graphics::TextureDesc desc {};
  desc.width = resolution.x;
  desc.height = resolution.y;
  desc.depth = 1U;
  desc.array_size = array_size;
  desc.mip_levels = 1U;
  desc.sample_count = 1U;
  desc.sample_quality = 0U;
  desc.format = Format::kDepth32Stencil8;
  desc.texture_type = TextureType::kTexture2DArray;
  desc.debug_name = "Vortex.DirectionalShadowSurface";
  desc.is_shader_resource = true;
  desc.is_render_target = true;
  desc.is_typeless = true;
  desc.use_clear_value = true;
  desc.clear_value = graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F };
  desc.initial_state = graphics::ResourceStates::kDepthWrite;

  allocation.surface = gfx->CreateTexture(desc);
  allocation.resolution = resolution;
  allocation.cascade_count = array_size;
  allocation.surface_srv = RegisterDirectionalSurfaceSrv(allocation.surface);
}

auto ConventionalShadowTargetAllocator::RegisterDirectionalSurfaceSrv(
  const std::shared_ptr<graphics::Texture>& surface) -> ShaderVisibleIndex
{
  if (!surface) {
    return kInvalidShaderVisibleIndex;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return kInvalidShaderVisibleIndex;
  }

  auto& registry = gfx->GetResourceRegistry();
  if (!registry.Contains(*surface)) {
    registry.Register(surface);
  }

  const auto view_desc = graphics::TextureViewDescription {
    .view_type = graphics::ResourceViewType::kTexture_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = ResolveDepthSrvFormat(surface->GetDescriptor().format),
    .dimension = surface->GetDescriptor().texture_type,
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
  };

  if (const auto existing
    = registry.FindShaderVisibleIndex(*surface, view_desc);
    existing.has_value()) {
    return *existing;
  }

  auto& allocator = gfx->GetDescriptorAllocator();
  auto handle
    = allocator.AllocateRaw(view_desc.view_type, view_desc.visibility);
  if (!handle.IsValid()) {
    return kInvalidShaderVisibleIndex;
  }

  const auto shader_visible_index = allocator.GetShaderVisibleIndex(handle);
  const auto view
    = registry.RegisterView(*surface, std::move(handle), view_desc);
  if (!view->IsValid()) {
    return kInvalidShaderVisibleIndex;
  }

  return shader_visible_index;
}

auto ConventionalShadowTargetAllocator::EnsureSpotSurface(
  const std::uint32_t shadow_count,
  const scene::ShadowResolutionHint resolution_hint) -> void
{
  const auto array_size = (std::max)(1U, (std::min)(shadow_count, 8U));
  const auto resolved_resolution
    = ResolveSpotResolution(resolution_hint, renderer_.GetShadowQualityTier());
  const auto resolution
    = glm::uvec2 { resolved_resolution, resolved_resolution };
  const auto needs_reallocation = !spot_surface_
    || spot_resolution_ != resolution || spot_array_size_ != array_size;
  if (!needs_reallocation) {
    if (!spot_surface_srv_.IsValid()) {
      spot_surface_srv_ = RegisterSpotSurfaceSrv();
    }
    return;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    spot_surface_.reset();
    spot_surface_srv_ = kInvalidShaderVisibleIndex;
    spot_resolution_ = {};
    spot_array_size_ = 0U;
    return;
  }

  graphics::TextureDesc desc {};
  desc.width = resolution.x;
  desc.height = resolution.y;
  desc.depth = 1U;
  desc.array_size = array_size;
  desc.mip_levels = 1U;
  desc.sample_count = 1U;
  desc.sample_quality = 0U;
  desc.format = Format::kDepth32Stencil8;
  desc.texture_type = TextureType::kTexture2DArray;
  desc.debug_name = "Vortex.SpotShadowSurface";
  desc.is_shader_resource = true;
  desc.is_render_target = true;
  desc.is_typeless = true;
  desc.use_clear_value = true;
  desc.clear_value = graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F };
  desc.initial_state = graphics::ResourceStates::kDepthWrite;

  spot_surface_ = gfx->CreateTexture(desc);
  spot_resolution_ = resolution;
  spot_array_size_ = array_size;
  spot_surface_srv_ = RegisterSpotSurfaceSrv();
}

auto ConventionalShadowTargetAllocator::RegisterSpotSurfaceSrv()
  -> ShaderVisibleIndex
{
  if (!spot_surface_) {
    return kInvalidShaderVisibleIndex;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return kInvalidShaderVisibleIndex;
  }

  auto& registry = gfx->GetResourceRegistry();
  if (!registry.Contains(*spot_surface_)) {
    registry.Register(spot_surface_);
  }

  const auto view_desc = graphics::TextureViewDescription {
    .view_type = graphics::ResourceViewType::kTexture_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = ResolveDepthSrvFormat(spot_surface_->GetDescriptor().format),
    .dimension = spot_surface_->GetDescriptor().texture_type,
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
  };

  if (const auto existing
    = registry.FindShaderVisibleIndex(*spot_surface_, view_desc);
    existing.has_value()) {
    return *existing;
  }

  auto& allocator = gfx->GetDescriptorAllocator();
  auto handle
    = allocator.AllocateRaw(view_desc.view_type, view_desc.visibility);
  if (!handle.IsValid()) {
    return kInvalidShaderVisibleIndex;
  }

  const auto shader_visible_index = allocator.GetShaderVisibleIndex(handle);
  const auto view
    = registry.RegisterView(*spot_surface_, std::move(handle), view_desc);
  if (!view->IsValid()) {
    return kInvalidShaderVisibleIndex;
  }

  return shader_visible_index;
}

auto ConventionalShadowTargetAllocator::EnsurePointSurface(
  const std::uint32_t shadow_count,
  const scene::ShadowResolutionHint resolution_hint) -> void
{
  const auto resolved_shadow_count
    = (std::max)(1U, (std::min)(shadow_count, 4U));
  const auto resolved_resolution
    = ResolvePointResolution(resolution_hint, renderer_.GetShadowQualityTier());
  const auto resolution
    = glm::uvec2 { resolved_resolution, resolved_resolution };
  const auto needs_reallocation = !point_surface_
    || point_resolution_ != resolution
    || point_shadow_count_ != resolved_shadow_count;
  if (!needs_reallocation) {
    if (!point_surface_srv_.IsValid()) {
      point_surface_srv_ = RegisterPointSurfaceSrv();
    }
    return;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    point_surface_.reset();
    point_surface_srv_ = kInvalidShaderVisibleIndex;
    point_resolution_ = {};
    point_shadow_count_ = 0U;
    return;
  }

  graphics::TextureDesc desc {};
  desc.width = resolution.x;
  desc.height = resolution.y;
  desc.depth = 1U;
  desc.array_size = resolved_shadow_count * 6U;
  desc.mip_levels = 1U;
  desc.sample_count = 1U;
  desc.sample_quality = 0U;
  desc.format = Format::kDepth32Stencil8;
  desc.texture_type = TextureType::kTextureCubeArray;
  desc.debug_name = "Vortex.PointShadowCubeSurface";
  desc.is_shader_resource = true;
  desc.is_render_target = true;
  desc.is_typeless = true;
  desc.use_clear_value = true;
  desc.clear_value = graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F };
  desc.initial_state = graphics::ResourceStates::kDepthWrite;

  point_surface_ = gfx->CreateTexture(desc);
  point_resolution_ = resolution;
  point_shadow_count_ = resolved_shadow_count;
  point_surface_srv_ = RegisterPointSurfaceSrv();
}

auto ConventionalShadowTargetAllocator::RegisterPointSurfaceSrv()
  -> ShaderVisibleIndex
{
  if (!point_surface_) {
    return kInvalidShaderVisibleIndex;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return kInvalidShaderVisibleIndex;
  }

  auto& registry = gfx->GetResourceRegistry();
  if (!registry.Contains(*point_surface_)) {
    registry.Register(point_surface_);
  }

  const auto view_desc = graphics::TextureViewDescription {
    .view_type = graphics::ResourceViewType::kTexture_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = ResolveDepthSrvFormat(point_surface_->GetDescriptor().format),
    .dimension = TextureType::kTexture2DArray,
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
  };

  if (const auto existing
    = registry.FindShaderVisibleIndex(*point_surface_, view_desc);
    existing.has_value()) {
    return *existing;
  }

  auto& allocator = gfx->GetDescriptorAllocator();
  auto handle
    = allocator.AllocateRaw(view_desc.view_type, view_desc.visibility);
  if (!handle.IsValid()) {
    return kInvalidShaderVisibleIndex;
  }

  const auto shader_visible_index = allocator.GetShaderVisibleIndex(handle);
  const auto view
    = registry.RegisterView(*point_surface_, std::move(handle), view_desc);
  if (!view->IsValid()) {
    return kInvalidShaderVisibleIndex;
  }

  return shader_visible_index;
}

} // namespace oxygen::vortex::shadows::internal
