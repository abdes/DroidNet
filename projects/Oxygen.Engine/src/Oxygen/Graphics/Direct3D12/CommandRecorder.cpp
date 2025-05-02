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

#include <d3d12.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Graphics/Direct3D12/CommandList.h>
#include <Oxygen/Graphics/Direct3D12/Detail/WindowSurface.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Resources/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/Resources/DescriptorHeap.h>

using oxygen::graphics::d3d12::CommandRecorder;
using oxygen::graphics::d3d12::detail::GetGraphics;

CommandRecorder::CommandRecorder(oxygen::graphics::CommandList* command_list, oxygen::graphics::CommandQueue* target_queue)
    : Base(command_list, target_queue)
{
    CreateRootSignature();
}

void CommandRecorder::Begin()
{
    oxygen::graphics::CommandRecorder::Begin();

    auto* command_list = GetConcreteCommandList();
    DCHECK_NOTNULL_F(command_list);
    DCHECK_EQ_F(command_list->GetState(), CommandList::State::kFree);

    // resource_state_cache_.OnBeginCommandBuffer();

    command_list->OnBeginRecording();
    ResetState();
}

auto CommandRecorder::End() -> oxygen::graphics::CommandList*
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
    return oxygen::graphics::CommandRecorder::End();
}

void CommandRecorder::SetViewport(
    const float left, const float width, const float top, const float height,
    const float min_depth, const float max_depth)
{
    auto* command_list = GetConcreteCommandList();
    DCHECK_NOTNULL_F(command_list);
    DCHECK_EQ_F(command_list->GetQueueRole(), QueueRole::kGraphics, "Invalid queue type");

    D3D12_VIEWPORT viewport;
    viewport.TopLeftX = left;
    viewport.TopLeftY = top;
    viewport.Width = width;
    viewport.Height = height;
    viewport.MinDepth = min_depth;
    viewport.MaxDepth = max_depth;
    command_list->GetCommandList()->RSSetViewports(1, &viewport);
}

void CommandRecorder::SetScissors(
    const int32_t left, const int32_t top, const int32_t right, const int32_t bottom)
{
    auto* command_list = GetConcreteCommandList();
    DCHECK_NOTNULL_F(command_list);

    D3D12_RECT rect;
    rect.left = left;
    rect.top = top;
    rect.right = right;
    rect.bottom = bottom;
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
    //}
}
void CommandRecorder::SetVertexBuffers(uint32_t num, const BufferPtr* vertex_buffers, const uint32_t* strides,
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
void CommandRecorder::SetRenderTarget(const RenderTargetNoDeletePtr render_target)
{
    auto* command_list = GetConcreteCommandList();
    DCHECK_NOTNULL_F(command_list);
    DCHECK_NOTNULL_F(render_target, "Invalid render target pointer");

    current_render_target_ = static_cast<const RenderTarget*>(render_target);
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
    //{
    //   mBoundVertexBuffers[i] = nullptr;
    // }
}
