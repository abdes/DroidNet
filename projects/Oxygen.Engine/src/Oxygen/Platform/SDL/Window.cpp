//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Platform/Window.h"

#include "Nursery.h"
#include "Oxygen/Base/Logging.h"
#include "Oxygen/Platform/SDL/Wrapper.h"

using oxygen::platform::Window;

namespace {

auto CheckNotInFullScreenMode(const Window& window,
    const std::string& operation) -> bool
{
    if (window.IsFullScreen()) {
        DLOG_F(WARNING,
            "Window [{}] is in full-screen mode and cannot be {}d. Call "
            "`FullScreen(off)` first.",
            window.Id(), operation);
        return false;
    }
    return true;
}

auto CheckNotBorderless(const Window& window, const std::string& operation)
    -> bool
{
    if (window.IsBorderLess()) {
        DLOG_F(WARNING,
            "Window [{}] is borderless and cannot be {}d.",
            window.Id(), operation);
        return false;
    }
    return true;
}

auto CheckNotMinimized(const Window& window, const std::string& operation)
    -> bool
{
    if (window.IsMinimized()) {
        DLOG_F(WARNING,
            "Window [{}] is minimized and cannot be {}d. Call `Restore()` first.",
            window.Id(), operation);
        return false;
    }
    return true;
}

auto CheckIsResizable(const Window& window, const std::string& operation)
    -> bool
{
    if (!window.IsResizable()) {
        DLOG_F(WARNING,
            "Window [{}] is setup to be not resizable and cannot be {}d.",
            window.Id(), operation);
        return false;
    }
    return true;
}
} // namespace

struct Window::Impl {
    explicit Impl(SDL_Window* window)
        : sdl_window(window)
    {
        DCHECK_NOTNULL_F(window);
        id = sdl::GetWindowId(sdl_window);
    }
    ~Impl()
    {
        if (sdl_window != nullptr) {
            LOG_F(INFO, "SDL3 Window[{}] destroyed", id);
            sdl::DestroyWindow(sdl_window);
        }
    }

    OXYGEN_MAKE_NON_COPYABLE(Impl)
    OXYGEN_MAKE_NON_MOVEABLE(Impl)

    SDL_Window* sdl_window;
    WindowIdType id { kInvalidWindowId };
    bool should_close { false };
    bool forced_close { false };
};

Window::Window(const Properties& props)
    : impl_(std::make_unique<Impl>(
          sdl::MakeWindow(
              props.title.c_str(),
              props.position ? props.position->x : SDL_WINDOWPOS_CENTERED,
              props.position ? props.position->y : SDL_WINDOWPOS_CENTERED,
              props.extent ? props.extent->width : SDL_WINDOWPOS_CENTERED,
              props.extent ? props.extent->height : SDL_WINDOWPOS_CENTERED,
              props.flags)))
{
    LOG_F(INFO, "SDL3 Window[{}] created", Id());
}

Window::~Window() = default;

auto Window::Id() const -> WindowIdType
{
    return impl_->id;
}

[[nodiscard]] auto Window::NativeWindow() const -> NativeWindowInfo
{
    return impl_->sdl_window == nullptr
        ? NativeWindowInfo {}
        : sdl::GetNativeWindow(impl_->sdl_window);
}

void Window::Maximize() const
{
    if (CheckNotInFullScreenMode(*this, "maximize")) {
        DoMaximize();
    }
}

void Window::Restore()
{
    if (CheckNotInFullScreenMode(*this, "restore")) {
        DoRestore();
    }
}

void Window::Size(const PixelExtent& extent)
{
    if (CheckNotInFullScreenMode(*this, "resize")
        && CheckNotBorderless(*this, "resize")
        && CheckIsResizable(*this, "resize")
        && CheckNotMinimized(*this, "resize")) {
        DoResize(extent);
    }
}

void Window::Position(const PixelPosition& position)
{
    if (CheckNotInFullScreenMode(*this, "re-position")
        && CheckNotMinimized(*this, "resize")) {
        if (IsMaximized()) {
            DoRestore();
        }
        DoPosition(position);
    }
}

void Window::Show() const { sdl::ShowWindow(impl_->sdl_window); }

void Window::Hide() const { sdl::HideWindow(impl_->sdl_window); }

void Window::FullScreen(const bool full_screen) const
{
    sdl::SetWindowFullScreen(impl_->sdl_window, full_screen);
}

auto Window::IsFullScreen() const -> bool
{
    const auto flag = sdl::GetWindowFlags(impl_->sdl_window);
    return (flag & SDL_WINDOW_FULLSCREEN) != 0U;
}

void Window::DoMaximize() const
{
    sdl::MaximizeWindow(impl_->sdl_window);
    if (IsBorderLess()) {
        // Get the display on which the window is located
        const auto display_id = SDL_GetDisplayForWindow(impl_->sdl_window);

        // Get display usable bounds
        SDL_Rect usable_area {};
        SDL_GetDisplayUsableBounds(display_id, &usable_area);
        LOG_SCOPE_F(1, "Window maximized");
        DLOG_F(1, "Window size {}", nostd::to_string(Size()).c_str());
        DLOG_F(INFO, "Display usable bounds x={} y={} w={} h={}", usable_area.x,
            usable_area.y, usable_area.w, usable_area.h);
        DLOG_F(INFO, "Window position {}", nostd::to_string(Position()));
    }
}

auto Window::IsMaximized() const -> bool
{
    const auto flag = sdl::GetWindowFlags(impl_->sdl_window);
    return (flag & SDL_WINDOW_MAXIMIZED) != 0U;
}

void Window::Minimize() const { sdl::MinimizeWindow(impl_->sdl_window); }

auto Window::IsMinimized() const -> bool
{
    const auto flag = sdl::GetWindowFlags(impl_->sdl_window);
    return (flag & SDL_WINDOW_MINIMIZED) != 0U;
}

void Window::DoRestore() const { sdl::RestoreWindow(impl_->sdl_window); }

void Window::DoResize(const PixelExtent& extent) const
{
    sdl::SetWindowSize(impl_->sdl_window, extent.width, extent.height);
}

auto Window::Size() const -> PixelExtent
{
    PixelExtent extent {};
    sdl::GetWindowSize(impl_->sdl_window, &extent.width, &extent.height);
    return extent;
}

void Window::MinimumSize(const PixelExtent& extent) const
{
    sdl::SetWindowMinimumSize(impl_->sdl_window, extent.width, extent.height);
}

void Window::MaximumSize(const PixelExtent& extent) const
{
    sdl::SetWindowMaximumSize(impl_->sdl_window, extent.width, extent.height);
}

void Window::Resizable(const bool resizable) const
{
    // SDL behavior is inconsistent with OS interactive behavior on most
    // platforms. Therefore, we only allow a window to be resizable if it is not
    // a borderless window.
    DCHECK_F(!resizable || !IsBorderLess());
    sdl::SetWindowResizable(impl_->sdl_window, resizable);
}

auto Window::IsResizable() const -> bool
{
    const auto flag = sdl::GetWindowFlags(impl_->sdl_window);
    return (flag & SDL_WINDOW_RESIZABLE) != 0U;
}

auto Window::IsBorderLess() const -> bool
{
    const auto flag = sdl::GetWindowFlags(impl_->sdl_window);
    return (flag & SDL_WINDOW_BORDERLESS) != 0U;
}

void Window::DoPosition(const PixelPosition& position) const
{
    sdl::SetWindowPosition(impl_->sdl_window, position.x, position.y);
}

void Window::InitiateClose(co::Nursery& n)
{
    if (impl_->forced_close) {
        LOG_F(INFO, "Window [id = {}] requested to force close", Id());
        DoClose();
        return;
    }

    n.Start([this]() -> co::Co<> {
        // Start the vote to close the window
        impl_->should_close = true;
        auto voters_count = close_vote_aw_.ParkedCount();
        if (voters_count != 0) {
            close_vote_count_.Set(voters_count);
            close_vote_aw_.UnParkAll();
            co_await close_vote_count_.UntilEquals(0);
        }
        // If the vote is successful, close the window
        if (impl_->should_close) {
            DoClose();
        }
    });
}

void Window::DoClose()
{
    LOG_F(INFO, "SDL3 Window[{}] is closing", Id());
    sdl::DestroyWindow(impl_->sdl_window);
}

auto Window::Position() const -> PixelPosition
{
    PixelPosition position {};
    sdl::GetWindowPosition(impl_->sdl_window, &position.x, &position.y);
    return position;
}

void Window::Title(const std::string& title) const
{
    sdl::SetWindowTitle(impl_->sdl_window, title);
}

auto Window::Title() const -> std::string
{
    return sdl::GetWindowTitle(impl_->sdl_window);
}

void Window::Activate() const { sdl::RaiseWindow(impl_->sdl_window); }

void Window::AlwaysOnTop(const bool always_on_top) const
{
    sdl::SetWindowAlwaysOnTop(impl_->sdl_window, always_on_top);
}

void Window::RequestNotToClose() const
{
    DCHECK_F(!impl_->forced_close);
    impl_->should_close = false;
}

void Window::RequestClose(const bool force) const
{
    if (impl_->should_close) {
        LOG_F(INFO, "Ongoing request to close the window exists, ignoring new request");
        return;
    }
    LOG_F(INFO, "Window [id = {}] requested to close(force={}) from code", Id(), force);
    SDL_Event event {
        .window {
            .type = SDL_EVENT_WINDOW_CLOSE_REQUESTED,
            .reserved = 0,
            .timestamp = SDL_GetTicksNS(),
            .windowID = Id(),
            .data1 = 0,
            .data2 = 0,
        },
    };
    sdl::PushEvent(&event);
}

auto Window::GetFrameBufferSize() const -> PixelExtent
{
    int width { 0 };
    int height { 0 };
    sdl::GetWindowSizeInPixels(impl_->sdl_window, &width, &height);
    return { width, height };
}
