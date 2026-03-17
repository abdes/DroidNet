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
#include <Oxygen/Renderer/Passes/VirtualShadowFallbackPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>

namespace oxygen::engine {

VirtualShadowFallbackPass::VirtualShadowFallbackPass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(config ? config->debug_name : "VirtualShadowFallbackPass")
  , gfx_(gfx)
  , config_(std::move(config))
{
}

VirtualShadowFallbackPass::~VirtualShadowFallbackPass() = default;

auto VirtualShadowFallbackPass::DoPrepareResources(
  graphics::CommandRecorder&) -> co::Co<>
{
  active_dispatch_ = false;
  active_view_id_ = {};
  active_clip_level_count_ = 0U;
  active_pages_per_axis_ = 0U;
  active_pages_per_level_ = 0U;
  active_total_page_count_ = 0U;
  active_page_table_uav_ = kInvalidShaderVisibleIndex;
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
    *gfx_, "VirtualShadowFallbackPass.Constants",
    detail::kVirtualShadowPassConstantsStride);
  const auto base_slot = static_cast<std::size_t>(Context().frame_slot.get())
    * kPassConstantsSlotsPerFrame;
  const auto packed_origin_x = detail::PackFloat4(active_clip_origin_x_);
  const auto packed_origin_y = detail::PackFloat4(active_clip_origin_y_);
  const auto packed_page_world = detail::PackFloat4(active_clip_page_world_);
  const auto dispatch_slot_count = detail::WritePerClipPassConstants(
    pass_constants_.MappedPtr(), base_slot,
    detail::kVirtualShadowPassConstantsStride, active_clip_level_count_,
    [&](const std::uint32_t clip_index) {
      return detail::VirtualShadowPassConstants {
      .page_table_uav_index = active_page_table_uav_,
      .total_page_count = active_total_page_count_,
      .pages_per_axis = active_pages_per_axis_,
      .clip_level_count = active_clip_level_count_,
      .pages_per_level = active_pages_per_level_,
      .target_clip_index = clip_index,
      .clip_origin_x_packed = packed_origin_x,
      .clip_origin_y_packed = packed_origin_y,
      .clip_page_world_packed = packed_page_world,
      };
    });

  active_dispatch_ = dispatch_slot_count > 0U;
  co_return;
}

auto VirtualShadowFallbackPass::DoExecute(graphics::CommandRecorder& recorder)
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
  std::uint32_t dispatch_slot = 0U;
  for (std::uint32_t clip_index = active_clip_level_count_ - 1U;
    clip_index-- > 0U;) {
    const auto slot = base_slot + dispatch_slot;
    SetPassConstantsIndex(pass_constants_.Index(slot));
    detail::DispatchVirtualPageManagementPass(
      *shadow_manager, active_view_id_, recorder,
      (active_pages_per_level_ + kDispatchGroupSize - 1U) / kDispatchGroupSize);
    ++dispatch_slot;
  }

  co_return;
}

auto VirtualShadowFallbackPass::ValidateConfig() -> void
{
  if (!gfx_) {
    throw std::runtime_error("VirtualShadowFallbackPass: graphics is null");
  }
  if (!config_) {
    throw std::runtime_error("VirtualShadowFallbackPass: config is null");
  }
}

auto VirtualShadowFallbackPass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();

  return graphics::ComputePipelineDesc::Builder()
    .SetComputeShader({ .stage = oxygen::ShaderType::kCompute,
      .source_path = "Lighting/VirtualShadowFallback.hlsl",
      .entry_point = "CS" })
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .SetDebugName("VirtualShadowFallback_PSO")
    .Build();
}

auto VirtualShadowFallbackPass::NeedRebuildPipelineState() const -> bool
{
  return !LastBuiltPsoDesc().has_value();
}

} // namespace oxygen::engine
