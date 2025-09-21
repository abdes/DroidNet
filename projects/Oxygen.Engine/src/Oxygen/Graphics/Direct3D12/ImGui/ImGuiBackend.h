//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

#include <d3d12.h>
#include <wrl/client.h>

#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/Graphics/Direct3D12/ImGui/imgui_impl_dx12.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>
#include <Oxygen/Imgui/ImGuiGraphicsBackend.h>

struct ImDrawData;
struct ImGuiContext;

namespace oxygen::graphics::d3d12 {

//! Simple adapter over imgui_impl_dx12.h backend
/*!
 This class provides a thin wrapper around the official ImGui D3D12 backend
 implementation. It manages initialization, shutdown, and integrates with
 the engine's bindless descriptor heap system.

 ### Key Features

 - **Minimal Overhead**: Direct delegation to imgui_impl_dx12.h
 - **Dedicated Heap**: Uses CBV_SRV_UAV:imgui heap for descriptor allocation
 - **Engine Integration**: Seamless integration with Oxygen's graphics system

 ### Usage Patterns

 Initialize once with the graphics system, then call NewFrame and
 Render as needed during rendering.

 @see ImGuiPass for render pass integration
 @see imgui_impl_dx12.h for underlying implementation
*/
class D3D12ImGuiGraphicsBackend final : public imgui::ImGuiGraphicsBackend {
public:
  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "ImGui D3D12";
  }

  OXGN_D3D12_API auto Init(std::weak_ptr<Graphics> gfx_weak) -> void override;
  OXGN_D3D12_API auto Shutdown() -> void override;
  OXGN_D3D12_API auto NewFrame() -> void override;
  OXGN_D3D12_API auto Render(CommandRecorder& recorder) -> void override;

  auto GetImGuiContext() -> ImGuiContext* override { return imgui_context_; }

private:
  ImGuiContext* imgui_context_ { nullptr };

  // ImGui D3D12 backend state
  std::unique_ptr<ImGui_ImplDX12_InitInfo> init_info_ {};
  bool initialized_ { false };

  // Descriptor heap for ImGui (managed by engine)
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> imgui_srv_heap_ {};
  UINT imgui_descriptor_increment_ { 0 };
  UINT next_descriptor_index_ { 0 };

  // Descriptor allocation callbacks for imgui_impl_dx12
  static auto SrvDescriptorAllocCallback(ImGui_ImplDX12_InitInfo* info,
    D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle,
    D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle) -> void;

  static auto SrvDescriptorFreeCallback(ImGui_ImplDX12_InitInfo* info,
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) -> void;
};

} // namespace oxygen::graphics::d3d12
