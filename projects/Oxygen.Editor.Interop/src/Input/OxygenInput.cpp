//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma managed

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>

//#ifdef _WIN32
//#include <WinSock2.h> // include before any header that might include <windows.h>
//#endif

#include <Oxygen/Base/Detail/NamedType_impl.h>
#pragma warning(push)
#pragma warning(disable : 4793)
#include <Oxygen/Base/Logging.h>
#pragma warning(pop)
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/EditorInterface/EngineContext.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Platform/Input.h>


#include <EditorModule/EditorModule.h>
#include <EditorModule/InputAccumulator.h>
#include <EngineContext.h>
#include <Input/OxygenInput.h>

using namespace System::Numerics;

using oxygen::observer_ptr;
using oxygen::interop::module::EditorModule;

namespace Oxygen::Interop::Input {

  // Static assertions to ensure managed enums stay in sync with native enums.
  // This provides compile-time safety without exposing native headers in the
  // managed header. Use macros to reduce repetition; token-paste matches native
  // enum names prefixed with 'k'.
#define KEY_ASSERT(NAME)                                                       \
  static_assert(static_cast<int>(PlatformKey::NAME) ==                         \
                static_cast<int>(oxygen::platform::Key::k##NAME))

  KEY_ASSERT(None);
  KEY_ASSERT(BackSpace);
  KEY_ASSERT(Delete);
  KEY_ASSERT(Tab);
  KEY_ASSERT(Clear);
  KEY_ASSERT(Return);
  KEY_ASSERT(Pause);
  KEY_ASSERT(Escape);
  KEY_ASSERT(Space);
  KEY_ASSERT(Keypad0);
  KEY_ASSERT(Keypad1);
  KEY_ASSERT(Keypad2);
  KEY_ASSERT(Keypad3);
  KEY_ASSERT(Keypad4);
  KEY_ASSERT(Keypad5);
  KEY_ASSERT(Keypad6);
  KEY_ASSERT(Keypad7);
  KEY_ASSERT(Keypad8);
  KEY_ASSERT(Keypad9);
  KEY_ASSERT(KeypadPeriod);
  KEY_ASSERT(KeypadDivide);
  KEY_ASSERT(KeypadMultiply);
  KEY_ASSERT(KeypadMinus);
  KEY_ASSERT(KeypadPlus);
  KEY_ASSERT(KeypadEnter);
  KEY_ASSERT(KeypadEquals);
  KEY_ASSERT(UpArrow);
  KEY_ASSERT(DownArrow);
  KEY_ASSERT(RightArrow);
  KEY_ASSERT(LeftArrow);
  KEY_ASSERT(Insert);
  KEY_ASSERT(Home);
  KEY_ASSERT(End);
  KEY_ASSERT(PageUp);
  KEY_ASSERT(PageDown);
  KEY_ASSERT(F1);
  KEY_ASSERT(F2);
  KEY_ASSERT(F3);
  KEY_ASSERT(F4);
  KEY_ASSERT(F5);
  KEY_ASSERT(F6);
  KEY_ASSERT(F7);
  KEY_ASSERT(F8);
  KEY_ASSERT(F9);
  KEY_ASSERT(F10);
  KEY_ASSERT(F11);
  KEY_ASSERT(F12);
  KEY_ASSERT(F13);
  KEY_ASSERT(F14);
  KEY_ASSERT(F15);
  KEY_ASSERT(Alpha0);
  KEY_ASSERT(Alpha1);
  KEY_ASSERT(Alpha2);
  KEY_ASSERT(Alpha3);
  KEY_ASSERT(Alpha4);
  KEY_ASSERT(Alpha5);
  KEY_ASSERT(Alpha6);
  KEY_ASSERT(Alpha7);
  KEY_ASSERT(Alpha8);
  KEY_ASSERT(Alpha9);
  KEY_ASSERT(Exclaim);
  KEY_ASSERT(DoubleQuote);
  KEY_ASSERT(Hash);
  KEY_ASSERT(Dollar);
  KEY_ASSERT(Percent);
  KEY_ASSERT(Ampersand);
  KEY_ASSERT(Quote);
  KEY_ASSERT(LeftParen);
  KEY_ASSERT(RightParen);
  KEY_ASSERT(Asterisk);
  KEY_ASSERT(Plus);
  KEY_ASSERT(Comma);
  KEY_ASSERT(Minus);
  KEY_ASSERT(Period);
  KEY_ASSERT(Slash);
  KEY_ASSERT(Colon);
  KEY_ASSERT(Semicolon);
  KEY_ASSERT(Less);
  KEY_ASSERT(Equals);
  KEY_ASSERT(Greater);
  KEY_ASSERT(Question);
  KEY_ASSERT(At);
  KEY_ASSERT(LeftBracket);
  KEY_ASSERT(Backslash);
  KEY_ASSERT(RightBracket);
  KEY_ASSERT(Caret);
  KEY_ASSERT(Underscore);
  KEY_ASSERT(BackQuote);
  KEY_ASSERT(A);
  KEY_ASSERT(B);
  KEY_ASSERT(C);
  KEY_ASSERT(D);
  KEY_ASSERT(E);
  KEY_ASSERT(F);
  KEY_ASSERT(G);
  KEY_ASSERT(H);
  KEY_ASSERT(I);
  KEY_ASSERT(J);
  KEY_ASSERT(K);
  KEY_ASSERT(L);
  KEY_ASSERT(M);
  KEY_ASSERT(N);
  KEY_ASSERT(O);
  KEY_ASSERT(P);
  KEY_ASSERT(Q);
  KEY_ASSERT(R);
  KEY_ASSERT(S);
  KEY_ASSERT(T);
  KEY_ASSERT(U);
  KEY_ASSERT(V);
  KEY_ASSERT(W);
  KEY_ASSERT(X);
  KEY_ASSERT(Y);
  KEY_ASSERT(Z);
  KEY_ASSERT(LeftCurlyBracket);
  KEY_ASSERT(Pipe);
  KEY_ASSERT(RightCurlyBracket);
  KEY_ASSERT(Tilde);
  KEY_ASSERT(NumLock);
  KEY_ASSERT(CapsLock);
  KEY_ASSERT(ScrollLock);
  KEY_ASSERT(RightShift);
  KEY_ASSERT(LeftShift);
  KEY_ASSERT(RightControl);
  KEY_ASSERT(LeftControl);
  KEY_ASSERT(RightAlt);
  KEY_ASSERT(LeftAlt);
  KEY_ASSERT(RightMeta);
  KEY_ASSERT(LeftMeta);
  KEY_ASSERT(Help);
  KEY_ASSERT(Print);
  KEY_ASSERT(SysReq);
  KEY_ASSERT(Menu);

#undef KEY_ASSERT

#define MOUSE_ASSERT(NAME)                                                     \
  static_assert(static_cast<int>(PlatformMouseButton::NAME) ==                 \
                static_cast<int>(oxygen::platform::MouseButton::k##NAME))

  MOUSE_ASSERT(None);
  MOUSE_ASSERT(Left);
  MOUSE_ASSERT(Right);
  MOUSE_ASSERT(Middle);
  MOUSE_ASSERT(ExtButton1);
  MOUSE_ASSERT(ExtButton2);

#undef MOUSE_ASSERT

  static std::optional<std::reference_wrapper<EditorModule>>
    GetEditorModule(EngineContext^ ctx) {
    auto native_ctx = ctx->NativePtr();
    if (!native_ctx || !native_ctx->engine) {
      LOG_F(WARNING, "Engine or its context is no longer valid.");
      return std::nullopt;
    }

    auto editor_module_opt = native_ctx->engine->GetModule<EditorModule>();
    if (!editor_module_opt.has_value()) {
      LOG_F(WARNING, "Engine does not have an EditorModule registered.");
      return std::nullopt;
    }

    return editor_module_opt;
  }

  static oxygen::time::PhysicalTime DateTimeToPhysicalTime(System::DateTime dt) {
    // If DateTime.MinValue is provided, use now
    if (dt == System::DateTime::MinValue)
      dt = System::DateTime::UtcNow;

    // Ensure UTC
    dt = dt.ToUniversalTime();
    // .NET ticks are 100 nanoseconds
    auto ticks = dt.Ticks; // Int64
    // Convert ticks -> nanoseconds
    long long ns = static_cast<long long>(ticks) * 100LL;
    using namespace std::chrono;
    auto d = nanoseconds(ns);
    return oxygen::time::PhysicalTime{
        oxygen::time::PhysicalTime::UnderlyingType{d} };
  }

  OxygenInput::OxygenInput(EngineContext^ context) : context_(context) {
    if (context_ == nullptr)
      throw gcnew System::ArgumentNullException("context");
  }

  void OxygenInput::PushKeyEvent(Oxygen::Interop::ViewIdManaged viewId,
    EditorKeyEventManaged ev) {
    using ::oxygen::SubPixelPosition;
    using ::oxygen::interop::module::EditorKeyEvent;

    auto editor_module = GetEditorModule(context_);
    if (!editor_module)
      return;

    auto& acc = editor_module->get().GetInputAccumulator();

    try {
      EditorKeyEvent nativeEv{};
      nativeEv.key =
        static_cast<::oxygen::platform::Key>(static_cast<int>(ev.key));
      nativeEv.pressed = ev.pressed;
      nativeEv.timestamp = DateTimeToPhysicalTime(ev.timestamp);
      nativeEv.position =
        ::oxygen::SubPixelPosition{ .x = ev.position.X, .y = ev.position.Y };
      nativeEv.repeat = ev.repeat;

      acc.PushKeyEvent(viewId.ToNative(), nativeEv);
    }
    catch (...) {
      // swallow to avoid crashing managed callers
    }
  }

  void OxygenInput::PushButtonEvent(Oxygen::Interop::ViewIdManaged viewId,
    EditorButtonEventManaged ev) {
    using ::oxygen::SubPixelPosition;
    using ::oxygen::interop::module::EditorButtonEvent;
    using ::oxygen::interop::module::EditorModule;

    auto editor_module = GetEditorModule(context_);
    if (!editor_module)
      return;

    auto& acc = editor_module->get().GetInputAccumulator();

    try {
      EditorButtonEvent nativeEv{};
      nativeEv.button = static_cast<::oxygen::platform::MouseButton>(
        static_cast<int>(ev.button));
      nativeEv.pressed = ev.pressed;
      nativeEv.timestamp = DateTimeToPhysicalTime(ev.timestamp);
      nativeEv.position =
        ::oxygen::SubPixelPosition{ .x = ev.position.X, .y = ev.position.Y };

      acc.PushButtonEvent(viewId.ToNative(), nativeEv);
    }
    catch (...) {
      // swallow
    }
  }

  void OxygenInput::PushMouseMotion(Oxygen::Interop::ViewIdManaged viewId,
    EditorMouseMotionEventManaged ev) {
    using ::oxygen::SubPixelMotion;
    using ::oxygen::SubPixelPosition;
    using ::oxygen::interop::module::EditorMouseMotionEvent;

    auto editor_module = GetEditorModule(context_);
    if (!editor_module)
      return;

    auto& acc = editor_module->get().GetInputAccumulator();

    try {
      EditorMouseMotionEvent nativeEv{};
      nativeEv.motion = SubPixelMotion{ .dx = ev.motion.X, .dy = ev.motion.Y };
      nativeEv.position =
        SubPixelPosition{ .x = ev.position.X, .y = ev.position.Y };
      nativeEv.timestamp = DateTimeToPhysicalTime(ev.timestamp);

      acc.PushMouseMotion(viewId.ToNative(), nativeEv);
    }
    catch (...) {
      // swallow
    }
  }

  void OxygenInput::PushMouseWheel(Oxygen::Interop::ViewIdManaged viewId,
    EditorMouseWheelEventManaged ev) {
    using ::oxygen::SubPixelMotion;
    using ::oxygen::SubPixelPosition;
    using ::oxygen::interop::module::EditorMouseWheelEvent;

    auto editor_module = GetEditorModule(context_);
    if (!editor_module)
      return;

    auto& acc = editor_module->get().GetInputAccumulator();

    try {
      EditorMouseWheelEvent nativeEv{};
      nativeEv.scroll = SubPixelMotion{ .dx = ev.scroll.X, .dy = ev.scroll.Y };
      nativeEv.position =
        SubPixelPosition{ .x = ev.position.X, .y = ev.position.Y };
      nativeEv.timestamp = DateTimeToPhysicalTime(ev.timestamp);

      acc.PushMouseWheel(viewId.ToNative(), nativeEv);
    }
    catch (...) {
      // swallow
    }
  }

  void OxygenInput::OnFocusLost(Oxygen::Interop::ViewIdManaged viewId) {
    auto editor_module = GetEditorModule(context_);
    if (!editor_module)
      return;

    auto& acc = editor_module->get().GetInputAccumulator();

    try {
      acc.OnFocusLost(viewId.ToNative());
    }
    catch (...) {
      // swallow
    }
  }

} // namespace Oxygen::Interop::Input
