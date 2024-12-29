//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Direct3d12/CommandRecorder.h"

#include <exception>
#include <memory>
#include <stdexcept>
#include <type_traits>

#include <d3d12.h>

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Renderers/Common/Types.h"
#include "Oxygen/Renderers/Direct3d12/CommandList.h"
#include "Oxygen/Renderers/Direct3d12/Detail/DescriptorHeap.h"
#include "Oxygen/Renderers/Direct3d12/Detail/WindowSurfaceImpl.h"
#include "Oxygen/Renderers/Direct3d12/Types.h"

using namespace oxygen::renderer::d3d12;

void CommandRecorder::Begin() {
  DCHECK_F(current_command_list_ == nullptr);

  //resource_state_cache_.OnBeginCommandBuffer();

  // TODO: consider recycling command lists
  auto command_list = std::make_unique<CommandList>();
  CHECK_NOTNULL_F(command_list);
  try {
    command_list->Initialize(GetQueueType());
    CHECK_EQ_F(command_list->GetState(), CommandList::State::kFree);
    command_list->OnBeginRecording();
  }
  catch (const std::exception& e) {
    LOG_F(ERROR, "Failed to begin recording to a command list: {}", e.what());
    throw;
  }

  current_command_list_ = std::move(command_list);

  ResetState();
}

oxygen::renderer::CommandListPtr CommandRecorder::End() {
  if (!current_command_list_) {
    throw std::runtime_error("No CommandList is being recorded");
  }

  D3D12_RESOURCE_BARRIER barrier{
    .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
    .Transition = {
      .pResource = current_render_target_->GetResource(),
      .Subresource = 0,
      .StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
      .StateAfter = D3D12_RESOURCE_STATE_PRESENT
    }
  };
  current_command_list_->GetCommandList()->ResourceBarrier(1, &barrier);

  try {
    current_command_list_->OnEndRecording();

    // TODO: consider recycling command lists

    return std::move(current_command_list_);
  }
  catch (const std::exception& e) {
    LOG_F(ERROR, "Recording failed: {}", e.what());
    return {};
  }
}

void CommandRecorder::Clear(uint32_t flags, uint32_t num_targets, const uint32_t* slots, const glm::vec4* colors,
                            float depth_value, uint8_t stencil_value)
{
  DCHECK_EQ_F(GetQueueType(), CommandListType::kGraphics, "Invalid queue type");
  CHECK_NOTNULL_F(current_render_target_);

  if (flags & kClearFlagsColor)
  {
    // TODO: temporarily accept only 1 target
    DCHECK_EQ_F(num_targets, 1u, "Only 1 render target is supported");

    for (uint32_t i = 0; i < num_targets; ++i)
    {
      // TODO: handle sub-resources

      const auto descriptor_handle = current_render_target_->Rtv().cpu;

      D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
      rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // Set the appropriate format
      rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
      rtv_desc.Texture2D.MipSlice = 0;
      rtv_desc.Texture2D.PlaneSlice = 0;

      GetMainDevice()->CreateRenderTargetView(current_render_target_->GetResource(), &rtv_desc, descriptor_handle);

      current_command_list_->GetCommandList()->ClearRenderTargetView(descriptor_handle, reinterpret_cast<const float*>(&colors[i]), 0, nullptr);
    }
  }

  //if (flags & (kClearFlagsDepth | kClearFlagsStencil))
  //{
  //  HeapAllocator& dsvAllocator = gDevice->GetDsvHeapAllocator();

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

void CommandRecorder::SetRenderTarget(const RenderTargetNoDeletePtr render_target)
{
  DCHECK_NOTNULL_F(render_target, "Invalid render target pointer");

  current_render_target_ = render_target;

  // Indicate that the back buffer will be used as a render target.
  const D3D12_RESOURCE_BARRIER barrier{
    .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
    .Transition = {
      .pResource = current_render_target_->GetResource(),
      .Subresource = 0,
      .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
      .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET
    }
  };
  current_command_list_->GetCommandList()->ResourceBarrier(1, &barrier);

  const D3D12_CPU_DESCRIPTOR_HANDLE render_target_views[1] = { current_render_target_->Rtv().cpu };
  current_command_list_->GetCommandList()->OMSetRenderTargets(static_cast<UINT>(1), render_target_views, FALSE, nullptr);
}

void CommandRecorder::ReleaseCommandRecorder() noexcept
{
  current_command_list_.reset();
}

void CommandRecorder::ResetState()
{
  //mGraphicsBindingState.Reset();
  //mComputeBindingState.Reset();

  //mCurrRenderTarget = nullptr;
  //mGraphicsPipelineState = nullptr;
  //mComputePipelineState = nullptr;

  //mGraphicsPipelineStateChanged = false;
  //mComputePipelineStateChanged = false;

  //mCurrPrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
  //mNumBoundVertexBuffers = 0u;
  //mBoundIndexBuffer = nullptr;

  //mVertexBufferChanged = false;
  //mIndexBufferChanged = false;

  //for (uint32 i = 0; i < NFE_RENDERER_MAX_VERTEX_BUFFERS; ++i)
  //{
  //  mBoundVertexBuffers[i] = nullptr;
  //}
}
