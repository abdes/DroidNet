//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <optional>
#include <variant>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>

#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/PlatformEvent.h>
#include <Oxygen/Platform/SDL/UiEvents.h>
#include <Oxygen/Platform/UiEvent.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::platform::Key;
using oxygen::platform::MouseButton;
using oxygen::platform::PlatformEvent;
using oxygen::platform::UiEvent;
using oxygen::platform::UiFocusEvent;
using oxygen::platform::UiKeyEvent;
using oxygen::platform::UiMouseButtonEvent;
using oxygen::platform::UiMouseLeaveEvent;
using oxygen::platform::UiMouseMotionEvent;
using oxygen::platform::UiMouseWheelEvent;
using oxygen::platform::UiTextInputEvent;
using oxygen::platform::sdl::TranslateUiEvent;

namespace {

constexpr auto kWindowId = 7U;

//! Wraps a native SDL event the way the event pump delivers it.
auto MakePlatformEvent(const SDL_Event& native) -> PlatformEvent
{
  auto event = PlatformEvent::Create<SDL_Event>();
  if (auto* const slot = event.NativeEventAs<SDL_Event>()) {
    *slot = native;
  }
  return event;
}

auto Translate(const SDL_Event& native) -> std::optional<UiEvent>
{
  const auto event = MakePlatformEvent(native);
  return TranslateUiEvent(event);
}

//! The translated event as `Event`, or null when there is none or it is
//! another kind.
template <typename Event>
auto As(const std::optional<UiEvent>& translated) -> const Event*
{
  return translated.has_value() ? std::get_if<Event>(&*translated) : nullptr;
}

//! A key press or release becomes a UI key event with the repeat flag.
NOLINT_TEST(UiEventsTest, KeyEventsCarryKeyStateAndRepeat)
{
  // Arrange
  SDL_Event native {};
  native.type = SDL_EVENT_KEY_DOWN;
  native.key.windowID = kWindowId;
  native.key.key = SDLK_TAB;
  native.key.down = true;
  native.key.repeat = true;

  // Act
  const auto translated = Translate(native);

  // Assert
  const auto* const key = As<UiKeyEvent>(translated);
  ASSERT_NE(key, nullptr);
  EXPECT_EQ(key->window_id, kWindowId);
  EXPECT_EQ(key->key, Key::kTab);
  EXPECT_TRUE(key->pressed);
  EXPECT_TRUE(key->repeat);
}

//! A key Oxygen has no vocabulary for never reaches the UI.
NOLINT_TEST(UiEventsTest, KeyWithoutOxygenEquivalentIsDropped)
{
  // Arrange
  SDL_Event native {};
  native.type = SDL_EVENT_KEY_UP;
  native.key.key = SDLK_AC_BACK;

  // Act
  const auto translated = Translate(native);

  // Assert
  EXPECT_FALSE(translated.has_value());
}

//! Mouse motion reports the position in window coordinates.
NOLINT_TEST(UiEventsTest, MouseMotionCarriesPosition)
{
  // Arrange
  SDL_Event native {};
  native.type = SDL_EVENT_MOUSE_MOTION;
  native.motion.windowID = kWindowId;
  native.motion.x = 12.5F;
  native.motion.y = 30.0F;

  // Act
  const auto translated = Translate(native);

  // Assert
  const auto* const motion = As<UiMouseMotionEvent>(translated);
  ASSERT_NE(motion, nullptr);
  EXPECT_EQ(motion->window_id, kWindowId);
  EXPECT_FLOAT_EQ(motion->position.x, 12.5F);
  EXPECT_FLOAT_EQ(motion->position.y, 30.0F);
}

//! A mouse button maps to Oxygen's vocabulary and keeps its state.
NOLINT_TEST(UiEventsTest, MouseButtonsMapToOxygenButtons)
{
  // Arrange
  SDL_Event native {};
  native.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  native.button.windowID = kWindowId;
  native.button.button = SDL_BUTTON_RIGHT;
  native.button.down = true;
  native.button.x = 4.0F;
  native.button.y = 5.0F;

  // Act
  const auto translated = Translate(native);

  // Assert
  const auto* const button = As<UiMouseButtonEvent>(translated);
  ASSERT_NE(button, nullptr);
  EXPECT_EQ(button->button, MouseButton::kRight);
  EXPECT_TRUE(button->pressed);
  EXPECT_FLOAT_EQ(button->position.x, 4.0F);
  EXPECT_FLOAT_EQ(button->position.y, 5.0F);
}

//! A button Oxygen has no vocabulary for never reaches the UI.
NOLINT_TEST(UiEventsTest, MouseButtonWithoutOxygenEquivalentIsDropped)
{
  // Arrange
  SDL_Event native {};
  native.type = SDL_EVENT_MOUSE_BUTTON_UP;
  native.button.button = 9;

  // Act
  const auto translated = Translate(native);

  // Assert
  EXPECT_FALSE(translated.has_value());
}

//! The wheel carries both axes as the user's settings report them.
NOLINT_TEST(UiEventsTest, MouseWheelCarriesBothAxes)
{
  // Arrange
  SDL_Event native {};
  native.type = SDL_EVENT_MOUSE_WHEEL;
  native.wheel.windowID = kWindowId;
  native.wheel.x = -1.0F;
  native.wheel.y = 2.0F;

  // Act
  const auto translated = Translate(native);

  // Assert
  const auto* const wheel = As<UiMouseWheelEvent>(translated);
  ASSERT_NE(wheel, nullptr);
  EXPECT_FLOAT_EQ(wheel->scroll.dx, -1.0F);
  EXPECT_FLOAT_EQ(wheel->scroll.dy, 2.0F);
}

//! Typed text and the window events a UI layer tracks are translated.
NOLINT_TEST(UiEventsTest, TextFocusAndMouseLeaveAreTranslated)
{
  // Arrange
  SDL_Event text {};
  text.type = SDL_EVENT_TEXT_INPUT;
  text.text.windowID = kWindowId;
  text.text.text = "hé";
  SDL_Event gained {};
  gained.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
  gained.window.windowID = kWindowId;
  SDL_Event lost {};
  lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
  lost.window.windowID = kWindowId;
  SDL_Event leave {};
  leave.type = SDL_EVENT_WINDOW_MOUSE_LEAVE;
  leave.window.windowID = kWindowId;

  // Act
  // The text refers to the native event, which must outlive its use.
  const auto text_event = MakePlatformEvent(text);
  const auto translated_text = TranslateUiEvent(text_event);
  const auto translated_gained = Translate(gained);
  const auto translated_lost = Translate(lost);
  const auto translated_leave = Translate(leave);

  // Assert
  const auto* const typed = As<UiTextInputEvent>(translated_text);
  ASSERT_NE(typed, nullptr);
  EXPECT_EQ(typed->text, "hé");
  const auto* const focus_gained = As<UiFocusEvent>(translated_gained);
  ASSERT_NE(focus_gained, nullptr);
  EXPECT_TRUE(focus_gained->gained);
  const auto* const focus_lost = As<UiFocusEvent>(translated_lost);
  ASSERT_NE(focus_lost, nullptr);
  EXPECT_FALSE(focus_lost->gained);
  const auto* const mouse_leave = As<UiMouseLeaveEvent>(translated_leave);
  ASSERT_NE(mouse_leave, nullptr);
  EXPECT_EQ(mouse_leave->window_id, kWindowId);
}

//! Events a UI layer does not consume are not translated.
NOLINT_TEST(UiEventsTest, UnrelatedEventsAreIgnored)
{
  // Arrange
  SDL_Event native {};
  native.type = SDL_EVENT_WINDOW_RESIZED;

  // Act
  const auto translated = Translate(native);

  // Assert
  EXPECT_FALSE(translated.has_value());
}

} // namespace
