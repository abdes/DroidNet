//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Platform/Detail/Window_ManagerInterface.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/SDL/Wrapper.h>
#include <Oxygen/Platform/Window.h>

using oxygen::platform::WindowManager;
using WindowEvent = oxygen::platform::window::Event;

namespace {

auto MapWindowEvent(const Uint32 event_type) -> WindowEvent
{
  switch (event_type) {
  case SDL_EVENT_WINDOW_SHOWN:
    return WindowEvent::kShown;
  case SDL_EVENT_WINDOW_HIDDEN:
    return WindowEvent::kHidden;
  case SDL_EVENT_WINDOW_EXPOSED:
    return WindowEvent::kExposed;
  case SDL_EVENT_WINDOW_MOVED:
    return WindowEvent::kMoved;
  case SDL_EVENT_WINDOW_RESIZED:
    return WindowEvent::kResized;
  case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    return WindowEvent::kPixelSizeChanged;
  case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
    return WindowEvent::kMetalViewResized;
  case SDL_EVENT_WINDOW_MINIMIZED:
    return WindowEvent::kMinimized;
  case SDL_EVENT_WINDOW_MAXIMIZED:
    return WindowEvent::kMaximized;
  case SDL_EVENT_WINDOW_RESTORED:
    return WindowEvent::kRestored;
  case SDL_EVENT_WINDOW_MOUSE_ENTER:
    return WindowEvent::kMouseEnter;
  case SDL_EVENT_WINDOW_MOUSE_LEAVE:
    return WindowEvent::kMouseLeave;
  case SDL_EVENT_WINDOW_FOCUS_GAINED:
    return WindowEvent::kFocusGained;
  case SDL_EVENT_WINDOW_FOCUS_LOST:
    return WindowEvent::kFocusLost;
  case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    return WindowEvent::kCloseRequested;
  case SDL_EVENT_WINDOW_ICCPROF_CHANGED:
    return WindowEvent::kIccProfChanged;
  case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
    return WindowEvent::kDisplayChanged;
  case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    return WindowEvent::kDisplayScaleChanged;
  case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
    return WindowEvent::kSafeAreaChanged;
  case SDL_EVENT_WINDOW_OCCLUDED:
    return WindowEvent::kOccluded;
  case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    return WindowEvent::kEnterFullscreen;
  case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
    return WindowEvent::kLeaveFullscreen;
  case SDL_EVENT_WINDOW_DESTROYED:
    return WindowEvent::kDestroyed;
  case SDL_EVENT_WINDOW_HDR_STATE_CHANGED:
    return WindowEvent::kHdrStateChanged;
  default:
    throw std::runtime_error("event type not supported");
  }
}

} // namespace

auto WindowManager::MakeWindow(const window::Properties& props)
  -> std::weak_ptr<Window>
{
  auto window = std::make_shared<Window>(props);
  windows_.push_back(window);
  return window;
}

auto WindowManager::ProcessPlatformEvents() -> co::Co<>
{
  DLOG_F(INFO, "Window Manager async event processing started");
  while (async_->IsRunning() && event_pump_->IsRunning()) {
    // Check if the event pump is still running. If not, the next event is a
    // dummy one that we should just ignore, and this loop should immediately
    // terminate.
    if (!event_pump_->IsRunning()) {
      event_pump_ = nullptr;
      DLOG_F(1, "Platform Input Events async processing stopped");
      break;
    }

    auto& event = co_await event_pump_->NextEvent();
    auto _ = co_await event_pump_->Lock();
    if (event.IsHandled()) {
      continue;
    }
    if (auto& sdl_event = *event.NativeEventAs<SDL_Event>();
      sdl_event.type >= SDL_EVENT_WINDOW_FIRST
      && sdl_event.type <= SDL_EVENT_WINDOW_LAST) {
      DLOG_F(2, "Window [id={}] event: {}", sdl_event.window.windowID,
        sdl::SdlEventName(sdl_event.window.type));
      if (sdl_event.type == SDL_EVENT_WINDOW_DESTROYED) {
        // Remove the window from the windows_ table
        std::erase_if(windows_, [&sdl_event](const auto& window) {
          return window->Id() == sdl_event.window.windowID;
        });
        LOG_F(INFO, "Window [id = {}] is closed", sdl_event.window.windowID);

        if (windows_.empty()) {
          last_window_closed_.Trigger();
        }
      } else {
        try {
          auto& window = WindowFromId(sdl_event.window.windowID);
          auto& window_mgt = window.GetManagerInterface();
          if (sdl_event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            window_mgt.InitiateClose(async_->Nursery());
          }
          window_mgt.DispatchEvent(MapWindowEvent(sdl_event.type));
        } catch (const std::exception& ex) {
          // SDL_EVENT_WINDOW_HIT_TEST event is ignored
          if (sdl_event.type != SDL_EVENT_WINDOW_HIT_TEST) {
            LOG_F(ERROR, "Window [id={}] event {} handling failed: {}",
              sdl_event.window.windowID, sdl::SdlEventName(sdl_event.type),
              ex.what());
          }
        }
      }
      event.SetHandled();
    }
  }
}

auto WindowManager::WindowFromId(WindowIdType window_id) const -> Window&
{
  const auto found = std::ranges::find_if(
    windows_, [window_id](auto& window) { return window->Id() == window_id; });
  // We should only call this method when we are sure the window id is valid
  DCHECK_NE_F(found, windows_.end(),
    "We should only call this method when we are sure the window id is valid");
  return **found;
}
