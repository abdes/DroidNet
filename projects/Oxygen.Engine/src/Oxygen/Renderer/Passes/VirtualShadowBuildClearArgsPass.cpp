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
#include <Oxygen/Renderer/Passes/VirtualShadowBuildClearArgsPass.h>
#include <Oxygen/Renderer/Passes/VirtualShadowSchedulePass.h>
#include <Oxygen/Renderer/RenderContext.h>

namespace oxygen::engine {

VirtualShadowBuildClearArgsPass::VirtualShadowBuildClearArgsPass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : ComputeRenderPass(
      config ? config->debug_name : "VirtualShadowBuildClearArgsPass")
  , gfx_(gfx)
  , config_(std::move(config))
{
}

VirtualShadowBuildClearArgsPass::~VirtualShadowBuildClearArgsPass() = default;

auto VirtualShadowBuildClearArgsPass::DoPrepareResources(
  graphics::CommandRecorder&) -> co::Co<>
{
  active_dispatch_ = false;
  active_view_id_ = {};

  if (Context().frame_slot == frame::kInvalidSlot) {
    co_return;
  }

  const auto* schedule_pass = Context().GetPass<VirtualShadowSchedulePass>();
  const auto* resources
    = schedule_pass != nullptr ? schedule_pass->GetScheduleResources() : nullptr;
  if (schedule_pass == nullptr || !schedule_pass->HasActiveDispatch()
    || resources == nullptr || !resources->count_uav.IsValid()
    || !resources->clear_args_uav.IsValid()
    || !resources->draw_page_counter_uav.IsValid()) {
    co_return;
  }

  pass_constants_.Ensure(
    *gfx_, "VirtualShadowBuildClearArgsPass.Constants",
    detail::kVirtualShadowPassConstantsStride);
  const auto slot = static_cast<std::size_t>(Context().frame_slot.get());
  const auto pass_constants_index = pass_constants_.Index(slot);

  const detail::VirtualShadowPassConstants constants {
    .schedule_count_uav_index = resources->count_uav,
    .clear_args_uav_index = resources->clear_args_uav,
    .draw_page_counter_uav_index = resources->draw_page_counter_uav,
  };
  auto* slot_ptr = static_cast<std::byte*>(pass_constants_.MappedPtr())
    + static_cast<std::ptrdiff_t>(slot * detail::kVirtualShadowPassConstantsStride);
  std::memcpy(slot_ptr, &constants, sizeof(constants));
  SetPassConstantsIndex(pass_constants_index);

  active_view_id_ = Context().current_view.view_id;
  active_dispatch_ = true;
  co_return;
}

auto VirtualShadowBuildClearArgsPass::DoExecute(
  graphics::CommandRecorder& recorder) -> co::Co<>
{
  if (!active_dispatch_) {
    co_return;
  }

  const auto* schedule_pass = Context().GetPass<VirtualShadowSchedulePass>();
  const auto* resources
    = schedule_pass != nullptr ? schedule_pass->GetScheduleResources() : nullptr;
  if (resources == nullptr) {
    co_return;
  }

  recorder.RequireResourceState(
    *resources->count_buffer, graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(
    *resources->clear_args_buffer, graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(*resources->draw_page_counter_buffer,
    graphics::ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();
  recorder.Dispatch(1U, 1U, 1U);

  co_return;
}

auto VirtualShadowBuildClearArgsPass::ValidateConfig() -> void
{
  if (!gfx_) {
    throw std::runtime_error(
      "VirtualShadowBuildClearArgsPass: graphics is null");
  }
  if (!config_) {
    throw std::runtime_error("VirtualShadowBuildClearArgsPass: config is null");
  }
}

auto VirtualShadowBuildClearArgsPass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto generated_bindings = BuildRootBindings();

  return graphics::ComputePipelineDesc::Builder()
    .SetComputeShader({ .stage = oxygen::ShaderType::kCompute,
      .source_path = "Lighting/VirtualShadowBuildClearArgs.hlsl",
      .entry_point = "CS" })
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .SetDebugName("VirtualShadowBuildClearArgs_PSO")
    .Build();
}

auto VirtualShadowBuildClearArgsPass::NeedRebuildPipelineState() const -> bool
{
  return !LastBuiltPsoDesc().has_value();
}

} // namespace oxygen::engine
