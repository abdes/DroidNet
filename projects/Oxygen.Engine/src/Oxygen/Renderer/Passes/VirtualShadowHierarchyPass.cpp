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
#include <Oxygen/Renderer/Passes/VirtualShadowHierarchyPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>

namespace oxygen::engine {

VirtualShadowHierarchyPass::VirtualShadowHierarchyPass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(config ? config->debug_name : "VirtualShadowHierarchyPass")
  , gfx_(gfx)
  , config_(std::move(config))
{
}

VirtualShadowHierarchyPass::~VirtualShadowHierarchyPass() = default;

auto VirtualShadowHierarchyPass::DoPrepareResources(
  graphics::CommandRecorder&) -> co::Co<>
{
  active_dispatch_ = false;
  active_view_id_ = {};
  active_clip_level_count_ = 0U;
  active_pages_per_axis_ = 0U;
  active_pages_per_level_ = 0U;
  active_total_page_count_ = 0U;
  active_page_table_uav_ = kInvalidShaderVisibleIndex;
  active_page_flags_uav_ = kInvalidShaderVisibleIndex;
  active_clip_origin_x_.fill(0.0F);
  active_clip_origin_y_.fill(0.0F);
  active_clip_page_world_.fill(0.0F);

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
    || !page_management_bindings->page_table_uav.IsValid()
    || !page_management_bindings->page_flags_uav.IsValid()
    || metadata->clip_level_count <= 1U || metadata->pages_per_axis == 0U) {
    co_return;
  }

  active_view_id_ = Context().current_view.view_id;
  active_clip_level_count_
    = std::min(metadata->clip_level_count, kMaxSupportedClipLevels);
  active_pages_per_axis_ = metadata->pages_per_axis;
  active_pages_per_level_ = metadata->pages_per_axis * metadata->pages_per_axis;
  active_total_page_count_ = metadata->clip_level_count * active_pages_per_level_;
  active_page_table_uav_ = page_management_bindings->page_table_uav;
  active_page_flags_uav_ = page_management_bindings->page_flags_uav;
  for (std::uint32_t clip_index = 0U; clip_index < active_clip_level_count_;
    ++clip_index) {
    active_clip_origin_x_[clip_index]
      = metadata->clip_metadata[clip_index].origin_page_scale.x;
    active_clip_origin_y_[clip_index]
      = metadata->clip_metadata[clip_index].origin_page_scale.y;
    active_clip_page_world_[clip_index]
      = metadata->clip_metadata[clip_index].origin_page_scale.z;
  }

  pass_constants_.Ensure(
    *gfx_, "VirtualShadowHierarchyPass.Constants",
    detail::kVirtualShadowPassConstantsStride);
  const auto base_slot = static_cast<std::size_t>(Context().frame_slot.get())
    * kPassConstantsSlotsPerFrame;
  const auto packed_origin_x = detail::PackFloat4(active_clip_origin_x_);
  const auto packed_origin_y = detail::PackFloat4(active_clip_origin_y_);
  const auto packed_page_world = detail::PackFloat4(active_clip_page_world_);
  detail::WritePerClipPassConstants(
    pass_constants_.MappedPtr(), base_slot,
    detail::kVirtualShadowPassConstantsStride, active_clip_level_count_,
    [&](const std::uint32_t fine_clip) {
      return detail::VirtualShadowPassConstants {
      .page_table_uav_index = active_page_table_uav_,
      .page_flags_uav_index = active_page_flags_uav_,
      .total_page_count = active_total_page_count_,
      .pages_per_axis = active_pages_per_axis_,
      .clip_level_count = active_clip_level_count_,
      .pages_per_level = active_pages_per_level_,
      .target_clip_index = fine_clip,
      .clip_origin_x_packed = packed_origin_x,
      .clip_origin_y_packed = packed_origin_y,
      .clip_page_world_packed = packed_page_world,
      };
    });

  active_dispatch_ = true;
  co_return;
}

auto VirtualShadowHierarchyPass::DoExecute(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  if (!active_dispatch_) {
    co_return;
  }

  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (shadow_manager == nullptr) {
    co_return;
  }

  const auto base_slot = static_cast<std::size_t>(Context().frame_slot.get())
    * kPassConstantsSlotsPerFrame;
  for (std::uint32_t fine_clip = 0U; fine_clip + 1U < active_clip_level_count_;
    ++fine_clip) {
    const auto slot = base_slot + fine_clip;
    SetPassConstantsIndex(pass_constants_.Index(slot));
    detail::DispatchVirtualPageManagementPass(
      *shadow_manager, active_view_id_, recorder,
      (active_pages_per_level_ + kDispatchGroupSize - 1U) / kDispatchGroupSize);
  }

  co_return;
}

auto VirtualShadowHierarchyPass::ValidateConfig() -> void
{
  if (!gfx_) {
    throw std::runtime_error("VirtualShadowHierarchyPass: graphics is null");
  }
  if (!config_) {
    throw std::runtime_error("VirtualShadowHierarchyPass: config is null");
  }
}

auto VirtualShadowHierarchyPass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();

  return graphics::ComputePipelineDesc::Builder()
    .SetComputeShader({ .stage = oxygen::ShaderType::kCompute,
      .source_path = "Lighting/VirtualShadowHierarchy.hlsl",
      .entry_point = "CS" })
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .SetDebugName("VirtualShadowHierarchy_PSO")
    .Build();
}

auto VirtualShadowHierarchyPass::NeedRebuildPipelineState() const -> bool
{
  return !LastBuiltPsoDesc().has_value();
}

} // namespace oxygen::engine
