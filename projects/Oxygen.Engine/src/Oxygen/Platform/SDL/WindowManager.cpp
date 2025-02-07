//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Platform/Platform.h"
#include "Oxygen/Platform/SDL/Wrapper.h"

using oxygen::platform::WindowManager;

namespace {

auto MapWindowEvent(const Uint32 event_type) -> oxygen::platform::Window::Event
{
    switch (event_type) {
    case SDL_EVENT_WINDOW_SHOWN:
        return oxygen::platform::Window::Event::kShown;
    case SDL_EVENT_WINDOW_HIDDEN:
        return oxygen::platform::Window::Event::kHidden;
    case SDL_EVENT_WINDOW_EXPOSED:
        return oxygen::platform::Window::Event::kExposed;
    case SDL_EVENT_WINDOW_MOVED:
        return oxygen::platform::Window::Event::kMoved;
    case SDL_EVENT_WINDOW_RESIZED:
        return oxygen::platform::Window::Event::kResized;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        return oxygen::platform::Window::Event::kPixelSizeChanged;
    case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
        return oxygen::platform::Window::Event::kMetalViewResized;
    case SDL_EVENT_WINDOW_MINIMIZED:
        return oxygen::platform::Window::Event::kMinimized;
    case SDL_EVENT_WINDOW_MAXIMIZED:
        return oxygen::platform::Window::Event::kMaximized;
    case SDL_EVENT_WINDOW_RESTORED:
        return oxygen::platform::Window::Event::kRestored;
    case SDL_EVENT_WINDOW_MOUSE_ENTER:
        return oxygen::platform::Window::Event::kMouseEnter;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        return oxygen::platform::Window::Event::kMouseLeave;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        return oxygen::platform::Window::Event::kFocusGained;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return oxygen::platform::Window::Event::kFocusLost;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return oxygen::platform::Window::Event::kCloseRequested;
    case SDL_EVENT_WINDOW_ICCPROF_CHANGED:
        return oxygen::platform::Window::Event::kIccProfChanged;
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        return oxygen::platform::Window::Event::kDisplayChanged;
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        return oxygen::platform::Window::Event::kDisplayScaleChanged;
    case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
        return oxygen::platform::Window::Event::kSafeAreaChanged;
    case SDL_EVENT_WINDOW_OCCLUDED:
        return oxygen::platform::Window::Event::kOccluded;
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        return oxygen::platform::Window::Event::kEnterFullscreen;
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        return oxygen::platform::Window::Event::kLeaveFullscreen;
    case SDL_EVENT_WINDOW_DESTROYED:
        return oxygen::platform::Window::Event::kDestroyed;
    case SDL_EVENT_WINDOW_HDR_STATE_CHANGED:
        return oxygen::platform::Window::Event::kHdrStateChanged;
    default:
        throw std::runtime_error("event type not supported");
    }
}

} // namespace

auto WindowManager::MakeWindow(const Window::Properties& props) -> std::weak_ptr<Window>
{
    auto window = std::make_shared<Window>(props);
    windows_.push_back(window);
    return window;
}

auto WindowManager::ProcessPlatformEvents() -> co::Co<>
{
    while (true) {
        auto& event = co_await event_pump_->WaitForNextEvent();
        if (event.IsHandled()) {
            continue;
        }
        if (auto& sdl_event = *event.NativeEventAs<SDL_Event>();
            sdl_event.type >= SDL_EVENT_WINDOW_FIRST && sdl_event.type <= SDL_EVENT_WINDOW_LAST) {
            DLOG_F(INFO, "Window [id={}] event: {}",
                sdl_event.window.windowID,
                sdl::SdlEventName(sdl_event.window.type));
            if (sdl_event.type == SDL_EVENT_WINDOW_DESTROYED) {
                // Remove the window from the windows_ table
                std::erase_if(windows_,
                    [&sdl_event](auto const& window) {
                        return window->Id() == sdl_event.window.windowID;
                    });
                LOG_F(INFO, "Window [id = {}] is closed", sdl_event.window.windowID);

                if (windows_.empty()) {
                    last_window_closed_.Trigger();
                }
            } else {
                auto& window = WindowFromId(sdl_event.window.windowID);
                try {
                    if (sdl_event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                        window.InitiateClose(async_->Nursery());
                    }
                    window.DispatchEvent(MapWindowEvent(sdl_event.type));
                } catch (const std::exception& ex) {
                    // SDL_EVENT_WINDOW_HIT_TEST event is ignored
                    if (sdl_event.type != SDL_EVENT_WINDOW_HIT_TEST) {
                        LOG_F(ERROR, "Window [id={}] event {} handling failed: {}",
                            sdl_event.window.windowID, sdl::SdlEventName(sdl_event.type), ex.what());
                    }
                }
            }
            event.SetHandled();
        }
    }
}

auto WindowManager::WindowFromId(WindowIdType window_id) const -> Window&
{
    const auto found = std::ranges::find_if(windows_, [window_id](auto& window) {
        return window->Id() == window_id;
    });
    // We should only call this method when we are sure the window id is valid
    DCHECK_NE_F(found, windows_.end(),
        "We should only call this method when we are sure the window id is valid");
    return **found;
}
