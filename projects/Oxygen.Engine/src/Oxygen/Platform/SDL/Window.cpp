//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Platform/Window.h"
#include "Oxygen/Base/Logging.h"
#include "Oxygen/Composition/Composition.h"
#include "Oxygen/OxCo/Nursery.h"
#include "Oxygen/Platform/Detail/Window_ManagerInterface.h"
#include "Oxygen/Platform/Platform.h"
#include "Oxygen/Platform/SDL/Wrapper.h"

using oxygen::platform::Window;

namespace {

auto CheckNotInFullScreenMode(const Window& window,
    const std::string& operation) -> bool
{
    if (window.FullScreen()) {
        DLOG_F(WARNING,
            "Window [{}] is in full-screen mode and cannot be {}d. Call "
            "`FullScreen(off)` first.",
            window.Id(), operation);
        return false;
    }
    return true;
}

auto CheckNotMinimized(const Window& window, const std::string& operation)
    -> bool
{
    if (window.Minimized()) {
        DLOG_F(WARNING,
            "Window [{}] is minimized and cannot be {}d. Call `Restore()` first.",
            window.Id(), operation);
        return false;
    }
    return true;
}

} // namespace

class Window::Data final : public Component {
    OXYGEN_COMPONENT(Data)
public:
    explicit Data(SDL_Window* window)
        : sdl_window_(window)
    {
        DCHECK_NOTNULL_F(window);
        id_ = sdl::GetWindowId(sdl_window_);
    }
    ~Data() override
    {
        if (sdl_window_ != nullptr) {
            LOG_F(INFO, "SDL3 Window[{}] destroyed", id_);
            sdl::DestroyWindow(sdl_window_);
        }
    }

    OXYGEN_MAKE_NON_COPYABLE(Data)
    OXYGEN_MAKE_NON_MOVABLE(Data)

    SDL_Window* sdl_window_ { nullptr };
    WindowIdType id_ { kInvalidWindowId };
    bool should_close_ { false };
    bool forced_close_ { false };
};

class Window::ManagerInterfaceImpl final : public Component, public window::ManagerInterface {
    OXYGEN_COMPONENT(ManagerInterfaceImpl)
    OXYGEN_COMPONENT_REQUIRES(Window::Data)
public:
    [[nodiscard]] auto Events() const -> co::Value<window::Event>&
    {
        return events_;
    }

    [[nodiscard]] auto CloseRequested() -> co::ParkingLot::Awaiter
    {
        return close_vote_aw_.Park();
    }

    void DoRestore() const override;
    void DoMaximize() const override;
    void DoResize(const window::ExtentT& extent) const override;
    void DoPosition(const window::PositionT& position) const override;
    void DispatchEvent(const window::Event event) const override { events_.Set(event); }
    void InitiateClose(co::Nursery& n) override;
    void DoClose() const;
    void RecordVote() const { close_vote_count_.Set(close_vote_count_.Get() - 1); }

protected:
    void UpdateDependencies(const Composition& composition) override
    {
        data_ = &composition.GetComponent<Data>();
    }

private:
    Data* data_ { nullptr };
    bool vote_in_progress_ { false };
    mutable co::Value<size_t> close_vote_count_ { 0 };
    co::ParkingLot close_vote_aw_;

    mutable co::Value<window::Event> events_ { window::Event::kUnknown };
};

void Window::ManagerInterfaceImpl::DoRestore() const
{
    sdl::RestoreWindow(data_->sdl_window_);
}

void Window::ManagerInterfaceImpl::DoMaximize() const
{
    sdl::MaximizeWindow(data_->sdl_window_);
}

void Window::ManagerInterfaceImpl::DoResize(const window::ExtentT& extent) const
{
    sdl::SetWindowSize(data_->sdl_window_, extent.width, extent.height);
}

void Window::ManagerInterfaceImpl::DoPosition(const window::PositionT& position) const
{
    sdl::SetWindowPosition(data_->sdl_window_, position.x, position.y);
}

void Window::ManagerInterfaceImpl::InitiateClose(co::Nursery& n)
{
    if (vote_in_progress_) {
        LOG_F(INFO, "Window [id = {}] close vote already in progress", data_->id_);
        return;
    }
    vote_in_progress_ = true;
    if (data_->forced_close_) {
        LOG_F(INFO, "Window [id = {}] requested to force close", data_->id_);
        DoClose();
        return;
    }

    data_->should_close_ = true;
    n.Start([this]() -> co::Co<> {
        // Start the async vote to close the window
        if (const auto voters_count = close_vote_aw_.ParkedCount();
            voters_count != 0) {
            close_vote_count_.Set(voters_count);
            close_vote_aw_.UnParkAll();
            co_await close_vote_count_.UntilEquals(0);
            vote_in_progress_ = false;
            DLOG_F(INFO, "Window [id = {}] vote complete -> {}", data_->id_, data_->should_close_);
        }
        // If the vote is successful, close the window
        if (data_->should_close_) {
            DoClose();
        }
    });
}

void Window::ManagerInterfaceImpl::DoClose() const
{
    LOG_F(INFO, "SDL3 Window[{}] is closing", data_->id_);
    sdl::DestroyWindow(data_->sdl_window_);
}

Window::Window(const window::Properties& props)
{
    // This will throw if the window creation failed
    auto* sdl_window = sdl::MakeWindow(
        props.title.c_str(),
        props.position ? props.position->x : SDL_WINDOWPOS_CENTERED,
        props.position ? props.position->y : SDL_WINDOWPOS_CENTERED,
        props.extent ? props.extent->width : SDL_WINDOWPOS_CENTERED,
        props.extent ? props.extent->height : SDL_WINDOWPOS_CENTERED,
        props.flags);
    DCHECK_NOTNULL_F(sdl_window);

    // Compose the window
    AddComponent<Data>(sdl_window);
    AddComponent<ManagerInterfaceImpl>();

    LOG_F(INFO, "SDL3 Window[{}] created", Id());
}

Window::~Window() = default;

auto Window::Id() const -> WindowIdType
{
    return GetComponent<Data>().id_;
}

auto Window::Native() const -> window::NativeHandles
{
    return sdl::GetNativeWindow(GetComponent<Data>().sdl_window_);
}
auto Window::Valid() const -> bool
{
    return Id() != kInvalidWindowId;
}

auto Window::Size() const -> window::ExtentT
{
    window::ExtentT extent {};
    sdl::GetWindowSize(GetComponent<Data>().sdl_window_, &extent.width, &extent.height);
    return extent;
}

auto Window::FrameBufferSize() const -> window::ExtentT
{
    window::ExtentT extent {};
    sdl::GetWindowSizeInPixels(GetComponent<Data>().sdl_window_, &extent.width, &extent.height);
    return extent;
}

auto Window::FullScreen() const -> bool
{
    const auto flag = sdl::GetWindowFlags(GetComponent<Data>().sdl_window_);
    return (flag & SDL_WINDOW_FULLSCREEN) != 0U;
}

auto Window::Maximized() const -> bool
{
    const auto flag = sdl::GetWindowFlags(GetComponent<Data>().sdl_window_);
    return (flag & SDL_WINDOW_MAXIMIZED) != 0U;
}

auto Window::Minimized() const -> bool
{
    const auto flag = sdl::GetWindowFlags(GetComponent<Data>().sdl_window_);
    return (flag & SDL_WINDOW_MINIMIZED) != 0U;
}

auto Window::Resizable() const -> bool
{
    const auto flag = sdl::GetWindowFlags(GetComponent<Data>().sdl_window_);
    return (flag & SDL_WINDOW_RESIZABLE) != 0U;
}

auto Window::BorderLess() const -> bool
{
    const auto flag = sdl::GetWindowFlags(GetComponent<Data>().sdl_window_);
    return (flag & SDL_WINDOW_BORDERLESS) != 0U;
}

auto Window::Position() const -> window::PositionT
{
    window::PositionT position {};
    sdl::GetWindowPosition(GetComponent<Data>().sdl_window_, &position.x, &position.y);
    return position;
}

auto Window::Title() const -> std::string
{
    return sdl::GetWindowTitle(GetComponent<Data>().sdl_window_);
}

auto Window::Show() const -> void
{
    sdl::ShowWindow(GetComponent<Data>().sdl_window_);
}

auto Window::Hide() const -> void
{
    sdl::HideWindow(GetComponent<Data>().sdl_window_);
}

void Window::EnterFullScreen() const
{
    sdl::SetWindowFullScreen(GetComponent<Data>().sdl_window_, true);
}

void Window::ExitFullScreen() const
{
    sdl::SetWindowFullScreen(GetComponent<Data>().sdl_window_, false);
}

void Window::Minimize() const
{
    sdl::MinimizeWindow(GetComponent<Data>().sdl_window_);
}

void Window::Maximize() const
{
    if (CheckNotInFullScreenMode(*this, "maximize")) {
        GetComponent<ManagerInterfaceImpl>().DoMaximize();
    }
}

void Window::Restore() const
{
    if (CheckNotInFullScreenMode(*this, "restore")) {
        GetComponent<ManagerInterfaceImpl>().DoRestore();
    }
}

void Window::SetMinimumSize(const window::ExtentT& extent) const
{
    sdl::SetWindowMinimumSize(GetComponent<Data>().sdl_window_, extent.width, extent.height);
}

void Window::SetMaximumSize(const window::ExtentT& extent) const
{
    sdl::SetWindowMaximumSize(GetComponent<Data>().sdl_window_, extent.width, extent.height);
}

void Window::EnableResizing() const
{
    // SDL behavior is inconsistent with OS interactive behavior on most
    // platforms. Therefore, we only allow a window to be resizable if it is not
    // a borderless window.
    DCHECK_F(!BorderLess());
    if (BorderLess()) {
        DLOG_F(WARNING, "Window [{}] is borderless and should not be be resizable", Id());
        return;
    }
    sdl::SetWindowResizable(GetComponent<Data>().sdl_window_, true);
}

void Window::DisableResizing() const
{
    sdl::SetWindowResizable(GetComponent<Data>().sdl_window_, false);
}

void Window::Resize(const window::ExtentT& extent) const
{
    if (CheckNotInFullScreenMode(*this, "resize") && CheckNotMinimized(*this, "resize")) {
        GetComponent<ManagerInterfaceImpl>().DoResize(extent);
    }
}

auto Window::MoveTo(const window::PositionT& position) const -> void
{
    if (CheckNotInFullScreenMode(*this, "re-position") && CheckNotMinimized(*this, "resize")) {
        if (Maximized()) {
            GetComponent<ManagerInterfaceImpl>().DoRestore();
        }
        GetComponent<ManagerInterfaceImpl>().DoPosition(position);
    }
}

void Window::SetTitle(const std::string& title) const
{
    sdl::SetWindowTitle(GetComponent<Data>().sdl_window_, title);
}

void Window::Activate() const
{
    sdl::RaiseWindow(GetComponent<Data>().sdl_window_);
}

void Window::KeepAlwaysOnTop(const bool always_on_top) const
{
    sdl::SetWindowAlwaysOnTop(GetComponent<Data>().sdl_window_, always_on_top);
}

void Window::RequestClose(const bool force) const
{
    if (GetComponent<Data>().should_close_) {
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

void Window::VoteToClose() const
{
    GetComponent<ManagerInterfaceImpl>().RecordVote();
}

void Window::VoteNotToClose() const
{
    GetComponent<ManagerInterfaceImpl>().RecordVote();
    auto& data = GetComponent<Data>();
    DCHECK_F(!data.forced_close_, "window is being force closed, but RequestNotToClose() was called");
    if (data.forced_close_ || !data.should_close_) {
        return;
    }
    LOG_F(INFO, "Window [id = {}] requested not to close", Id());
    data.should_close_ = false;
}

auto Window::Events() const -> co::Value<window::Event>&
{
    return GetComponent<ManagerInterfaceImpl>().Events();
}

auto Window::CloseRequested() const -> co::ParkingLot::Awaiter
{
    return GetComponent<ManagerInterfaceImpl>().CloseRequested();
}

auto Window::GetManagerInterface() const -> window::ManagerInterface&
{
    return GetComponent<ManagerInterfaceImpl>();
}
