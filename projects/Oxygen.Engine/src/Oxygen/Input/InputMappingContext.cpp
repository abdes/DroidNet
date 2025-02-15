//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdlib>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "Oxygen/Input/Action.h"
#include "Oxygen/Input/InputActionMapping.h"
#include "Oxygen/Input/InputMappingContext.h"
#include "Oxygen/Platform/Input.h"
#include "Oxygen/Platform/InputEvent.h"
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/TimeUtils.h>
#include <Oxygen/Composition/Composition.h>

using oxygen::input::InputMappingContext;
using oxygen::platform::InputEvent;
using oxygen::platform::InputSlot;
using oxygen::platform::InputSlots;
using oxygen::platform::MouseMotionEvent;
using oxygen::platform::MouseWheelEvent;

InputMappingContext::InputMappingContext(std::string name)
    : name_(std::move(name))
{
}

void InputMappingContext::AddMapping(
    std::shared_ptr<InputActionMapping> mapping)
{
    mappings_.emplace_back(std::move(mapping));
}

namespace {

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto SimilarSlots(const InputSlot& mapping_slot,
    const InputSlot& event_slot,
    const InputEvent& event)
{
    using oxygen::platform::input::MouseMotionComponent;
    using oxygen::platform::input::MouseWheelComponent;

    if (event_slot == InputSlots::MouseXY) {
        const auto [dx, dy] = event.GetComponent<MouseMotionComponent>().GetMotion();
        return (mapping_slot == InputSlots::MouseX && std::abs(dx) > 0)
            || (mapping_slot == InputSlots::MouseY && std::abs(dy) > 0)
            || mapping_slot == InputSlots::MouseXY;
    }
    if (event_slot == InputSlots::MouseWheelXY) {
        const auto [dx, dy] = event.GetComponent<MouseWheelComponent>().GetScrollAmount();
        return mapping_slot == InputSlots::MouseWheelXY
            || (mapping_slot == InputSlots::MouseWheelY && std::abs(dy) > 0)
            || (mapping_slot == InputSlots::MouseWheelUp && dy > 0)
            || (mapping_slot == InputSlots::MouseWheelDown && dy < 0)
            || (mapping_slot == InputSlots::MouseWheelX && std::abs(dx) > 0)
            || (mapping_slot == InputSlots::MouseWheelLeft && dx < 0)
            || (mapping_slot == InputSlots::MouseWheelRight && dx > 0);
    }
    if (event_slot == InputSlots::MouseWheelX) {
        const auto [dx, dy] = event.GetComponent<MouseWheelComponent>().GetScrollAmount();
        return mapping_slot == InputSlots::MouseWheelX
            || (mapping_slot == InputSlots::MouseWheelLeft && dx < 0)
            || (mapping_slot == InputSlots::MouseWheelRight && dx > 0);
    }
    if (event_slot == InputSlots::MouseWheelY) {
        const auto [dx, dy] = event.GetComponent<MouseWheelComponent>().GetScrollAmount();
        return mapping_slot == InputSlots::MouseWheelY
            || (mapping_slot == InputSlots::MouseWheelUp && dy > 0)
            || (mapping_slot == InputSlots::MouseWheelDown && dy < 0);
    }
    return mapping_slot == event_slot;
}

} // namespace

void InputMappingContext::HandleInput(const InputSlot& slot,
    const InputEvent& event) const
{
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

auto InputMappingContext::Update(const Duration delta_time) const -> bool
{
    // Iterate over all mapping contexts associated with the slot that had the
    // input event.
    bool input_consumed { false };
    for (const auto& mapping : mappings_) {
        if (!input_consumed) {
            input_consumed = mapping->Update(delta_time);
        } else {
            DLOG_F(1, "Cancel input for action: {}", mapping->GetAction()->GetName());
            // Input is consumed by a triggered action
            mapping->CancelInput();
        }
    }
    return input_consumed;
}
