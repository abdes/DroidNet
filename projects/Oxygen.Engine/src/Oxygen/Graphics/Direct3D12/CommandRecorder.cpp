//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <variant>

#include <wrl/client.h> // For Microsoft::WRL::ComPtr

#include <Oxygen/Base/VariantHelpers.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Detail/Barriers.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/DescriptorAllocator.h>
#include <Oxygen/Graphics/Direct3D12/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/CommandList.h>
#include <Oxygen/Graphics/Direct3D12/CommandRecorder.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Converters.h>
#include <Oxygen/Graphics/Direct3D12/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Direct3D12/Detail/WindowSurface.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/common/Framebuffer.h>

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
  DLOG_F(4, ". buffer barrier: {} {} -> {}",
    nostd::to_string(desc.resource).c_str(),
    nostd::to_string(desc.before).c_str(),
    nostd::to_string(desc.after).c_str());

  auto* p_resource = desc.resource->AsPointer<ID3D12Resource>();
  DCHECK_NOTNULL_F(
    p_resource, "Transition barrier (Buffer) cannot have a null resource.");

  const D3D12_RESOURCE_BARRIER d3d12_barrier {
    .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
    .Transition = { .pResource = p_resource,
      // TODO: Or specific sub-resource if provided
      .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
      .StateBefore = ConvertResourceStates(desc.before),
      .StateAfter = ConvertResourceStates(desc.after), },
  };
  return d3d12_barrier;
}

auto ProcessBarrierDesc(const TextureBarrierDesc& desc)
  -> D3D12_RESOURCE_BARRIER
{
  DLOG_F(4, ". texture barrier: {} {} -> {}",
    nostd::to_string(desc.resource).c_str(),
    nostd::to_string(desc.before).c_str(),
    nostd::to_string(desc.after).c_str());

  auto* p_resource = desc.resource->AsPointer<ID3D12Resource>();
  DCHECK_NOTNULL_F(
    p_resource, "Transition barrier (Texture) cannot have a null resource.");

  const D3D12_RESOURCE_BARRIER d3d12_barrier { .Type
    = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
    .Transition = { .pResource = p_resource,
      // TODO(abdes): Or specific sub-resource if provided
      .Subresource
      = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
      .StateBefore = ConvertResourceStates(desc.before),
      .StateAfter = ConvertResourceStates(desc.after), }, };
  return d3d12_barrier;
}

auto ProcessBarrierDesc(const MemoryBarrierDesc& desc) -> D3D12_RESOURCE_BARRIER
{
  DLOG_F(
    4, ". memory barrier: 0x{:X}", nostd::to_string(desc.resource).c_str());

  const D3D12_RESOURCE_BARRIER d3d12_barrier
    = { .Type = D3D12_RESOURCE_BARRIER_TYPE_UAV,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .UAV = {
          .pResource = desc.resource->AsPointer<ID3D12Resource>() != nullptr
            ? desc.resource->AsPointer<ID3D12Resource>()
            : nullptr,
        } };
  return d3d12_barrier;
}

} // anonymous namespace

CommandRecorder::CommandRecorder(std::weak_ptr<Graphics> graphics_weak,
  std::shared_ptr<graphics::CommandList> command_list,
  observer_ptr<graphics::CommandQueue> target_queue)
  : Base(command_list, target_queue)
  , graphics_weak_(std::move(graphics_weak))
{
  DCHECK_F(!graphics_weak_.expired(), "Graphics backend cannot be null");
}

CommandRecorder::~CommandRecorder()
{
  DCHECK_F(!graphics_weak_.expired(), "Graphics backend cannot be null");
}

auto CommandRecorder::Begin() -> void { graphics::CommandRecorder::Begin(); }

namespace {
// Modern bindless root signature layout:
// Root Param 0: Single unbounded SRV descriptor table (t0, space0)
// Root Param 1: Direct CBV for SceneConstants (b1, space0)
// Root Param 2: Direct CBV for MaterialConstants (b2, space0) - graphics only
constexpr UINT kRootIndex_UnboundedSRV_Table = 0;
constexpr UINT kRootIndex_SceneConstants_CBV = 1;
constexpr UINT kRootIndex_MaterialConstants_CBV = 2;
} // namespace

auto CommandRecorder::SetupDescriptorTables(
  const std::span<const detail::ShaderVisibleHeapInfo> heaps) const -> void
{
  // Modern bindless approach: Bind the single unbounded SRV descriptor table.
  // The heap(s) bound here must be the same as those used to allocate CBV/SRV
  // handles in MainModule.cpp. This ensures the shader can access resources
  // using ResourceDescriptorHeap with direct global indices.
  auto* d3d12_command_list = GetConcreteCommandList().GetCommandList();
  DCHECK_NOTNULL_F(d3d12_command_list);

  auto queue_role = GetCommandList().GetQueueRole();
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
      heaps_to_set.size(), GetConcreteCommandList().GetName());
    d3d12_command_list->SetDescriptorHeaps(
      static_cast<UINT>(heaps_to_set.size()), heaps_to_set.data());
  }

  auto set_table = [this, d3d12_command_list, queue_role](const UINT root_index,
                     const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) {
    if (queue_role == QueueRole::kGraphics) {
      DLOG_F(3,
        "recorder: SetGraphicsRootDescriptorTable for command list: {}, root "
        "index={}, gpu_handle={}",
        GetConcreteCommandList().GetName(), root_index, gpu_handle.ptr);
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
    } else {
      DLOG_F(WARNING,
        "Unsupported descriptor heap type for root table binding: {}",
        static_cast<std::underlying_type_t<D3D12_DESCRIPTOR_HEAP_TYPE>>(
          heap_info.heap_type));
    }
  }
}

auto CommandRecorder::SetViewport(const ViewPort& viewport) -> void
{
  const auto& command_list = GetConcreteCommandList();
  DCHECK_EQ_F(
    command_list.GetQueueRole(), QueueRole::kGraphics, "Invalid queue type");

  D3D12_VIEWPORT d3d_viewport;
  d3d_viewport.TopLeftX = viewport.top_left_x;
  d3d_viewport.TopLeftY = viewport.top_left_y;
  d3d_viewport.Width = viewport.width;
  d3d_viewport.Height = viewport.height;
  d3d_viewport.MinDepth = viewport.min_depth;
  d3d_viewport.MaxDepth = viewport.max_depth;

  command_list.GetCommandList()->RSSetViewports(1, &d3d_viewport);
}

auto CommandRecorder::SetScissors(const Scissors& scissors) -> void
{
  const auto& command_list = GetConcreteCommandList();

  D3D12_RECT rect;
  rect.left = scissors.left;
  rect.top = scissors.top;
  rect.right = scissors.right;
  rect.bottom = scissors.bottom;
  command_list.GetCommandList()->RSSetScissorRects(1, &rect);
}

auto CommandRecorder::SetVertexBuffers(const uint32_t num,
  const std::shared_ptr<graphics::Buffer>* vertex_buffers,
  const uint32_t* strides) const -> void
{
  const auto& command_list = GetConcreteCommandList();
  DCHECK_EQ_F(
    command_list.GetQueueRole(), QueueRole::kGraphics, "Invalid queue type");

  std::vector<D3D12_VERTEX_BUFFER_VIEW> vertex_buffer_views(num);
  for (uint32_t i = 0; i < num; ++i) {
    const auto buffer = std::static_pointer_cast<Buffer>(vertex_buffers[i]);
    vertex_buffer_views[i].BufferLocation
      = buffer->GetResource()->GetGPUVirtualAddress();
    vertex_buffer_views[i].SizeInBytes = static_cast<UINT>(buffer->GetSize());
    vertex_buffer_views[i].StrideInBytes = strides[i];
  }

  command_list.GetCommandList()->IASetVertexBuffers(
    0, num, vertex_buffer_views.data());
}

auto CommandRecorder::Draw(const uint32_t vertex_num,
  const uint32_t instances_num, const uint32_t vertex_offset,
  const uint32_t instance_offset) -> void
{
  const auto& command_list = GetConcreteCommandList();
  DCHECK_EQ_F(
    command_list.GetQueueRole(), QueueRole::kGraphics, "Invalid queue type");

  // Prepare for Draw
  command_list.GetCommandList()->IASetPrimitiveTopology(
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  command_list.GetCommandList()->DrawInstanced(
    vertex_num, instances_num, vertex_offset, instance_offset);
}

auto CommandRecorder::Dispatch(uint32_t thread_group_count_x,
  uint32_t thread_group_count_y, uint32_t thread_group_count_z) -> void
{
  const auto& command_list = GetConcreteCommandList();
  DCHECK_EQ_F(
    command_list.GetQueueRole(), QueueRole::kCompute, "Invalid queue type");

  command_list.GetCommandList()->Dispatch(
    thread_group_count_x, thread_group_count_y, thread_group_count_z);
}

auto CommandRecorder::SetPipelineState(GraphicsPipelineDesc desc) -> void
{
  auto graphics = graphics_weak_.lock();
  DCHECK_F(graphics != nullptr, "Graphics backend is no longer valid");

  const auto debug_name = desc.GetName(); // Save before moving desc
  graphics_pipeline_hash_ = std::hash<GraphicsPipelineDesc> {}(desc);

  auto [pipeline_state, root_signature] = graphics->GetOrCreateGraphicsPipeline(
    std::move(desc), graphics_pipeline_hash_);
  DCHECK_NOTNULL_F(pipeline_state);
  DCHECK_NOTNULL_F(root_signature);

  const auto& command_list = GetConcreteCommandList();
  auto* d3d12_command_list = command_list.GetCommandList();

  d3d12_command_list->SetGraphicsRootSignature(root_signature);
  // NOLINTNEXTLINE(*-pro-type-static-cast-downcast)
  auto& allocator = static_cast<DescriptorAllocator&>(
    const_cast<graphics::DescriptorAllocator&>(
      graphics->GetDescriptorAllocator()));
  SetupDescriptorTables(allocator.GetShaderVisibleHeaps());

  d3d12_command_list->SetPipelineState(pipeline_state);
}

auto CommandRecorder::SetPipelineState(ComputePipelineDesc desc) -> void
{
  auto graphics = graphics_weak_.lock();
  DCHECK_F(graphics != nullptr, "Graphics backend is no longer valid");

  const auto debug_name = desc.GetName(); // Save before moving desc
  compute_pipeline_hash_ = std::hash<ComputePipelineDesc> {}(desc);

  auto [pipeline_state, root_signature] = graphics->GetOrCreateComputePipeline(
    std::move(desc), compute_pipeline_hash_);
  DCHECK_NOTNULL_F(pipeline_state);
  DCHECK_NOTNULL_F(root_signature);

  const auto& command_list = GetConcreteCommandList();
  auto* d3d12_command_list = command_list.GetCommandList();

  d3d12_command_list->SetGraphicsRootSignature(root_signature);
  // NOLINTNEXTLINE(*-pro-type-static-cast-downcast)
  auto& allocator = static_cast<DescriptorAllocator&>(
    const_cast<graphics::DescriptorAllocator&>(
      graphics->GetDescriptorAllocator()));
  SetupDescriptorTables(allocator.GetShaderVisibleHeaps());

  // Name them for debugging

  d3d12_command_list->SetPipelineState(pipeline_state);
}

auto CommandRecorder::SetGraphicsRootConstantBufferView(
  uint32_t root_parameter_index, uint64_t buffer_gpu_address) -> void
{
  const auto& command_list = GetConcreteCommandList();
  auto* d3d12_command_list = command_list.GetCommandList();
  d3d12_command_list->SetGraphicsRootConstantBufferView(
    root_parameter_index, buffer_gpu_address);
}

auto CommandRecorder::SetComputeRootConstantBufferView(
  uint32_t root_parameter_index, uint64_t buffer_gpu_address) -> void
{
  const auto& command_list = GetConcreteCommandList();
  auto* d3d12_command_list = command_list.GetCommandList();
  d3d12_command_list->SetComputeRootConstantBufferView(
    root_parameter_index, buffer_gpu_address);
}

auto CommandRecorder::SetGraphicsRoot32BitConstant(
  uint32_t root_parameter_index, uint32_t src_data,
  uint32_t dest_offset_in_32bit_values) -> void
{
  const auto& command_list = GetConcreteCommandList();
  auto* d3d12_command_list = command_list.GetCommandList();
  d3d12_command_list->SetGraphicsRoot32BitConstant(
    root_parameter_index, src_data, dest_offset_in_32bit_values);
}

auto CommandRecorder::SetComputeRoot32BitConstant(uint32_t root_parameter_index,
  uint32_t src_data, uint32_t dest_offset_in_32bit_values) -> void
{
  const auto& command_list = GetConcreteCommandList();
  auto* d3d12_command_list = command_list.GetCommandList();
  d3d12_command_list->SetComputeRoot32BitConstant(
    root_parameter_index, src_data, dest_offset_in_32bit_values);
}

auto CommandRecorder::ExecuteBarriers(const std::span<const Barrier> barriers)
  -> void
{
  if (barriers.empty()) {
    return;
  }

  const auto& command_list_impl = GetConcreteCommandList();
  auto* d3d12_command_list = command_list_impl.GetCommandList();
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

auto CommandRecorder::GetConcreteCommandList() const -> CommandList&
{
  // NOLINTNEXTLINE(*-pro-type-static-cast_down_cast)
  return static_cast<CommandList&>(GetCommandList());
}

// TODO: legacy - should be replaced once render passes are implemented
auto CommandRecorder::BindFrameBuffer(const Framebuffer& framebuffer) -> void
{
  // NOLINTNEXTLINE(*-pro-type-static-cast_down_cast)
  const auto& fb = static_cast<const Framebuffer&>(framebuffer);
  StaticVector<D3D12_CPU_DESCRIPTOR_HANDLE, kMaxRenderTargets> rtvs;
  for (const auto& rtv : fb.GetRenderTargetViews()) {
    rtvs.emplace_back(rtv->AsInteger());
  }

  D3D12_CPU_DESCRIPTOR_HANDLE dsv = {};
  if (fb.GetDescriptor().depth_attachment.IsValid()) {
    dsv.ptr = fb.GetDepthStencilView()->AsInteger();
  }

  const auto& command_list_impl = GetConcreteCommandList();
  auto* d3d12_command_list = command_list_impl.GetCommandList();
  DCHECK_NOTNULL_F(d3d12_command_list);

  d3d12_command_list->OMSetRenderTargets(static_cast<UINT>(rtvs.size()),
    rtvs.data(), 0,
    fb.GetDescriptor().depth_attachment.IsValid() ? &dsv : nullptr);
}

auto CommandRecorder::ClearFramebuffer(const Framebuffer& framebuffer,
  const std::optional<std::vector<std::optional<Color>>> color_clear_values,
  const std::optional<float> depth_clear_value,
  const std::optional<uint8_t> stencil_clear_value) -> void
{
  using graphics::detail::GetFormatInfo;

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  const auto& fb = static_cast<const Framebuffer&>(framebuffer);

  const auto& command_list_impl = GetConcreteCommandList();
  auto* d3d12_command_list = command_list_impl.GetCommandList();
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
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle { .ptr
      = rtvs[i]->AsInteger() };
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
      = fb.GetDepthStencilView()->AsInteger() };
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

auto CommandRecorder::CopyBuffer(graphics::Buffer& dst, const size_t dst_offset,
  const graphics::Buffer& src, const size_t src_offset, const size_t size)
  -> void
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

  const auto& command_list = GetConcreteCommandList();
  auto* d3d12_command_list = command_list.GetCommandList();
  DCHECK_NOTNULL_F(d3d12_command_list);

  d3d12_command_list->CopyBufferRegion(
    dst_resource, dst_offset, src_resource, src_offset, size);
}

// NOTE (UPLOAD PATH LIMITATIONS & ROADMAP)
// ---------------------------------------------------------------------------
// Current implementation notes:
// - The implementation below uses ID3D12Device::GetCopyableFootprints to
//   obtain authoritative placed-subresource footprints for the destination
//   texture and issues CopyTextureRegion calls that reference those
//   placed footprints.
// - To bridge from the caller-provided source buffer to the placed
//   footprint we currently only adjust the placed footprint Offset by
//   `region.buffer_offset` and then perform a full-subresource
//   CopyTextureRegion.
// - This implementation implicitly assumes the source data in the buffer is
//   packed to match the device footprint layout (RowPitch and slice pitch)
//   or that region.buffer_offset points directly to a placed footprint that
//   already encodes the correct row pitch. In practice callers may provide
//   `buffer_row_pitch`/`buffer_slice_pitch` values that differ from the
//   device RowPitch. Those cases are NOT fully handled by this code and can
//   lead to incorrect copies.
//
// Known limitations:
// 1) Mismatched row/slice pitches (most common): If the source buffer uses
//    a different row pitch than the device footprint, the implementation
//    must repack rows into an upload region that matches the D3D12
//    RowPitch before issuing CopyTextureRegion. Currently we do not
//    repack; we only offset the placed footprint. This is a correctness
//    gap for partial/heterogeneous uploads.
// 2) Partial-row or sub-row uploads: The code issues a full-subresource copy
//    (via the placed footprint). Partial-region copies that touch only a
//    subset of rows or columns require per-row/box copies or repacking; the
//    current implementation does not perform per-row CopyTextureRegion
//    with manual D3D12_BOX widths computed from buffer_row_pitch.
// 3) No persistent mapped upload ring: A production-quality path should
//    write into a persistent, fenced, mapped upload ring (per-frame). That
//    ring provides allocation, wrap-around, and GPU-fenced lifetime so the
//    CPU can efficiently memcpy into GPU-visible memory without temporary
//    heap allocations. Right now we have no ring implementation; adding one
//    is required to implement efficient repacking at scale.
// 4) Block-compressed formats: These must be handled at block-row
//    granularity. Repacking logic must read/write in block units (e.g. 4x4)
//    and respect the device block layout. The current code treats the
//    placed footprint as authoritative but does not repack from arbitrary
//    buffer pitches into block-padded footprints.
// 5) Alignment constraints: D3D12 enforces D3D12_TEXTURE_DATA_PITCH_ALIGNMENT
//    for RowPitch and other alignment constraints. Any repack must honor
//    these constraints and pad accordingly.
// 6) Synchronization and fencing: When implementing an upload ring the
//    uploader must wait on fences (or allocate only when a slot is free)
//    to avoid overwriting in-flight regions. This logic is not present and
//    must be added with per-frame fences or by integrating with the existing
//    frame/slot system.
//
// Recommended roadmap to make uploads robust and performant:
// A) Implement a persistent mapped upload ring (preferred):
//    - Per-frame ring allocator with mapped CPU pointer and fence-based
//      slot reclamation.
//    - Allocation API that returns a mapped pointer and a GPU-visible
//      placed-footprint offset (or a small temporary buffer) ready for
//      CopyTextureRegion.
//    - Repack source rows into the mapped region using the footprint's
//      RowPitch and slice pitch. For block-compressed formats repack in
//      block-rows.
//    - Emit a single CopyTextureRegion per subresource using the placed
//      footprint offset (fast GPU-side copy).
//
// B) Fallback per-row copy path (only for tiny uploads):
//    - If ring allocation fails or the upload is extremely small, perform
//      per-row (or per-block-row) CopyTextureRegion calls where the
//      src D3D12_TEXTURE_COPY_LOCATION references the buffer with the
//      correct offset for each row and a D3D12_BOX limits the copied extents.
//    - This avoids extra staging memory but generates many GPU copy
//      commands and should be rate-limited.
//
// C) Logging, validation and tests:
//    - Emit warnings when buffer_row_pitch or buffer_slice_pitch differ from
//      the footprint RowPitch/slice pitch and when falling back to per-row
//      copies.
//    - Add unit/integration tests covering row-pitch mismatch, partial
//      uploads, and block-compressed formats.
//
// D) Performance and budgeting:
//    - Size the upload ring to avoid frequent waiting/alloc failures.
//    - Consider batching multiple small uploads into one staging region to
//      reduce command overhead.
//
// Until the upload ring and repack path are implemented this method should
// be considered best-effort and primarily suitable for tightly-packed
// uploads where the caller ensures buffer pitches match device footprints.
// ---------------------------------------------------------------------------
auto CommandRecorder::CopyBufferToTexture(const graphics::Buffer& src,
  const TextureUploadRegion& region, graphics::Texture& dst) -> void
{
  // Single-region wrapper
  CopyBufferToTexture(
    src, std::span<const TextureUploadRegion>(&region, 1), dst);
}

auto CommandRecorder::CopyBufferToTexture(const graphics::Buffer& src,
  std::span<const TextureUploadRegion> regions, graphics::Texture& dst) -> void
{
  // Expectations: caller ensured resource states (src is COPY_SOURCE, dst is
  // COPY_DEST)
  const auto& command_list = GetConcreteCommandList();
  auto* d3d12_command_list = command_list.GetCommandList();
  DCHECK_NOTNULL_F(d3d12_command_list);

  // NOLINTBEGIN(cppcoreguidelines-pro-type-static-cast-downcast)
  const auto& src_buf = static_cast<const Buffer&>(src);
  // NOLINTEND(cppcoreguidelines-pro-type-static-cast-downcast)

  auto* src_resource = src_buf.GetResource();
  auto* dst_native = dst.GetNativeResource()->AsPointer<ID3D12Resource>();
  DCHECK_NOTNULL_F(src_resource);
  DCHECK_NOTNULL_F(dst_native);

  // Use device to compute copyable footprints
  auto graphics = graphics_weak_.lock();
  DCHECK_F(graphics != nullptr, "Graphics backend is no longer valid");
  auto* device = graphics->GetCurrentDevice();
  const auto& desc = dst.GetDescriptor();

  for (const auto& region : regions) {
    // Resolve destination slice and subresources
    auto dst_slice = region.dst_slice.Resolve(desc);
    auto subresources = region.dst_subresources.Resolve(desc, true);

    const UINT first_sub
      = dst_slice.array_slice * desc.mip_levels + dst_slice.mip_level;
    const UINT num_sub
      = subresources.num_array_slices * subresources.num_mip_levels;

    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(num_sub);
    std::vector<UINT> row_counts(num_sub);
    UINT64 total_bytes = 0;

    if (device) {
      // Get a resource desc for the destination texture
      D3D12_RESOURCE_DESC rd = dst_native->GetDesc();
      device->GetCopyableFootprints(&rd, first_sub, num_sub, 0,
        footprints.data(), row_counts.data(), nullptr, &total_bytes);
    }

    // Iterate each array slice / mip targeted
    for (UINT si = 0; si < subresources.num_array_slices; ++si) {
      for (UINT mi = 0; mi < subresources.num_mip_levels; ++mi) {
        const UINT sub_index = si * subresources.num_mip_levels + mi;
        const auto& fp = footprints[sub_index];

        // Setup src (buffer) location using the placed footprint but adjust the
        // offset by the region.buffer_offset if provided.
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT adjusted_fp = fp;
        adjusted_fp.Offset += static_cast<UINT64>(region.buffer_offset);

        D3D12_TEXTURE_COPY_LOCATION src_loc = {};
        src_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src_loc.pResource = src_resource;
        src_loc.PlacedFootprint = adjusted_fp;

        // Setup dst (texture) location
        D3D12_TEXTURE_COPY_LOCATION dst_loc = {};
        dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst_loc.pResource = dst_native;
        const UINT dst_subresource_index
          = (dst_slice.array_slice + si) * desc.mip_levels
          + (dst_slice.mip_level + mi);
        dst_loc.SubresourceIndex = dst_subresource_index;

        // Copy the full subresource as defined by the footprint
        d3d12_command_list->CopyTextureRegion(
          &dst_loc, dst_slice.x, dst_slice.y, dst_slice.z, &src_loc, nullptr);
      }
    }
  }
}

// D3D12 specific command implementations
auto CommandRecorder::ClearDepthStencilView(const graphics::Texture& texture,
  const NativeView& dsv, const ClearFlags clear_flags, const float depth,
  const uint8_t stencil) -> void
{
  const auto& command_list_impl = GetConcreteCommandList();
  auto* d3d12_command_list = command_list_impl.GetCommandList();
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
  const D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle { .ptr = dsv->AsInteger() };
  d3d12_command_list->ClearDepthStencilView(
    dsv_handle, d3d12_clear_flags, clear_depth, clear_stencil,
    0, // NumRects
    nullptr // pRects
  );
}

auto CommandRecorder::SetRenderTargets(
  const std::span<NativeView> rtvs, const std::optional<NativeView> dsv) -> void
{
  DCHECK_F(!rtvs.empty() || dsv.has_value(),
    "At least one render target must be specified.");
  DCHECK_LE_F(rtvs.size(), kMaxRenderTargets,
    "Too many render targets specified. Maximum is {}.", kMaxRenderTargets);

  std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtv_handles;
  rtv_handles.reserve(rtvs.size());
  for (const auto& rtv : rtvs) {
    if (!rtv->IsValid()) {
      LOG_F(ERROR, "invalid render target view: {} view, skipped",
        nostd::to_string(rtv).c_str());
      continue; // Skip invalid RTVs
    }
    rtv_handles.push_back({ .ptr = rtv->AsInteger() });
  }

  const D3D12_CPU_DESCRIPTOR_HANDLE* dsv_handle_ptr { nullptr };
  if (dsv.has_value()) {
    if (!(*dsv)->IsValid()) {
      LOG_F(ERROR, "invalid depth/stencil view: {}, dropped",
        nostd::to_string(*dsv).c_str());
    } else {
      D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle { .ptr = (*dsv)->AsInteger() };
      dsv_handle_ptr = &dsv_handle;
    }
  }

  const auto& command_list_impl = GetConcreteCommandList();
  auto* d3d12_command_list = command_list_impl.GetCommandList();
  DCHECK_NOTNULL_F(d3d12_command_list);

  d3d12_command_list->OMSetRenderTargets(static_cast<UINT>(rtv_handles.size()),
    rtv_handles.data(), dsv_handle_ptr != nullptr ? 1 : 0, dsv_handle_ptr);
}

auto CommandRecorder::BindIndexBuffer(
  const graphics::Buffer& buffer, Format format) -> void
{
  const auto& command_list = GetConcreteCommandList();
  DCHECK_EQ_F(
    command_list.GetQueueRole(), QueueRole::kGraphics, "Invalid queue type");

  const auto* d3d12_buffer = static_cast<const Buffer*>(&buffer);
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
  command_list.GetCommandList()->IASetIndexBuffer(&ib_view);
}
