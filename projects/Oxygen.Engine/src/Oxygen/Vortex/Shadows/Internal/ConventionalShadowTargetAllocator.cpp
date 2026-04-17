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

auto ResolveDirectionalResolution() -> std::uint32_t
{
  return 2048U;
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
  const std::uint32_t cascade_count) -> DirectionalAllocation
{
  EnsureDirectionalSurface(cascade_count);
  return {
    .surface = directional_surface_,
    .surface_srv = directional_surface_srv_,
    .resolution = directional_resolution_,
    .cascade_count = directional_array_size_,
  };
}

auto ConventionalShadowTargetAllocator::EnsureDirectionalSurface(
  const std::uint32_t cascade_count) -> void
{
  const auto array_size
    = (std::max)(1U, (std::min)(cascade_count, 4U));
  const auto resolution = glm::uvec2 { ResolveDirectionalResolution(),
    ResolveDirectionalResolution() };
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
  desc.format = Format::kDepth32;
  desc.texture_type = TextureType::kTexture2DArray;
  desc.debug_name = "Vortex.DirectionalShadowSurface";
  desc.is_shader_resource = true;
  desc.is_render_target = true;
  desc.is_typeless = true;
  desc.use_clear_value = true;
  desc.clear_value = graphics::Color { 1.0F, 0.0F, 0.0F, 0.0F };
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

} // namespace oxygen::vortex::shadows::internal
