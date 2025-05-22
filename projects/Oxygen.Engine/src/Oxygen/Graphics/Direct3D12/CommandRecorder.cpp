//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/CommandRecorder.h>

#include "Detail/dx12_utils.h"

#include <exception>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <d3d12.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/VariantHelpers.h> // Added for Overloads
#include <Oxygen/Graphics/Common/Detail/Barriers.h>
#include <Oxygen/Graphics/Common/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Direct3D12/Allocator/D3D12MemAlloc.h>
#include <Oxygen/Graphics/Direct3D12/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/CommandList.h>
#include <Oxygen/Graphics/Direct3D12/Detail/WindowSurface.h>
#include <Oxygen/Graphics/Direct3D12/Framebuffer.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>

using oxygen::graphics::DeferredObjectRelease;
using oxygen::graphics::d3d12::CommandRecorder;
using oxygen::graphics::d3d12::detail::GetGraphics;
using oxygen::graphics::detail::Barrier;
using oxygen::graphics::detail::BufferBarrierDesc;
using oxygen::graphics::detail::MemoryBarrierDesc;
using oxygen::graphics::detail::TextureBarrierDesc;

namespace {

// Helper function to convert common ResourceStates to D3D12_RESOURCE_STATES
auto ConvertResourceStates(oxygen::graphics::ResourceStates common_states) -> D3D12_RESOURCE_STATES
{
    using oxygen::graphics::ResourceStates;

    DCHECK_F(common_states != ResourceStates::kUnknown,
        "Illegal `ResourceStates::kUnknown` encountered in barrier state mapping to D3D12.");

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
    auto map_flag_if_present =
        [&](const ResourceStates flag_to_check, const D3D12_RESOURCE_STATES d3d12_equivalent) {
            if ((common_states & flag_to_check) == flag_to_check) {
                d3d_states |= d3d12_equivalent;
            }
        };

    map_flag_if_present(ResourceStates::kBuildAccelStructureRead, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    map_flag_if_present(ResourceStates::kBuildAccelStructureWrite, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    map_flag_if_present(ResourceStates::kConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    map_flag_if_present(ResourceStates::kCopyDest, D3D12_RESOURCE_STATE_COPY_DEST);
    map_flag_if_present(ResourceStates::kCopySource, D3D12_RESOURCE_STATE_COPY_SOURCE);
    map_flag_if_present(ResourceStates::kDepthRead, D3D12_RESOURCE_STATE_DEPTH_READ);
    map_flag_if_present(ResourceStates::kDepthWrite, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    map_flag_if_present(ResourceStates::kGenericRead, D3D12_RESOURCE_STATE_GENERIC_READ);
    map_flag_if_present(ResourceStates::kIndexBuffer, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    map_flag_if_present(ResourceStates::kIndirectArgument, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    map_flag_if_present(ResourceStates::kInputAttachment, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    map_flag_if_present(ResourceStates::kRayTracing, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    map_flag_if_present(ResourceStates::kRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
    map_flag_if_present(ResourceStates::kResolveDest, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    map_flag_if_present(ResourceStates::kResolveSource, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    map_flag_if_present(ResourceStates::kShaderResource, (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    map_flag_if_present(ResourceStates::kShadingRate, D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE);
    map_flag_if_present(ResourceStates::kStreamOut, D3D12_RESOURCE_STATE_STREAM_OUT);
    map_flag_if_present(ResourceStates::kUnorderedAccess, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    map_flag_if_present(ResourceStates::kVertexBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

    if (d3d_states == 0) {
        DLOG_F(WARNING, "ResourceStates ({:#X}) did not map to any specific D3D12 states; "
                        "falling back to D3D12_RESOURCE_STATE_COMMON.",
            static_cast<std::underlying_type_t<ResourceStates>>(common_states));
        return D3D12_RESOURCE_STATE_COMMON;
    }

    return d3d_states;
}

// Static helper functions to process specific barrier types
auto ProcessBarrierDesc(const BufferBarrierDesc& desc) -> D3D12_RESOURCE_BARRIER
{
    D3D12_RESOURCE_BARRIER d3d12_barrier = {};
    d3d12_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    d3d12_barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;

    auto* p_resource = desc.resource.AsPointer<ID3D12Resource>();
    DCHECK_NOTNULL_F(p_resource, "Transition barrier (Buffer) cannot have a null resource.");

    d3d12_barrier.Transition.pResource = p_resource;
    d3d12_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; // Or specific subresource if provided
    d3d12_barrier.Transition.StateBefore = ConvertResourceStates(desc.before);
    d3d12_barrier.Transition.StateAfter = ConvertResourceStates(desc.after);
    return d3d12_barrier;
}

auto ProcessBarrierDesc(const TextureBarrierDesc& desc) -> D3D12_RESOURCE_BARRIER
{
    D3D12_RESOURCE_BARRIER d3d12_barrier = {};
    d3d12_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    d3d12_barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;

    auto* p_resource = desc.resource.AsPointer<ID3D12Resource>();
    DCHECK_NOTNULL_F(p_resource, "Transition barrier (Texture) cannot have a null resource.");

    d3d12_barrier.Transition.pResource = p_resource;
    d3d12_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; // Or specific subresource if provided
    d3d12_barrier.Transition.StateBefore = ConvertResourceStates(desc.before);
    d3d12_barrier.Transition.StateAfter = ConvertResourceStates(desc.after);
    return d3d12_barrier;
}

auto ProcessBarrierDesc(const MemoryBarrierDesc& desc) -> D3D12_RESOURCE_BARRIER
{
    D3D12_RESOURCE_BARRIER d3d12_barrier = {};
    d3d12_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    d3d12_barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    if (desc.resource.AsPointer<ID3D12Resource>() != nullptr) {
        d3d12_barrier.UAV.pResource = desc.resource.AsPointer<ID3D12Resource>();
    } else {
        d3d12_barrier.UAV.pResource = nullptr; // Global UAV barrier
    }
    return d3d12_barrier;
}

} // anonymous namespace

CommandRecorder::CommandRecorder(
    graphics::detail::PerFrameResourceManager* resource_manager,
    graphics::CommandList* command_list, graphics::CommandQueue* target_queue)
    : Base(command_list, target_queue)
    , resource_manager_(resource_manager)
{
    DCHECK_NOTNULL_F(resource_manager, "Resource manager cannot be null");

    CreateRootSignature();
}

CommandRecorder::~CommandRecorder()
{
    if (resource_manager_ != nullptr) {
        DeferredObjectRelease(root_signature_, *resource_manager_);
        DeferredObjectRelease(pipeline_state_, *resource_manager_);
    }
}

void CommandRecorder::Begin()
{
    graphics::CommandRecorder::Begin();

    // resource_state_cache_.OnBeginCommandBuffer();

    ResetState();
}

auto CommandRecorder::End() -> graphics::CommandList*
{
    // if (current_render_target_ != nullptr) {
    //     auto* command_list = GetConcreteCommandList();
    //     DCHECK_NOTNULL_F(command_list);

    //     D3D12_RESOURCE_BARRIER barrier {
    //         .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
    //         .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
    //         .Transition = {
    //             .pResource = current_render_target_->GetResource(),
    //             .Subresource = 0,
    //             .StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
    //             .StateAfter = D3D12_RESOURCE_STATE_PRESENT }
    //     };
    //     command_list->GetCommandList()->ResourceBarrier(1, &barrier);
    // }
    return graphics::CommandRecorder::End();
}

void CommandRecorder::SetupDescriptorTables(std::span<detail::ShaderVisibleHeapInfo> heaps)
{
    auto* d3d12_command_list = GetConcreteCommandList()->GetCommandList();
    DCHECK_NOTNULL_F(d3d12_command_list);

    auto queue_role = GetCommandList()->GetQueueRole();
    DCHECK_F(queue_role == QueueRole::kGraphics || queue_role == QueueRole::kCompute,
        "Invalid command list type for SetupDescriptorTables. Expected Graphics or Compute, got: {}",
        static_cast<int>(queue_role));

    constexpr UINT kRootIndex_CBV_SRV_UAV = 0;
    constexpr UINT kRootIndex_Sampler = 1;

    auto set_table = [this, d3d12_command_list, queue_role](
                         UINT root_index, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) {
        if (queue_role == QueueRole::kGraphics) {
            d3d12_command_list->SetGraphicsRootDescriptorTable(root_index, gpu_handle);
        } else if (queue_role == QueueRole::kCompute) {
            d3d12_command_list->SetComputeRootDescriptorTable(root_index, gpu_handle);
        }
    };

    // Prepare the descriptor heaps for the command list, and set the root
    // descriptor tables as we go.

    std::vector<ID3D12DescriptorHeap*> heaps_ptrs(heaps.size());
    for (const auto& heap_info : heaps) {
        DCHECK_NOTNULL_F(heap_info.heap, "Heap pointer cannot be null");

        UINT root_index;
        // Set the root descriptor table for that heap type
        if (heap_info.heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
            root_index = kRootIndex_CBV_SRV_UAV;
        } else if (heap_info.heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
            root_index = kRootIndex_Sampler;
        } else {
            DLOG_F(WARNING, "Unsupported descriptor heap type: {}",
                static_cast<std::underlying_type_t<D3D12_DESCRIPTOR_HEAP_TYPE>>(heap_info.heap_type));
            continue;
        }

        heaps_ptrs[root_index] = heap_info.heap;
        set_table(root_index, heap_info.gpu_handle);
    }

    // Set the descriptor heaps for the command list
    d3d12_command_list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps_ptrs.data());
}

void CommandRecorder::SetViewport(const ViewPort& viewport)
{
    auto* command_list = GetConcreteCommandList();
    DCHECK_NOTNULL_F(command_list);
    DCHECK_EQ_F(command_list->GetQueueRole(), QueueRole::kGraphics, "Invalid queue type");

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
    auto* command_list = GetConcreteCommandList();
    DCHECK_NOTNULL_F(command_list);

    D3D12_RECT rect;
    rect.left = scissors.left;
    rect.top = scissors.top;
    rect.right = scissors.right;
    rect.bottom = scissors.bottom;
    command_list->GetCommandList()->RSSetScissorRects(1, &rect);
}

void CommandRecorder::SetVertexBuffers(
    const uint32_t num,
    const std::shared_ptr<graphics::Buffer>* vertex_buffers,
    const uint32_t* strides,
    const uint32_t* offsets)
{
    auto* command_list = GetConcreteCommandList();
    DCHECK_NOTNULL_F(command_list);
    DCHECK_EQ_F(command_list->GetQueueRole(), QueueRole::kGraphics, "Invalid queue type");

    std::vector<D3D12_VERTEX_BUFFER_VIEW> vertex_buffer_views(num);
    for (uint32_t i = 0; i < num; ++i) {
        auto buffer = std::static_pointer_cast<Buffer>(vertex_buffers[i]);
        vertex_buffer_views[i].BufferLocation = buffer->GetResource()->GetGPUVirtualAddress();
        vertex_buffer_views[i].SizeInBytes = buffer->GetSize();
        vertex_buffer_views[i].StrideInBytes = strides[i];
    }

    command_list->GetCommandList()->IASetVertexBuffers(0, num, vertex_buffer_views.data());
}

void CommandRecorder::Draw(const uint32_t vertex_num, const uint32_t instances_num, const uint32_t vertex_offset, const uint32_t instance_offset)
{
    auto* command_list = GetConcreteCommandList();
    DCHECK_NOTNULL_F(command_list);
    DCHECK_EQ_F(command_list->GetQueueRole(), QueueRole::kGraphics, "Invalid queue type");

    // Prepare for Draw
    command_list->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    command_list->GetCommandList()->DrawInstanced(vertex_num, instances_num, vertex_offset, instance_offset);
}

void CommandRecorder::DrawIndexed(uint32_t index_num, uint32_t instances_num, uint32_t index_offset, int32_t vertex_offset, uint32_t instance_offset)
{
}

void CommandRecorder::CreateRootSignature()
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc = {};
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> signature_blob;
    Microsoft::WRL::ComPtr<ID3DBlob> error_blob;
    HRESULT hr = D3D12SerializeRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature_blob, &error_blob);
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to serialize root signature");
    }

    // Create raw pointer first
    ID3D12RootSignature* raw_ptr = nullptr;
    // Use IID_PPV_ARGS with the raw pointer
    hr = GetGraphics().GetCurrentDevice()->CreateRootSignature(
        0,
        signature_blob->GetBufferPointer(),
        signature_blob->GetBufferSize(),
        IID_PPV_ARGS(&raw_ptr));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create root signature");
    }
    // Reset the unique_ptr with the raw pointer
    root_signature_.reset(raw_ptr);
}
// void CommandRecorder::SetRenderTarget(std::unique_ptr<graphics::RenderTarget> render_target)
// {
//     auto* command_list = GetConcreteCommandList();
//     DCHECK_NOTNULL_F(command_list);
//     DCHECK_NOTNULL_F(render_target, "Invalid render target pointer");

//     current_render_target_ = std::unique_ptr<RenderTarget>(
//         static_cast<RenderTarget*>(render_target.release()));
//     CHECK_NOTNULL_F(current_render_target_, "unexpected failed dynamic cast");

//     // Indicate that the back buffer will be used as a render target.
//     const D3D12_RESOURCE_BARRIER barrier {
//         .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
//         .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
//         .Transition = {
//             .pResource = current_render_target_->GetResource(),
//             .Subresource = 0,
//             .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
//             .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET }
//     };
//     command_list->GetCommandList()->ResourceBarrier(1, &barrier);

//     const D3D12_CPU_DESCRIPTOR_HANDLE render_target_views[1] = { current_render_target_->Rtv().cpu };
//     command_list->GetCommandList()->OMSetRenderTargets(1, render_target_views, FALSE, nullptr);
// }

void CommandRecorder::SetPipelineState(
    const std::shared_ptr<IShaderByteCode>& vertex_shader,
    const std::shared_ptr<IShaderByteCode>& pixel_shader)
{
    auto* command_list = GetConcreteCommandList();
    DCHECK_NOTNULL_F(command_list);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.pRootSignature = root_signature_.get();
    pso_desc.VS = { .pShaderBytecode = vertex_shader->Data(), .BytecodeLength = vertex_shader->Size() };
    pso_desc.PS = { .pShaderBytecode = pixel_shader->Data(), .BytecodeLength = pixel_shader->Size() };    pso_desc.BlendState = kBlendState.disabled;
    pso_desc.SampleMask = UINT_MAX;
    pso_desc.RasterizerState = kRasterizerState.no_cull;
    pso_desc.DepthStencilState = kDepthState.disabled;
    pso_desc.InputLayout = { .pInputElementDescs = nullptr, .NumElements = 0 }; // Assuming no input layout for full-screen triangle
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = kDefaultBackBufferFormat;
    pso_desc.SampleDesc.Count = 1;

    // Create raw pointer first
    ID3D12PipelineState* raw_pso = nullptr;
    // Use IID_PPV_ARGS with the raw pointer
    HRESULT hr = GetGraphics().GetCurrentDevice()->CreateGraphicsPipelineState(
        &pso_desc,
        IID_PPV_ARGS(&raw_pso));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create pipeline state object");
    }
    // Reset the unique_ptr with the raw pointer
    pipeline_state_.reset(raw_pso);

    command_list->GetCommandList()->SetPipelineState(pipeline_state_.get());
    command_list->GetCommandList()->SetGraphicsRootSignature(root_signature_.get());
}

void CommandRecorder::ResetState()
{
    // mGraphicsBindingState.Reset();
    // mComputeBindingState.Reset();

    // mCurrRenderTarget = nullptr;
    // mGraphicsPipelineState = nullptr;
    // mComputePipelineState = nullptr;

    // mGraphicsPipelineStateChanged = false;
    // mComputePipelineStateChanged = false;

    // mCurrPrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    // mNumBoundVertexBuffers = 0u;
    // mBoundIndexBuffer = nullptr;

    // mVertexBufferChanged = false;
    // mIndexBufferChanged = false;

    // for (uint32 i = 0; i < NFE_RENDERER_MAX_VERTEX_BUFFERS; ++i)
    // {
    //     mBoundVertexBuffers[i] = nullptr;
    // }
}

void CommandRecorder::ExecuteBarriers(const std::span<const Barrier> barriers)
{
    if (barriers.empty()) {
        return;
    }

    auto* command_list_impl = GetConcreteCommandList();
    DCHECK_NOTNULL_F(command_list_impl);
    auto* d3d12_command_list = command_list_impl->GetCommandList();
    DCHECK_NOTNULL_F(d3d12_command_list);

    std::vector<D3D12_RESOURCE_BARRIER> d3d12_barriers;
    d3d12_barriers.reserve(barriers.size());

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
        d3d12_command_list->ResourceBarrier(static_cast<UINT>(d3d12_barriers.size()), d3d12_barriers.data());
    }
}

auto CommandRecorder::GetConcreteCommandList() const -> CommandList*
{
    return static_cast<CommandList*>(GetCommandList()); // NOLINT(*-pro-type-static-cast-downcast)
}

void CommandRecorder::BindFrameBuffer(const graphics::Framebuffer& framebuffer)
{
    const auto& fb = static_cast<const Framebuffer&>(framebuffer); // NOLINT(*-pro-type-static-cast-downcast)
    StaticVector<D3D12_CPU_DESCRIPTOR_HANDLE, kMaxRenderTargets> rtvs;
    for (const auto& rtv : fb.GetRenderTargetViews()) {
        rtvs.emplace_back(rtv);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsv = {};
    if (fb.GetDescriptor().depth_attachment.IsValid()) {
        dsv.ptr = fb.GetDepthStencilView();
    }

    auto* command_list_impl = GetConcreteCommandList();
    DCHECK_NOTNULL_F(command_list_impl);
    auto* d3d12_command_list = command_list_impl->GetCommandList();
    DCHECK_NOTNULL_F(d3d12_command_list);

    d3d12_command_list->OMSetRenderTargets(
        static_cast<UINT>(rtvs.size()),
        rtvs.data(),
        0,
        fb.GetDescriptor().depth_attachment.IsValid() ? &dsv : nullptr);
}

void CommandRecorder::ClearTextureFloat(
    graphics::Texture* _t,
    TextureSubResourceSet sub_resources,
    const Color& clearColor)
{
    auto* t = static_cast<Texture*>(_t); // NOLINT(*-pro-type-static-cast-downcast)
    const auto& desc = t->GetDescriptor();

#ifdef _DEBUG
    const graphics::detail::FormatInfo& formatInfo = graphics::detail::GetFormatInfo(desc.format);
    assert(!formatInfo.has_depth && !formatInfo.has_stencil);
    assert(desc.is_uav || desc.is_render_target);
#endif

    sub_resources = sub_resources.Resolve(desc, false);

    auto* command_list_impl = GetConcreteCommandList();
    DCHECK_NOTNULL_F(command_list_impl);
    auto* d3d12_command_list = command_list_impl->GetCommandList();
    DCHECK_NOTNULL_F(d3d12_command_list);

    // TODO: use the registry cache for the views

    if (desc.is_render_target) {
        // if (m_EnableAutomaticBarriers)
        //{
        //     requireTextureState(t, subresources, ResourceStates::RenderTarget);
        // }
        // commitBarriers();

        // TextureViewDescription rtv_desc {
        //     .view_type = ResourceViewType::kTexture_RTV,
        //     .visibility = DescriptorVisibility::kCpuOnly,
        //     .format = Format::kUnknown,
        //     .dimension = desc.dimension,
        //     .sub_resources = sub_resources,
        // };

        // D3D12_CPU_DESCRIPTOR_HANDLE RTV = { t->GetNativeView(rtv_desc).AsInteger() };

        // d3d12_command_list->ClearRenderTargetView(
        //     RTV,
        //     &clearColor.r,
        //     0, nullptr);
    } else {
        // if (m_EnableAutomaticBarriers)
        //{
        //     requireTextureState(t, subresources, ResourceStates::UnorderedAccess);
        // }
        // commitBarriers();

        // commitDescriptorHeaps();

        // for (MipLevel mipLevel = sub_resources.base_mip_level; mipLevel < sub_resources.base_mip_level + sub_resources.num_mip_levels; mipLevel++) {
        //     const auto& desc_handle = t->GetClearMipLevelUnorderedAccessView(mipLevel);
        //     assert(desc_handle.IsValid());
        //     d3d12_command_list->ClearUnorderedAccessViewFloat(
        //         desc_handle.gpu,
        //         desc_handle.cpu,
        //         t->GetNativeResource().AsPointer<ID3D12Resource>(),
        //         &clearColor.r,
        //         0,
        //         nullptr);
        // }
    }
}

void CommandRecorder::ClearFramebuffer(
    const graphics::Framebuffer& framebuffer,
    std::optional<std::vector<std::optional<Color>>> color_clear_values,
    std::optional<float> depth_clear_value,
    std::optional<uint8_t> stencil_clear_value)
{
    using oxygen::graphics::d3d12::Framebuffer;
    using oxygen::graphics::detail::GetFormatInfo;

    const auto& fb = static_cast<const Framebuffer&>(framebuffer);
    auto* command_list_impl = GetConcreteCommandList();
    DCHECK_NOTNULL_F(command_list_impl);
    auto* d3d12_command_list = command_list_impl->GetCommandList();
    DCHECK_NOTNULL_F(d3d12_command_list);

    const auto& desc = fb.GetDescriptor();

    // Clear color attachments
    auto rtvs = fb.GetRenderTargetViews();
    for (size_t i = 0; i < rtvs.size(); ++i) {
        const auto& attachment = desc.color_attachments[i];
        const auto& format_info = GetFormatInfo(attachment.format);
        if (format_info.has_depth || format_info.has_stencil) {
            continue;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = {};
        rtv_handle.ptr = rtvs[i];
        Color clear_color = attachment.ResolveClearColor(
            color_clear_values && i < color_clear_values->size() ? (*color_clear_values)[i] : std::nullopt);
        const float color[4] = { clear_color.r, clear_color.g, clear_color.b, clear_color.a };
        d3d12_command_list->ClearRenderTargetView(rtv_handle, color, 0, nullptr);
    }

    // Clear depth/stencil attachment
    if (desc.depth_attachment.IsValid()) {
        D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = {};
        dsv_handle.ptr = fb.GetDepthStencilView();
        const auto& depth_format_info = GetFormatInfo(desc.depth_attachment.format);

        auto [depth, stencil] = desc.depth_attachment.ResolveDepthStencil(
            depth_clear_value, stencil_clear_value, desc.depth_attachment.format);

        D3D12_CLEAR_FLAGS clear_flags = static_cast<D3D12_CLEAR_FLAGS>(0);
        if (depth_format_info.has_depth) {
            clear_flags = static_cast<D3D12_CLEAR_FLAGS>(clear_flags | D3D12_CLEAR_FLAG_DEPTH);
        }
        if (depth_format_info.has_stencil) {
            clear_flags = static_cast<D3D12_CLEAR_FLAGS>(clear_flags | D3D12_CLEAR_FLAG_STENCIL);
        }

        if (clear_flags != 0) {
            d3d12_command_list->ClearDepthStencilView(
                dsv_handle,
                clear_flags,
                depth,
                stencil,
                0,
                nullptr);
        }
    }
}
