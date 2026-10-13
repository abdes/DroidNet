//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <memory>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/ImGui/api_export.h>
#include <Oxygen/Platform/Types.h>

struct ImGuiContext;

namespace oxygen {
class IAsyncEngine;
class Platform;
namespace graphics {
  class CommandRecorder;
  namespace imgui {
    class ImGuiGraphicsBackend;
  } // namespace imgui
} // namespace graphics
} // namespace oxygen

namespace oxygen::imgui {

class PlatformBackend;

//! Owns the engine's ImGui context and its frame lifecycle.
/*!
 On attach the module creates the graphics backend's ImGui renderer, which
 creates the context, and applies the Spectrum style, fonts and icons. Once
 bound to a window it starts an ImGui frame at `kFrameStart` and ends it at
 `kFrameEnd`; applications draw in between, usually in `OnGuiUpdate`.

 The module does not render. Whoever composites the frame calls `Render`
 into a bound target while `IsFrameActive()` holds.
*/
class ImGuiModule final : public engine::EngineModule {
  OXYGEN_TYPED(ImGuiModule)

public:
  OXGN_IMGUI_API ImGuiModule();
  OXGN_IMGUI_API ~ImGuiModule() override;

  OXYGEN_MAKE_NON_COPYABLE(ImGuiModule)
  OXYGEN_MAKE_NON_MOVABLE(ImGuiModule)

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "ImGuiModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> engine::ModulePriority override
  {
    return engine::kImGuiModulePriority;
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> engine::ModulePhaseMask override
  {
    return engine::MakeModuleMask<core::PhaseId::kFrameStart,
      core::PhaseId::kFrameEnd>();
  }

  OXGN_IMGUI_NDAPI auto OnAttached(observer_ptr<IAsyncEngine> engine) noexcept
    -> bool override;
  OXGN_IMGUI_API auto OnShutdown() noexcept -> void override;

  OXGN_IMGUI_API auto OnFrameStart(observer_ptr<engine::FrameContext> context)
    -> void override;
  OXGN_IMGUI_API auto OnFrameEnd(observer_ptr<engine::FrameContext> context)
    -> void override;

  //! Binds ImGui input and display metrics to a window; an invalid id unbinds.
  /*!
   The module unbinds itself when the window is about to be destroyed. No
   frame starts while no window is bound.
  */
  OXGN_IMGUI_API auto SetWindowId(platform::WindowIdType window_id) -> void;

  OXGN_IMGUI_NDAPI auto GetImGuiContext() const noexcept -> ImGuiContext*;

  //! True between this frame's `NewFrame` and `EndFrame`.
  [[nodiscard]] auto IsFrameActive() const noexcept -> bool
  {
    return frame_started_;
  }

  //! Records this frame's draw data into the recorder's bound framebuffer.
  OXGN_IMGUI_API auto Render(graphics::CommandRecorder& recorder) -> void;

private:
  auto ApplyDefaultStyleAndFonts() const -> void;
  auto UnregisterWindowHandler() noexcept -> void;

  std::shared_ptr<Platform> platform_;
  std::unique_ptr<graphics::imgui::ImGuiGraphicsBackend> graphics_backend_;
  std::unique_ptr<PlatformBackend> platform_backend_;
  platform::WindowIdType window_id_ { platform::kInvalidWindowId };
  std::size_t window_destroy_handler_token_ { 0U };
  bool frame_started_ { false };
};

} // namespace oxygen::imgui
