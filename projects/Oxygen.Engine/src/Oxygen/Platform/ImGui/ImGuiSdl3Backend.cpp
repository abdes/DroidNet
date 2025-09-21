//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <SDL3/SDL.h>
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Platform/ImGui/ImGuiSdl3Backend.h>
#include <Oxygen/Platform/ImGui/imgui_impl_sdl3.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/PlatformEvent.h>
#include <Oxygen/Platform/SDL/Wrapper.h>

namespace oxygen::platform::imgui {

ImGuiSdl3Backend::ImGuiSdl3Backend(std::shared_ptr<Platform> platform,
  const platform::WindowIdType window_id, ImGuiContext* imgui_context)
  : platform_(std::move(platform))
  , imgui_context_(imgui_context)
{
  DCHECK_NOTNULL_F(platform_);
  DCHECK_NOTNULL_F(imgui_context_);
  DCHECK_NE_F(window_id, platform::kInvalidWindowId);

  // Store context for async event forwarding
  ImGui::SetCurrentContext(imgui_context_.get());

  const auto window = platform::sdl::GetWindowFromId(window_id);
  DCHECK_NOTNULL_F(window);
  ImGui_ImplSDL3_InitForD3D(window);

  // Adjust the scaling to take into account the current DPI
  const float window_scale = platform::sdl::GetWindowDisplayScale(window);
  DLOG_F(INFO, "Using DPI scale: {}", window_scale);
  auto& io = ImGui::GetIO();
  io.FontGlobalScale = window_scale;
  ImGui::GetStyle().ScaleAllSizes(window_scale);

  // The Platform will invoke an event filter, if registered, before other event
  // processors. We'll register ours so ImGui gets the first opportunity to
  // handle events.
  platform_->RegisterEventFilter([&](const platform::PlatformEvent& event) {
    ProcessPlatformEvents(event);
  });
}

ImGuiSdl3Backend::~ImGuiSdl3Backend()
{
  // Clear the context pointer and unregister our event filter to immediately
  // prevent further event processing
  imgui_context_ = nullptr;
  platform_->ClearEventFilter();

  // Shutdown ImGui platform backend and allow the forwarder coroutine to
  // exit naturally when platform shuts down.
  ImGui_ImplSDL3_Shutdown();
}

// ReSharper disable once CppMemberFunctionMayBeStatic
auto ImGuiSdl3Backend::NewFrame() -> void { ImGui_ImplSDL3_NewFrame(); }

// ReSharper disable once CppMemberFunctionMayBeConst
auto ImGuiSdl3Backend::ProcessPlatformEvents(
  const platform::PlatformEvent& event) -> void
{
  // Bailout immediately if we have no context to work with.
  if (!imgui_context_) {
    return;
  }
  if (const auto sdl_event_ptr = event.NativeEventAs<SDL_Event>()) {
    const auto& sdl_event = *sdl_event_ptr;
    ImGui::SetCurrentContext(imgui_context_.get());
    if (ImGui_ImplSDL3_ProcessEvent(&sdl_event)) {
      const auto& io = ImGui::GetIO();

      const bool is_keyboard_event = (sdl_event.type == SDL_EVENT_KEY_DOWN
        || sdl_event.type == SDL_EVENT_KEY_UP
        || sdl_event.type == SDL_EVENT_TEXT_EDITING
        || sdl_event.type == SDL_EVENT_TEXT_INPUT);

      const bool is_mouse_event = (sdl_event.type == SDL_EVENT_MOUSE_MOTION
        || sdl_event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
        || sdl_event.type == SDL_EVENT_MOUSE_BUTTON_UP
        || sdl_event.type == SDL_EVENT_MOUSE_WHEEL);

      if ((io.WantCaptureKeyboard && is_keyboard_event)
        || (io.WantCaptureMouse && is_mouse_event)) {
        event.SetHandled();
      }
    }
  }
}

} // namespace oxygen::platform::imgui
