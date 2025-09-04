//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Renderer/Internal/RenderScope.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/RenderPass.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Types/DrawMetaData.h>

using oxygen::engine::RenderPass;
using oxygen::graphics::CommandRecorder;

RenderPass::RenderPass(const std::string_view name)
{
  AddComponent<ObjectMetaData>(name);
}

auto RenderPass::PrepareResources(
  const RenderContext& context, CommandRecorder& recorder) -> co::Co<>
{
  detail::RenderScope ctx_scope(context_, context);

  DLOG_SCOPE_F(2, "RenderPass PrepareResources");
  DLOG_F(2, "pass: {}", GetName());

  ValidateConfig();

  // Check if we need to rebuild the pipeline state and the root signature.
  if (NeedRebuildPipelineState()) {
    last_built_pso_desc_ = CreatePipelineStateDesc();
  }

  co_await DoPrepareResources(recorder);

  co_return;
}

auto RenderPass::Execute(
  const RenderContext& context, CommandRecorder& recorder) -> co::Co<>
{
  DCHECK_F(!NeedRebuildPipelineState()); // built in PrepareResources

  detail::RenderScope ctx_scope(context_, context);

  DLOG_SCOPE_F(2, "RenderPass Execute");
  DLOG_F(2, "pass: {}", GetName());

  // This will try to get a cached pipeline state or create a new one if needed.
  // It also sets the bindless root signature.
  recorder.SetPipelineState(*last_built_pso_desc_);
  // Do these after the pipeline state is set.
  BindIndicesBuffer(recorder);
  BindSceneConstantsBuffer(recorder);

  try {
    co_await DoExecute(recorder);
  } catch (const std::exception& ex) {
    DLOG_F(ERROR, "{}: Execute failed: {}", GetName(), ex.what());
    throw; // Re-throw to propagate the error
  }

  co_return;
}

auto RenderPass::GetName() const noexcept -> std::string_view
{
  return GetComponent<ObjectMetaData>().GetName();
}

auto RenderPass::SetName(std::string_view name) noexcept -> void
{
  GetComponent<ObjectMetaData>().SetName(name);
}
auto RenderPass::Context() const -> const RenderContext&
{
  DCHECK_NOTNULL_F(context_);
  return *context_;
}

auto RenderPass::BindSceneConstantsBuffer(CommandRecorder& recorder) const
  -> void
{
  using graphics::DirectBufferBinding;

  DCHECK_NOTNULL_F(Context().scene_constants);
  DCHECK_F(LastBuiltPsoDesc().has_value());

  constexpr auto root_param_index
    = static_cast<std::span<const graphics::RootBindingItem>::size_type>(
      binding::RootParam::kSceneConstants);
  const auto& root_param = LastBuiltPsoDesc()->RootBindings()[root_param_index];

  DCHECK_F(std::holds_alternative<DirectBufferBinding>(root_param.data),
    "Expected root parameter {}'s data to be DirectBufferBinding",
    root_param_index);

  // Bind the buffer as a root CBV (direct GPU virtual address)
  recorder.SetGraphicsRootConstantBufferView(
    root_param.GetRootParameterIndex(), // should be binding 2 (b1, space0)
    Context().scene_constants->GetGPUVirtualAddress());
}

auto RenderPass::BindIndicesBuffer(CommandRecorder& recorder) const -> void
{
  // In the bindless rendering model, the indices buffer (DrawResourceIndices)
  // is already accessible through the descriptor table at heap index 0.
  // The shader accesses it via g_DrawResourceIndices[0] in space0.
  // No additional binding is required here.
  (void)recorder; // Suppress unused parameter warning
}

auto RenderPass::BindDrawIndexConstant(
  CommandRecorder& recorder, uint32_t draw_index) const -> void
{
  using graphics::PushConstantsBinding;

  DCHECK_F(LastBuiltPsoDesc().has_value());

  constexpr auto root_param_index
    = static_cast<std::span<const graphics::RootBindingItem>::size_type>(
      binding::RootParam::kDrawIndex);
  const auto& root_param = LastBuiltPsoDesc()->RootBindings()[root_param_index];

  DCHECK_F(std::holds_alternative<PushConstantsBinding>(root_param.data),
    "Expected root parameter {}'s data to be PushConstantsBinding",
    root_param_index);

  // Bind the draw index as a root constant (32-bit value)
  recorder.SetGraphicsRoot32BitConstant(
    root_param.GetRootParameterIndex(), // should be binding 3 for draw index
    draw_index,
    0); // offset within the constant (0 for single 32-bit value)
}

auto RenderPass::IssueDrawCalls(CommandRecorder& recorder) const -> bool
{
  // Returns true if any draws were emitted.
  const auto psf = Context().prepared_frame;
  if (!psf || !psf->IsValid() || psf->draw_metadata_bytes.empty()) {
    return false;
  }

  const auto count
    = psf->draw_metadata_bytes.size() / sizeof(engine::DrawMetadata);
  DLOG_F(2, "RenderPass SoA metadata available: records={}", count);
  const auto* records = reinterpret_cast<const engine::DrawMetadata*>(
    psf->draw_metadata_bytes.data());

  bool emitted = false;
  for (size_t draw_index = 0; draw_index < count; ++draw_index) {
    const auto& md = records[draw_index];

    if (md.is_indexed) {
      DCHECK_F(md.index_count > 0, "Indexed draw requires index_count > 0");
    } else {
      DCHECK_F(
        md.vertex_count > 0, "Non-indexed draw requires vertex_count > 0");
    }

    BindDrawIndexConstant(recorder, static_cast<uint32_t>(draw_index));

    recorder.Draw(md.is_indexed ? md.index_count : md.vertex_count,
      /*instanceCount*/ 1, /*firstVertex*/ 0,
      /*firstInstance*/ 0);

    emitted = true;
  }
  return emitted;
}
