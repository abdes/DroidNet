//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "window.h"

#include <cassert>

#include "detail/wrapper.h"
#include "Oxygen/Base/logging.h"
#include "SDL3/SDL.h"

using oxygen::platform::sdl::Window;

namespace {
  constexpr oxygen::platform::sdl::detail::Wrapper kSdl;
}  // namespace

Window::Window(const std::string& title, const PixelExtent& extent)
  : sdl_window_(kSdl.MakeWindow(title.c_str(),
                                SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED,
                                extent.width,
                                extent.height,
                                {})) {
  LOG_F(INFO, "SDL3 Window[{}] created", Id());
}

Window::Window(const std::string& title,
               const PixelPosition& position,
               const PixelExtent& extent)
  : sdl_window_(kSdl.MakeWindow(title.c_str(),
                                position.x,
                                position.y,
                                extent.width,
                                extent.height,
                                {})) {
  LOG_F(INFO, "SDL3 Window[{}] created", Id());
}

Window::Window(const std::string& title,
               const PixelExtent& extent,
               const InitialFlags& flags)
  : sdl_window_(kSdl.MakeWindow(title.c_str(),
                                SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED,
                                extent.width,
                                extent.height,
                                flags)) {
  LOG_F(INFO, "SDL3 Window[{}] created", Id());
}

Window::Window(const std::string& title,
               const PixelPosition& position,
               const PixelExtent& extent,
               const InitialFlags& flags)
  : sdl_window_(kSdl.MakeWindow(title.c_str(),
                                position.x,
                                position.y,
                                extent.width,
                                extent.height,
                                flags)) {
  LOG_F(INFO, "SDL3 Window[{}] created", Id());
}

Window::~Window() {
  if (sdl_window_ != nullptr) {
    LOG_F(INFO, "SDL3 Window[{}] destroyed", Id());
    kSdl.DestroyWindow(sdl_window_);
  }
}

auto Window::Id() const -> oxygen::platform::WindowIdType {
  return sdl_window_ == nullptr ? kInvalidWindowId
    : kSdl.GetWindowId(sdl_window_);
}

auto Window::NativeWindow() const -> oxygen::platform::NativeWindowInfo {
  return sdl_window_ == nullptr ? NativeWindowInfo{}
  : kSdl.GetNativeWindow(sdl_window_);
}

void Window::Show() {
  kSdl.ShowWindow(sdl_window_);
}

void Window::Hide() {
  kSdl.HideWindow(sdl_window_);
}

void Window::FullScreen(const bool full_screen) {
  kSdl.SetWindowFullScreen(sdl_window_, full_screen);
}

auto Window::IsFullScreen() const -> bool {
  const auto flag = kSdl.GetWindowFlags(sdl_window_);
  return (flag & SDL_WINDOW_FULLSCREEN) != 0U;
}

void Window::DoMaximize() {
  kSdl.MaximizeWindow(sdl_window_);
  if (IsBorderLess()) {
    // Get the display on which the window is located
    const auto display_id = SDL_GetDisplayForWindow(sdl_window_);

    // Get display usable bounds
    SDL_Rect usable_area{};
    SDL_GetDisplayUsableBounds(display_id, &usable_area);
    LOG_SCOPE_F(1, "Window maximized to size {}", nostd::to_string(Size()));
    DLOG_F(INFO,
           "Display usable bounds x={} y={} w={} h={}",
           usable_area.x,
           usable_area.y,
           usable_area.w,
           usable_area.h);
    DLOG_F(INFO, "Window position {}", nostd::to_string(Position()));
  }
}

auto Window::IsMaximized() const -> bool {
  const auto flag = kSdl.GetWindowFlags(sdl_window_);
  return (flag & SDL_WINDOW_MAXIMIZED) != 0U;
}

void Window::Minimize() {
  kSdl.MinimizeWindow(sdl_window_);
}

auto Window::IsMinimized() const -> bool {
  const auto flag = kSdl.GetWindowFlags(sdl_window_);
  return (flag & SDL_WINDOW_MINIMIZED) != 0U;
}

void Window::DoRestore() {
  kSdl.RestoreWindow(sdl_window_);
}

void Window::DoResize(const PixelExtent& extent) {
  kSdl.SetWindowSize(sdl_window_, extent.width, extent.height);
}

auto Window::Size() const -> oxygen::PixelExtent {
  PixelExtent extent{};
  kSdl.GetWindowSize(sdl_window_, &extent.width, &extent.height);
  return extent;
}

void Window::MinimumSize(const PixelExtent& extent) {
  kSdl.SetWindowMinimumSize(sdl_window_, extent.width, extent.height);
}

void Window::MaximumSize(const PixelExtent& extent) {
  kSdl.SetWindowMaximumSize(sdl_window_, extent.width, extent.height);
}

void Window::Resizable(const bool resizable) {
  // SDL behavior is inconsistent with OS interactive behavior on most
  // platforms. Therefore, we only allow a window to be resizable if it is not
  // a borderless window.
  assert(!resizable || !IsBorderLess());
  kSdl.SetWindowResizable(sdl_window_, resizable);
}

auto Window::IsResizable() const -> bool {
  const auto flag = kSdl.GetWindowFlags(sdl_window_);
  return (flag & SDL_WINDOW_RESIZABLE) != 0U;
}

auto Window::IsBorderLess() const -> bool {
  const auto flag = kSdl.GetWindowFlags(sdl_window_);
  return (flag & SDL_WINDOW_BORDERLESS) != 0U;
}

void Window::DoPosition(const PixelPosition& position) {
  kSdl.SetWindowPosition(sdl_window_, position.x, position.y);
}

auto Window::Position() const -> oxygen::PixelPosition {
  PixelPosition position{};
  kSdl.GetWindowPosition(sdl_window_, &position.x, &position.y);
  return position;
}

void Window::Title(const std::string& title) {
  kSdl.SetWindowTitle(sdl_window_, title);
}

auto Window::Title() const -> std::string {
  return kSdl.GetWindowTitle(sdl_window_);
}

void Window::Activate() {
  kSdl.RaiseWindow(sdl_window_);
}

void Window::AlwaysOnTop(const bool always_on_top) {
  kSdl.SetWindowAlwaysOnTop(sdl_window_, always_on_top);
}

auto Window::CreateRenderer() const -> SDL_Renderer* {
  return kSdl.CreateRenderer(sdl_window_);
}

void Window::ProcessCloseRequest(bool force) {
  SDL_Event event{
      .window{
          .type = SDL_EVENT_WINDOW_CLOSE_REQUESTED,
          .reserved = 0,
          .timestamp = SDL_GetTicksNS(),
          .windowID = Id(),
          .data1 = 0,
          .data2 = 0,
      },
  };
  kSdl.PushEvent(&event);
}

auto Window::GetFrameBufferSize() const -> oxygen::PixelExtent {
  int width{ 0 };
  int height{ 0 };
  kSdl.GetWindowSizeInPixels(sdl_window_, &width, &height);
  return { width, height };
}
