//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <span>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Renderer/Internal/GpuDebugManager.h>
#include <Oxygen/Renderer/Passes/GpuDebugClearPass.h>
#include <Oxygen/Renderer/RenderContext.h>

namespace oxygen::engine {

GpuDebugClearPass::GpuDebugClearPass(observer_ptr<Graphics> /*gfx*/)
  : ComputeRenderPass("GpuDebugClearPass")
{
}

auto GpuDebugClearPass::ValidateConfig() -> void
{
  // No specific configuration required for this pass, but do sanity checks on
  // when it used.
  auto debug_manager = Context().gpu_debug_manager;
  if (!debug_manager) {
    return;
    DCHECK_NOTNULL_F(debug_manager->GetCounterBuffer());
    DCHECK_NOTNULL_F(debug_manager->GetLineBuffer());
  }
}

auto GpuDebugClearPass::DoPrepareResources(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  auto debug_manager = Context().gpu_debug_manager;
  if (!debug_manager) {
    co_return; // We need a debug manager or this pass is a no-op.
  }
  DCHECK_NOTNULL_F(debug_manager->GetCounterBuffer());
  DCHECK_NOTNULL_F(debug_manager->GetLineBuffer());

  // Start tracking the resources. Since this is the first pass that uses them,
  // we initialize their tracking state. We assume kCommon as the baseline
  // state for these persistent buffers.
  recorder.BeginTrackingResourceState(
    *debug_manager->GetLineBuffer(), graphics::ResourceStates::kCommon);
  recorder.BeginTrackingResourceState(
    *debug_manager->GetCounterBuffer(), graphics::ResourceStates::kCommon);

  // Ensure line buffer and counter buffer are in UAV state for clearing.
  recorder.RequireResourceState(*debug_manager->GetLineBuffer(),
    graphics::ResourceStates::kUnorderedAccess);
  recorder.RequireResourceState(*debug_manager->GetCounterBuffer(),
    graphics::ResourceStates::kUnorderedAccess);

  co_return;
}

auto GpuDebugClearPass::DoExecute(graphics::CommandRecorder& recorder)
  -> co::Co<>
{
  auto debug_manager = Context().gpu_debug_manager;
  if (!debug_manager) {
    co_return; // We need a debug manager or this pass is a no-op.
  }

  // Dispatch the clear shader.
  // One thread is enough as it only resets the counter.
  recorder.Dispatch(1, 1, 1);

  co_return;
}

auto GpuDebugClearPass::CreatePipelineStateDesc()
  -> graphics::ComputePipelineDesc
{
  auto root_bindings = RenderPass::BuildRootBindings();
  const auto bindings = std::span<const graphics::RootBindingItem>(
    root_bindings.data(), root_bindings.size());

  return graphics::ComputePipelineDesc::Builder()
    .SetComputeShader(graphics::ShaderRequest {
      .stage = ShaderType::kCompute,
      .source_path = "Renderer/GpuDebugClear.hlsl",
      .entry_point = "CS",
    })
    .SetRootBindings(bindings)
    .Build();
}

auto GpuDebugClearPass::NeedRebuildPipelineState() const -> bool
{
  return !LastBuiltPsoDesc().has_value();
}

} // namespace oxygen::engine
