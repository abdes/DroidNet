//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "oxygen/input/input_mapping_context.h"

#include <cmath>

#include "oxygen/base/logging.h"
#include "oxygen/input/action.h"
#include "oxygen/input/input_action_mapping.h"
#include "oxygen/platform/input.h"
#include "oxygen/platform/input_event.h"

using oxygen::input::InputMappingContext;
using oxygen::platform::InputEvent;
using oxygen::platform::InputSlot;
using oxygen::platform::InputSlots;
using oxygen::platform::MouseMotionEvent;
using oxygen::platform::MouseWheelEvent;

InputMappingContext::InputMappingContext(std::string name)
  : name_(std::move(name)) {
}

void InputMappingContext::AddMapping(
  std::shared_ptr<InputActionMapping> mapping) {
  mappings_.emplace_back(std::move(mapping));
}

namespace {

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  auto SimilarSlots(const InputSlot& mapping_slot,
                    const InputSlot& event_slot,
                    const InputEvent& event) {
    if (event_slot == InputSlots::MouseXY) {
      const auto& mm_event = dynamic_cast<const MouseMotionEvent&>(event);
      return (mapping_slot == InputSlots::MouseX
              && std::abs(mm_event.GetMotion().dx) > 0)
        || (mapping_slot == InputSlots::MouseY
            && std::abs(mm_event.GetMotion().dy) > 0)
        || mapping_slot == InputSlots::MouseXY;
    }
    if (event_slot == InputSlots::MouseWheelXY) {
      const auto& mw_event = dynamic_cast<const MouseWheelEvent&>(event);
      return mapping_slot == InputSlots::MouseWheelXY
        || (mapping_slot == InputSlots::MouseWheelY
            && std::abs(mw_event.GetScrollAmount().dy) > 0)
        || (mapping_slot == InputSlots::MouseWheelUp
            && mw_event.GetScrollAmount().dy > 0)
        || (mapping_slot == InputSlots::MouseWheelDown
            && mw_event.GetScrollAmount().dy < 0)
        || (mapping_slot == InputSlots::MouseWheelX
            && std::abs(mw_event.GetScrollAmount().dx) > 0)
        || (mapping_slot == InputSlots::MouseWheelLeft
            && mw_event.GetScrollAmount().dx < 0)
        || (mapping_slot == InputSlots::MouseWheelRight
            && mw_event.GetScrollAmount().dx > 0);
    }
    if (event_slot == InputSlots::MouseWheelX) {
      const auto& mw_event = dynamic_cast<const MouseWheelEvent&>(event);
      return mapping_slot == InputSlots::MouseWheelX
        || (mapping_slot == InputSlots::MouseWheelLeft
            && mw_event.GetScrollAmount().dx < 0)
        || (mapping_slot == InputSlots::MouseWheelRight
            && mw_event.GetScrollAmount().dx > 0);
    }
    if (event_slot == InputSlots::MouseWheelY) {
      const auto& mw_event = dynamic_cast<const MouseWheelEvent&>(event);
      return mapping_slot == InputSlots::MouseWheelY
        || (mapping_slot == InputSlots::MouseWheelUp
            && mw_event.GetScrollAmount().dy > 0)
        || (mapping_slot == InputSlots::MouseWheelDown
            && mw_event.GetScrollAmount().dy < 0);
    }
    return mapping_slot == event_slot;
  }

}  // namespace

void InputMappingContext::HandleInput(const InputSlot& slot,
                                      const InputEvent& event) const {
  // Iterate over all mapping contexts associated with the slot that had the
  // input event.
  for (const auto& mapping : mappings_) {
    // Extrapolate the slot from platform to the action mapping slot based on
    // the input values. For example if the platform gives us a MouseXY but the
    // mapping is for MouseX, then we still call the mapping.
    if (SimilarSlots(mapping->GetSlot(), slot, event)) {
      mapping->HandleInput(event);
    }
  }
}

auto InputMappingContext::Update(Duration delta_time) const -> bool {
  // Iterate over all mapping contexts associated with the slot that had the
  // input event.
  bool input_consumed{ false };
  for (const auto& mapping : mappings_) {
    if (!input_consumed) {
      input_consumed = mapping->Update(delta_time);
    }
    else {
      DLOG_F(1, "Cancel input for action: {}", mapping->GetAction()->GetName());
      // Input is consumed by a triggered action
      mapping->CancelInput();
    }
  }
  return input_consumed;
}
