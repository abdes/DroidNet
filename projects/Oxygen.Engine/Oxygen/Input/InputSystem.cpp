//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Input/InputSystem.h"

#include <algorithm>
#include <cassert>
#include <ranges>

#include "Oxygen/Base/logging.h"
#include "Oxygen/Input/Action.h"
#include "Oxygen/Platform/Common/InputEvent.h"
#include "Oxygen/Platform/Common/input.h"
#include "Oxygen/Platform/Common/platform.h"
#include "oxygen/input/InputMappingContext.h"

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

InputSystem::InputSystem(Platform& platform)
  : platform_(platform)
{
}

void InputSystem::ProcessInput(const platform::InputEvent& event)
{
  if (event.GetType() == platform::InputEventType::kKeyEvent) {
    const auto& key_event = dynamic_cast<const platform::KeyEvent&>(event);
    const auto& slot = Platform::GetInputSlotForKey(key_event.GetKeyCode());
    HandleInput(slot, key_event);
  } else if (event.GetType() == platform::InputEventType::kMouseButtonEvent) {
    const auto& mb_event = dynamic_cast<const platform::MouseButtonEvent&>(event);
    const InputSlot* slot { nullptr };
    switch (mb_event.GetButton()) {
    case platform::MouseButton::kLeft:
      slot = &InputSlots::LeftMouseButton;
      break;
    case platform::MouseButton::kRight:
      slot = &InputSlots::RightMouseButton;
      break;
    case platform::MouseButton::kMiddle:
      slot = &InputSlots::MiddleMouseButton;
      break;
    case platform::MouseButton::kExtButton1:
      slot = &InputSlots::ThumbMouseButton1;
      break;
    case platform::MouseButton::kExtButton2:
      slot = &InputSlots::ThumbMouseButton2;
      break;
    case platform::MouseButton::kNone:
      slot = &InputSlots::None;
    }
    assert(slot != nullptr);
    if (slot != nullptr && *slot != InputSlots::None) {
      HandleInput(*slot, mb_event);
    }
  } else if (event.GetType() == platform::InputEventType::kMouseMotionEvent) {
    const auto& mm_event = dynamic_cast<const platform::MouseMotionEvent&>(event);
    if (std::abs(mm_event.GetMotion().dx) > 0
      || std::abs(mm_event.GetMotion().dy) > 0) {
      HandleInput(InputSlots::MouseXY, mm_event);
    }
  } else if (event.GetType() == platform::InputEventType::kMouseWheelEvent) {
    const auto& mw_event = dynamic_cast<const platform::MouseWheelEvent&>(event);
    if (abs(mw_event.GetScrollAmount().dx) > 0
      && abs(mw_event.GetScrollAmount().dy) > 0) {
      HandleInput(InputSlots::MouseWheelXY, mw_event);
      return;
    }
    if (abs(mw_event.GetScrollAmount().dx) > 0) {
      HandleInput(InputSlots::MouseWheelX, mw_event);
    }
    if (abs(mw_event.GetScrollAmount().dy) > 0) {
      HandleInput(InputSlots::MouseWheelY, mw_event);
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

InputSystem::~InputSystem() = default;

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
  return (found != actions_.cend()) ? *found : std::shared_ptr<Action>();
}

void InputSystem::AddMappingContext(
  const std::shared_ptr<InputMappingContext>& context,
  const int32_t priority)
{
  if (GetMappingContextByName(context->GetName())) {
    DLOG_F(1,
      "Input mapping context with [{}] already exists",
      context->GetName());
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
  return (found != std::ranges::cend(mapping_contexts_))
    ? found->mapping_context
    : std::shared_ptr<InputMappingContext>();
}

void InputSystem::ActivateMappingContext(
  const std::shared_ptr<InputMappingContext>& context)
{
  assert(context);

  const auto found = FindInputMappingContextEntry(mapping_contexts_, *context);
  if (found == std::ranges::cend(mapping_contexts_)) {
    DLOG_F(WARNING,
      "Input mapping context with [] has not been previously added",
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
    DLOG_F(WARNING,
      "Input mapping context with [] has not been previously added",
      context->GetName());
    return;
  }
  found->is_active = false;
}
