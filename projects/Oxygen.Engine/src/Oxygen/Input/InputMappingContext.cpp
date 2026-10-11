//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/InputEvent.h>

using oxygen::input::InputMappingContext;
using oxygen::platform::InputEvent;
using oxygen::platform::InputSlot;
using oxygen::platform::InputSlots;

namespace {

auto IsDigitalEvent(const InputEvent& event) -> bool
{
  const auto type = event.GetTypeId();
  return type == oxygen::platform::KeyEvent::ClassTypeId()
    || type == oxygen::platform::MouseButtonEvent::ClassTypeId();
}

//! Whether a mapping on `mapping_slot` reads an event routed on `event_slot`.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto Reads(const InputSlot& mapping_slot, const InputSlot& event_slot,
  const InputEvent& event) -> bool
{
  using oxygen::platform::input::MouseMotionComponent;
  using oxygen::platform::input::MouseWheelComponent;

  if (mapping_slot == InputSlots::AnyKey) {
    return IsDigitalEvent(event);
  }
  if (event_slot == InputSlots::MouseXY) {
    const auto [dx, dy]
      = event.GetComponent<MouseMotionComponent>().GetMotion();
    return mapping_slot == InputSlots::MouseXY
      || (mapping_slot == InputSlots::MouseX && dx != 0.0F)
      || (mapping_slot == InputSlots::MouseY && dy != 0.0F);
  }
  if (event_slot == InputSlots::MouseWheelXY) {
    const auto [dx, dy]
      = event.GetComponent<MouseWheelComponent>().GetScrollAmount();
    return mapping_slot == InputSlots::MouseWheelXY
      || (mapping_slot == InputSlots::MouseWheelX && dx != 0.0F)
      || (mapping_slot == InputSlots::MouseWheelLeft && dx < 0.0F)
      || (mapping_slot == InputSlots::MouseWheelRight && dx > 0.0F)
      || (mapping_slot == InputSlots::MouseWheelY && dy != 0.0F)
      || (mapping_slot == InputSlots::MouseWheelUp && dy > 0.0F)
      || (mapping_slot == InputSlots::MouseWheelDown && dy < 0.0F);
  }
  return mapping_slot == event_slot;
}

//! Append the physical sources a slot reads (see InputMappingContext).
void AppendSources(const InputSlot& slot, const InputSlot* event_slot,
  InputMappingContext::ClaimedSources& out)
{
  if (slot == InputSlots::MouseXY) {
    out.push_back(&InputSlots::MouseX);
    out.push_back(&InputSlots::MouseY);
  } else if (slot == InputSlots::MouseWheelXY) {
    out.push_back(&InputSlots::MouseWheelX);
    out.push_back(&InputSlots::MouseWheelY);
  } else if (slot == InputSlots::MouseWheelX
    || slot == InputSlots::MouseWheelLeft
    || slot == InputSlots::MouseWheelRight) {
    out.push_back(&InputSlots::MouseWheelX);
  } else if (slot == InputSlots::MouseWheelY || slot == InputSlots::MouseWheelUp
    || slot == InputSlots::MouseWheelDown) {
    out.push_back(&InputSlots::MouseWheelY);
  } else if (slot == InputSlots::AnyKey) {
    if (event_slot != nullptr) {
      out.push_back(event_slot);
    }
  } else {
    out.push_back(&slot);
  }
}

auto IsClaimed(const InputSlot& slot, const InputSlot* event_slot,
  const InputMappingContext::ClaimedSources& claimed) -> bool
{
  if (claimed.empty()) {
    return false;
  }
  InputMappingContext::ClaimedSources sources;
  AppendSources(slot, event_slot, sources);
  return std::ranges::any_of(
    sources, [&claimed](const InputSlot* source) -> bool {
      return std::ranges::any_of(claimed,
        [source](const InputSlot* c) -> bool { return *c == *source; });
    });
}

} // namespace

InputMappingContext::InputMappingContext(std::string name)
  : name_(std::move(name))
{
}

void InputMappingContext::AddMapping(
  std::shared_ptr<InputActionMapping> mapping)
{
  mappings_.emplace_back(std::move(mapping));
}

void InputMappingContext::HandleInput(
  const InputSlot& slot, const InputEvent& event) const
{
  for (const auto& mapping : mappings_) {
    if (Reads(mapping->GetSlot(), slot, event)) {
      mapping->HandleInput(event);
    }
  }
}

auto InputMappingContext::Update(
  const oxygen::time::CanonicalDuration delta_time, ClaimedSources& claimed,
  const InputSlot* event_slot) const -> bool
{
  bool consumed = false;
  for (const auto& mapping : mappings_) {
    if (IsClaimed(mapping->GetSlot(), event_slot, claimed)) {
      DLOG_F(2, "input claimed; skipping action {}",
        mapping->GetAction()->GetName());
      if (mapping->IsActive()) {
        mapping->CancelInput();
      } else {
        mapping->AbortStaged();
      }
      continue;
    }
    if (mapping->Update(delta_time)) {
      consumed = true;
      AppendSources(mapping->GetSlot(), event_slot, claimed);
    }
  }
  return consumed;
}

auto InputMappingContext::Update(
  const oxygen::time::CanonicalDuration delta_time) const -> bool
{
  ClaimedSources claimed;
  return Update(delta_time, claimed);
}

void InputMappingContext::DeactivateAll() const
{
  for (const auto& mapping : mappings_) {
    mapping->Deactivate();
  }
}
