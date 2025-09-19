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
#include <Oxygen/Engine/EngineTag.h>
#include <Oxygen/Engine/FrameContext.h>
#include <Oxygen/Engine/System.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSnapshot.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Platform/Platform.h>

using oxygen::engine::SystemUpdateContext;
using oxygen::input::InputMappingContext;
using oxygen::platform::InputEvent;
using oxygen::platform::InputSlot;
using oxygen::platform::InputSlots;

using namespace oxygen::input;
using oxygen::engine::InputSystem;

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

namespace oxygen::engine {

InputSystem::InputSystem(co::ReaderContext<platform::InputEvent> input_reader)
  : input_reader_(std::move(input_reader))
{
  AddComponent<ObjectMetadata>("InputSystem");
}

auto InputSystem::OnAttached(observer_ptr<AsyncEngine> engine) noexcept -> bool
{
  // InputSystem is now ready for frame-based processing
  return true;
}

auto InputSystem::GetName() const noexcept -> std::string_view
{
  return GetComponent<ObjectMetadata>().GetName();
}

void InputSystem::SetName(std::string_view name) noexcept
{
  GetComponent<ObjectMetadata>().SetName(name);
}

auto InputSystem::OnFrameStart(FrameContext& context) -> void
{
  // Begin frame tracking for all actions
  for (auto& action : actions_) {
    action->BeginFrameTracking();
  }
}

auto InputSystem::OnInput(FrameContext& context) -> co::Co<>
{
  // Drain all events from BroadcastChannel for this frame
  frame_events_.clear();
  while (auto event = input_reader_.TryReceive()) {
    frame_events_.push_back(event);
    ProcessInputEvent(event); // Use existing ProcessInputEvent method

    // Micro-update pass: evaluate triggers immediately after this event with
    // zero dt so same-frame sequences (press->release) are recognized and
    // consumption rules are applied promptly.
    for (const auto& [priority, is_active, mapping_context] :
      std::ranges::reverse_view(mapping_contexts_)) {
      if (is_active) {
        if (mapping_context->Update(Duration::zero())) {
          DLOG_F(1,
            "Stopping updates to mapping contexts after event (input "
            "consumed)");
          // Flush staged input in remaining active contexts (earlier in the
          // reverse traversal) to avoid leaking staged events into later
          // contexts/frames.
          // mapping_contexts_ is sorted ascending by priority. We evaluate
          // in reverse (descending). When a higher-priority context consumes,
          // we must flush the remaining lower-priority contexts that would be
          // evaluated after it. In the forward-sorted list, those contexts are
          // the ones that come BEFORE the consumer.
          auto it = std::ranges::find_if(mapping_contexts_,
            [&mapping_context](const InputMappingContextEntry& e) {
              return e.mapping_context.get() == mapping_context.get();
            });
          for (auto fit = mapping_contexts_.begin(); fit != it; ++fit) {
            if (fit->is_active) {
              fit->mapping_context->FlushPending();
            }
          }
          break;
        }
      }
    }
  }

  // Per-frame pass: advance time for triggers using the game delta time.
  // Skip this pass when delta time is zero to avoid immediately clearing
  // transient motion/wheel values after micro-updates in the same frame.
  if (context.GetGameDeltaTime() > Duration::zero()) {
    for (const auto& [priority, is_active, mapping_context] :
      std::ranges::reverse_view(mapping_contexts_)) {
      if (is_active) {
        if (mapping_context->Update(context.GetGameDeltaTime())) {
          // Input is consumed
          DLOG_F(1, "Stopping updates to mapping contexts (input consumed)");
          // Flush staged input in remaining active contexts to avoid leaking
          // stale staged events into subsequent frames.
          auto it = std::ranges::find_if(mapping_contexts_,
            [&mapping_context](const InputMappingContextEntry& e) {
              return e.mapping_context.get() == mapping_context.get();
            });
          for (auto fit = mapping_contexts_.begin(); fit != it; ++fit) {
            if (fit->is_active) {
              fit->mapping_context->FlushPending();
            }
          }
          break;
        }
      }
    }
  }

  // Action states are updated by the mapping system during the Update calls
  // above

  // Freeze input for this frame: build the snapshot now so it is available to
  // subsequent phases (FixedSim, Gameplay, etc.). Then end frame tracking so
  // edges do not accumulate past this phase.
  current_snapshot_ = std::make_shared<input::InputSnapshot>(actions_);

  for (auto& action : actions_) {
    action->EndFrameTracking();
  }
  co_return;
}

auto InputSystem::OnSnapshot(FrameContext& context) -> void
{
  // Input snapshot is frozen at the end of kInput. Here, we could publish it
  // into the FrameContext unified snapshot once the context supports our
  // snapshot type.
  // TODO: Integrate with FrameContext::SetInputSnapshot when types align.
}

auto InputSystem::OnFrameEnd(FrameContext& context) -> void
{
  // Clear frame data for next frame
  frame_events_.clear();
  current_snapshot_.reset();
}

void InputSystem::ProcessInputEvent(std::shared_ptr<InputEvent> event)
{
  using platform::KeyEvent;
  using platform::MouseButtonEvent;
  using platform::MouseMotionEvent;
  using platform::MouseWheelEvent;
  using platform::input::KeyComponent;
  using platform::input::MouseButtonComponent;
  using platform::input::MouseMotionComponent;
  using platform::input::MouseWheelComponent;

  DLOG_F(2, "Processing input event of type {}", event->GetTypeNamePretty());

  // Keyboard events
  if (const auto event_type = event->GetTypeId();
    event_type == KeyEvent::ClassTypeId()) {
    DCHECK_F(event->HasComponent<platform::input::KeyComponent>());
    const auto key_code
      = event->GetComponent<KeyComponent>().GetKeyInfo().GetKeyCode();
    const auto& slot = Platform::GetInputSlotForKey(key_code);
    HandleInput(slot, *event);
  }
  // Mouse button events
  else if (event_type == MouseButtonEvent::ClassTypeId()) {
    DCHECK_F(event->HasComponent<platform::input::MouseButtonComponent>());
    const auto button = event->GetComponent<MouseButtonComponent>().GetButton();
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
      HandleInput(*slot, *event);
    }
  }
  // Mouse motion events
  else if (event_type == MouseMotionEvent::ClassTypeId()) {
    DCHECK_F(event->HasComponent<platform::input::MouseMotionComponent>());
    if (const auto [dx, dy]
      = event->GetComponent<MouseMotionComponent>().GetMotion();
      std::abs(dx) > 0 || std::abs(dy) > 0) {
      HandleInput(InputSlots::MouseXY, *event);
    }
  }
  // Mouse wheel events
  else if (event_type == MouseWheelEvent::ClassTypeId()) {
    DCHECK_F(event->HasComponent<platform::input::MouseWheelComponent>());
    const auto [dx, dy]
      = event->GetComponent<MouseWheelComponent>().GetScrollAmount();
    if (std::abs(dx) > 0 && std::abs(dy) > 0) {
      HandleInput(InputSlots::MouseWheelXY, *event);
      return;
    }
    if (std::abs(dx) > 0) {
      HandleInput(InputSlots::MouseWheelX, *event);
    }
    if (std::abs(dy) > 0) {
      HandleInput(InputSlots::MouseWheelY, *event);
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

#if 0
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
#endif

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

void InputSystem::ClearAllActions() { actions_.clear(); }

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
  const std::shared_ptr<InputMappingContext>& context, const int32_t priority)
{
  if (GetMappingContextByName(context->GetName())) {
    DLOG_F(
      1, "Input mapping context with [{}] already exists", context->GetName());
    return;
  }
  InputMappingContextEntry new_entry {
    .priority = priority,
    .mapping_context = context,
  };
  const auto insert_location
    = std::ranges::lower_bound(mapping_contexts_, new_entry,
      [](const InputMappingContextEntry& entry,
        const InputMappingContextEntry& value) {
        return entry.priority < value.priority;
      });
  mapping_contexts_.emplace(insert_location, new_entry);
}

void InputSystem::RemoveMappingContext(
  const std::shared_ptr<InputMappingContext>& context)
{
  std::erase_if(
    mapping_contexts_, [&context](const InputMappingContextEntry& entry) {
      return context == entry.mapping_context;
    });
}

void InputSystem::ClearAllMappingContexts() { mapping_contexts_.clear(); }

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
    DLOG_F(WARNING,
      "Input mapping context with [{}] has not been previously added",
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
      "Input mapping context with [{}] has not been previously added",
      context->GetName());
    return;
  }
  found->is_active = false;
}

} // namespace oxygen::engine
