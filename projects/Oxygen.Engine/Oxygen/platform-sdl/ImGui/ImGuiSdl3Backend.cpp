//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/platform-sdl/ImGui/ImGuiSdl3Backend.h"

#include <SDL3/SDL.h>

#include "Oxygen/platform-sdl/ImGui/imgui_impl_sdl3.h"
#include "Oxygen/Platform-sdl/Platform.h"

using oxygen::imgui::sdl3::ImGuiSdl3Backend;

void ImGuiSdl3Backend::NewFrame()
{
  ImGui_ImplSDL3_NewFrame();
}

void ImGuiSdl3Backend::OnInitialize(ImGuiContext* imgui_context)
{
  DCHECK_NOTNULL_F(imgui_context);
  ImGui::SetCurrentContext(imgui_context);

  const auto window = SDL_GetWindowFromID(window_id_);

  DCHECK_NOTNULL_F(window);
  ImGui_ImplSDL3_InitForD3D(window);

  // Adjust the scaling to take into account the current DPI
  const float window_scale = SDL_GetWindowDisplayScale(window);
  DLOG_F(INFO, "[{}] Using DPI scale: {}", ObjectName(), window_scale);
  auto& io = ImGui::GetIO();
  io.FontGlobalScale = window_scale;
  ImGui::GetStyle().ScaleAllSizes(window_scale);

  // Register to process SDL events
  const auto sdl3_platform = std::dynamic_pointer_cast<const platform::sdl::Platform>(platform_);
  DCHECK_NOTNULL_F(sdl3_platform);
  sdl3_platform->OnPlatformEvent().connect(
    [this](const SDL_Event& event, bool& capture_mouse, bool& capture_keyboard)
    {
      // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to
      // tell if dear imgui wants to use your inputs.
      // - When io.WantCaptureMouse is true, do not dispatch mouse input data to
      //   your main application, or clear/overwrite your copy of the mouse
      //   data.
      // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input
      //   data to your main application, or clear/overwrite your copy of the
      //   keyboard data.
      //   Generally you may always pass all inputs to dear imgui, and hide them
      //   from your application based on those two flags. If you have multiple
      //   SDL events and some of them are not meant to be used by dear imgui,
      //   you may need to filter events based on their windowID field.

      if (const auto handled = ImGui_ImplSDL3_ProcessEvent(&event); !handled) {
        capture_mouse = capture_keyboard = false;
      }
      else
      {
        capture_mouse = ImGui::GetIO().WantCaptureMouse;
        capture_keyboard = ImGui::GetIO().WantCaptureKeyboard;
      }
    });
}

void ImGuiSdl3Backend::OnShutdown()
{
  ImGui_ImplSDL3_Shutdown();
}
