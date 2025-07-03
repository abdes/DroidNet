//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <variant>

#include <wrl/client.h> // For Microsoft::WRL::ComPtr

#include <Oxygen/Base/VariantHelpers.h> // Added for Overloads
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Detail/Barriers.h>
#include <Oxygen/Graphics/Common/Types/Scissors.h>
#include <Oxygen/Graphics/Common/Types/ViewPort.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/DescriptorAllocator.h>
#include <Oxygen/Graphics/Direct3D12/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/CommandList.h>
#include <Oxygen/Graphics/Direct3D12/CommandRecorder.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Converters.h>
#include <Oxygen/Graphics/Direct3D12/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Direct3D12/Detail/WindowSurface.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>
#include <Oxygen/Graphics/Direct3D12/Framebuffer.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/RenderController.h>

using oxygen::Overloads;
using oxygen::graphics::d3d12::CommandRecorder;
using oxygen::graphics::detail::Barrier;
using oxygen::graphics::detail::BufferBarrierDesc;
using oxygen::graphics::detail::GetFormatInfo;
using oxygen::graphics::detail::MemoryBarrierDesc;
using oxygen::graphics::detail::TextureBarrierDesc;

namespace {

// Helper function to convert common ResourceStates to D3D12_RESOURCE_STATES
auto ConvertResourceStates(oxygen::graphics::ResourceStates common_states)
  -> D3D12_RESOURCE_STATES
{
  using oxygen::graphics::ResourceStates;

  DCHECK_F(common_states != ResourceStates::kUnknown,
    "Illegal `ResourceStates::kUnknown` encountered in barrier state mapping "
    "to D3D12.");

  // Handle specific, non-bitwise states first. Mixing these with other states
  // is meaningless and can lead to undefined behavior.
  if (common_states == ResourceStates::kUndefined) {
    // Typically initial state for many resources
    return D3D12_RESOURCE_STATE_COMMON;
  }
  if (common_states == ResourceStates::kPresent) {
    // For swap chain presentation
    return D3D12_RESOURCE_STATE_PRESENT;
  }
  if (common_states == ResourceStates::kCommon) {
    // Explicit request for D3D12 common state
    return D3D12_RESOURCE_STATE_COMMON;
  }

  D3D12_RESOURCE_STATES d3d_states = {};

  // Define a local capturing lambda to handle the mapping
  auto map_flag_if_present = [&](const ResourceStates flag_to_check,
                               const D3D12_RESOURCE_STATES d3d12_equivalent) {
    if ((common_states & flag_to_check) == flag_to_check) {
      d3d_states |= d3d12_equivalent;
    }
  };

  map_flag_if_present(ResourceStates::kBuildAccelStructureRead,
    D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
  map_flag_if_present(ResourceStates::kBuildAccelStructureWrite,
    D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
  map_flag_if_present(ResourceStates::kConstantBuffer,
    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
  map_flag_if_present(
    ResourceStates::kCopyDest, D3D12_RESOURCE_STATE_COPY_DEST);
  map_flag_if_present(
    ResourceStates::kCopySource, D3D12_RESOURCE_STATE_COPY_SOURCE);
  map_flag_if_present(
    ResourceStates::kDepthRead, D3D12_RESOURCE_STATE_DEPTH_READ);
  map_flag_if_present(
    ResourceStates::kDepthWrite, D3D12_RESOURCE_STATE_DEPTH_WRITE);
  map_flag_if_present(
    ResourceStates::kGenericRead, D3D12_RESOURCE_STATE_GENERIC_READ);
  map_flag_if_present(
    ResourceStates::kIndexBuffer, D3D12_RESOURCE_STATE_INDEX_BUFFER);
  map_flag_if_present(
    ResourceStates::kIndirectArgument, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
  map_flag_if_present(ResourceStates::kInputAttachment,
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  map_flag_if_present(ResourceStates::kRayTracing,
    D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
  map_flag_if_present(
    ResourceStates::kRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
  map_flag_if_present(
    ResourceStates::kResolveDest, D3D12_RESOURCE_STATE_RESOLVE_DEST);
  map_flag_if_present(
    ResourceStates::kResolveSource, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
  map_flag_if_present(ResourceStates::kShaderResource,
    (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
      | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
  map_flag_if_present(
    ResourceStates::kShadingRate, D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE);
  map_flag_if_present(
    ResourceStates::kStreamOut, D3D12_RESOURCE_STATE_STREAM_OUT);
  map_flag_if_present(
    ResourceStates::kUnorderedAccess, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  map_flag_if_present(ResourceStates::kVertexBuffer,
    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

  if (d3d_states == 0) {
    DLOG_F(WARNING,
      "ResourceStates ({:#X}) did not map to any specific D3D12 states; "
      "falling back to D3D12_RESOURCE_STATE_COMMON.",
      static_cast<std::underlying_type_t<ResourceStates>>(common_states));
    return D3D12_RESOURCE_STATE_COMMON;
  }

  return d3d_states;
}

// Static helper functions to process specific barrier types
auto ProcessBarrierDesc(const BufferBarrierDesc& desc) -> D3D12_RESOURCE_BARRIER
{
  DLOG_F(4, ". buffer barrier: {} {} -> {}", nostd::to_string(desc.resource),
    nostd::to_string(desc.before), nostd::to_string(desc.after));

  auto* p_resource = desc.resource.AsPointer<ID3D12Resource>();
  DCHECK_NOTNULL_F(
    p_resource, "Transition barrier (Buffer) cannot have a null resource.");

  const D3D12_RESOURCE_BARRIER d3d12_barrier { .Type
    = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
    .Transition = { .pResource = p_resource,
      .Subresource
      = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, // TODO: Or specific
                                                 // sub-resource if provided
      .StateBefore = ConvertResourceStates(desc.before),
      .StateAfter = ConvertResourceStates(desc.after) } };
  return d3d12_barrier;
}

auto ProcessBarrierDesc(const TextureBarrierDesc& desc)
  -> D3D12_RESOURCE_BARRIER
{
  DLOG_F(4, ". texture barrier: {} {} -> {}", nostd::to_string(desc.resource),
    nostd::to_string(desc.before), nostd::to_string(desc.after));

  auto* p_resource = desc.resource.AsPointer<ID3D12Resource>();
  DCHECK_NOTNULL_F(
    p_resource, "Transition barrier (Texture) cannot have a null resource.");

  const D3D12_RESOURCE_BARRIER d3d12_barrier { .Type
    = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
    .Transition = { .pResource = p_resource,
      .Subresource
      = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, // TODO: Or specific
                                                 // sub-resource if provided
      .StateBefore = ConvertResourceStates(desc.before),
      .StateAfter = ConvertResourceStates(desc.after) } };
  return d3d12_barrier;
}

auto ProcessBarrierDesc(const MemoryBarrierDesc& desc) -> D3D12_RESOURCE_BARRIER
{
  DLOG_F(4, ". memory barrier: 0x{:X}", nostd::to_string(desc.resource));

  const D3D12_RESOURCE_BARRIER d3d12_barrier
    = { .Type = D3D12_RESOURCE_BARRIER_TYPE_UAV,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .UAV = {
          .pResource = desc.resource.AsPointer<ID3D12Resource>() != nullptr
            ? desc.resource.AsPointer<ID3D12Resource>()
            : nullptr,
        } };
  return d3d12_barrier;
}

} // anonymous namespace

CommandRecorder::CommandRecorder(RenderController* renderer,
  graphics::CommandList* command_list, graphics::CommandQueue* target_queue)
  : Base(command_list, target_queue)
  , renderer_(renderer)
{
  DCHECK_NOTNULL_F(renderer, "RenderController cannot be null");
}

CommandRecorder::~CommandRecorder()
{
  DCHECK_NOTNULL_F(renderer_, "RenderController cannot be null");
}

void CommandRecorder::Begin() { graphics::CommandRecorder::Begin(); }

auto CommandRecorder::End() -> graphics::CommandList*
{
  return graphics::CommandRecorder::End();
}

namespace {
// Modern bindless root signature layout:
// Root Param 0: Single unbounded SRV descriptor table (t0, space0)
// Root Param 1: Direct CBV for SceneConstants (b1, space0)
// Root Param 2: Direct CBV for MaterialConstants (b2, space0) - graphics only
constexpr UINT kRootIndex_UnboundedSRV_Table = 0;
constexpr UINT kRootIndex_SceneConstants_CBV = 1;
constexpr UINT kRootIndex_MaterialConstants_CBV = 2;
} // namespace

void CommandRecorder::SetupDescriptorTables(
  const std::span<const detail::ShaderVisibleHeapInfo> heaps) const
{
  // Modern bindless approach: Bind the single unbounded SRV descriptor table.
  // The heap(s) bound here must be the same as those used to allocate CBV/SRV
  // handles in MainModule.cpp. This ensures the shader can access resources
  // using ResourceDescriptorHeap with direct global indices.
  auto* d3d12_command_list = GetConcreteCommandList()->GetCommandList();
  DCHECK_NOTNULL_F(d3d12_command_list);

  auto queue_role = GetCommandList()->GetQueueRole();
  DCHECK_F(
    queue_role == QueueRole::kGraphics || queue_role == QueueRole::kCompute,
    "Invalid command list type for SetupDescriptorTables. Expected Graphics or "
    "Compute, got: {}",
    static_cast<int>(queue_role));

  std::vector<ID3D12DescriptorHeap*> heaps_to_set;
  // Collect all unique heaps first to call SetDescriptorHeaps once.
  // This assumes that if CBV_SRV_UAV and Sampler heaps are different, they are
  // distinct. If they could be the same heap object (not typical for D3D12
  // types), logic would need adjustment.
  for (const auto& heap_info : heaps) {
    bool found = false;
    for (const ID3D12DescriptorHeap* existing_heap : heaps_to_set) {
      if (existing_heap == heap_info.heap) {
        found = true;
        break;
      }
    }
    if (!found) {
      heaps_to_set.push_back(heap_info.heap);
    }
  }

  if (!heaps_to_set.empty()) {
    DLOG_F(2, "recorder: set {} descriptor heaps for command list: {}",
      heaps_to_set.size(), GetConcreteCommandList()->GetName());
    d3d12_command_list->SetDescriptorHeaps(
      static_cast<UINT>(heaps_to_set.size()), heaps_to_set.data());
  }

  auto set_table = [this, d3d12_command_list, queue_role](const UINT root_index,
                     const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) {
    if (queue_role == QueueRole::kGraphics) {
      DLOG_F(0,
        "recorder: SetGraphicsRootDescriptorTable for command list: {}, root "
        "index={}, gpu_handle={}",
        GetConcreteCommandList()->GetName(), root_index, gpu_handle.ptr);
      d3d12_command_list->SetGraphicsRootDescriptorTable(
        root_index, gpu_handle);
    } else if (queue_role == QueueRole::kCompute) {
      d3d12_command_list->SetComputeRootDescriptorTable(root_index, gpu_handle);
    }
  };

  for (const auto& heap_info : heaps) {
    if (heap_info.heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
      // Bind the single unbounded SRV descriptor table
      // The shader uses ResourceDescriptorHeap to access all resources by
      // global index
      set_table(kRootIndex_UnboundedSRV_Table, heap_info.gpu_handle);

    } else if (heap_info.heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
      // Note: Current root signature doesn't include sampler tables
      // If samplers are needed, they would need to be added to the root
      // signature
      DLOG_F(WARNING,
        "Sampler descriptor heap detected but no sampler table in root "
        "signature");
      continue;
    } else {
      DLOG_F(WARNING,
        "Unsupported descriptor heap type for root table binding: {}",
        static_cast<std::underlying_type_t<D3D12_DESCRIPTOR_HEAP_TYPE>>(
          heap_info.heap_type));
      continue;
    }
  }
}

void CommandRecorder::SetViewport(const ViewPort& viewport)
{
  const auto* command_list = GetConcreteCommandList();
  DCHECK_NOTNULL_F(command_list);
  DCHECK_EQ_F(
    command_list->GetQueueRole(), QueueRole::kGraphics, "Invalid queue type");

  D3D12_VIEWPORT d3d_viewport;
  d3d_viewport.TopLeftX = viewport.top_left_x;
  d3d_viewport.TopLeftY = viewport.top_left_y;
  d3d_viewport.Width = viewport.width;
  d3d_viewport.Height = viewport.height;
  d3d_viewport.MinDepth = viewport.min_depth;
  d3d_viewport.MaxDepth = viewport.max_depth;

  command_list->GetCommandList()->RSSetViewports(1, &d3d_viewport);
}

void CommandRecorder::SetScissors(const Scissors& scissors)
{
  const auto* command_list = GetConcreteCommandList();
  DCHECK_NOTNULL_F(command_list);

  D3D12_RECT rect;
  rect.left = scissors.left;
  rect.top = scissors.top;
  rect.right = scissors.right;
  rect.bottom = scissors.bottom;
  command_list->GetCommandList()->RSSetScissorRects(1, &rect);
}

void CommandRecorder::SetVertexBuffers(const uint32_t num,
  const std::shared_ptr<graphics::Buffer>* vertex_buffers,
  const uint32_t* strides) const
{
  const auto* command_list = GetConcreteCommandList();
  DCHECK_NOTNULL_F(command_list);
  DCHECK_EQ_F(
    command_list->GetQueueRole(), QueueRole::kGraphics, "Invalid queue type");

  std::vector<D3D12_VERTEX_BUFFER_VIEW> vertex_buffer_views(num);
  for (uint32_t i = 0; i < num; ++i) {
    const auto buffer = std::static_pointer_cast<Buffer>(vertex_buffers[i]);
    vertex_buffer_views[i].BufferLocation
      = buffer->GetResource()->GetGPUVirtualAddress();
    vertex_buffer_views[i].SizeInBytes = static_cast<UINT>(buffer->GetSize());
    vertex_buffer_views[i].StrideInBytes = strides[i];
  }

  command_list->GetCommandList()->IASetVertexBuffers(
    0, num, vertex_buffer_views.data());
}

void CommandRecorder::Draw(const uint32_t vertex_num,
  const uint32_t instances_num, const uint32_t vertex_offset,
  const uint32_t instance_offset)
{
  const auto* command_list = GetConcreteCommandList();
  DCHECK_NOTNULL_F(command_list);
  DCHECK_EQ_F(
    command_list->GetQueueRole(), QueueRole::kGraphics, "Invalid queue type");

  // Prepare for Draw
  command_list->GetCommandList()->IASetPrimitiveTopology(
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  command_list->GetCommandList()->DrawInstanced(
    vertex_num, instances_num, vertex_offset, instance_offset);
}

void CommandRecorder::DrawIndexed(uint32_t index_count, uint32_t instance_count,
  uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
{
  const auto* command_list = GetConcreteCommandList();
  DCHECK_NOTNULL_F(command_list);
  DCHECK_EQ_F(
    command_list->GetQueueRole(), QueueRole::kGraphics, "Invalid queue type");

  // Prepare for DrawIndexed
  command_list->GetCommandList()->IASetPrimitiveTopology(
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  command_list->GetCommandList()->DrawIndexedInstanced(
    index_count, instance_count, first_index, vertex_offset, first_instance);
}

void CommandRecorder::Dispatch(uint32_t thread_group_count_x,
  uint32_t thread_group_count_y, uint32_t thread_group_count_z)
{
  const auto* command_list = GetConcreteCommandList();
  DCHECK_NOTNULL_F(command_list);
  DCHECK_EQ_F(
    command_list->GetQueueRole(), QueueRole::kCompute, "Invalid queue type");

  command_list->GetCommandList()->Dispatch(
    thread_group_count_x, thread_group_count_y, thread_group_count_z);
}

/**
 * @brief Describes the root signature requirements for descriptor heaps in
 * D3D12.
 *
 * In Direct3D 12, root signatures define how resources are bound to the GPU
 * pipeline. When using descriptor heaps, especially in bindless rendering
 * scenarios, the following requirements apply:
 *
 * - Each root signature should contain **only one descriptor table per
 *   descriptor heap type**. This includes:
 *   - One descriptor table for `D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV`
 *   - One descriptor table for `D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER`
 *
 * - This restriction exists because only one descriptor heap of each type can
 *   be bound at a time using `ID3D12GraphicsCommandList::SetDescriptorHeaps`.
 *   Having multiple descriptor tables for the same heap type in a root
 *   signature is not allowed, as it would create ambiguity in shader resource
 *   binding.
 *
 * - RTV (Render Target View) and DSV (Depth Stencil View) heaps are not
 *   shader-visible and are not included in the root signature.
 */

//! Create a bindless root signature for graphics or compute pipelines
// Modern bindless root signature layout:
//   - Root Parameter 0: Single unbounded SRV descriptor table (t0, space0)
//   - Root Parameter 1: Direct CBV for SceneConstants (b1, space0)
//   - Root Parameter 2: Direct CBV for MaterialConstants (b2, space0) -
//   graphics only
// The flag D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED is set
// for true bindless access. This layout must match the expectations of both the
// engine and the shaders (see DepthPrePass.hlsl and FullScreenTriangle.hlsl).
auto CommandRecorder::CreateBindlessRootSignature(const bool is_graphics) const
  -> dx::IRootSignature*
{
  const size_t num_cbv_params
    = is_graphics ? 2 : 1; // SceneConstants + MaterialConstants (graphics only)
  const size_t num_root_params = 1 + num_cbv_params; // 1 SRV table + CBV(s)

  std::vector<D3D12_ROOT_PARAMETER> root_params(num_root_params);
  D3D12_DESCRIPTOR_RANGE srv_range {};

  // Single unbounded SRV range for all resources (t0, space0)
  srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srv_range.NumDescriptors = UINT_MAX; // Unbounded
  srv_range.BaseShaderRegister = 0; // t0
  srv_range.RegisterSpace = 0; // space0
  srv_range.OffsetInDescriptorsFromTableStart = 0;

  // Root Parameter 0: Single unbounded SRV descriptor table (t0, space0)
  root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root_params[0].DescriptorTable.NumDescriptorRanges = 1;
  root_params[0].DescriptorTable.pDescriptorRanges = &srv_range;
  root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Root Parameter 1: Direct CBV for SceneConstants (b1, space0)
  root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  root_params[1].Descriptor.ShaderRegister = 1; // b1
  root_params[1].Descriptor.RegisterSpace = 0; // space0
  root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Root Parameter 2: Direct CBV for MaterialConstants (b2, space0) - graphics
  // only
  if (is_graphics) {
    root_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_params[2].Descriptor.ShaderRegister = 2; // b2
    root_params[2].Descriptor.RegisterSpace = 0; // space0
    root_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }

  const D3D12_ROOT_SIGNATURE_DESC root_sig_desc {
    .NumParameters = static_cast<UINT>(root_params.size()),
    .pParameters = root_params.data(),
    .NumStaticSamplers = 0,
    .pStaticSamplers = nullptr,
    .Flags = (is_graphics
                 ? D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
                 : D3D12_ROOT_SIGNATURE_FLAG_NONE)
      | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED,
  };

  Microsoft::WRL::ComPtr<ID3DBlob> sig_blob, err_blob;
  HRESULT hr = D3D12SerializeRootSignature(
    &root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &sig_blob, &err_blob);
  if (FAILED(hr)) {
    std::string error_msg = "Failed to serialize root signature: ";
    if (err_blob) {
      error_msg += static_cast<const char*>(err_blob->GetBufferPointer());
    }
    throw std::runtime_error(error_msg);
  }

  dx::IRootSignature* root_sig = nullptr;
  auto* device = renderer_->GetGraphics().GetCurrentDevice();
  hr = device->CreateRootSignature(0, sig_blob->GetBufferPointer(),
    sig_blob->GetBufferSize(), IID_PPV_ARGS(&root_sig));
  if (FAILED(hr)) {
    throw std::runtime_error("Failed to create root signature");
  }

  return root_sig;
}

void CommandRecorder::SetPipelineState(GraphicsPipelineDesc desc)
{
  DCHECK_NOTNULL_F(renderer_);

  const auto debug_name = desc.GetName(); // Save before moving desc
  graphics_pipeline_hash_ = std::hash<GraphicsPipelineDesc> {}(desc);

  auto [pipeline_state, root_signature]
    = renderer_->GetOrCreateGraphicsPipeline(
      std::move(desc), graphics_pipeline_hash_);
  DCHECK_NOTNULL_F(pipeline_state);
  DCHECK_NOTNULL_F(root_signature);

  const auto* command_list = GetConcreteCommandList();
  DCHECK_NOTNULL_F(command_list);
  auto* d3d12_command_list = command_list->GetCommandList();

  d3d12_command_list->SetGraphicsRootSignature(root_signature);
  // NOLINTNEXTLINE(*-pro-type-static-cast-downcast)
  auto& allocator
    = static_cast<DescriptorAllocator&>(renderer_->GetDescriptorAllocator());
  SetupDescriptorTables(allocator.GetShaderVisibleHeaps());

  d3d12_command_list->SetPipelineState(pipeline_state);
}

void CommandRecorder::SetPipelineState(ComputePipelineDesc desc)
{
  DCHECK_NOTNULL_F(renderer_);

  const auto debug_name = desc.GetName(); // Save before moving desc
  compute_pipeline_hash_ = std::hash<ComputePipelineDesc> {}(desc);

  auto [pipeline_state, root_signature] = renderer_->GetOrCreateComputePipeline(
    std::move(desc), compute_pipeline_hash_);
  DCHECK_NOTNULL_F(pipeline_state);
  DCHECK_NOTNULL_F(root_signature);

  const auto* command_list = GetConcreteCommandList();
  DCHECK_NOTNULL_F(command_list);
  auto* d3d12_command_list = command_list->GetCommandList();

  d3d12_command_list->SetGraphicsRootSignature(root_signature);
  // NOLINTNEXTLINE(*-pro-type-static-cast-downcast)
  auto& allocator
    = static_cast<DescriptorAllocator&>(renderer_->GetDescriptorAllocator());
  SetupDescriptorTables(allocator.GetShaderVisibleHeaps());

  // Name them for debugging

  d3d12_command_list->SetPipelineState(pipeline_state);
}

void CommandRecorder::SetGraphicsRootConstantBufferView(
  uint32_t root_parameter_index, uint64_t buffer_gpu_address)
{
  const auto* command_list = GetConcreteCommandList();
  DCHECK_NOTNULL_F(command_list);
  auto* d3d12_command_list = command_list->GetCommandList();
  d3d12_command_list->SetGraphicsRootConstantBufferView(
    root_parameter_index, buffer_gpu_address);
}

void CommandRecorder::SetComputeRootConstantBufferView(
  uint32_t root_parameter_index, uint64_t buffer_gpu_address)
{
  const auto* command_list = GetConcreteCommandList();
  DCHECK_NOTNULL_F(command_list);
  auto* d3d12_command_list = command_list->GetCommandList();
  d3d12_command_list->SetComputeRootConstantBufferView(
    root_parameter_index, buffer_gpu_address);
}

void CommandRecorder::ExecuteBarriers(const std::span<const Barrier> barriers)
{
  if (barriers.empty()) {
    return;
  }

  const auto* command_list_impl = GetConcreteCommandList();
  DCHECK_NOTNULL_F(command_list_impl);
  auto* d3d12_command_list = command_list_impl->GetCommandList();
  DCHECK_NOTNULL_F(d3d12_command_list);

  std::vector<D3D12_RESOURCE_BARRIER> d3d12_barriers;
  d3d12_barriers.reserve(barriers.size());

  DLOG_F(4, "executing {} barriers", barriers.size());

  for (const auto& barrier : barriers) {
    const auto& desc_variant = barrier.GetDescriptor();
    d3d12_barriers.push_back(std::visit(
      Overloads {
        [](const BufferBarrierDesc& desc) { return ProcessBarrierDesc(desc); },
        [](const TextureBarrierDesc& desc) { return ProcessBarrierDesc(desc); },
        [](const MemoryBarrierDesc& desc) { return ProcessBarrierDesc(desc); },
      },
      desc_variant));
  }

  if (!d3d12_barriers.empty()) {
    d3d12_command_list->ResourceBarrier(
      static_cast<UINT>(d3d12_barriers.size()), d3d12_barriers.data());
  }
}

auto CommandRecorder::GetConcreteCommandList() const -> CommandList*
{
  // NOLINTNEXTLINE(*-pro-type-static-cast_down_cast)
  return static_cast<CommandList*>(
    GetCommandList()); // NOLINT(*-pro-type-static-cast_down_cast)
}

// TODO: legacy - should be replaced once render passes are implemented
void CommandRecorder::BindFrameBuffer(const graphics::Framebuffer& framebuffer)
{
  // NOLINTNEXTLINE(*-pro-type-static-cast_down_cast)
  const auto& fb = static_cast<const Framebuffer&>(framebuffer);
  StaticVector<D3D12_CPU_DESCRIPTOR_HANDLE, kMaxRenderTargets> rtvs;
  for (const auto& rtv : fb.GetRenderTargetViews()) {
    rtvs.emplace_back(rtv);
  }

  D3D12_CPU_DESCRIPTOR_HANDLE dsv = {};
  if (fb.GetDescriptor().depth_attachment.IsValid()) {
    dsv.ptr = fb.GetDepthStencilView();
  }

  const auto* command_list_impl = GetConcreteCommandList();
  DCHECK_NOTNULL_F(command_list_impl);
  auto* d3d12_command_list = command_list_impl->GetCommandList();
  DCHECK_NOTNULL_F(d3d12_command_list);

  d3d12_command_list->OMSetRenderTargets(static_cast<UINT>(rtvs.size()),
    rtvs.data(), 0,
    fb.GetDescriptor().depth_attachment.IsValid() ? &dsv : nullptr);
}

void CommandRecorder::ClearFramebuffer(const graphics::Framebuffer& framebuffer,
  const std::optional<std::vector<std::optional<Color>>> color_clear_values,
  const std::optional<float> depth_clear_value,
  const std::optional<uint8_t> stencil_clear_value)
{
  using d3d12::Framebuffer;
  using graphics::detail::GetFormatInfo;

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  const auto& fb = static_cast<const Framebuffer&>(framebuffer);

  const auto* command_list_impl = GetConcreteCommandList();
  DCHECK_NOTNULL_F(command_list_impl);
  auto* d3d12_command_list = command_list_impl->GetCommandList();
  DCHECK_NOTNULL_F(d3d12_command_list);

  const auto& desc = fb.GetDescriptor();

  // Clear color attachments
  const auto rtvs = fb.GetRenderTargetViews();
  for (size_t i = 0; i < rtvs.size(); ++i) {
    const auto& attachment = desc.color_attachments[i];
    const auto& format_info = GetFormatInfo(attachment.format);
    if (format_info.has_depth || format_info.has_stencil) {
      continue;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle { .ptr = rtvs[i] };
    const Color clear_color = attachment.ResolveClearColor(
      color_clear_values && i < color_clear_values->size()
        ? (*color_clear_values)[i]
        : std::nullopt);
    const float color[4]
      = { clear_color.r, clear_color.g, clear_color.b, clear_color.a };
    d3d12_command_list->ClearRenderTargetView(rtv_handle, color, 0, nullptr);
  }

  // Clear depth/stencil attachment
  if (desc.depth_attachment.IsValid()) {
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle { .ptr
      = fb.GetDepthStencilView() };
    const auto& depth_format_info = GetFormatInfo(desc.depth_attachment.format);

    auto [depth, stencil] = desc.depth_attachment.ResolveDepthStencil(
      depth_clear_value, stencil_clear_value);

    auto clear_flags = static_cast<D3D12_CLEAR_FLAGS>(0);
    if (depth_format_info.has_depth) {
      clear_flags = clear_flags | D3D12_CLEAR_FLAG_DEPTH;
    }
    if (depth_format_info.has_stencil) {
      clear_flags = clear_flags | D3D12_CLEAR_FLAG_STENCIL;
    }

    if (clear_flags != 0) {
      d3d12_command_list->ClearDepthStencilView(
        dsv_handle, clear_flags, depth, stencil, 0, nullptr);
    }
  }
}

void CommandRecorder::CopyBuffer(graphics::Buffer& dst, const size_t dst_offset,
  const graphics::Buffer& src, const size_t src_offset, const size_t size)
{
  // Expectations:
  // - src must be in D3D12_RESOURCE_STATE_COPY_SOURCE
  // - dst must be in D3D12_RESOURCE_STATE_COPY_DEST
  // The caller is responsible for ensuring correct resource states.

  // NOLINTBEGIN(cppcoreguidelines-pro-type-static-cast-downcast)
  const auto& dst_buffer = static_cast<Buffer&>(dst);
  const auto& src_buffer = static_cast<const Buffer&>(src);
  // NOLINTEND(cppcoreguidelines-pro-type-static-cast-downcast)

  auto* dst_resource = dst_buffer.GetResource();
  auto* src_resource = src_buffer.GetResource();

  DCHECK_NOTNULL_F(dst_resource, "Destination buffer resource is null");
  DCHECK_NOTNULL_F(src_resource, "Source buffer resource is null");

  // D3D12 requires that the copy region does not exceed the buffer bounds.
  DCHECK_F(dst_offset + size <= dst_buffer.GetSize(),
    "CopyBuffer: dst_offset + size ({}) exceeds destination buffer size ({})",
    dst_offset + size, dst_buffer.GetSize());
  DCHECK_F(src_offset + size <= src_buffer.GetSize(),
    "CopyBuffer: src_offset + size ({}) exceeds source buffer size ({})",
    src_offset + size, src_buffer.GetSize());

  const auto* command_list = GetConcreteCommandList();
  DCHECK_NOTNULL_F(command_list);
  auto* d3d12_command_list = command_list->GetCommandList();
  DCHECK_NOTNULL_F(d3d12_command_list);

  d3d12_command_list->CopyBufferRegion(
    dst_resource, dst_offset, src_resource, src_offset, size);
}

// D3D12 specific command implementations
void CommandRecorder::ClearDepthStencilView(const graphics::Texture& texture,
  const NativeObject& dsv, const ClearFlags clear_flags, const float depth,
  const uint8_t stencil)
{
  const auto* command_list_impl = GetConcreteCommandList();
  DCHECK_NOTNULL_F(command_list_impl);
  auto* d3d12_command_list = command_list_impl->GetCommandList();
  DCHECK_NOTNULL_F(d3d12_command_list);

  // Resolve the clear values for depth and stencil using the texture format.
  const auto& depth_tex_desc = texture.GetDescriptor();
  const auto format_info = GetFormatInfo(depth_tex_desc.format);
  const float clear_depth
    = (depth_tex_desc.use_clear_value && format_info.has_depth)
    ? depth_tex_desc.clear_value.r
    : depth;
  const uint8_t clear_stencil
    = (depth_tex_desc.use_clear_value && format_info.has_stencil)
    ? static_cast<uint8_t>(
        depth_tex_desc.clear_value.g) // Assuming stencil is in .g
    : stencil;

  const auto d3d12_clear_flags = detail::ConvertClearFlags(clear_flags);
  const D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle { .ptr = dsv.AsInteger() };
  d3d12_command_list->ClearDepthStencilView(
    dsv_handle, d3d12_clear_flags, clear_depth, clear_stencil,
    0, // NumRects
    nullptr // pRects
  );
}

void CommandRecorder::SetRenderTargets(
  const std::span<NativeObject> rtvs, const std::optional<NativeObject> dsv)
{
  DCHECK_F(!rtvs.empty() || dsv.has_value(),
    "At least one render target must be specified.");
  DCHECK_LE_F(rtvs.size(), kMaxRenderTargets,
    "Too many render targets specified. Maximum is {}.", kMaxRenderTargets);

  std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtv_handles;
  rtv_handles.reserve(rtvs.size());
  for (const auto& rtv : rtvs) {
    if (!rtv.IsValid()) {
      LOG_F(ERROR, "invalid render target view: {} view, skipped",
        nostd::to_string(rtv));
      continue; // Skip invalid RTVs
    }
    rtv_handles.push_back({ .ptr = rtv.AsInteger() });
  }

  const D3D12_CPU_DESCRIPTOR_HANDLE* dsv_handle_ptr { nullptr };
  if (dsv.has_value()) {
    if (!dsv->IsValid()) {
      LOG_F(ERROR, "invalid depth/stencil view: {}, dropped",
        nostd::to_string(*dsv));
    } else {
      D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle { .ptr = dsv->AsInteger() };
      dsv_handle_ptr = &dsv_handle;
    }
  }

  const auto* command_list_impl = GetConcreteCommandList();
  DCHECK_NOTNULL_F(command_list_impl);
  auto* d3d12_command_list = command_list_impl->GetCommandList();
  DCHECK_NOTNULL_F(d3d12_command_list);

  d3d12_command_list->OMSetRenderTargets(static_cast<UINT>(rtv_handles.size()),
    rtv_handles.data(), dsv_handle_ptr != nullptr ? 1 : 0, dsv_handle_ptr);
}

void CommandRecorder::BindIndexBuffer(
  const graphics::Buffer& buffer, Format format)
{
  const auto* command_list = GetConcreteCommandList();
  DCHECK_NOTNULL_F(command_list);
  DCHECK_EQ_F(
    command_list->GetQueueRole(), QueueRole::kGraphics, "Invalid queue type");

  const auto* d3d12_buffer = static_cast<const d3d12::Buffer*>(&buffer);
  DCHECK_NOTNULL_F(d3d12_buffer, "Buffer must be a D3D12 buffer");

  D3D12_INDEX_BUFFER_VIEW ib_view = {};
  ib_view.BufferLocation = d3d12_buffer->GetResource()->GetGPUVirtualAddress();
  ib_view.SizeInBytes = static_cast<UINT>(d3d12_buffer->GetSize());
  switch (format) {
  case Format::kR16UInt:
    ib_view.Format = DXGI_FORMAT_R16_UINT;
    break;
  case Format::kR32UInt:
    ib_view.Format = DXGI_FORMAT_R32_UINT;
    break;
  default:
    DCHECK_F(false, "Unsupported index buffer format");
    ib_view.Format = DXGI_FORMAT_UNKNOWN;
    break;
  }
  command_list->GetCommandList()->IASetIndexBuffer(&ib_view);
}
