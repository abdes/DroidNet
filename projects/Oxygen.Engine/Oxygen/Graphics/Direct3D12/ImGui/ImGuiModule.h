//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Imgui/ImguiModule.h"
#include "Oxygen/Graphics/Direct3d12/Detail/DescriptorHeap.h"

namespace oxygen::renderer::d3d12 {

  class ImGuiModule final : public imgui::ImguiModule
  {
  public:
    using Base = ImguiModule;

    template <typename... Args>
    explicit ImGuiModule(
      EngineWeakPtr engine,
      platform::WindowIdType window_id,
      Args &&... ctor_args)
      : Base("DX12 ImGui", engine, window_id, std::forward<Args>(ctor_args)...)
    {
    }

    ~ImGuiModule() override = default;

    OXYGEN_MAKE_NON_COPYABLE(ImGuiModule);
    OXYGEN_MAKE_NON_MOVEABLE(ImGuiModule);

  protected:
    void ImGuiBackendInit(const oxygen::Renderer* renderer) override;
    void ImGuiBackendShutdown() override;
    void ImGuiBackendNewFrame() override;
    auto ImGuiBackendRenderRawData(const oxygen::Renderer* renderer, ImDrawData* draw_data)
      -> CommandListPtr override;

  private:
    detail::DescriptorHandle font_srv_handle_{};
  };

}  // namespace oxygen::renderer::d3d12
