//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <list>
#include <memory>
#include <ranges>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Engine/System.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Platform/Platform.h>

using oxygen::engine::SystemUpdateContext;
using oxygen::input::InputMappingContext;
using oxygen::input::InputSystem;
using oxygen::platform::InputEvent;
using oxygen::platform::InputSlot;
using oxygen::platform::InputSlots;

namespace {
[[nodiscard]] auto FindInputMappingContextEntry(
    std::list<InputSystem::InputMappingContextEntry>& list,
    const InputMappingContext& context)
{
    return std::ranges::find_if(
        list, [&context](const InputSystem::InputMappingContextEntry& entry) {
            return context.GetName() == entry.mapping_context->GetName();
        });
}
} // namespace

InputSystem::InputSystem(PlatformPtr platform)
    : platform_(std::move(platform))
{
    platform_->Async().Nursery().Start(
        [this]() -> co::Co<> {
            auto input = platform_->Input().ForRead();
            while (true) {
                auto event = co_await input.Receive();
                ProcessInputEvent(*event);
            }
        });
}

void InputSystem::ProcessInputEvent(const InputEvent& event)
{
    using platform::KeyEvent;
    using platform::MouseButtonEvent;
    using platform::MouseMotionEvent;
    using platform::MouseWheelEvent;
    using platform::input::KeyComponent;
    using platform::input::MouseButtonComponent;
    using platform::input::MouseMotionComponent;
    using platform::input::MouseWheelComponent;

    // Keyboard events
    if (const auto event_type = event.GetTypeId();
        event_type == KeyEvent::ClassTypeId()) {
        DCHECK_F(event.HasComponent<platform::input::KeyComponent>());
        const auto key_code = event.GetComponent<KeyComponent>().GetKeyInfo().GetKeyCode();
        const auto& slot = Platform::GetInputSlotForKey(key_code);
        HandleInput(slot, event);
    }
    // Mouse button events
    else if (event_type == MouseButtonEvent::ClassTypeId()) {
        DCHECK_F(event.HasComponent<platform::input::MouseButtonComponent>());
        const auto button = event.GetComponent<MouseButtonComponent>().GetButton();
        const InputSlot* slot { nullptr };

        using enum platform::MouseButton;
        switch (button) {
        case kLeft:
            slot = &InputSlots::LeftMouseButton;
            break;
        case kRight:
            slot = &InputSlots::RightMouseButton;
            break;
        case kMiddle:
            slot = &InputSlots::MiddleMouseButton;
            break;
        case kExtButton1:
            slot = &InputSlots::ThumbMouseButton1;
            break;
        case kExtButton2:
            slot = &InputSlots::ThumbMouseButton2;
            break;
        case kNone:
            slot = &InputSlots::None;
        }
        assert(slot != nullptr);
        if (slot != nullptr && *slot != InputSlots::None) {
            HandleInput(*slot, event);
        }
    }
    // Mouse motion events
    else if (event_type == MouseMotionEvent::ClassTypeId()) {
        DCHECK_F(event.HasComponent<platform::input::MouseMotionComponent>());
        if (const auto [dx, dy] = event.GetComponent<MouseMotionComponent>().GetMotion();
            std::abs(dx) > 0 || std::abs(dy) > 0) {
            HandleInput(InputSlots::MouseXY, event);
        }
    }
    // Mouse wheel events
    else if (event_type == MouseWheelEvent::ClassTypeId()) {
        DCHECK_F(event.HasComponent<platform::input::MouseWheelComponent>());
        const auto [dx, dy] = event.GetComponent<MouseWheelComponent>().GetScrollAmount();
        if (abs(dx) > 0 && abs(dy) > 0) {
            HandleInput(InputSlots::MouseWheelXY, event);
            return;
        }
        if (abs(dx) > 0) {
            HandleInput(InputSlots::MouseWheelX, event);
        }
        if (abs(dy) > 0) {
            HandleInput(InputSlots::MouseWheelY, event);
        }
    }
}

void InputSystem::HandleInput(const InputSlot& slot, const InputEvent& event)
{
    // iterate over mapping contexts in the reverse order (higher priority
    // first)
    for (const auto& [priority, is_active, context] :
        std::ranges::reverse_view(mapping_contexts_)) {
        if (is_active) {
            context->HandleInput(slot, event);
        }
    }
}

void InputSystem::Update(const SystemUpdateContext& update_context)
{
    // iterate over mapping contexts in the reverse order (higher priority first)
    for (const auto& [priority, is_active, mapping_context] :
        std::ranges::reverse_view(mapping_contexts_)) {
        if (is_active) {
            if (mapping_context->Update(update_context.delta_time)) {
                // Input is consumed
                DLOG_F(1, "Stopping updates to mapping contexts (input consumed)");
                break;
            }
        }
    }

    // Reset the triggered state of all actions at each tick. The state will be
    // re-evaluated fresh for use by chained action triggers and any listeners on
    // the action state changes.
    for (const auto& action : actions_) {
        action->ClearTriggeredState();
    }
}

void InputSystem::AddAction(const std::shared_ptr<Action>& action)
{
    if (std::ranges::find(actions_, action) != std::ranges::cend(actions_)) {
        DLOG_F(WARNING, "Action [{}] has already been added", action->GetName());
        return;
    }

    actions_.push_back(action);
}

void InputSystem::RemoveAction(const std::shared_ptr<Action>& action)
{
    std::erase(actions_, action);
}

void InputSystem::ClearAllActions()
{
    actions_.clear();
}

auto InputSystem::GetActionByName(std::string_view name) const
    -> std::shared_ptr<Action>
{
    const auto found = std::ranges::find_if(
        actions_, [&name](const std::shared_ptr<Action>& entry) {
            return entry->GetName() == name;
        });
    return found != actions_.cend() ? *found : std::shared_ptr<Action>();
}

void InputSystem::AddMappingContext(
    const std::shared_ptr<InputMappingContext>& context,
    const int32_t priority)
{
    if (GetMappingContextByName(context->GetName())) {
        DLOG_F(1, "Input mapping context with [{}] already exists", context->GetName());
        return;
    }
    InputMappingContextEntry new_entry {
        .priority = priority,
        .mapping_context = context,
    };
    const auto insert_location = std::ranges::lower_bound(mapping_contexts_,
        new_entry,
        [](const InputMappingContextEntry& entry,
            const InputMappingContextEntry& value) {
            return entry.priority < value.priority;
        });
    mapping_contexts_.emplace(insert_location, new_entry);
}

void InputSystem::RemoveMappingContext(
    const std::shared_ptr<InputMappingContext>& context)
{
    std::erase_if(mapping_contexts_,
        [&context](const InputMappingContextEntry& entry) {
            return context == entry.mapping_context;
        });
}

void InputSystem::ClearAllMappingContexts()
{
    mapping_contexts_.clear();
}

auto InputSystem::GetMappingContextByName(std::string_view name) const
    -> std::shared_ptr<InputMappingContext>
{
    const auto found = std::ranges::find_if(
        mapping_contexts_, [&name](const InputMappingContextEntry& entry) {
            return name == entry.mapping_context->GetName();
        });
    return found != std::ranges::cend(mapping_contexts_)
        ? found->mapping_context
        : std::shared_ptr<InputMappingContext>();
}

void InputSystem::ActivateMappingContext(
    const std::shared_ptr<InputMappingContext>& context)
{
    assert(context);

    const auto found = FindInputMappingContextEntry(mapping_contexts_, *context);
    if (found == std::ranges::cend(mapping_contexts_)) {
        DLOG_F(WARNING, "Input mapping context with [] has not been previously added",
            context->GetName());
        return;
    }

    found->is_active = true;
}

void InputSystem::DeactivateMappingContext(
    const std::shared_ptr<InputMappingContext>& context)
{
    assert(context);
    const auto found = FindInputMappingContextEntry(mapping_contexts_, *context);
    if (found == std::ranges::cend(mapping_contexts_)) {
        DLOG_F(WARNING, "Input mapping context with [] has not been previously added",
            context->GetName());
        return;
    }
    found->is_active = false;
}
