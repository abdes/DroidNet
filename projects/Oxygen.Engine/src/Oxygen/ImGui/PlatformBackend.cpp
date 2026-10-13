//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cfloat>
#include <chrono>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/ImGui/PlatformBackend.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/Types.h>
#include <Oxygen/Platform/UiEvent.h>
#include <Oxygen/Platform/Window.h>

namespace oxygen::imgui {

namespace {

  using platform::Key;
  using platform::MouseButton;
  using platform::MouseCursor;

  // ImGui reads the combined modifier state, not the modifier keys.
  constexpr auto kLeft = 0U;
  constexpr auto kRight = 1U;

  // The frame time ImGui is given on the first frame, when there is no
  // previous frame to measure against.
  constexpr auto kFirstFrameDeltaTime = 1.0F / 60.0F;

  // How far a held button must move before the mouse is captured, in pixels.
  constexpr auto kDragThreshold = 1.0F;

  auto ToImGuiKey(const Key key) -> ImGuiKey
  {
    switch (key) {
    case Key::kBackSpace:
      return ImGuiKey_Backspace;
    case Key::kDelete:
      return ImGuiKey_Delete;
    case Key::kTab:
      return ImGuiKey_Tab;
    case Key::kReturn:
      return ImGuiKey_Enter;
    case Key::kPause:
      return ImGuiKey_Pause;
    case Key::kEscape:
      return ImGuiKey_Escape;
    case Key::kSpace:
      return ImGuiKey_Space;

    case Key::kKeypad0:
      return ImGuiKey_Keypad0;
    case Key::kKeypad1:
      return ImGuiKey_Keypad1;
    case Key::kKeypad2:
      return ImGuiKey_Keypad2;
    case Key::kKeypad3:
      return ImGuiKey_Keypad3;
    case Key::kKeypad4:
      return ImGuiKey_Keypad4;
    case Key::kKeypad5:
      return ImGuiKey_Keypad5;
    case Key::kKeypad6:
      return ImGuiKey_Keypad6;
    case Key::kKeypad7:
      return ImGuiKey_Keypad7;
    case Key::kKeypad8:
      return ImGuiKey_Keypad8;
    case Key::kKeypad9:
      return ImGuiKey_Keypad9;
    case Key::kKeypadPeriod:
      return ImGuiKey_KeypadDecimal;
    case Key::kKeypadDivide:
      return ImGuiKey_KeypadDivide;
    case Key::kKeypadMultiply:
      return ImGuiKey_KeypadMultiply;
    case Key::kKeypadMinus:
      return ImGuiKey_KeypadSubtract;
    case Key::kKeypadPlus:
      return ImGuiKey_KeypadAdd;
    case Key::kKeypadEnter:
      return ImGuiKey_KeypadEnter;
    case Key::kKeypadEquals:
      return ImGuiKey_KeypadEqual;

    case Key::kUpArrow:
      return ImGuiKey_UpArrow;
    case Key::kDownArrow:
      return ImGuiKey_DownArrow;
    case Key::kRightArrow:
      return ImGuiKey_RightArrow;
    case Key::kLeftArrow:
      return ImGuiKey_LeftArrow;

    case Key::kInsert:
      return ImGuiKey_Insert;
    case Key::kHome:
      return ImGuiKey_Home;
    case Key::kEnd:
      return ImGuiKey_End;
    case Key::kPageUp:
      return ImGuiKey_PageUp;
    case Key::kPageDown:
      return ImGuiKey_PageDown;

    case Key::kF1:
      return ImGuiKey_F1;
    case Key::kF2:
      return ImGuiKey_F2;
    case Key::kF3:
      return ImGuiKey_F3;
    case Key::kF4:
      return ImGuiKey_F4;
    case Key::kF5:
      return ImGuiKey_F5;
    case Key::kF6:
      return ImGuiKey_F6;
    case Key::kF7:
      return ImGuiKey_F7;
    case Key::kF8:
      return ImGuiKey_F8;
    case Key::kF9:
      return ImGuiKey_F9;
    case Key::kF10:
      return ImGuiKey_F10;
    case Key::kF11:
      return ImGuiKey_F11;
    case Key::kF12:
      return ImGuiKey_F12;
    case Key::kF13:
      return ImGuiKey_F13;
    case Key::kF14:
      return ImGuiKey_F14;
    case Key::kF15:
      return ImGuiKey_F15;

    case Key::kAlpha0:
      return ImGuiKey_0;
    case Key::kAlpha1:
      return ImGuiKey_1;
    case Key::kAlpha2:
      return ImGuiKey_2;
    case Key::kAlpha3:
      return ImGuiKey_3;
    case Key::kAlpha4:
      return ImGuiKey_4;
    case Key::kAlpha5:
      return ImGuiKey_5;
    case Key::kAlpha6:
      return ImGuiKey_6;
    case Key::kAlpha7:
      return ImGuiKey_7;
    case Key::kAlpha8:
      return ImGuiKey_8;
    case Key::kAlpha9:
      return ImGuiKey_9;

    case Key::kQuote:
      return ImGuiKey_Apostrophe;
    case Key::kComma:
      return ImGuiKey_Comma;
    case Key::kMinus:
      return ImGuiKey_Minus;
    case Key::kPeriod:
      return ImGuiKey_Period;
    case Key::kSlash:
      return ImGuiKey_Slash;
    case Key::kSemicolon:
      return ImGuiKey_Semicolon;
    case Key::kEquals:
      return ImGuiKey_Equal;
    case Key::kLeftBracket:
      return ImGuiKey_LeftBracket;
    case Key::kBackslash:
      return ImGuiKey_Backslash;
    case Key::kRightBracket:
      return ImGuiKey_RightBracket;
    case Key::kBackQuote:
      return ImGuiKey_GraveAccent;

    case Key::kA:
      return ImGuiKey_A;
    case Key::kB:
      return ImGuiKey_B;
    case Key::kC:
      return ImGuiKey_C;
    case Key::kD:
      return ImGuiKey_D;
    case Key::kE:
      return ImGuiKey_E;
    case Key::kF:
      return ImGuiKey_F;
    case Key::kG:
      return ImGuiKey_G;
    case Key::kH:
      return ImGuiKey_H;
    case Key::kI:
      return ImGuiKey_I;
    case Key::kJ:
      return ImGuiKey_J;
    case Key::kK:
      return ImGuiKey_K;
    case Key::kL:
      return ImGuiKey_L;
    case Key::kM:
      return ImGuiKey_M;
    case Key::kN:
      return ImGuiKey_N;
    case Key::kO:
      return ImGuiKey_O;
    case Key::kP:
      return ImGuiKey_P;
    case Key::kQ:
      return ImGuiKey_Q;
    case Key::kR:
      return ImGuiKey_R;
    case Key::kS:
      return ImGuiKey_S;
    case Key::kT:
      return ImGuiKey_T;
    case Key::kU:
      return ImGuiKey_U;
    case Key::kV:
      return ImGuiKey_V;
    case Key::kW:
      return ImGuiKey_W;
    case Key::kX:
      return ImGuiKey_X;
    case Key::kY:
      return ImGuiKey_Y;
    case Key::kZ:
      return ImGuiKey_Z;

    case Key::kNumLock:
      return ImGuiKey_NumLock;
    case Key::kCapsLock:
      return ImGuiKey_CapsLock;
    case Key::kScrollLock:
      return ImGuiKey_ScrollLock;
    case Key::kRightShift:
      return ImGuiKey_RightShift;
    case Key::kLeftShift:
      return ImGuiKey_LeftShift;
    case Key::kRightControl:
      return ImGuiKey_RightCtrl;
    case Key::kLeftControl:
      return ImGuiKey_LeftCtrl;
    case Key::kRightAlt:
      return ImGuiKey_RightAlt;
    case Key::kLeftAlt:
      return ImGuiKey_LeftAlt;
    case Key::kRightMeta:
      return ImGuiKey_RightSuper;
    case Key::kLeftMeta:
      return ImGuiKey_LeftSuper;

    case Key::kPrint:
      return ImGuiKey_PrintScreen;
    case Key::kMenu:
      return ImGuiKey_Menu;

    // Keys ImGui has no use for, including the shifted symbols that arrive as
    // a different key plus a modifier.
    case Key::kNone:
    case Key::kClear:
    case Key::kExclaim:
    case Key::kDoubleQuote:
    case Key::kHash:
    case Key::kDollar:
    case Key::kPercent:
    case Key::kAmpersand:
    case Key::kLeftParen:
    case Key::kRightParen:
    case Key::kAsterisk:
    case Key::kPlus:
    case Key::kColon:
    case Key::kLess:
    case Key::kGreater:
    case Key::kQuestion:
    case Key::kAt:
    case Key::kCaret:
    case Key::kUnderscore:
    case Key::kLeftCurlyBracket:
    case Key::kPipe:
    case Key::kRightCurlyBracket:
    case Key::kTilde:
    case Key::kHelp:
    case Key::kSysReq:
      break;
    }
    return ImGuiKey_None;
  }

  auto ToImGuiMouseButton(const MouseButton button) -> int
  {
    switch (button) {
    case MouseButton::kLeft:
      return ImGuiMouseButton_Left;
    case MouseButton::kRight:
      return ImGuiMouseButton_Right;
    case MouseButton::kMiddle:
      return ImGuiMouseButton_Middle;
    case MouseButton::kExtButton1:
      return 3;
    case MouseButton::kExtButton2:
      return 4;
    case MouseButton::kNone:
      break;
    }
    return -1;
  }

  auto ToPlatformCursor(const ImGuiMouseCursor cursor) -> MouseCursor
  {
    switch (cursor) {
    case ImGuiMouseCursor_None:
      return MouseCursor::kHidden;
    case ImGuiMouseCursor_TextInput:
      return MouseCursor::kTextInput;
    case ImGuiMouseCursor_ResizeAll:
      return MouseCursor::kResizeAll;
    case ImGuiMouseCursor_ResizeNS:
      return MouseCursor::kResizeNS;
    case ImGuiMouseCursor_ResizeEW:
      return MouseCursor::kResizeEW;
    case ImGuiMouseCursor_ResizeNESW:
      return MouseCursor::kResizeNESW;
    case ImGuiMouseCursor_ResizeNWSE:
      return MouseCursor::kResizeNWSE;
    case ImGuiMouseCursor_Hand:
      return MouseCursor::kHand;
    case ImGuiMouseCursor_Wait:
      return MouseCursor::kWait;
    case ImGuiMouseCursor_Progress:
      return MouseCursor::kProgress;
    case ImGuiMouseCursor_NotAllowed:
      return MouseCursor::kNotAllowed;
    default:
      return MouseCursor::kArrow;
    }
  }

  // ImGui declares its flag sets as signed integers.
  auto HasFlag(const int flags, const int flag) -> bool
  {
    return (static_cast<unsigned>(flags) & static_cast<unsigned>(flag)) != 0U;
  }

  auto AddFlag(const int flags, const int flag) -> int
  {
    return static_cast<int>(
      static_cast<unsigned>(flags) | static_cast<unsigned>(flag));
  }

  auto RemoveFlag(const int flags, const int flag) -> int
  {
    return static_cast<int>(
      static_cast<unsigned>(flags) & ~static_cast<unsigned>(flag));
  }

  //! The backend that owns the current ImGui context.
  auto CurrentBackend() -> void*
  {
    return ImGui::GetCurrentContext() != nullptr
      ? ImGui::GetIO().BackendPlatformUserData
      : nullptr;
  }

} // namespace

PlatformBackend::PlatformBackend(std::shared_ptr<Platform> platform,
  const platform::WindowIdType window_id, ImGuiContext* context)
  : platform_(std::move(platform))
  , window_id_(window_id)
  , context_(context)
{
  DCHECK_NOTNULL_F(platform_);
  DCHECK_NOTNULL_F(context_);
  DCHECK_NE_F(window_id_, platform::kInvalidWindowId);

  const auto window = platform_->Windows().FindWindow(window_id_);
  DCHECK_NOTNULL_F(window);

  ImGui::SetCurrentContext(context_.get());
  auto& io = ImGui::GetIO();
  DCHECK_F(io.BackendPlatformUserData == nullptr,
    "the ImGui context already has a platform backend");
  io.BackendPlatformUserData = this;
  io.BackendPlatformName = "oxygen platform";
  io.BackendFlags = AddFlag(io.BackendFlags, ImGuiBackendFlags_HasMouseCursors);

  auto& platform_io = ImGui::GetPlatformIO();
  platform_io.Platform_GetClipboardTextFn = &PlatformBackend::GetClipboardText;
  platform_io.Platform_SetClipboardTextFn = &PlatformBackend::SetClipboardText;
  platform_io.Platform_SetImeDataFn = &PlatformBackend::SetImeData;

  auto* const viewport = ImGui::GetMainViewport();
  viewport->PlatformHandleRaw = window->Native().window_handle;

  // Adjust the scaling to take into account the current DPI
  const auto window_scale = window->DisplayScale();
  DLOG_F(INFO, "Using DPI scale: {}", window_scale);
  io.FontGlobalScale = window_scale;
  ImGui::GetStyle().ScaleAllSizes(window_scale);

  // The platform offers events to the filter before any other processor, so
  // ImGui gets the first opportunity to handle them.
  platform_->RegisterUiEventFilter(
    [this](
      const platform::UiEvent& event) -> bool { return HandleEvent(event); });
}

PlatformBackend::~PlatformBackend()
{
  // Stop event processing first, so that nothing reaches a context that is
  // being detached.
  platform_->ClearUiEventFilter();
  auto* const context = context_.get();
  context_ = nullptr;

  if (text_input_active_) {
    platform_->Ui().StopTextInput(window_id_);
  }
  if (mouse_captured_) {
    platform_->Ui().CaptureMouse(false);
  }

  // Leave the context as we found it, so that it can be destroyed.
  ImGui::SetCurrentContext(context);
  auto& io = ImGui::GetIO();
  io.BackendPlatformName = nullptr;
  io.BackendPlatformUserData = nullptr;
  io.BackendFlags
    = RemoveFlag(io.BackendFlags, ImGuiBackendFlags_HasMouseCursors);
  ImGui::GetPlatformIO().ClearPlatformHandlers();
}

auto PlatformBackend::GetClipboardText(ImGuiContext* /*context*/) -> const char*
{
  auto* const backend = static_cast<PlatformBackend*>(CurrentBackend());
  DCHECK_NOTNULL_F(backend);
  backend->clipboard_text_ = backend->platform_->Ui().GetClipboardText();
  return backend->clipboard_text_.c_str();
}

auto PlatformBackend::SetClipboardText(
  ImGuiContext* /*context*/, const char* text) -> void
{
  auto* const backend = static_cast<PlatformBackend*>(CurrentBackend());
  DCHECK_NOTNULL_F(backend);
  backend->platform_->Ui().SetClipboardText(text != nullptr ? text : "");
}

auto PlatformBackend::SetImeData(ImGuiContext* /*context*/,
  ImGuiViewport* /*viewport*/, ImGuiPlatformImeData* data) -> void
{
  auto* const backend = static_cast<PlatformBackend*>(CurrentBackend());
  DCHECK_NOTNULL_F(backend);
  backend->ime_data_ = *data;
  backend->ime_dirty_ = true;
  backend->UpdateTextInput();
}

auto PlatformBackend::NewFrame() -> void
{
  if (!context_) {
    return;
  }
  const auto window = platform_->Windows().FindWindow(window_id_);
  if (!window) {
    return;
  }

  ImGui::SetCurrentContext(context_.get());
  auto& io = ImGui::GetIO();

  // The display size follows the window every frame; a minimized window has no
  // area.
  const auto size
    = window->Minimized() ? platform::window::ExtentT {} : window->Size();
  const auto framebuffer_size = window->FrameBufferSize();
  io.DisplaySize = ImVec2 { static_cast<float>(size.width),
    static_cast<float>(size.height) };
  io.DisplayFramebufferScale = ImVec2 {
    size.width > 0U ? static_cast<float>(framebuffer_size.width)
        / static_cast<float>(size.width)
                    : 1.0F,
    size.height > 0U ? static_cast<float>(framebuffer_size.height)
        / static_cast<float>(size.height)
                     : 1.0F,
  };

  const auto now = std::chrono::steady_clock::now();
  io.DeltaTime = has_last_frame_time_
    ? std::chrono::duration<float>(now - last_frame_time_).count()
    : kFirstFrameDeltaTime;
  if (io.DeltaTime <= 0.0F) {
    // The clock did not advance; ImGui requires a strictly positive step.
    io.DeltaTime = 1.0F / 1000000.0F;
  }
  last_frame_time_ = now;
  has_last_frame_time_ = true;

  // The mouse leave is applied a frame late, so that an enter that follows
  // immediately does not interrupt a drag by clearing the mouse position.
  if (pending_mouse_leave_frame_ != 0
    && pending_mouse_leave_frame_ >= ImGui::GetFrameCount()
    && mouse_buttons_down_ == 0U) {
    pending_mouse_leave_frame_ = 0;
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  }

  UpdateMouseCapture();
  UpdateMouseCursor();
  UpdateTextInput();
}

auto PlatformBackend::HandleEvent(const platform::UiEvent& event) -> bool
{
  if (!context_) {
    return false;
  }
  ImGui::SetCurrentContext(context_.get());

  return std::visit(
    [this](const auto& typed) -> bool {
      using Event = std::decay_t<decltype(typed)>;
      if constexpr (std::is_same_v<Event, platform::UiKeyEvent>) {
        return OnKey(typed);
      } else if constexpr (std::is_same_v<Event,
                             platform::UiMouseMotionEvent>) {
        return OnMouseMotion(typed);
      } else if constexpr (std::is_same_v<Event,
                             platform::UiMouseButtonEvent>) {
        return OnMouseButton(typed);
      } else if constexpr (std::is_same_v<Event, platform::UiMouseWheelEvent>) {
        return OnMouseWheel(typed);
      } else if constexpr (std::is_same_v<Event, platform::UiMouseLeaveEvent>) {
        return OnMouseLeave(typed);
      } else if constexpr (std::is_same_v<Event, platform::UiTextInputEvent>) {
        return OnTextInput(typed);
      } else {
        return OnFocus(typed);
      }
    },
    event);
}

auto PlatformBackend::OnKey(const platform::UiKeyEvent& event) -> bool
{
  if (event.window_id != window_id_) {
    return false;
  }
  auto& io = ImGui::GetIO();
  UpdateModifiers(event.key, event.pressed);
  if (const auto key = ToImGuiKey(event.key); key != ImGuiKey_None) {
    io.AddKeyEvent(key, event.pressed);
  }
  return io.WantCaptureKeyboard;
}

auto PlatformBackend::OnMouseMotion(
  const platform::UiMouseMotionEvent& event) const -> bool
{
  if (event.window_id != window_id_) {
    return false;
  }
  auto& io = ImGui::GetIO();
  io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
  io.AddMousePosEvent(event.position.x, event.position.y);
  return io.WantCaptureMouse;
}

auto PlatformBackend::OnMouseButton(const platform::UiMouseButtonEvent& event)
  -> bool
{
  if (event.window_id != window_id_) {
    return false;
  }
  auto& io = ImGui::GetIO();
  const auto button = ToImGuiMouseButton(event.button);
  if (button < 0) {
    return false;
  }
  io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
  io.AddMouseButtonEvent(button, event.pressed);
  const auto mask = 1U << static_cast<unsigned>(button);
  mouse_buttons_down_ = event.pressed ? (mouse_buttons_down_ | mask)
                                      : (mouse_buttons_down_ & ~mask);
  return io.WantCaptureMouse;
}

auto PlatformBackend::OnMouseWheel(
  const platform::UiMouseWheelEvent& event) const -> bool
{
  if (event.window_id != window_id_) {
    return false;
  }
  auto& io = ImGui::GetIO();
  io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
  // ImGui scrolls content right for a negative horizontal wheel value.
  io.AddMouseWheelEvent(-event.scroll.dx, event.scroll.dy);
  return io.WantCaptureMouse;
}

auto PlatformBackend::OnMouseLeave(const platform::UiMouseLeaveEvent& event)
  -> bool
{
  if (event.window_id != window_id_) {
    return false;
  }
  pending_mouse_leave_frame_ = ImGui::GetFrameCount() + 1;
  return false;
}

auto PlatformBackend::OnTextInput(const platform::UiTextInputEvent& event) const
  -> bool
{
  if (event.window_id != window_id_) {
    return false;
  }
  auto& io = ImGui::GetIO();
  // ImGui reads a null-terminated string.
  io.AddInputCharactersUTF8(std::string { event.text }.c_str());
  return io.WantCaptureKeyboard;
}

auto PlatformBackend::OnFocus(const platform::UiFocusEvent& event) -> bool
{
  if (event.window_id != window_id_) {
    return false;
  }
  ImGui::GetIO().AddFocusEvent(event.gained);
  if (!event.gained) {
    // Releases that happen while another window has the focus are never seen.
    ClearModifiers();
  }
  return false;
}

auto PlatformBackend::UpdateModifiers(const Key key, const bool pressed) -> void
{
  switch (key) {
  case Key::kLeftControl:
    control_down_.at(kLeft) = pressed;
    break;
  case Key::kRightControl:
    control_down_.at(kRight) = pressed;
    break;
  case Key::kLeftShift:
    shift_down_.at(kLeft) = pressed;
    break;
  case Key::kRightShift:
    shift_down_.at(kRight) = pressed;
    break;
  case Key::kLeftAlt:
    alt_down_.at(kLeft) = pressed;
    break;
  case Key::kRightAlt:
    alt_down_.at(kRight) = pressed;
    break;
  case Key::kLeftMeta:
    super_down_.at(kLeft) = pressed;
    break;
  case Key::kRightMeta:
    super_down_.at(kRight) = pressed;
    break;
  default:
    return;
  }

  auto& io = ImGui::GetIO();
  io.AddKeyEvent(
    ImGuiMod_Ctrl, control_down_.at(kLeft) || control_down_.at(kRight));
  io.AddKeyEvent(
    ImGuiMod_Shift, shift_down_.at(kLeft) || shift_down_.at(kRight));
  io.AddKeyEvent(ImGuiMod_Alt, alt_down_.at(kLeft) || alt_down_.at(kRight));
  io.AddKeyEvent(
    ImGuiMod_Super, super_down_.at(kLeft) || super_down_.at(kRight));
}

auto PlatformBackend::ClearModifiers() -> void
{
  control_down_ = {};
  shift_down_ = {};
  alt_down_ = {};
  super_down_ = {};
  auto& io = ImGui::GetIO();
  io.AddKeyEvent(ImGuiMod_Ctrl, false);
  io.AddKeyEvent(ImGuiMod_Shift, false);
  io.AddKeyEvent(ImGuiMod_Alt, false);
  io.AddKeyEvent(ImGuiMod_Super, false);
}

auto PlatformBackend::UpdateMouseCapture() -> void
{
  // While a drag is in progress, the platform keeps reporting the mouse
  // outside the window, so a drag can extend past its borders.
  auto dragging = false;
  for (int button = 0; button < ImGuiMouseButton_COUNT && !dragging; ++button) {
    dragging = ImGui::IsMouseDragging(button, kDragThreshold);
  }
  if (dragging != mouse_captured_) {
    platform_->Ui().CaptureMouse(dragging);
    mouse_captured_ = dragging;
  }
}

auto PlatformBackend::UpdateMouseCursor() -> void
{
  const auto& io = ImGui::GetIO();
  if (HasFlag(io.ConfigFlags, ImGuiConfigFlags_NoMouseCursorChange)) {
    return;
  }
  // ImGui draws the cursor itself, or wants none: hide the operating system's.
  const auto cursor = io.MouseDrawCursor
    ? MouseCursor::kHidden
    : ToPlatformCursor(ImGui::GetMouseCursor());
  platform_->Ui().SetMouseCursor(cursor);
}

auto PlatformBackend::UpdateTextInput() -> void
{
  const auto window = platform_->Windows().FindWindow(window_id_);
  const auto has_focus = window && window->HasInputFocus();
  const auto wanted = ime_data_.WantVisible || ime_data_.WantTextInput;

  if (text_input_active_ && (!wanted || !has_focus)) {
    platform_->Ui().StopTextInput(window_id_);
    text_input_active_ = false;
  }
  if (!wanted || !has_focus || (text_input_active_ && !ime_dirty_)) {
    return;
  }

  ime_dirty_ = false;
  platform_->Ui().StartTextInput(window_id_,
    platform::TextInputArea {
      .x = ime_data_.InputPos.x,
      .y = ime_data_.InputPos.y,
      .width = 1.0F,
      .height = ime_data_.InputLineHeight,
    });
  text_input_active_ = true;
}

} // namespace oxygen::imgui
