//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/Types.h>
#include <Oxygen/Platform/UiEvent.h>

namespace oxygen::platform {

namespace {

  constexpr auto kCursorCount
    = static_cast<size_t>(MouseCursor::kNotAllowed) + 1U;

  auto SystemCursorFor(const MouseCursor cursor) -> SDL_SystemCursor
  {
    switch (cursor) {
    case MouseCursor::kTextInput:
      return SDL_SYSTEM_CURSOR_TEXT;
    case MouseCursor::kResizeAll:
      return SDL_SYSTEM_CURSOR_MOVE;
    case MouseCursor::kResizeNS:
      return SDL_SYSTEM_CURSOR_NS_RESIZE;
    case MouseCursor::kResizeEW:
      return SDL_SYSTEM_CURSOR_EW_RESIZE;
    case MouseCursor::kResizeNESW:
      return SDL_SYSTEM_CURSOR_NESW_RESIZE;
    case MouseCursor::kResizeNWSE:
      return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
    case MouseCursor::kHand:
      return SDL_SYSTEM_CURSOR_POINTER;
    case MouseCursor::kWait:
      return SDL_SYSTEM_CURSOR_WAIT;
    case MouseCursor::kProgress:
      return SDL_SYSTEM_CURSOR_PROGRESS;
    case MouseCursor::kNotAllowed:
      return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
    case MouseCursor::kHidden:
    case MouseCursor::kArrow:
      break;
    }
    return SDL_SYSTEM_CURSOR_DEFAULT;
  }

} // namespace

// The cursors are created on first use. SDL frees them when it shuts down,
// which happens before this component is destroyed, so they are not destroyed
// here.
struct UiServices::Cursors {
  std::array<SDL_Cursor*, kCursorCount> system {};
  MouseCursor current { MouseCursor::kHidden };
  bool initialized { false };
};

UiServices::UiServices()
  : cursors_(std::make_unique<Cursors>())
{
}

UiServices::~UiServices() = default;

auto UiServices::GetClipboardText() const -> std::string
{
  char* const text = SDL_GetClipboardText();
  if (text == nullptr) {
    return {};
  }
  std::string result { text };
  SDL_free(text);
  return result;
}

auto UiServices::SetClipboardText(const std::string_view text) -> void
{
  // SDL needs a null-terminated string.
  SDL_SetClipboardText(std::string { text }.c_str());
}

auto UiServices::SetMouseCursor(const MouseCursor cursor) -> void
{
  if (cursor == MouseCursor::kHidden) {
    SDL_HideCursor();
    cursors_->current = cursor;
    cursors_->initialized = true;
    return;
  }

  auto& native = cursors_->system.at(static_cast<size_t>(cursor));
  if (native == nullptr) {
    native = SDL_CreateSystemCursor(SystemCursorFor(cursor));
  }
  // SDL_SetCursor has no early out, so only call it on a change.
  if (!cursors_->initialized || cursors_->current != cursor) {
    SDL_SetCursor(native);
    cursors_->current = cursor;
    cursors_->initialized = true;
  }
  SDL_ShowCursor();
}

auto UiServices::CaptureMouse(const bool capture) -> void
{
  SDL_CaptureMouse(capture);
}

auto UiServices::StartTextInput(
  const WindowIdType window_id, const TextInputArea& area) -> void
{
  SDL_Window* const window = SDL_GetWindowFromID(window_id);
  if (window == nullptr) {
    return;
  }
  const SDL_Rect rect {
    .x = static_cast<int>(area.x),
    .y = static_cast<int>(area.y),
    .w = static_cast<int>(area.width),
    .h = static_cast<int>(area.height),
  };
  SDL_SetTextInputArea(window, &rect, 0);
  if (!SDL_TextInputActive(window)) {
    SDL_StartTextInput(window);
  }
}

auto UiServices::StopTextInput(const WindowIdType window_id) -> void
{
  if (SDL_Window* const window = SDL_GetWindowFromID(window_id)) {
    SDL_StopTextInput(window);
  }
}

} // namespace oxygen::platform
