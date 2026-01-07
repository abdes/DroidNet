//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Renderer/Passes/GraphicsRenderPass.h>
#include <Oxygen/Renderer/RenderContext.h>

using oxygen::engine::GraphicsRenderPass;
using oxygen::graphics::CommandRecorder;

GraphicsRenderPass::GraphicsRenderPass(const std::string_view name)
  : RenderPass(name)
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

  // Set the graphics pipeline state
  recorder.SetPipelineState(*last_built_pso_desc_);

  // Bind common resources
  BindIndicesBuffer(recorder);
  BindSceneConstantsBuffer(recorder);
  BindPassConstantsIndexConstant(recorder, GetPassConstantsIndex());

  // Allow derived class additional setup
  DoSetupPipeline(recorder);
}

auto GraphicsRenderPass::DoSetupPipeline(CommandRecorder& /*recorder*/) -> void
{
  // Default implementation does nothing
}

auto GraphicsRenderPass::BindSceneConstantsBuffer(
  CommandRecorder& recorder) const -> void
{
  DCHECK_NOTNULL_F(Context().scene_constants);
  DCHECK_F(last_built_pso_desc_.has_value());

  recorder.SetGraphicsRootConstantBufferView(
    static_cast<uint32_t>(binding::RootParam::kSceneConstants),
    Context().scene_constants->GetGPUVirtualAddress());
}

auto GraphicsRenderPass::BindIndicesBuffer(CommandRecorder& /*recorder*/) const
  -> void
{
  // In the bindless rendering model, the indices buffer is accessible through
  // the descriptor table at heap index 0. No additional binding required.
}

auto GraphicsRenderPass::BindPassConstantsIndexConstant(
  CommandRecorder& recorder, ShaderVisibleIndex pass_constants_index) const
  -> void
{
  DCHECK_F(last_built_pso_desc_.has_value());

  recorder.SetGraphicsRoot32BitConstant(
    static_cast<uint32_t>(binding::RootParam::kRootConstants),
    pass_constants_index.get(), 1);
}

namespace {
auto RangeTypeToViewType(oxygen::engine::binding::RangeType rt)
  -> oxygen::graphics::ResourceViewType
{
  using oxygen::graphics::ResourceViewType;

  switch (rt) {
  case oxygen::engine::binding::RangeType::SRV:
    return ResourceViewType::kRawBuffer_SRV;
  case oxygen::engine::binding::RangeType::Sampler:
    return ResourceViewType::kSampler;
  case oxygen::engine::binding::RangeType::UAV:
    return ResourceViewType::kRawBuffer_UAV;
  default:
    return ResourceViewType::kNone;
  }
}
} // namespace

// BuildRootBindings() moved to RenderPass.
