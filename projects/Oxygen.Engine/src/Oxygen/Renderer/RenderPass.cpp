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
#include <Oxygen/Renderer/RenderItem.h>
#include <Oxygen/Renderer/RenderPass.h>
#include <Oxygen/Renderer/Renderer.h>

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
      RootBindings::kSceneConstantsCbv);
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

auto RenderPass::BindMaterialConstantsBuffer(CommandRecorder& recorder) const
  -> void
{
  using graphics::DirectBufferBinding;

  // Material constants buffer is optional, so check if it exists
  if (!Context().material_constants) {
    // If no material constants buffer is available, we can skip binding
    // The shader should handle this case gracefully or use default values
    return;
  }

  DCHECK_F(LastBuiltPsoDesc().has_value());

  constexpr auto root_param_index
    = static_cast<std::span<const graphics::RootBindingItem>::size_type>(
      RootBindings::kMaterialConstantsCbv);
  const auto& root_param = LastBuiltPsoDesc()->RootBindings()[root_param_index];

  DCHECK_F(std::holds_alternative<DirectBufferBinding>(root_param.data),
    "Expected root parameter {}'s data to be DirectBufferBinding",
    root_param_index);

  // Bind the buffer as a root CBV (direct GPU virtual address)
  recorder.SetGraphicsRootConstantBufferView(
    root_param.GetRootParameterIndex(), // should be binding 3 (b2, space0)
    Context().material_constants->GetGPUVirtualAddress());
}

auto RenderPass::BindDrawIndexConstant(
  CommandRecorder& recorder, uint32_t draw_index) const -> void
{
  using graphics::PushConstantsBinding;

  DCHECK_F(LastBuiltPsoDesc().has_value());

  constexpr auto root_param_index
    = static_cast<std::span<const graphics::RootBindingItem>::size_type>(
      RootBindings::kDrawIndexConstant);
  const auto& root_param = LastBuiltPsoDesc()->RootBindings()[root_param_index];

  DCHECK_F(std::holds_alternative<PushConstantsBinding>(root_param.data),
    "Expected root parameter {}'s data to be PushConstantsBinding",
    root_param_index);

  // Bind the draw index as a root constant (32-bit value)
  recorder.SetGraphicsRoot32BitConstant(
    root_param.GetRootParameterIndex(), // should be binding 4 for draw index
    draw_index,
    0); // offset within the constant (0 for single 32-bit value)
}

auto RenderPass::IssueDrawCalls(CommandRecorder& command_recorder) const -> void
{
  using data::Vertex;
  using graphics::Buffer;

  // Note on D3D12 Upload Heap Resource States:
  //
  // Buffers created on D3D12_HEAP_TYPE_UPLOAD (like these temporary vertex
  // buffers) are typically implicitly in a state
  // (D3D12_RESOURCE_STATE_GENERIC_READ) that allows them to be read by the GPU
  // after CPU writes without explicit state transition barriers. Thus, explicit
  // CommandRecorder::RequireResourceState calls are often not strictly needed
  // for these specific transient resources on D3D12.

  const auto& context = Context();
  const auto& draw_list = GetDrawList();
  LOG_F(3, "DEBUG: Processing {} items in draw list", draw_list.size());

  uint32_t draw_index = 0;
  for (const auto& item : draw_list) {
    LOG_F(3, "DEBUG: Processing item {} with mesh: {}", draw_index,
      item.mesh ? "valid" : "null");
    if (!item.mesh || item.mesh->VertexCount() == 0) {
      LOG_F(3, "Skipping RenderItem with no mesh or no vertices.");
      continue;
    }

    // Use cached vertex buffer from renderer for each individual mesh
    const auto vertex_buffer
      = context.GetRenderer().GetVertexBuffer(*item.mesh);
    if (!vertex_buffer) {
      LOG_F(WARNING, "Could not get the vertex buffer for mesh {}. Skipping.",
        item.mesh.get()->GetName());
      continue;
    }

    // For separate meshes, bind the draw index as a root constant
    // This allows each draw call to access the correct entry in
    // DrawResourceIndices array
    BindDrawIndexConstant(command_recorder, draw_index);

    if (item.mesh->IsIndexed()) {
      // For indexed meshes in bindless rendering, use Draw with index count
      // The shader will perform index lookup through ResourceDescriptorHeap
      const auto index_count = static_cast<uint32_t>(item.mesh->IndexCount());
      LOG_F(3,
        "Draw call {} (indexed): indices={}, instances=1, firstVertex=0, "
        "drawIndex={}",
        draw_index, index_count, draw_index);
      // Use normal Draw call since draw index is now passed via root constant
      command_recorder.Draw(index_count, 1, 0, 0);
    } else {
      // For non-indexed meshes, use Draw with vertex count
      const auto vertex_count = static_cast<uint32_t>(item.mesh->VertexCount());
      LOG_F(3,
        "Draw call {} (non-indexed): vertices={}, instances=1, firstVertex=0, "
        "drawIndex={}",
        draw_index, vertex_count, draw_index);
      // Use normal Draw call since draw index is now passed via root constant
      command_recorder.Draw(vertex_count, 1, 0, 0);
    }
    ++draw_index;
  }
}
