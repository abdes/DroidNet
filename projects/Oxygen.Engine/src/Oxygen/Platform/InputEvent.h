//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/TimeUtils.h"
#include "Oxygen/Base/Types/Geometry.h"
#include "Oxygen/Composition/ComponentMacros.h"
#include "Oxygen/Composition/Composition.h"
#include "Oxygen/Platform/Input.h"
#include "Oxygen/Platform/Types.h"

namespace oxygen::platform {

enum class ButtonState : uint8_t {
    kReleased, // Key has just been released this frame.
    kPressed, // Key has just been pressed down this frame.
};

namespace input {

    class EventInfoComponent final : public Component {
        OXYGEN_COMPONENT(EventInfoComponent)
    public:
        EventInfoComponent(const TimePoint time, const WindowIdType window_id)
            : window_id_(window_id)
            , time_(time)
        {
        }

        [[nodiscard]] auto GetWindowId() const { return window_id_; }
        [[nodiscard]] auto GetTime() const { return time_; }

    private:
        WindowIdType window_id_ { kInvalidWindowId };
        TimePoint time_ {};
    };

    class EventPositionComponent final : public Component {
        OXYGEN_COMPONENT(EventPositionComponent)
    public:
        explicit EventPositionComponent(const SubPixelPosition position)
            : position_(position)
        {
        }
        [[nodiscard]] auto GetPosition() const { return position_; }

    private:
        SubPixelPosition position_; // relative to window
    };

    class ButtonStateComponent final : public Component {
        OXYGEN_COMPONENT(ButtonStateComponent)
    public:
        explicit ButtonStateComponent(const ButtonState state)
            : state_(state)
        {
        }
        [[nodiscard]] auto GetState() const { return state_; }

    private:
        ButtonState state_;
    };

    class KeyInfo {
    public:
        constexpr KeyInfo() = default;
        constexpr explicit KeyInfo(const Key key_code, const bool repeat = false)
            : key_code_(key_code)
            , repeat_(repeat)
        {
        }

        [[nodiscard]] constexpr auto GetKeyCode() const { return key_code_; }
        [[nodiscard]] constexpr auto IsRepeat() const { return repeat_; }

    private:
        Key key_code_ { Key::kNone };
        bool repeat_ { false };
    };

    class KeyComponent final : public Component {
        OXYGEN_COMPONENT(KeyComponent)
    public:
        explicit KeyComponent(const KeyInfo key_info)
            : key_info_(key_info)
        {
        }
        [[nodiscard]] auto GetKeyInfo() const { return key_info_; }

    private:
        KeyInfo key_info_ {};
    };

    class MouseButtonComponent final : public Component {
        OXYGEN_COMPONENT(MouseButtonComponent)
    public:
        explicit MouseButtonComponent(const MouseButton button)
            : button_(button)
        {
        }
        [[nodiscard]] auto GetButton() const { return button_; }

    private:
        MouseButton button_;
    };

    class MouseMotionComponent final : public Component {
        OXYGEN_COMPONENT(MouseMotionComponent)
    public:
        explicit MouseMotionComponent(const SubPixelMotion motion)
            : motion_(motion)
        {
        }
        [[nodiscard]] auto GetMotion() const { return motion_; }

    private:
        SubPixelMotion motion_;
    };

    class MouseWheelComponent final : public Component {
        OXYGEN_COMPONENT(MouseWheelComponent)
    public:
        explicit MouseWheelComponent(const SubPixelMotion scroll_amount)
            : scroll_amount_(scroll_amount)
        {
        }
        [[nodiscard]] auto GetScrollAmount() const { return scroll_amount_; }

    private:
        // The amount scrolled, positive horizontally to the right and vertically
        // away from the user
        SubPixelMotion scroll_amount_;
    };

} // namespace oxygen::platform::input

class InputEvent : public Composition {
    OXYGEN_TYPED(InputEvent)
public:
    InputEvent(const TimePoint time, const WindowIdType window_id)
    {
        AddComponent<input::EventInfoComponent>(time, window_id);
    }

    ~InputEvent() override = default;

    OXYGEN_DEFAULT_COPYABLE(InputEvent)
    OXYGEN_DEFAULT_MOVABLE(InputEvent)

    [[nodiscard]] auto GetWindowId() const
    {
        return GetComponent<input::EventInfoComponent>().GetWindowId();
    }
    [[nodiscard]] auto IsFromWindow(const WindowIdType window_id) const
    {
        return GetWindowId() == window_id;
    }

    [[nodiscard]] auto GetTime() const
    {
        return GetComponent<input::EventInfoComponent>().GetTime();
    }
};

class KeyEvent : public InputEvent {
    OXYGEN_TYPED(KeyEvent)
public:
    KeyEvent(const TimePoint& time, const WindowIdType window_id, input::KeyInfo key, ButtonState state)
        : InputEvent(time, window_id)
    {
        AddComponent<input::KeyComponent>(key);
        AddComponent<input::ButtonStateComponent>(state);
    }

    ~KeyEvent() override = default;

    OXYGEN_DEFAULT_COPYABLE(KeyEvent)
    OXYGEN_DEFAULT_MOVABLE(KeyEvent)

    [[nodiscard]] auto GetKeyCode() const
    {
        return GetComponent<input::KeyComponent>().GetKeyInfo().GetKeyCode();
    }
    [[nodiscard]] auto GetKeyState() const
    {
        return GetComponent<input::ButtonStateComponent>().GetState();
    }
    [[nodiscard]] auto IsRepeat() const
    {
        return GetComponent<input::KeyComponent>().GetKeyInfo().IsRepeat();
    }
};

class MouseButtonEvent : public InputEvent {
    OXYGEN_TYPED(MouseButtonEvent)
public:
    MouseButtonEvent(
        const TimePoint& time, const WindowIdType window_id,
        SubPixelPosition position,
        MouseButton button,
        ButtonState state)
        : InputEvent(time, window_id)
    {
        AddComponent<input::EventPositionComponent>(position);
        AddComponent<input::MouseButtonComponent>(button);
        AddComponent<input::ButtonStateComponent>(state);
    }

    ~MouseButtonEvent() override = default;

    OXYGEN_DEFAULT_COPYABLE(MouseButtonEvent)
    OXYGEN_DEFAULT_MOVABLE(MouseButtonEvent)

    [[nodiscard]] auto GetPosition() const
    {
        return GetComponent<input::EventPositionComponent>().GetPosition();
    }

    [[nodiscard]] auto GetButton() const
    {
        return GetComponent<input::MouseButtonComponent>().GetButton();
    }

    [[nodiscard]] auto GetButtonState() const
    {
        return GetComponent<input::ButtonStateComponent>().GetState();
    }
};

class MouseMotionEvent : public InputEvent {
    OXYGEN_TYPED(MouseMotionEvent)
public:
    MouseMotionEvent(
        const TimePoint& time, const WindowIdType window_id,
        SubPixelPosition position,
        SubPixelMotion motion)
        : InputEvent(time, window_id)
    {
        AddComponent<input::EventPositionComponent>(position);
        AddComponent<input::MouseMotionComponent>(motion);
    }

    ~MouseMotionEvent() override = default;

    OXYGEN_DEFAULT_COPYABLE(MouseMotionEvent)
    OXYGEN_DEFAULT_MOVABLE(MouseMotionEvent)

    [[nodiscard]] auto GetPosition() const
    {
        return GetComponent<input::EventPositionComponent>().GetPosition();
    }

    [[nodiscard]] auto GetMotion() const
    {
        return GetComponent<input::MouseMotionComponent>().GetMotion();
    }
};

class MouseWheelEvent : public InputEvent {
    OXYGEN_TYPED(MouseWheelEvent)
public:
    MouseWheelEvent(
        const TimePoint& time, const WindowIdType window_id,
        SubPixelPosition position,
        SubPixelMotion scroll_amount)
        : InputEvent(time, window_id)
    {
        AddComponent<input::EventPositionComponent>(position);
        AddComponent<input::MouseWheelComponent>(scroll_amount);
    }

    ~MouseWheelEvent() override = default;

    OXYGEN_DEFAULT_COPYABLE(MouseWheelEvent)
    OXYGEN_DEFAULT_MOVABLE(MouseWheelEvent)

    [[nodiscard]] auto GetPosition() const
    {
        return GetComponent<input::EventPositionComponent>().GetPosition();
    }

    [[nodiscard]] auto GetScrollAmount() const
    {
        return GetComponent<input::MouseWheelComponent>().GetScrollAmount();
    }
};

} // namespace oxygen::platform
