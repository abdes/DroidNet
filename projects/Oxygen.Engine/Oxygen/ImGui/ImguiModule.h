//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Core/Module.h"
#include "Oxygen/Graphics/Common/Types.h"
#include "Oxygen/api_export.h"

struct ImGuiContext;
struct ImDrawData;

namespace oxygen::imgui {

class ImGuiRenderInterface;
class ImGuiPlatformBackend;

class ImguiModule : public core::Module
{
 public:
  using Base = Module;

  template <typename... Args>
  explicit ImguiModule(
    const char* name, EngineWeakPtr engine,
    const platform::WindowIdType window_id,
    Args&&... ctor_args)
    : Base(name, engine, std::forward<Args>(ctor_args)...)
    , window_id_(window_id)
  {
  }

  OXYGEN_API ~ImguiModule() override;

  OXYGEN_MAKE_NON_COPYABLE(ImguiModule);
  OXYGEN_MAKE_NON_MOVEABLE(ImguiModule);

  OXYGEN_API auto ProcessInput(const platform::InputEvent& event) -> void override;
  OXYGEN_API auto Update(Duration delta_time) -> void override;
  OXYGEN_API auto FixedUpdate() -> void override;

  OXYGEN_API virtual auto GetRenderInterface() -> ImGuiRenderInterface;

 protected:
  OXYGEN_API void OnInitialize(const graphics::Renderer* renderer) override;
  OXYGEN_API void OnShutdown() override;

  OXYGEN_API virtual void ImGuiBackendInit(const graphics::Renderer* renderer) = 0;
  OXYGEN_API virtual void ImGuiBackendShutdown() = 0;
  OXYGEN_API virtual void ImGuiBackendNewFrame() = 0;
  OXYGEN_API virtual auto ImGuiBackendRenderRawData(const graphics::Renderer* renderer, ImDrawData* draw_data)
    -> graphics::CommandListPtr
    = 0;

  [[nodiscard]] auto GetImGuiContext() const { return imgui_context_; }
  [[nodiscard]] auto GetWindowId() const { return window_id_; }

 private:
  friend class ImGuiRenderInterface;
  auto NewFrame(const graphics::Renderer* renderer) -> void;
  auto ImGuiRender(const graphics::Renderer* renderer) -> graphics::CommandListPtr;

  auto Render(const graphics::Renderer* /*renderer*/) -> void override { }

  ImGuiContext* imgui_context_ { nullptr };
  platform::WindowIdType window_id_ {};
  std::unique_ptr<ImGuiPlatformBackend> imgui_platform_ {};
};

} // namespace oxygen
