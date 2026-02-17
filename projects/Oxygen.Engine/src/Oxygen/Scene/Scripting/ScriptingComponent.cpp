//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <stdexcept>

#include <Oxygen/Scene/Scripting/ScriptingComponent.h>

namespace oxygen::scene {

auto ScriptingComponent::AddSlot(std::shared_ptr<const data::ScriptAsset> asset)
  -> void
{
  slots_.emplace_back();
  auto& slot = slots_.back();
  slot.asset_ = std::move(asset);
}

auto ScriptingComponent::RemoveSlot(const Slot& slot) -> bool
{
  auto it = std::ranges::find_if(
    slots_, [&slot](const Slot& candidate) { return &candidate == &slot; });
  if (it == slots_.end()) {
    return false;
  }
  slots_.erase(it);
  return true;
}

auto ScriptingComponent::SetParameter(
  const Slot& slot, std::string_view name, data::ScriptParam value) -> bool
{
  auto slot_it = std::ranges::find_if(
    slots_, [&slot](const Slot& candidate) { return &candidate == &slot; });
  if (slot_it == slots_.end()) {
    return false;
  }

  auto& overrides = slot_it->overrides_;
  auto it = std::ranges::find_if(
    overrides, [name](const auto& pair) { return pair.first == name; });

  if (it != overrides.end()) {
    it->second = std::move(value);
  } else {
    overrides.emplace_back(std::string(name), std::move(value));
  }
  return true;
}

auto ScriptingComponent::TryGetParameter(
  const Slot& slot, std::string_view name) const
  -> std::optional<std::reference_wrapper<const data::ScriptParam>>
{
  auto slot_it = std::ranges::find_if(
    slots_, [&slot](const Slot& candidate) { return &candidate == &slot; });
  if (slot_it == slots_.end()) {
    return std::nullopt;
  }

  // 1. Check runtime overrides
  auto it = std::ranges::find_if(slot_it->overrides_,
    [name](const auto& pair) { return pair.first == name; });

  if (it != slot_it->overrides_.end()) {
    return std::cref(it->second);
  }

  // 2. Fallback to asset defaults
  if (slot_it->asset_) {
    if (const auto value = slot_it->asset_->TryGetParameter(name)) {
      return value;
    }
  }

  return std::nullopt;
}

auto ScriptingComponent::GetParameter(
  const Slot& slot, std::string_view name) const -> const data::ScriptParam&
{
  if (const auto value = TryGetParameter(slot, name)) {
    return value->get();
  }
  throw std::out_of_range(
    "ScriptingComponent parameter '" + std::string(name) + "' was not found");
}

auto ScriptingComponent::Parameters(const Slot& slot) const
  -> Slot::EffectiveParametersView
{
  auto slot_it = std::ranges::find_if(
    slots_, [&slot](const Slot& candidate) { return &candidate == &slot; });
  if (slot_it == slots_.end()) {
    return {};
  }
  return slot_it->Parameters();
}

auto ScriptingComponent::Slot::GetOverrideParameter(std::string_view key) const
  -> const data::ScriptParam&
{
  if (const auto value = TryGetOverrideParameter(key)) {
    return value->get();
  }
  throw std::out_of_range(
    "Override parameter '" + std::string(key) + "' was not found");
}

auto ScriptingComponent::Clone() const -> std::unique_ptr<Component>
{
  auto clone = std::make_unique<ScriptingComponent>();
  clone->slots_ = this->slots_;
  return clone;
}

} // namespace oxygen::scene
