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
#include <Oxygen/Renderer/Detail/ContextScope.h>
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
  detail::ContextScope ctx_scope(context_, context);

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

  detail::ContextScope ctx_scope(context_, context);

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
  for (const auto& item : GetDrawList()) {
    if (!item.mesh || item.mesh->VertexCount() == 0) {
      LOG_F(INFO, "Skipping RenderItem with no mesh or no vertices.");
      continue;
    }

    // Use cached vertex buffer from renderer
    const auto vertex_buffer
      = context.GetRenderer().GetVertexBuffer(*item.mesh);
    if (!vertex_buffer) {
      LOG_F(WARNING, "Could not get the vertex buffer for mesh {}. Skipping.",
        item.mesh.get()->GetName());
      continue;
    }

    const std::shared_ptr<Buffer> buffer_array[1] = { vertex_buffer };
    constexpr uint32_t stride_array[1]
      = { static_cast<uint32_t>(sizeof(Vertex)) };
    command_recorder.SetVertexBuffers(1, buffer_array, stride_array);

    // In bindless rendering, both vertex and index buffers are accessed through
    // SRVs The shader handles index lookups internally, so we always use Draw
    // with IndexCount for indexed meshes (the shader reads indices from the
    // bindless index buffer)
    if (item.mesh->IsIndexed()) {
      // Draw with the number of indices (shader will resolve vertex indices
      // internally)
      command_recorder.Draw(
        static_cast<uint32_t>(item.mesh->IndexCount()), 1, 0, 0);
    } else {
      // Draw with the number of vertices for non-indexed meshes
      command_recorder.Draw(
        static_cast<uint32_t>(item.mesh->VertexCount()), 1, 0, 0);
    }
  }
}
