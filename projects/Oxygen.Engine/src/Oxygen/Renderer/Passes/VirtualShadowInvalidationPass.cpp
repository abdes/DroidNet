//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Passes/Detail/VirtualShadowPassConstants.h>
#include <Oxygen/Renderer/Passes/VirtualShadowInvalidationPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>

namespace oxygen::engine {

VirtualShadowInvalidationPass::VirtualShadowInvalidationPass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(
      config ? config->debug_name : "VirtualShadowInvalidationPass")
  , gfx_(gfx)
  , config_(std::move(config))
{
}

VirtualShadowInvalidationPass::~VirtualShadowInvalidationPass() = default;

auto VirtualShadowInvalidationPass::DoPrepareResources(
  graphics::CommandRecorder&) -> co::Co<>
{
  active_dispatch_ = false;
  active_view_id_ = {};
  active_dispatch_group_count_ = 0U;
  active_current_light_view_ = glm::mat4 { 1.0F };
  active_previous_light_view_ = glm::mat4 { 1.0F };
  active_clip_page_world_.fill(0.0F);
  active_page_management_bindings_ = {};

  if (Context().frame_slot == frame::kInvalidSlot) {
    co_return;
  }

  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (shadow_manager == nullptr) {
    co_return;
  }

  shadow_manager->ResolveVirtualCurrentFrame(Context().current_view.view_id);

  const auto* metadata = shadow_manager->TryGetVirtualDirectionalMetadata(
    Context().current_view.view_id);
  const auto* page_management_bindings
    = shadow_manager->TryGetVirtualPageManagementBindings(
      Context().current_view.view_id);
  if (metadata == nullptr || page_management_bindings == nullptr
    || !page_management_bindings->dirty_page_flags_uav.IsValid()
    || !page_management_bindings->physical_page_metadata_srv.IsValid()
    || metadata->clip_level_count == 0U || metadata->pages_per_axis == 0U
    || page_management_bindings->physical_page_capacity == 0U) {
    co_return;
  }

  pass_constants_.Ensure(
    *gfx_, "VirtualShadowInvalidationPass.Constants",
    detail::kVirtualShadowPassConstantsStride);
  const auto slot = static_cast<std::size_t>(Context().frame_slot.get());
  const auto pass_constants_index = pass_constants_.Index(slot);

  const auto clip_count
    = std::min(metadata->clip_level_count, kMaxSupportedClipLevels);
  for (std::uint32_t clip_index = 0U; clip_index < clip_count; ++clip_index) {
    active_clip_page_world_[clip_index]
      = metadata->clip_metadata[clip_index].origin_page_scale.z;
  }

  const detail::VirtualShadowPassConstants constants {
    .previous_shadow_caster_bounds_srv_index
    = page_management_bindings->previous_shadow_caster_bounds_srv,
    .current_shadow_caster_bounds_srv_index
    = page_management_bindings->current_shadow_caster_bounds_srv,
    .dirty_page_flags_uav_index = page_management_bindings->dirty_page_flags_uav,
    .physical_page_metadata_srv_index
    = page_management_bindings->physical_page_metadata_srv,
    .current_light_view_matrix = metadata->light_view,
    .previous_light_view_matrix = page_management_bindings->previous_light_view,
    .shadow_caster_bound_count = page_management_bindings->shadow_caster_bound_count,
    .clip_level_count = metadata->clip_level_count,
    .physical_page_capacity = page_management_bindings->physical_page_capacity,
    .global_dirty_resident_contents
    = page_management_bindings->global_dirty_resident_contents ? 1U : 0U,
    .clip_page_world_packed = detail::PackFloat4(active_clip_page_world_),
  };

  auto* slot_ptr = static_cast<std::byte*>(pass_constants_.MappedPtr())
    + static_cast<std::ptrdiff_t>(slot * detail::kVirtualShadowPassConstantsStride);
  std::memcpy(slot_ptr, &constants, sizeof(constants));
  SetPassConstantsIndex(pass_constants_index);

  active_dispatch_ = true;
  active_view_id_ = Context().current_view.view_id;
  active_dispatch_group_count_
    = (page_management_bindings->physical_page_capacity + kDispatchGroupSize - 1U)
    / kDispatchGroupSize;
  active_current_light_view_ = metadata->light_view;
  active_previous_light_view_ = page_management_bindings->previous_light_view;
  active_page_management_bindings_ = *page_management_bindings;

  co_return;
}

auto VirtualShadowInvalidationPass::DoExecute(
  graphics::CommandRecorder& recorder) -> co::Co<>
{
  if (!active_dispatch_) {
    co_return;
  }

  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (shadow_manager == nullptr) {
    co_return;
  }

  detail::DispatchVirtualPageManagementPass(
    *shadow_manager, active_view_id_, recorder, active_dispatch_group_count_);

  co_return;
}

auto VirtualShadowInvalidationPass::ValidateConfig() -> void
{
  if (!gfx_) {
    throw std::runtime_error("VirtualShadowInvalidationPass: graphics is null");
  }
  if (!config_) {
    throw std::runtime_error("VirtualShadowInvalidationPass: config is null");
  }
}

auto VirtualShadowInvalidationPass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();

  return graphics::ComputePipelineDesc::Builder()
    .SetComputeShader({ .stage = oxygen::ShaderType::kCompute,
      .source_path = "Lighting/VirtualShadowInvalidation.hlsl",
      .entry_point = "CS" })
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .SetDebugName("VirtualShadowInvalidation_PSO")
    .Build();
}

auto VirtualShadowInvalidationPass::NeedRebuildPipelineState() const -> bool
{
  return !LastBuiltPsoDesc().has_value();
}

} // namespace oxygen::engine
