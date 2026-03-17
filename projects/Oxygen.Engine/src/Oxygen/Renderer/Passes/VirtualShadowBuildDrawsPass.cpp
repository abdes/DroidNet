//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Passes/Detail/VirtualShadowPassConstants.h>
#include <Oxygen/Renderer/Passes/VirtualShadowBuildDrawsPass.h>
#include <Oxygen/Renderer/Passes/VirtualShadowSchedulePass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShadowManager.h>

namespace oxygen::engine {

VirtualShadowBuildDrawsPass::VirtualShadowBuildDrawsPass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(config ? config->debug_name : "VirtualShadowBuildDrawsPass")
  , gfx_(gfx)
  , config_(std::move(config))
{
}

VirtualShadowBuildDrawsPass::~VirtualShadowBuildDrawsPass() = default;

auto VirtualShadowBuildDrawsPass::DoPrepareResources(
  graphics::CommandRecorder&) -> co::Co<>
{
  active_dispatch_ = false;
  active_view_id_ = {};
  active_page_management_bindings_ = {};
  active_schedule_resources_ = nullptr;
  active_draw_count_ = 0U;
  active_dispatch_group_count_ = 0U;

  if (Context().frame_slot == frame::kInvalidSlot) {
    co_return;
  }

  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (shadow_manager == nullptr) {
    co_return;
  }

  const auto* schedule_pass = Context().GetPass<VirtualShadowSchedulePass>();
  const auto* resources
    = schedule_pass != nullptr ? schedule_pass->GetScheduleResources() : nullptr;
  if (schedule_pass == nullptr || !schedule_pass->HasActiveDispatch()
    || resources == nullptr || !resources->schedule_uav.IsValid()
    || !resources->schedule_lookup_uav.IsValid()
    || !resources->count_uav.IsValid() || !resources->draw_args_uav.IsValid()
    || !resources->draw_page_ranges_uav.IsValid()
    || !resources->draw_page_indices_uav.IsValid()
    || !resources->draw_page_counter_uav.IsValid()) {
    co_return;
  }

  shadow_manager->ResolveVirtualCurrentFrame(Context().current_view.view_id);
  const auto* metadata = shadow_manager->TryGetVirtualDirectionalMetadata(
    Context().current_view.view_id);
  const auto* page_management_bindings
    = shadow_manager->TryGetVirtualPageManagementBindings(
      Context().current_view.view_id);
  if (metadata == nullptr || page_management_bindings == nullptr
    || metadata->clip_level_count == 0U || metadata->pages_per_axis == 0U) {
    co_return;
  }

  pass_constants_.Ensure(
    *gfx_, "VirtualShadowBuildDrawsPass.Constants",
    detail::kVirtualShadowPassConstantsStride);
  const auto slot = static_cast<std::size_t>(Context().frame_slot.get());
  const auto pass_constants_index = pass_constants_.Index(slot);
  const detail::VirtualShadowPassConstants constants {
    .draw_bounds_srv_index = schedule_pass->GetActiveDrawBoundsSrv(),
    .schedule_uav_index = resources->schedule_uav,
    .schedule_lookup_uav_index = resources->schedule_lookup_uav,
    .schedule_count_uav_index = resources->count_uav,
    .draw_args_uav_index = resources->draw_args_uav,
    .draw_page_ranges_uav_index = resources->draw_page_ranges_uav,
    .draw_page_indices_uav_index = resources->draw_page_indices_uav,
    .draw_page_counter_uav_index = resources->draw_page_counter_uav,
    .pages_per_axis = metadata->pages_per_axis,
    .clip_level_count = metadata->clip_level_count,
    .pages_per_level = metadata->pages_per_axis * metadata->pages_per_axis,
    .draw_count = schedule_pass->GetActiveDrawCount(),
    .draw_page_list_capacity = resources->draw_page_index_capacity,
  };
  auto* slot_ptr = static_cast<std::byte*>(pass_constants_.MappedPtr())
    + static_cast<std::ptrdiff_t>(slot * detail::kVirtualShadowPassConstantsStride);
  std::memcpy(slot_ptr, &constants, sizeof(constants));
  SetPassConstantsIndex(pass_constants_index);

  active_view_id_ = Context().current_view.view_id;
  active_page_management_bindings_ = *page_management_bindings;
  active_schedule_resources_ = resources;
  active_draw_count_ = schedule_pass->GetActiveDrawCount();
  active_dispatch_group_count_
    = (active_draw_count_ + kDispatchGroupSize - 1U) / kDispatchGroupSize;
  active_dispatch_ = active_draw_count_ > 0U;
  co_return;
}

auto VirtualShadowBuildDrawsPass::DoExecute(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  if (!active_dispatch_ || active_schedule_resources_ == nullptr) {
    co_return;
  }

  const auto shadow_manager = Context().GetRenderer().GetShadowManager();
  if (shadow_manager == nullptr) {
    co_return;
  }

  recorder.RequireResourceState(*active_schedule_resources_->schedule_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(*active_schedule_resources_->schedule_lookup_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(*active_schedule_resources_->count_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(*active_schedule_resources_->draw_args_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(*active_schedule_resources_->draw_page_ranges_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(*active_schedule_resources_->draw_page_indices_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(*active_schedule_resources_->draw_page_counter_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();
  recorder.Dispatch(active_dispatch_group_count_, 1U, 1U);

  shadow_manager->FinalizeVirtualPageManagementOutputs(
    active_view_id_, recorder);
  recorder.RequireResourceState(
    *active_schedule_resources_->schedule_buffer,
    graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *active_schedule_resources_->count_buffer,
    graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *active_schedule_resources_->draw_page_ranges_buffer,
    graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *active_schedule_resources_->draw_page_indices_buffer,
    graphics::ResourceStates::kShaderResource);
  recorder.RequireResourceState(
    *active_schedule_resources_->clear_args_buffer,
    graphics::ResourceStates::kIndirectArgument);
  recorder.RequireResourceState(
    *active_schedule_resources_->draw_args_buffer,
    graphics::ResourceStates::kIndirectArgument);
  recorder.FlushBarriers();

  shadow_manager->SubmitVirtualGpuRasterInputs(active_view_id_,
    renderer::VirtualShadowGpuRasterInputs {
      .schedule_buffer = active_schedule_resources_->schedule_buffer,
      .schedule_srv = active_schedule_resources_->schedule_srv,
      .schedule_count_buffer = active_schedule_resources_->count_buffer,
      .schedule_count_srv = active_schedule_resources_->count_srv,
      .draw_page_ranges_buffer
      = active_schedule_resources_->draw_page_ranges_buffer,
      .draw_page_ranges_srv = active_schedule_resources_->draw_page_ranges_srv,
      .draw_page_indices_buffer
      = active_schedule_resources_->draw_page_indices_buffer,
      .draw_page_indices_srv
      = active_schedule_resources_->draw_page_indices_srv,
      .clear_indirect_args_buffer
      = active_schedule_resources_->clear_args_buffer,
      .draw_indirect_args_buffer = active_schedule_resources_->draw_args_buffer,
      .source_frame_sequence = Context().frame_sequence,
      .draw_count = active_draw_count_,
      .pending_raster_page_count
      = active_page_management_bindings_.pending_raster_page_count,
    });

  co_return;
}

auto VirtualShadowBuildDrawsPass::ValidateConfig() -> void
{
  if (!gfx_) {
    throw std::runtime_error("VirtualShadowBuildDrawsPass: graphics is null");
  }
  if (!config_) {
    throw std::runtime_error("VirtualShadowBuildDrawsPass: config is null");
  }
}

auto VirtualShadowBuildDrawsPass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();

  return graphics::ComputePipelineDesc::Builder()
    .SetComputeShader({ .stage = oxygen::ShaderType::kCompute,
      .source_path = "Lighting/VirtualShadowBuildDraws.hlsl",
      .entry_point = "CS" })
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .SetDebugName("VirtualShadowBuildDraws_PSO")
    .Build();
}

auto VirtualShadowBuildDrawsPass::NeedRebuildPipelineState() const -> bool
{
  return !LastBuiltPsoDesc().has_value();
}

} // namespace oxygen::engine
