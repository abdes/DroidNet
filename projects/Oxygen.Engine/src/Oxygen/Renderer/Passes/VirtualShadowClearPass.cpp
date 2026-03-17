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
#include <Oxygen/Renderer/Passes/VirtualShadowClearPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>

namespace oxygen::engine {

VirtualShadowClearPass::VirtualShadowClearPass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(config ? config->debug_name : "VirtualShadowClearPass")
  , gfx_(gfx)
  , config_(std::move(config))
{
}

VirtualShadowClearPass::~VirtualShadowClearPass() = default;

auto VirtualShadowClearPass::DoPrepareResources(
  graphics::CommandRecorder&) -> co::Co<>
{
  active_dispatch_ = false;
  active_view_id_ = {};
  active_page_management_bindings_ = {};
  active_total_page_count_ = 0U;
  active_dispatch_group_count_ = 0U;

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
    || !page_management_bindings->dirty_page_flags_uav.IsValid()
    || !page_management_bindings->physical_page_metadata_uav.IsValid()
    || !page_management_bindings->resolve_stats_uav.IsValid()
    || metadata->clip_level_count == 0U || metadata->pages_per_axis == 0U) {
    co_return;
  }

  const auto total_page_count = metadata->clip_level_count
    * metadata->pages_per_axis * metadata->pages_per_axis;
  if (total_page_count == 0U) {
    co_return;
  }

  pass_constants_.Ensure(
    *gfx_, "VirtualShadowClearPass.Constants",
    detail::kVirtualShadowPassConstantsStride);
  const auto slot = static_cast<std::size_t>(Context().frame_slot.get());
  const auto pass_constants_index = pass_constants_.Index(slot);

  const detail::VirtualShadowPassConstants constants {
    .page_table_uav_index = page_management_bindings->page_table_uav,
    .page_flags_uav_index = page_management_bindings->page_flags_uav,
    .dirty_page_flags_uav_index = page_management_bindings->dirty_page_flags_uav,
    .physical_page_metadata_uav_index
    = page_management_bindings->physical_page_metadata_uav,
    .resolve_stats_uav_index = page_management_bindings->resolve_stats_uav,
    .total_page_count = total_page_count,
    .physical_page_capacity = page_management_bindings->physical_page_capacity,
    .atlas_tiles_per_axis = page_management_bindings->atlas_tiles_per_axis,
    .reset_page_management_state
    = page_management_bindings->reset_page_management_state ? 1U : 0U,
  };

  auto* slot_ptr = static_cast<std::byte*>(pass_constants_.MappedPtr())
    + static_cast<std::ptrdiff_t>(slot * detail::kVirtualShadowPassConstantsStride);
  std::memcpy(slot_ptr, &constants, sizeof(constants));
  SetPassConstantsIndex(pass_constants_index);

  active_dispatch_ = true;
  active_view_id_ = Context().current_view.view_id;
  active_page_management_bindings_ = *page_management_bindings;
  active_total_page_count_ = total_page_count;
  active_dispatch_group_count_ = (std::max(
                                    total_page_count,
                                    page_management_bindings->physical_page_capacity * 3U)
                                  + kDispatchGroupSize - 1U)
    / kDispatchGroupSize;

  co_return;
}

auto VirtualShadowClearPass::DoExecute(graphics::CommandRecorder& recorder)
  -> co::Co<>
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

auto VirtualShadowClearPass::ValidateConfig() -> void
{
  if (!gfx_) {
    throw std::runtime_error("VirtualShadowClearPass: graphics is null");
  }
  if (!config_) {
    throw std::runtime_error("VirtualShadowClearPass: config is null");
  }
}

auto VirtualShadowClearPass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();

  return graphics::ComputePipelineDesc::Builder()
    .SetComputeShader({ .stage = oxygen::ShaderType::kCompute,
      .source_path = "Lighting/VirtualShadowClear.hlsl",
      .entry_point = "CS" })
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .SetDebugName("VirtualShadowClear_PSO")
    .Build();
}

auto VirtualShadowClearPass::NeedRebuildPipelineState() const -> bool
{
  return !LastBuiltPsoDesc().has_value();
}

} // namespace oxygen::engine
