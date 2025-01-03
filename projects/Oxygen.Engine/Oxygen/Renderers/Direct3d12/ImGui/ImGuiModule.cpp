//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Direct3d12/ImGui/ImGuiModule.h"

#include <imgui_impl_dx12.h>

#include "Oxygen/Renderers/Direct3d12/CommandRecorder.h"
#include "Oxygen/Renderers/Direct3d12/Detail/DescriptorHeap.h"
#include "Oxygen/Renderers/Direct3d12/RenderTarget.h"
#include "Oxygen/Renderers/Direct3d12/Renderer.h"
#include "Oxygen/Renderers/Direct3d12/Types.h"

using namespace oxygen::renderer::d3d12;

void ImGuiModule::ImGuiBackendInit(const oxygen::Renderer* renderer)
{
  const auto d3d12_render = dynamic_cast<const Renderer*>(renderer);

  font_srv_handle_ = d3d12_render->SrvHeap().Allocate();
  ImGui::SetCurrentContext(GetImGuiContext());
  ImGui_ImplDX12_Init(GetMainDevice(), kFrameBufferCount, DXGI_FORMAT_R8G8B8A8_UNORM,
    d3d12_render->SrvHeap().Heap(), font_srv_handle_.cpu, font_srv_handle_.gpu);
}

void ImGuiModule::ImGuiBackendShutdown()
{
  ImGui_ImplDX12_Shutdown();

  // TODO: this is hacky and will be obsolete once DescriptorHandle is RAII friendly
  detail::GetRenderer().SrvHeap().Free(font_srv_handle_);
}

void ImGuiModule::ImGuiBackendNewFrame()
{
  ImGui_ImplDX12_NewFrame();
}

auto ImGuiModule::ImGuiBackendRenderRawData(const oxygen::Renderer* /*renderer*/, ImDrawData* draw_data)
  -> CommandListPtr
{
  auto& current_render_target = detail::GetRenderer().GetCurrentRenderTarget();

  // TODO: Refactor to remove CommandRecorder and use CommandList directly

  auto command_list = std::make_unique<CommandList>();
  CHECK_NOTNULL_F(command_list);
  try {
    command_list->Initialize(CommandListType::kGraphics);
    CHECK_EQ_F(command_list->GetState(), CommandList::State::kFree);
    command_list->OnBeginRecording();
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Failed to begin recording to a command list: {}", e.what());
    throw;
  }

  // Render Target
  D3D12_RESOURCE_BARRIER barrier {
    .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
    .Transition = {
      .pResource = current_render_target.GetResource(),
      .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
      .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
      .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET }
  };
  command_list->GetCommandList()->ResourceBarrier(1, &barrier);

  const D3D12_CPU_DESCRIPTOR_HANDLE render_target_views[1] = { current_render_target.Rtv().cpu };
  command_list->GetCommandList()->OMSetRenderTargets(1, render_target_views, FALSE, nullptr);
  ID3D12DescriptorHeap* heaps[] = {
    detail::GetRenderer().SrvHeap().Heap()
  };
  command_list->GetCommandList()->SetDescriptorHeaps(1, heaps);

  // cast the command list to d3d12 command list
  ImGui_ImplDX12_RenderDrawData(draw_data, command_list->GetCommandList());

  // End

  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
  command_list->GetCommandList()->ResourceBarrier(1, &barrier);

  try {
    command_list->OnEndRecording();
    return std::move(command_list);
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Recording failed: {}", e.what());
    return {};
  }
}
