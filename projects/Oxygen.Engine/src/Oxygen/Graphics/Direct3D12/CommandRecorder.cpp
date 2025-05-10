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
#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Direct3D12/CommandList.h>
#include <Oxygen/Graphics/Direct3D12/Detail/WindowSurface.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Resources/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DescriptorHeap.h>

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
        [&](ResourceStates flag_to_check, D3D12_RESOURCE_STATES d3d12_equivalent) {
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

CommandRecorder::CommandRecorder(graphics::CommandList* command_list, graphics::CommandQueue* target_queue)
    : Base(command_list, target_queue)
{
    CreateRootSignature();
}

void CommandRecorder::Begin()
{
    graphics::CommandRecorder::Begin();

    // resource_state_cache_.OnBeginCommandBuffer();

    ResetState();
}

auto CommandRecorder::End() -> graphics::CommandList*
{
    if (current_render_target_ != nullptr) {
        auto* command_list = GetConcreteCommandList();
        DCHECK_NOTNULL_F(command_list);

        D3D12_RESOURCE_BARRIER barrier {
            .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
            .Transition = {
                .pResource = current_render_target_->GetResource(),
                .Subresource = 0,
                .StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
                .StateAfter = D3D12_RESOURCE_STATE_PRESENT }
        };
        command_list->GetCommandList()->ResourceBarrier(1, &barrier);
    }
    return graphics::CommandRecorder::End();
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

void CommandRecorder::Clear(const uint32_t flags, const uint32_t num_targets, const uint32_t* slots, const glm::vec4* colors,
    float depth_value, uint8_t stencil_value)
{
    auto* command_list = GetConcreteCommandList();
    DCHECK_NOTNULL_F(command_list);
    DCHECK_EQ_F(command_list->GetQueueRole(), QueueRole::kGraphics, "Invalid queue type");
    CHECK_NOTNULL_F(current_render_target_);

    if ((flags & kClearFlagsColor) != 0u) {
        // TODO: temporarily accept only 1 target
        DCHECK_EQ_F(num_targets, 1u, "Only 1 render target is supported");

        for (uint32_t i = 0; i < num_targets; ++i) {
            // TODO: handle sub-resources

            const auto descriptor_handle = current_render_target_->Rtv().cpu;

            D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
            rtv_desc.Format = kDefaultBackBufferFormat; // Set the appropriate format
            rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            rtv_desc.Texture2D.MipSlice = 0;
            rtv_desc.Texture2D.PlaneSlice = 0;

            GetGraphics().GetCurrentDevice()->CreateRenderTargetView(current_render_target_->GetResource(), &rtv_desc, descriptor_handle);

            command_list->GetCommandList()->ClearRenderTargetView(descriptor_handle, reinterpret_cast<const float*>(&colors[i]), 0, nullptr);
        }
    }

    // if (flags & (kClearFlagsDepth | kClearFlagsStencil))
    //{
    //   HeapAllocator& dsvAllocator = gDevice->GetDsvHeapAllocator();

    //  if (mCurrRenderTarget->GetDSV() == -1)
    //    return;

    //  D3D12_CPU_DESCRIPTOR_HANDLE handle = dsvAllocator.GetCpuHandle();
    //  handle.ptr += mCurrRenderTarget->GetDSV() * dsvAllocator.GetDescriptorSize();

    //  D3D12_CLEAR_FLAGS clearFlags = static_cast<D3D12_CLEAR_FLAGS>(0);
    //  if (flags & kClearFlagsDepth)
    //    clearFlags |= D3D12_CLEAR_FLAG_DEPTH;
    //  if (flags & kClearFlagsStencil)
    //    clearFlags |= D3D12_CLEAR_FLAG_STENCIL;

    //  mCommandList->ClearDepthStencilView(handle, clearFlags, depthValue, stencilValue, 0, NULL);
    // }
}
void CommandRecorder::SetVertexBuffers(
    uint32_t num,
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

void CommandRecorder::Draw(uint32_t vertex_num, uint32_t instances_num, uint32_t vertex_offset, uint32_t instance_offset)
{
    auto* command_list = GetConcreteCommandList();
    DCHECK_NOTNULL_F(command_list);
    DCHECK_EQ_F(command_list->GetQueueRole(), QueueRole::kGraphics, "Invalid queue type");
    CHECK_NOTNULL_F(current_render_target_);

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

    hr = GetGraphics().GetCurrentDevice()->CreateRootSignature(
        0,
        signature_blob->GetBufferPointer(),
        signature_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature_));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create root signature");
    }
}
void CommandRecorder::SetRenderTarget(std::unique_ptr<graphics::RenderTarget> render_target)
{
    auto* command_list = GetConcreteCommandList();
    DCHECK_NOTNULL_F(command_list);
    DCHECK_NOTNULL_F(render_target, "Invalid render target pointer");

    current_render_target_ = std::unique_ptr<RenderTarget>(
        static_cast<RenderTarget*>(render_target.release()));
    CHECK_NOTNULL_F(current_render_target_, "unexpected failed dynamic cast");

    // Indicate that the back buffer will be used as a render target.
    const D3D12_RESOURCE_BARRIER barrier {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .Transition = {
            .pResource = current_render_target_->GetResource(),
            .Subresource = 0,
            .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
            .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET }
    };
    command_list->GetCommandList()->ResourceBarrier(1, &barrier);

    const D3D12_CPU_DESCRIPTOR_HANDLE render_target_views[1] = { current_render_target_->Rtv().cpu };
    command_list->GetCommandList()->OMSetRenderTargets(1, render_target_views, FALSE, nullptr);
}

void CommandRecorder::SetPipelineState(
    const std::shared_ptr<IShaderByteCode>& vertex_shader,
    const std::shared_ptr<IShaderByteCode>& pixel_shader)
{
    auto* command_list = GetConcreteCommandList();
    DCHECK_NOTNULL_F(command_list);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.pRootSignature = root_signature_.Get();
    pso_desc.VS = { .pShaderBytecode = vertex_shader->Data(), .BytecodeLength = vertex_shader->Size() };
    pso_desc.PS = { .pShaderBytecode = pixel_shader->Data(), .BytecodeLength = pixel_shader->Size() };
    pso_desc.BlendState = kBlendState.disabled;
    pso_desc.SampleMask = UINT_MAX;
    pso_desc.RasterizerState = kRasterizerState.no_cull;
    pso_desc.DepthStencilState = kDepthState.disabled;
    pso_desc.InputLayout = { nullptr, 0 }; // Assuming no input layout for full-screen triangle
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = kDefaultBackBufferFormat;
    pso_desc.SampleDesc.Count = 1;

    HRESULT hr = GetGraphics().GetCurrentDevice()->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state_));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create pipeline state object");
    }

    command_list->GetCommandList()->SetPipelineState(pipeline_state_.Get());
    command_list->GetCommandList()->SetGraphicsRootSignature(root_signature_.Get());
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

void CommandRecorder::ExecuteBarriers(std::span<const Barrier> barriers)
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
