//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <stdexcept>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/Scripting/ScriptingComponent.h>

namespace oxygen::scene {

auto ScriptingComponent::AddSlot(std::shared_ptr<const data::ScriptAsset> asset)
  -> void
{
  slots_.emplace_back();
  auto& slot = slots_.back();
  slot.slot_id_ = next_slot_id_++;
  slot.asset_ = std::move(asset);
  slot.compile_state_ = Slot::CompileState::kPendingCompilation;
  slot.diagnostics_.clear();
  slot.executable_.reset();
}

auto ScriptingComponent::RemoveSlot(const Slot& slot) -> bool
{
  auto it = FindSlotById(slot.slot_id_);
  if (it == slots_.end()) {
    return false;
  }
  slots_.erase(it);
  return true;
}

auto ScriptingComponent::SetParameter(
  const Slot& slot, std::string_view name, data::ScriptParam value) -> bool
{
  auto slot_it = FindSlotById(slot.slot_id_);
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
  auto slot_it = FindSlotById(slot.slot_id_);
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
  auto slot_it = FindSlotById(slot.slot_id_);
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

auto ScriptingComponent::MarkSlotReady(const Slot& slot,
  std::shared_ptr<const scripting::ScriptExecutable> executable) -> bool
{
  if (!executable) {
    return false;
  }

  auto slot_it = FindSlotById(slot.slot_id_);
  if (slot_it == slots_.end()) {
    return false;
  }

  slot_it->compile_state_ = Slot::CompileState::kReady;
  slot_it->executable_ = std::move(executable);
  DLOG_F(2, "slot state set to ready");
  return true;
}

auto ScriptingComponent::MarkSlotCompilationFailed(
  const Slot& slot, std::string diagnostic_message) -> bool
{
  auto slot_it = FindSlotById(slot.slot_id_);
  if (slot_it == slots_.end()) {
    return false;
  }

  if (slot_it->compile_state_ == Slot::CompileState::kCompilationFailed) {
    return true;
  }

  slot_it->compile_state_ = Slot::CompileState::kCompilationFailed;
  slot_it->executable_.reset();
  slot_it->diagnostics_.push_back(
    Slot::Diagnostic { .message = std::move(diagnostic_message) });
  LOG_F(WARNING, "slot state set to compilation failed: {}",
    slot_it->diagnostics_.back().message);
  return true;
}

auto ScriptingComponent::Clone() const -> std::unique_ptr<Component>
{
  auto clone = std::make_unique<ScriptingComponent>();
  clone->slots_ = this->slots_;
  clone->next_slot_id_ = this->next_slot_id_;
  return clone;
}

auto ScriptingComponent::FindSlotById(const uint64_t slot_id) noexcept
  -> std::vector<Slot>::iterator
{
  return std::ranges::find_if(slots_,
    [slot_id](const Slot& candidate) { return candidate.slot_id_ == slot_id; });
}

auto ScriptingComponent::FindSlotById(const uint64_t slot_id) const noexcept
  -> std::vector<Slot>::const_iterator
{
  return std::ranges::find_if(slots_,
    [slot_id](const Slot& candidate) { return candidate.slot_id_ == slot_id; });
}

} // namespace oxygen::scene
