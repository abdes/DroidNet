//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <exception>
#include <list>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/ObjectMetadata.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSnapshot.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/OxCo/BroadcastChannel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/InputEvent.h>

using oxygen::input::Action;
using oxygen::input::InputMappingContext;
using oxygen::platform::InputEvent;
using oxygen::platform::InputSlot;
using oxygen::platform::InputSlots;

using oxygen::engine::InputSystem;

namespace {
constexpr std::string_view kCVarInputEnabled = "nput.enabled";
constexpr std::string_view kCVarInputLogEvents = "nput.log_events";

[[nodiscard]] auto FindInputMappingContextEntry(
  std::list<InputSystem::InputMappingContextEntry>& list,
  const InputMappingContext& context)
{
  return std::ranges::find_if(list,
    [&context](const InputSystem::InputMappingContextEntry& entry) -> bool {
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

auto InputSystem::OnAttached(observer_ptr<IAsyncEngine> /*engine*/) noexcept
  -> bool
{
  // InputSystem is now ready for frame-based processing
  return true;
}

auto InputSystem::RegisterConsoleBindings(
  const observer_ptr<console::Console> console) noexcept -> void
{
  if (console == nullptr) {
    return;
  }

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarInputEnabled),
    .help = "Enable input processing for InputSystem",
    .default_value = input_enabled_,
    .flags = console::CVarFlags::kArchive,
    .min_value = {},
    .max_value = {},
  });

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarInputLogEvents),
    .help = "Enable verbose InputSystem per-event logging",
    .default_value = log_events_,
    .flags = console::CVarFlags::kDevOnly,
    .min_value = {},
    .max_value = {},
  });
}

auto InputSystem::ApplyConsoleCVars(
  const observer_ptr<const console::Console> console) noexcept -> void
{
  if (console == nullptr) {
    return;
  }

  bool input_enabled = input_enabled_;
  if (console->TryGetCVarValue<bool>(kCVarInputEnabled, input_enabled)) {
    input_enabled_ = input_enabled;
  }

  bool log_events_enabled = log_events_;
  if (console->TryGetCVarValue<bool>(kCVarInputLogEvents, log_events_enabled)) {
    log_events_ = log_events_enabled;
  }
}

auto InputSystem::OnSessionBegin(observer_ptr<IAsyncEngine> /*engine*/) noexcept
  -> bool
{
  try {
    auto baseline = SessionBaseline { .actions = actions_, .contexts = {} };
    baseline.contexts.reserve(mapping_contexts_.size());
    for (const auto& entry : mapping_contexts_) {
      baseline.contexts.emplace_back(entry.mapping_context, entry.is_active);
    }
    session_baseline_ = std::move(baseline);
    return true;
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "Input session could not begin: {}", ex.what());
  }
  return false;
}

auto InputSystem::OnSessionEnd(observer_ptr<IAsyncEngine> /*engine*/) noexcept
  -> void
{
  if (!session_baseline_) {
    return;
  }
  const auto baseline = std::move(*session_baseline_);
  session_baseline_.reset();

  // Contexts first: removing one ends the contributions of its bindings.
  const auto was_present = [&baseline](const auto& context) -> bool {
    return std::ranges::any_of(
      baseline.contexts, [&context](const auto& recorded) -> bool {
        return recorded.first == context;
      });
  };
  std::vector<std::shared_ptr<input::InputMappingContext>> added;
  for (const auto& entry : mapping_contexts_) {
    if (!was_present(entry.mapping_context)) {
      added.push_back(entry.mapping_context);
    }
  }
  for (const auto& context : added) {
    RemoveMappingContext(context);
  }

  pending_deactivations_.clear();
  for (auto& entry : mapping_contexts_) {
    const auto recorded = std::ranges::find_if(
      baseline.contexts, [&entry](const auto& item) -> bool {
        return item.first == entry.mapping_context;
      });
    const bool was_active
      = recorded != baseline.contexts.end() && recorded->second;
    if (entry.is_active && !was_active) {
      entry.mapping_context->DeactivateAll();
    }
    entry.is_active = was_active;
  }

  std::erase_if(actions_, [&baseline](const auto& action) -> bool {
    return std::ranges::find(baseline.actions, action)
      == baseline.actions.end();
  });
  for (const auto& action : actions_) {
    action->pending_fixed_triggers_ = 0U;
  }
}

auto InputSystem::GetName() const noexcept -> std::string_view
{
  return GetComponent<ObjectMetadata>().GetName();
}

void InputSystem::SetName(std::string_view name) noexcept
{
  GetComponent<ObjectMetadata>().SetName(name);
}

auto InputSystem::OnFrameStart(observer_ptr<FrameContext> /*context*/) -> void
{
  for (auto& action : actions_) {
    action->BeginFrameTracking();
  }
}

auto InputSystem::OnInput(observer_ptr<FrameContext> context) -> co::Co<>
{
  ApplyPendingDeactivations();

  // Trigger time is physical time: hold, tap and pulse windows are durations
  // the player feels, so they keep running in pause menus and do not stretch
  // in slow motion. Fall back to game time when the engine provides no
  // physical delta.
  const auto& timing = context->GetModuleTimingData();
  const auto trigger_dt
    = timing.physical_delta_time > time::CanonicalDuration {}
    ? timing.physical_delta_time
    : context->GetGameDeltaTime();

  // Advance time before draining events, so that relative (mouse) mappings,
  // whose value clears after each evaluation, are not re-evaluated with a
  // cleared value after this frame's events.
  if (trigger_dt > time::CanonicalDuration {}) {
    EvaluateContexts(trigger_dt, nullptr);
  }

  if (!input_enabled_) {
    if (was_enabled_) {
      for (const auto& entry : mapping_contexts_) {
        entry.mapping_context->DeactivateAll();
      }
      was_enabled_ = false;
    }
    DrainPendingInputEvents();
  } else {
    was_enabled_ = true;
    while (const auto event = input_reader_.TryReceive()) {
      // Evaluate after each event with zero delta, so press/release within
      // one frame is recognized and consumption applies to this event.
      const auto slot = RouteInputEvent(*event);
      EvaluateContexts(
        time::CanonicalDuration {}, slot ? &slot.value() : nullptr);
    }
  }

  // Freeze this frame's input for the later phases.
  current_snapshot_ = std::make_shared<input::InputSnapshot>(actions_);
  for (auto& action : actions_) {
    action->EndFrameTracking();
  }
  co_return;
}

auto InputSystem::OnSnapshot(observer_ptr<FrameContext> /*context*/) -> void
{
  // The snapshot is frozen at the end of kInput and published by the engine.
}

auto InputSystem::OnFrameEnd(observer_ptr<FrameContext> /*context*/) -> void
{
  current_snapshot_.reset();
}

auto InputSystem::DrainPendingInputEvents() -> void
{
  while (input_reader_.TryReceive()) { }
}

void InputSystem::ApplyPendingDeactivations()
{
  for (const auto& context : pending_deactivations_) {
    context->DeactivateAll();
  }
  pending_deactivations_.clear();
}

void InputSystem::EvaluateContexts(const time::CanonicalDuration delta_time,
  const platform::InputSlot* event_slot)
{
  input::InputMappingContext::ClaimedSources claimed;
  for (const auto& entry : std::ranges::reverse_view(mapping_contexts_)) {
    if (entry.is_active) {
      (void)entry.mapping_context->Update(delta_time, claimed, event_slot);
    }
  }
}

auto InputSystem::RouteInputEvent(const InputEvent& event)
  -> std::optional<InputSlot>
{
  using platform::KeyEvent;
  using platform::MouseButtonEvent;
  using platform::MouseMotionEvent;
  using platform::MouseWheelEvent;
  using platform::input::KeyComponent;
  using platform::input::MouseButtonComponent;
  using platform::input::MouseMotionComponent;
  using platform::input::MouseWheelComponent;

  if (log_events_) {
    DLOG_F(2, "Processing input event of type {}", event.GetTypeNamePretty());
  }

  std::optional<InputSlot> slot;
  if (const auto event_type = event.GetTypeId();
    event_type == KeyEvent::ClassTypeId()) {
    DCHECK_F(event.HasComponent<KeyComponent>());
    slot = InputSlots::GetInputSlotForKey(
      event.GetComponent<KeyComponent>().GetKeyInfo().GetKeyCode());
  } else if (event_type == MouseButtonEvent::ClassTypeId()) {
    DCHECK_F(event.HasComponent<MouseButtonComponent>());
    using enum platform::MouseButton;
    switch (event.GetComponent<MouseButtonComponent>().GetButton()) {
    case kLeft:
      slot = InputSlots::LeftMouseButton;
      break;
    case kRight:
      slot = InputSlots::RightMouseButton;
      break;
    case kMiddle:
      slot = InputSlots::MiddleMouseButton;
      break;
    case kExtButton1:
      slot = InputSlots::ThumbMouseButton1;
      break;
    case kExtButton2:
      slot = InputSlots::ThumbMouseButton2;
      break;
    case kNone:
      break;
    }
  } else if (event_type == MouseMotionEvent::ClassTypeId()) {
    DCHECK_F(event.HasComponent<MouseMotionComponent>());
    const auto [dx, dy]
      = event.GetComponent<MouseMotionComponent>().GetMotion();
    if (dx != 0.0F || dy != 0.0F) {
      slot = InputSlots::MouseXY;
    }
  } else if (event_type == MouseWheelEvent::ClassTypeId()) {
    DCHECK_F(event.HasComponent<MouseWheelComponent>());
    // Every wheel event is routed on MouseWheelXY; contexts fan it out to
    // the axis and direction slots that read it.
    const auto [dx, dy]
      = event.GetComponent<MouseWheelComponent>().GetScrollAmount();
    if (dx != 0.0F || dy != 0.0F) {
      slot = InputSlots::MouseWheelXY;
    }
  }

  if (!slot || *slot == InputSlots::None) {
    return std::nullopt;
  }
  for (const auto& entry : std::ranges::reverse_view(mapping_contexts_)) {
    if (entry.is_active) {
      entry.mapping_context->HandleInput(*slot, event);
    }
  }
  return slot;
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

void InputSystem::ClearAllActions() { actions_.clear(); }

auto InputSystem::GetActionByName(std::string_view name) const
  -> std::shared_ptr<Action>
{
  const auto found = std::ranges::find_if(
    actions_, [&name](const std::shared_ptr<Action>& entry) -> bool {
      return entry->GetName() == name;
    });
  return found != actions_.cend() ? *found : std::shared_ptr<Action>();
}

auto InputSystem::GetActionByKey(const data::AssetKey& key) const
  -> std::shared_ptr<Action>
{
  if (key.IsNil()) {
    return {};
  }
  const auto found = std::ranges::find_if(
    actions_, [&key](const std::shared_ptr<Action>& entry) -> bool {
      return entry->GetAssetKey() == key;
    });
  return found != actions_.cend() ? *found : std::shared_ptr<Action>();
}

void InputSystem::AddMappingContext(
  const std::shared_ptr<InputMappingContext>& context, const int32_t priority)
{
  // An asset context is identified by its key; a code context by its name.
  const auto& key = context->GetAssetKey();
  if (key.IsNil() ? GetMappingContextByName(context->GetName()) != nullptr
                  : GetMappingContextByKey(key) != nullptr) {
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
        const InputMappingContextEntry& value) -> bool {
        return entry.priority < value.priority;
      });
  mapping_contexts_.emplace(insert_location, new_entry);
}

void InputSystem::RemoveMappingContext(
  const std::shared_ptr<InputMappingContext>& context)
{
  if (context) {
    context->DeactivateAll();
    std::erase(pending_deactivations_, context);
  }
  std::erase_if(mapping_contexts_,
    [&context](const InputMappingContextEntry& entry) -> bool {
      return context == entry.mapping_context;
    });
}

void InputSystem::ClearAllMappingContexts()
{
  for (const auto& entry : mapping_contexts_) {
    entry.mapping_context->DeactivateAll();
  }
  pending_deactivations_.clear();
  mapping_contexts_.clear();
}

auto InputSystem::GetMappingContextByName(std::string_view name) const
  -> std::shared_ptr<InputMappingContext>
{
  const auto found = std::ranges::find_if(
    mapping_contexts_, [&name](const InputMappingContextEntry& entry) -> bool {
      return name == entry.mapping_context->GetName();
    });
  return found != std::ranges::cend(mapping_contexts_)
    ? found->mapping_context
    : std::shared_ptr<InputMappingContext>();
}

auto InputSystem::GetMappingContextByKey(const data::AssetKey& key) const
  -> std::shared_ptr<InputMappingContext>
{
  if (key.IsNil()) {
    return {};
  }
  const auto found = std::ranges::find_if(
    mapping_contexts_, [&key](const InputMappingContextEntry& entry) -> bool {
      return entry.mapping_context->GetAssetKey() == key;
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

  // Reactivated before its deactivation took effect: keep its state.
  std::erase(pending_deactivations_, found->mapping_context);
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
  if (found->is_active) {
    pending_deactivations_.push_back(found->mapping_context);
  }
  found->is_active = false;
}

} // namespace oxygen::engine
