//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Shadows/Internal/ConventionalShadowTargetAllocator.h>

#include <algorithm>

#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::vortex::shadows::internal {

namespace {

auto ResolveDirectionalResolutionRequest(const std::uint32_t resolution_hint)
  -> std::uint32_t
{
  switch (resolution_hint) {
  case 0U:
    return 1024U;
  case 1U:
    return 2048U;
  case 2U:
    return 3072U;
  case 3U:
    return 4096U;
  default:
    return 2048U;
  }
}

auto ResolveSpotResolutionRequest(const std::uint32_t resolution_hint)
  -> std::uint32_t
{
  switch (resolution_hint) {
  case 0U:
    return 512U;
  case 1U:
    return 1024U;
  case 2U:
    return 2048U;
  case 3U:
    return 2048U;
  default:
    return 1024U;
  }
}

auto ResolvePointResolutionRequest(const std::uint32_t resolution_hint)
  -> std::uint32_t
{
  switch (resolution_hint) {
  case 0U:
    return 512U;
  case 1U:
    return 1024U;
  case 2U:
    return 2048U;
  case 3U:
    return 2048U;
  default:
    return 1024U;
  }
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

auto ResolveDirectionalResolution(const std::uint32_t resolution_hint,
  const ShadowQualityTier quality_tier) -> std::uint32_t
{
  return (std::min)(ResolveDirectionalResolutionRequest(resolution_hint),
    ResolveDirectionalResolutionBudget(quality_tier));
}

auto ResolveSpotResolution(const std::uint32_t resolution_hint,
  const ShadowQualityTier quality_tier) -> std::uint32_t
{
  return (std::min)(ResolveSpotResolutionRequest(resolution_hint),
    ResolveDirectionalResolutionBudget(quality_tier));
}

auto ResolvePointResolution(const std::uint32_t resolution_hint,
  const ShadowQualityTier quality_tier) -> std::uint32_t
{
  return (std::min)(ResolvePointResolutionRequest(resolution_hint),
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

ConventionalShadowTargetAllocator::~ConventionalShadowTargetAllocator() = default;

auto ConventionalShadowTargetAllocator::OnFrameStart() -> void
{
}

auto ConventionalShadowTargetAllocator::AcquireDirectionalSurface(
  const std::uint32_t cascade_count, const std::uint32_t resolution_hint)
  -> DirectionalAllocation
{
  EnsureDirectionalSurface(cascade_count, resolution_hint);
  return {
    .surface = directional_surface_,
    .surface_srv = directional_surface_srv_,
    .resolution = directional_resolution_,
    .cascade_count = directional_array_size_,
  };
}

auto ConventionalShadowTargetAllocator::AcquireSpotSurface(
  const std::uint32_t shadow_count, const std::uint32_t resolution_hint)
  -> SpotAllocation
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
  const std::uint32_t shadow_count, const std::uint32_t resolution_hint)
  -> PointAllocation
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
  const std::uint32_t cascade_count, const std::uint32_t resolution_hint) -> void
{
  const auto array_size
    = (std::max)(1U, (std::min)(cascade_count, 4U));
  const auto resolved_resolution = ResolveDirectionalResolution(
    resolution_hint, renderer_.GetShadowQualityTier());
  const auto resolution
    = glm::uvec2 { resolved_resolution, resolved_resolution };
  const auto needs_reallocation = !directional_surface_
    || directional_resolution_ != resolution
    || directional_array_size_ != array_size;
  if (!needs_reallocation) {
    if (!directional_surface_srv_.IsValid()) {
      directional_surface_srv_ = RegisterDirectionalSurfaceSrv();
    }
    return;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    directional_surface_.reset();
    directional_surface_srv_ = kInvalidShaderVisibleIndex;
    directional_resolution_ = {};
    directional_array_size_ = 0U;
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

  directional_surface_ = gfx->CreateTexture(desc);
  directional_resolution_ = resolution;
  directional_array_size_ = array_size;
  directional_surface_srv_ = RegisterDirectionalSurfaceSrv();
}

auto ConventionalShadowTargetAllocator::RegisterDirectionalSurfaceSrv()
  -> ShaderVisibleIndex
{
  if (!directional_surface_) {
    return kInvalidShaderVisibleIndex;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return kInvalidShaderVisibleIndex;
  }

  auto& registry = gfx->GetResourceRegistry();
  if (!registry.Contains(*directional_surface_)) {
    registry.Register(directional_surface_);
  }

  const auto view_desc = graphics::TextureViewDescription {
    .view_type = graphics::ResourceViewType::kTexture_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = ResolveDepthSrvFormat(
      directional_surface_->GetDescriptor().format),
    .dimension = directional_surface_->GetDescriptor().texture_type,
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
  };

  if (const auto existing = registry.FindShaderVisibleIndex(
        *directional_surface_, view_desc);
    existing.has_value()) {
    return *existing;
  }

  auto& allocator = gfx->GetDescriptorAllocator();
  auto handle = allocator.AllocateRaw(
    view_desc.view_type, view_desc.visibility);
  if (!handle.IsValid()) {
    return kInvalidShaderVisibleIndex;
  }

  const auto shader_visible_index = allocator.GetShaderVisibleIndex(handle);
  const auto view
    = registry.RegisterView(*directional_surface_, std::move(handle), view_desc);
  if (!view->IsValid()) {
    return kInvalidShaderVisibleIndex;
  }

  return shader_visible_index;
}

auto ConventionalShadowTargetAllocator::EnsureSpotSurface(
  const std::uint32_t shadow_count, const std::uint32_t resolution_hint) -> void
{
  const auto array_size
    = (std::max)(1U, (std::min)(shadow_count, 8U));
  const auto resolved_resolution
    = ResolveSpotResolution(resolution_hint, renderer_.GetShadowQualityTier());
  const auto resolution
    = glm::uvec2 { resolved_resolution, resolved_resolution };
  const auto needs_reallocation = !spot_surface_
    || spot_resolution_ != resolution
    || spot_array_size_ != array_size;
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

  if (const auto existing = registry.FindShaderVisibleIndex(
        *spot_surface_, view_desc);
    existing.has_value()) {
    return *existing;
  }

  auto& allocator = gfx->GetDescriptorAllocator();
  auto handle = allocator.AllocateRaw(
    view_desc.view_type, view_desc.visibility);
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
  const std::uint32_t shadow_count, const std::uint32_t resolution_hint) -> void
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

  if (const auto existing = registry.FindShaderVisibleIndex(
        *point_surface_, view_desc);
    existing.has_value()) {
    return *existing;
  }

  auto& allocator = gfx->GetDescriptorAllocator();
  auto handle = allocator.AllocateRaw(
    view_desc.view_type, view_desc.visibility);
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
