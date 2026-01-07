//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Renderer/Passes/ComputeRenderPass.h>
#include <Oxygen/Renderer/RenderContext.h>

using oxygen::engine::ComputeRenderPass;
using oxygen::graphics::CommandRecorder;

// BuildRootBindings() moved to RenderPass.

ComputeRenderPass::ComputeRenderPass(const std::string_view name)
  : RenderPass(name)
{
}

auto ComputeRenderPass::OnPrepareResources(CommandRecorder& /*recorder*/)
  -> void
{
  if (NeedRebuildPipelineState()) {
    last_built_pso_desc_ = CreatePipelineStateDesc();
  }
}

auto ComputeRenderPass::OnExecute(CommandRecorder& recorder) -> void
{
  DCHECK_F(last_built_pso_desc_.has_value(),
    "Compute pipeline state not built - NeedRebuildPipelineState() returned "
    "false without prior build");

  // Set the compute pipeline state
  recorder.SetPipelineState(*last_built_pso_desc_);

  // Bind common resources expected by the engine root signature.
  DCHECK_NOTNULL_F(Context().scene_constants);
  recorder.SetComputeRootConstantBufferView(
    static_cast<uint32_t>(binding::RootParam::kSceneConstants),
    Context().scene_constants->GetGPUVirtualAddress());

  // Root constants at b2, space0.
  // - DWORD0: g_DrawIndex (unused for compute)
  // - DWORD1: g_PassConstantsIndex
  recorder.SetComputeRoot32BitConstant(
    static_cast<uint32_t>(binding::RootParam::kRootConstants), 0U, 0);
  recorder.SetComputeRoot32BitConstant(
    static_cast<uint32_t>(binding::RootParam::kRootConstants),
    GetPassConstantsIndex().get(), 1);
}
