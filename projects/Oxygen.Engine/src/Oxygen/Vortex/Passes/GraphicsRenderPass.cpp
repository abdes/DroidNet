//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Vortex/Passes/GraphicsRenderPass.h>
#include <Oxygen/Vortex/RenderContext.h>

using oxygen::graphics::CommandRecorder;
using oxygen::vortex::GraphicsRenderPass;

GraphicsRenderPass::GraphicsRenderPass(
  const std::string_view name, const bool require_view_constants)
  : RenderPass(name)
  , require_view_constants_(require_view_constants)
{
}

auto GraphicsRenderPass::OnPrepareResources(CommandRecorder& /*recorder*/)
  -> void
{
  if (NeedRebuildPipelineState()) {
    last_built_pso_desc_ = CreatePipelineStateDesc();
  }
}

auto GraphicsRenderPass::OnExecute(CommandRecorder& recorder) -> void
{
  DCHECK_F(last_built_pso_desc_.has_value(),
    "Pipeline state not built - NeedRebuildPipelineState() returned false "
    "without prior build");

  recorder.SetPipelineState(*last_built_pso_desc_);
  BindIndicesBuffer(recorder);
  if (require_view_constants_) {
    BindViewConstantsBuffer(recorder);
  }
  BindPassConstantsIndexConstant(recorder, GetPassConstantsIndex());
  DoSetupPipeline(recorder);
}

auto GraphicsRenderPass::DoSetupPipeline(
  graphics::CommandRecorder& /*recorder*/) -> void
{
}

auto GraphicsRenderPass::RebindCommonRootParameters(
  CommandRecorder& recorder) const -> void
{
  if (require_view_constants_) {
    BindViewConstantsBuffer(recorder);
  }
  BindPassConstantsIndexConstant(recorder, GetPassConstantsIndex());
}

auto GraphicsRenderPass::BindViewConstantsBuffer(
  CommandRecorder& recorder) const -> void
{
  DCHECK_NOTNULL_F(Context().view_constants);
  DCHECK_F(last_built_pso_desc_.has_value());

  recorder.SetGraphicsRootConstantBufferView(
    static_cast<uint32_t>(
      oxygen::bindless::generated::d3d12::RootParam::kViewConstants),
    Context().view_constants->GetGPUVirtualAddress());
}

auto GraphicsRenderPass::BindIndicesBuffer(CommandRecorder& /*recorder*/) const
  -> void
{
}

auto GraphicsRenderPass::BindPassConstantsIndexConstant(
  CommandRecorder& recorder,
  const ShaderVisibleIndex pass_constants_index) const -> void
{
  DCHECK_F(last_built_pso_desc_.has_value());

  recorder.SetGraphicsRoot32BitConstant(
    static_cast<uint32_t>(
      oxygen::bindless::generated::d3d12::RootParam::kRootConstants),
    pass_constants_index.get(), 1);
}
