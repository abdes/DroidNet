//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Time.h"
#include "Oxygen/Base/Types/Geometry.h"
#include "Oxygen/Platform/Common/Types.h"

namespace oxygen::platform {

enum class InputEventType : uint8_t {
  kKeyEvent,
  kMouseButtonEvent,
  kMouseMotionEvent,
  kMouseWheelEvent,
};

class InputEvent
{
 public:
  // TODO: temporarily pass the raw event to ImGui
  // Should implement an adapter for ImGui
  explicit InputEvent(const void* raw_event, const TimePoint& time)
    : time_(time)
    , raw_event_(raw_event)
  {
  }
  virtual ~InputEvent() = default;

  InputEvent(const InputEvent&) = default;
  auto operator=(const InputEvent&) -> InputEvent& = default;

  InputEvent(InputEvent&& other) noexcept = default;
  auto operator=(InputEvent&& other) noexcept -> InputEvent& = default;

  [[nodiscard]] virtual auto GetType() const -> InputEventType = 0;

  [[nodiscard]] auto GetWindowId() const { return window_id_; }
  auto SetWindowId(const WindowIdType window_id) { window_id_ = window_id; }

  [[nodiscard]] auto GetTime() const { return time_; }

  [[nodiscard]] auto IsFromWindow(const WindowIdType window_id) const
  {
    return window_id_ == window_id;
  }

  [[nodiscard]] auto GetRawEvent() const -> const void* { return raw_event_; }

 private:
  WindowIdType window_id_ { platform::kInvalidWindowId };

  // time at which the event occurred relative to the core starting time.
  TimePoint time_;

  const void* raw_event_;
};

class ButtonInputEvent : public virtual InputEvent
{
 public:
  ~ButtonInputEvent() override = default;

  ButtonInputEvent(const ButtonInputEvent&) = default;
  auto operator=(const ButtonInputEvent&) -> ButtonInputEvent& = default;

  ButtonInputEvent(ButtonInputEvent&& other) noexcept = default;
  auto operator=(ButtonInputEvent&& other) noexcept
    -> ButtonInputEvent& = default;

  [[nodiscard]] auto GetButtonState() const { return state_; }

 protected:
  explicit ButtonInputEvent(const ButtonState& state)
    : state_(state)
  {
  }

 private:
  ButtonState state_;
};

class KeyEvent : public ButtonInputEvent
{
 public:
  struct KeyInfo {
    explicit KeyInfo(const Key key_code, const bool repeat = false)
      : key_code_(key_code)
      , repeat_(repeat)
    {
    }

    [[nodiscard]] auto GetKeyCode() const { return key_code_; }
    [[nodiscard]] auto IsRepeat() const { return repeat_; }

   private:
    Key key_code_;
    bool repeat_;
  };

  KeyEvent(const void* raw_event,
    const TimePoint& time,
    const KeyInfo& key,
    const ButtonState& state)
    : InputEvent(raw_event, time)
    , ButtonInputEvent(state)
    , key_(key)
  {
  }

  ~KeyEvent() override = default;

  KeyEvent(const KeyEvent&) = default;
  auto operator=(const KeyEvent&) -> KeyEvent& = default;

  KeyEvent(KeyEvent&& other) noexcept = default;
  auto operator=(KeyEvent&& other) noexcept -> KeyEvent& = default;

  [[nodiscard]] auto GetType() const -> InputEventType override
  {
    return InputEventType::kKeyEvent;
  }

  [[nodiscard]] auto GetKeyCode() const { return key_.GetKeyCode(); }
  [[nodiscard]] auto IsRepeat() const { return key_.IsRepeat(); }

 private:
  KeyInfo key_;
};

class MouseEvent : public virtual InputEvent
{
 public:
  ~MouseEvent() override = default;

  MouseEvent(const MouseEvent&) = default;
  auto operator=(const MouseEvent&) -> MouseEvent& = default;

  MouseEvent(MouseEvent&& other) noexcept = default;
  auto operator=(MouseEvent&& other) noexcept -> MouseEvent& = default;

  [[nodiscard]] auto GetPosition() const { return position_; }

 protected:
  explicit MouseEvent(const SubPixelPosition& position)
    : position_(position)
  {
  }

 private:
  SubPixelPosition position_; // relative to window
};

class MouseButtonEvent : public MouseEvent, public ButtonInputEvent
{
 public:
  MouseButtonEvent(const void* raw_event,
    const TimePoint& time,
    const SubPixelPosition& position,
    const MouseButton button,
    const ButtonState& state)
    : InputEvent(raw_event, time)
    , MouseEvent(position)
    , ButtonInputEvent(state)
    , button_(button)
  {
  }

  ~MouseButtonEvent() override = default;

  MouseButtonEvent(const MouseButtonEvent&) = default;
  auto operator=(const MouseButtonEvent&) -> MouseButtonEvent& = default;

  MouseButtonEvent(MouseButtonEvent&& other) noexcept = default;
  auto operator=(MouseButtonEvent&& other) noexcept
    -> MouseButtonEvent& = default;

  [[nodiscard]] auto GetType() const -> InputEventType override
  {
    return InputEventType::kMouseButtonEvent;
  }

  [[nodiscard]] auto GetButton() const { return button_; }

 private:
  MouseButton button_;
};

class MouseMotionEvent : public MouseEvent
{
 public:
  MouseMotionEvent(const void* raw_event,
    const TimePoint& time,
    const SubPixelPosition& position,
    const SubPixelMotion& motion)
    : InputEvent(raw_event, time)
    , MouseEvent(position)
    , motion_(motion)
  {
  }

  ~MouseMotionEvent() override = default;

  MouseMotionEvent(const MouseMotionEvent&) = default;
  auto operator=(const MouseMotionEvent&) -> MouseMotionEvent& = default;

  MouseMotionEvent(MouseMotionEvent&& other) noexcept = default;
  auto operator=(MouseMotionEvent&& other) noexcept
    -> MouseMotionEvent& = default;

  [[nodiscard]] auto GetType() const -> InputEventType override
  {
    return InputEventType::kMouseMotionEvent;
  }

  [[nodiscard]] auto GetMotion() const { return motion_; }

 private:
  SubPixelMotion motion_; // relative motion from last position
};

class MouseWheelEvent : public MouseEvent
{
 public:
  MouseWheelEvent(const void* raw_event,
    const TimePoint& time,
    const SubPixelPosition& position,
    const SubPixelMotion& scroll_amount)
    : InputEvent(raw_event, time)
    , MouseEvent(position)
    , scroll_amount_(scroll_amount)
  {
  }

  ~MouseWheelEvent() override = default;

  MouseWheelEvent(const MouseWheelEvent&) = default;
  auto operator=(const MouseWheelEvent&) -> MouseWheelEvent& = default;

  MouseWheelEvent(MouseWheelEvent&& other) noexcept = default;
  auto operator=(MouseWheelEvent&& other) noexcept
    -> MouseWheelEvent& = default;

  [[nodiscard]] auto GetType() const -> InputEventType override
  {
    return InputEventType::kMouseWheelEvent;
  }

  [[nodiscard]] auto GetScrollAmount() const { return scroll_amount_; }

 private:
  // The amount scrolled, positive horizontally to the right and vertically
  // away from the user
  SubPixelMotion scroll_amount_;
};

} // namespace oxygen::platform
