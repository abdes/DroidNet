//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string_view>
#include <vector>

#include "Oxygen/Platform/Common/Types.h"
#include "Oxygen/api_export.h"

//------------------------------------------------------------------------------
// InputSlotDetails
//------------------------------------------------------------------------------

namespace oxygen::platform::detail {
class InputSlotDetails;
} // namespace oxygen::platform::detail

//------------------------------------------------------------------------------
// InputSlot
//------------------------------------------------------------------------------

namespace oxygen::platform {

class InputSlot
{
 public:
  explicit InputSlot(const std::string_view name)
    : name_(name)
  {
  }

  [[nodiscard]] auto GetName() const { return name_; }

  OXYGEN_API auto IsModifierKey() const -> bool;
  OXYGEN_API auto IsKeyboardKey() const -> bool;
  OXYGEN_API auto IsMouseButton() const -> bool;

  OXYGEN_API auto IsAxis1D() const -> bool;
  OXYGEN_API auto IsAxis2D() const -> bool;
  OXYGEN_API auto IsAxis3D() const -> bool;

  friend class InputSlots;

  OXYGEN_API
  [[nodiscard]] auto GetDisplayString() const -> std::string_view;

  OXYGEN_API
  [[nodiscard]] auto GetInputCategoryName() const -> std::string_view;

  friend auto operator==(const InputSlot& lhs, const InputSlot& rhs) -> bool
  {
    return lhs.name_ == rhs.name_;
  }
  friend auto operator!=(const InputSlot& lhs, const InputSlot& rhs) -> bool
  {
    return lhs.name_ != rhs.name_;
  }
  friend auto operator<(const InputSlot& lhs, const InputSlot& rhs) -> bool
  {
    return lhs.name_ < rhs.name_;
  }

 private:
  std::string_view name_;
  mutable std::shared_ptr<detail::InputSlotDetails> details_;

  void UpdateDetailsIfNotUpdated() const;
};
} // namespace oxygen::platform

template <>
struct std::hash<oxygen::platform::InputSlot> {
  auto operator()(const oxygen::platform::InputSlot& slot) const noexcept
    -> size_t
  {
    return hash<std::string_view>()(slot.GetName());
  }
};

//------------------------------------------------------------------------------
// InputSlots
//------------------------------------------------------------------------------

namespace oxygen::platform {

class InputSlots
{
 public:
  // Category names static string_view literals
  OXYGEN_API static const std::string_view kKeyCategoryName;
  OXYGEN_API static const std::string_view kMouseCategoryName;

  // -- Static input slots
  // NOLINTBEGIN
  // Mouse slots
  OXYGEN_API static const InputSlot MouseWheelUp;
  OXYGEN_API static const InputSlot MouseWheelDown;
  OXYGEN_API static const InputSlot MouseWheelLeft;
  OXYGEN_API static const InputSlot MouseWheelRight;
  OXYGEN_API static const InputSlot MouseWheelX;
  OXYGEN_API static const InputSlot MouseWheelY;
  OXYGEN_API static const InputSlot MouseWheelXY;
  OXYGEN_API static const InputSlot LeftMouseButton;
  OXYGEN_API static const InputSlot RightMouseButton;
  OXYGEN_API static const InputSlot MiddleMouseButton;
  OXYGEN_API static const InputSlot ThumbMouseButton1;
  OXYGEN_API static const InputSlot ThumbMouseButton2;
  OXYGEN_API static const InputSlot MouseX;
  OXYGEN_API static const InputSlot MouseY;
  OXYGEN_API static const InputSlot MouseXY;

  // Keyboard slots
  OXYGEN_API static const InputSlot None;
  OXYGEN_API static const InputSlot AnyKey;
  OXYGEN_API static const InputSlot BackSpace;
  OXYGEN_API static const InputSlot Delete;
  OXYGEN_API static const InputSlot Tab;
  OXYGEN_API static const InputSlot Clear;
  OXYGEN_API static const InputSlot Return;
  OXYGEN_API static const InputSlot Pause;
  OXYGEN_API static const InputSlot Escape;
  OXYGEN_API static const InputSlot Space;
  OXYGEN_API static const InputSlot Keypad0;
  OXYGEN_API static const InputSlot Keypad1;
  OXYGEN_API static const InputSlot Keypad2;
  OXYGEN_API static const InputSlot Keypad3;
  OXYGEN_API static const InputSlot Keypad4;
  OXYGEN_API static const InputSlot Keypad5;
  OXYGEN_API static const InputSlot Keypad6;
  OXYGEN_API static const InputSlot Keypad7;
  OXYGEN_API static const InputSlot Keypad8;
  OXYGEN_API static const InputSlot Keypad9;
  OXYGEN_API static const InputSlot KeypadPeriod;
  OXYGEN_API static const InputSlot KeypadDivide;
  OXYGEN_API static const InputSlot KeypadMultiply;
  OXYGEN_API static const InputSlot KeypadMinus;
  OXYGEN_API static const InputSlot KeypadPlus;
  OXYGEN_API static const InputSlot KeypadEnter;
  OXYGEN_API static const InputSlot KeypadEquals;
  OXYGEN_API static const InputSlot UpArrow;
  OXYGEN_API static const InputSlot DownArrow;
  OXYGEN_API static const InputSlot RightArrow;
  OXYGEN_API static const InputSlot LeftArrow;
  OXYGEN_API static const InputSlot Insert;
  OXYGEN_API static const InputSlot Home;
  OXYGEN_API static const InputSlot End;
  OXYGEN_API static const InputSlot PageUp;
  OXYGEN_API static const InputSlot PageDown;
  OXYGEN_API static const InputSlot F1;
  OXYGEN_API static const InputSlot F2;
  OXYGEN_API static const InputSlot F3;
  OXYGEN_API static const InputSlot F4;
  OXYGEN_API static const InputSlot F5;
  OXYGEN_API static const InputSlot F6;
  OXYGEN_API static const InputSlot F7;
  OXYGEN_API static const InputSlot F8;
  OXYGEN_API static const InputSlot F9;
  OXYGEN_API static const InputSlot F10;
  OXYGEN_API static const InputSlot F11;
  OXYGEN_API static const InputSlot F12;
  OXYGEN_API static const InputSlot F13;
  OXYGEN_API static const InputSlot F14;
  OXYGEN_API static const InputSlot F15;
  OXYGEN_API static const InputSlot Alpha0;
  OXYGEN_API static const InputSlot Alpha1;
  OXYGEN_API static const InputSlot Alpha2;
  OXYGEN_API static const InputSlot Alpha3;
  OXYGEN_API static const InputSlot Alpha4;
  OXYGEN_API static const InputSlot Alpha5;
  OXYGEN_API static const InputSlot Alpha6;
  OXYGEN_API static const InputSlot Alpha7;
  OXYGEN_API static const InputSlot Alpha8;
  OXYGEN_API static const InputSlot Alpha9;
  OXYGEN_API static const InputSlot Exclaim;
  OXYGEN_API static const InputSlot DoubleQuote;
  OXYGEN_API static const InputSlot Hash;
  OXYGEN_API static const InputSlot Dollar;
  OXYGEN_API static const InputSlot Percent;
  OXYGEN_API static const InputSlot Ampersand;
  OXYGEN_API static const InputSlot Quote;
  OXYGEN_API static const InputSlot LeftParen;
  OXYGEN_API static const InputSlot RightParen;
  OXYGEN_API static const InputSlot Asterisk;
  OXYGEN_API static const InputSlot Plus;
  OXYGEN_API static const InputSlot Comma;
  OXYGEN_API static const InputSlot Minus;
  OXYGEN_API static const InputSlot Period;
  OXYGEN_API static const InputSlot Slash;
  OXYGEN_API static const InputSlot Colon;
  OXYGEN_API static const InputSlot Semicolon;
  OXYGEN_API static const InputSlot Less;
  OXYGEN_API static const InputSlot Equals;
  OXYGEN_API static const InputSlot Greater;
  OXYGEN_API static const InputSlot Question;
  OXYGEN_API static const InputSlot At;
  OXYGEN_API static const InputSlot LeftBracket;
  OXYGEN_API static const InputSlot Backslash;
  OXYGEN_API static const InputSlot RightBracket;
  OXYGEN_API static const InputSlot Caret;
  OXYGEN_API static const InputSlot Underscore;
  OXYGEN_API static const InputSlot BackQuote;
  OXYGEN_API static const InputSlot A;
  OXYGEN_API static const InputSlot B;
  OXYGEN_API static const InputSlot C;
  OXYGEN_API static const InputSlot D;
  OXYGEN_API static const InputSlot E;
  OXYGEN_API static const InputSlot F;
  OXYGEN_API static const InputSlot G;
  OXYGEN_API static const InputSlot H;
  OXYGEN_API static const InputSlot I;
  OXYGEN_API static const InputSlot J;
  OXYGEN_API static const InputSlot K;
  OXYGEN_API static const InputSlot L;
  OXYGEN_API static const InputSlot M;
  OXYGEN_API static const InputSlot N;
  OXYGEN_API static const InputSlot O;
  OXYGEN_API static const InputSlot P;
  OXYGEN_API static const InputSlot Q;
  OXYGEN_API static const InputSlot R;
  OXYGEN_API static const InputSlot S;
  OXYGEN_API static const InputSlot T;
  OXYGEN_API static const InputSlot U;
  OXYGEN_API static const InputSlot V;
  OXYGEN_API static const InputSlot W;
  OXYGEN_API static const InputSlot X;
  OXYGEN_API static const InputSlot Y;
  OXYGEN_API static const InputSlot Z;
  OXYGEN_API static const InputSlot NumLock;
  OXYGEN_API static const InputSlot CapsLock;
  OXYGEN_API static const InputSlot ScrollLock;
  OXYGEN_API static const InputSlot RightShift;
  OXYGEN_API static const InputSlot LeftShift;
  OXYGEN_API static const InputSlot RightControl;
  OXYGEN_API static const InputSlot LeftControl;
  OXYGEN_API static const InputSlot RightAlt;
  OXYGEN_API static const InputSlot LeftAlt;
  OXYGEN_API static const InputSlot LeftMeta;
  OXYGEN_API static const InputSlot RightMeta;
  OXYGEN_API static const InputSlot Help;
  OXYGEN_API static const InputSlot Print;
  OXYGEN_API static const InputSlot SysReq;
  OXYGEN_API static const InputSlot Menu;
  // Mouse buttons
  // NOLINTEND

  friend class oxygen::Platform;
  friend class InputSlot;

  // TODO: review visibility and keep safe methods public
  // private:
  OXYGEN_API static void Initialize();

  OXYGEN_API static void GetAllInputSlots(std::vector<InputSlot>& out_keys);
  OXYGEN_API static auto GetInputSlotForKey(Key key) -> InputSlot;

  OXYGEN_API static auto GetCategoryDisplayName(std::string_view category_name)
    -> std::string_view;

  struct CategoryInfo {
    std::string_view display_string;
  };

  static std::map<InputSlot, std::shared_ptr<detail::InputSlotDetails>> slots_;
  static std::map<Key, InputSlot> key_slots_;
  static std::map<std::string_view, CategoryInfo> categories_;

  // TODO(abdes) add user defined slots and categories
  static void AddCategory(std::string_view category_name,
    std::string_view display_string);
  static void AddInputSlot(const detail::InputSlotDetails& detail);
  static void AddKeyInputSlot(Key key_code,
    const detail::InputSlotDetails& detail);
  [[nodiscard]] static auto GetInputSlotDetails(const InputSlot& slot)
    -> std::shared_ptr<detail::InputSlotDetails>;
};

} // namespace oxygen::platform
