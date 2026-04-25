//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Vortex/Passes/ComputeRenderPass.h>
#include <Oxygen/Vortex/RenderContext.h>

using oxygen::graphics::CommandRecorder;
using oxygen::vortex::ComputeRenderPass;

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

  recorder.SetPipelineState(*last_built_pso_desc_);

  DCHECK_NOTNULL_F(Context().view_constants);
  recorder.SetComputeRootConstantBufferView(
    static_cast<uint32_t>(
      oxygen::bindless::generated::d3d12::RootParam::kViewConstants),
    Context().view_constants->GetGPUVirtualAddress());

  recorder.SetComputeRoot32BitConstant(
    static_cast<uint32_t>(
      oxygen::bindless::generated::d3d12::RootParam::kRootConstants),
    0U, 0);
  recorder.SetComputeRoot32BitConstant(
    static_cast<uint32_t>(
      oxygen::bindless::generated::d3d12::RootParam::kRootConstants),
    GetPassConstantsIndex().get(), 1);
}
