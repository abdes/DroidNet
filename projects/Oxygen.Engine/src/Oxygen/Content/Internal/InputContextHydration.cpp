//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/InputContextHydration.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/InputActionAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/PakFormat_input.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionState.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Platform/Input.h>

namespace oxygen::content {

namespace {

  //! The Input module's value type for the cooked one, which the action's
  //! loader checked.
  auto ToActionValueType(const data::pak::input::InputActionValueType value)
    -> input::ActionValueType
  {
    using data::pak::input::InputActionValueType;
    switch (value) {
    case InputActionValueType::kBool:
      return input::ActionValueType::kBool;
    case InputActionValueType::kAxis1D:
      return input::ActionValueType::kAxis1D;
    case InputActionValueType::kAxis2D:
      return input::ActionValueType::kAxis2D;
    }
    ABORT_F("the input action loader refuses value type {}",
      std::to_underlying(value));
  }

  //! ActionState flags occupy the low byte of a combo step's completion
  //! states.
  constexpr uint32_t kCompletionStatesMask = 0xFFU;

  //! Even the largest nanosecond count (about 1.8e10 s) fits a float.
  auto NanosecondsToSeconds(const uint64_t nanoseconds) -> float
  {
    constexpr auto kNanosPerSecond = 1'000'000'000.0;
    return static_cast<float>(
      static_cast<double>(nanoseconds) / kNanosPerSecond);
  }

  //! Builds one input context from its loaded asset, all or nothing: the
  //! actions it creates join the input system only once every mapping built.
  /*!
   The asset's loader refused unknown slots, trigger types and behaviors,
   out-of-range trigger and step ranges and missing action keys, and its load
   bound every action it names. What remains to fail is an action this loader
   did not bind, as for an asset another loader published.
  */
  class ContextBuilder final {
  public:
    ContextBuilder(const data::InputMappingContextAsset& asset,
      IAssetLoader& loader, const engine::InputSystem& input_system)
      : asset_(&asset)
      , loader_(&loader)
      , input_system_(&input_system)
    {
    }

    //! The context, or null when one of its actions does not resolve.
    auto Build() -> std::shared_ptr<input::InputMappingContext>
    {
      auto context = std::make_shared<input::InputMappingContext>(
        ContextName(), asset_->GetAssetKey());
      for (const auto& record : asset_->GetMappings()) {
        auto mapping = Mapping(record);
        if (!mapping) {
          return nullptr;
        }
        context->AddMapping(std::move(mapping));
      }
      return context;
    }

    //! The actions the context created, which the input system does not hold
    //! yet.
    [[nodiscard]] auto CreatedActions() const -> auto
    {
      return created_ | std::views::values;
    }

  private:
    [[nodiscard]] auto ContextName() const -> std::string
    {
      const auto name = asset_->GetAssetName();
      return name.empty() ? data::to_string(asset_->GetAssetKey())
                          : std::string(name);
    }

    //! The runtime action of the action asset `key`: the input system's when
    //! another context created it, else one created for this context.
    auto Action(const data::AssetKey& key) -> std::shared_ptr<input::Action>
    {
      // An action asset hydrates once; every context that maps it shares the
      // runtime action. Actions of different assets stay distinct even when
      // they share a name.
      if (auto existing = input_system_->GetActionByKey(key)) {
        return existing;
      }
      if (const auto created = created_.find(key); created != created_.end()) {
        return created->second;
      }
      const auto action_asset = loader_->GetInputActionAsset(key, *asset_);
      if (!action_asset) {
        LOG_F(ERROR,
          "ERROR [input.context.hydration_failed]: context '{}' maps action "
          "{}, which its load did not bind",
          ContextName(), data::to_string(key));
        return nullptr;
      }
      auto action = std::make_shared<input::Action>(
        std::string(action_asset->GetAssetName()),
        ToActionValueType(action_asset->GetValueType()), key);
      action->SetConsumesInput(action_asset->ConsumesInput());
      action->SetAccumulation(action_asset->AccumulatesCumulatively()
          ? input::ActionAccumulation::kCumulative
          : input::ActionAccumulation::kHighestAbsolute);
      created_.emplace(key, action);
      return action;
    }

    auto Mapping(const data::pak::input::InputActionMappingRecord& record)
      -> std::shared_ptr<input::InputActionMapping>
    {
      const auto slot_name = asset_->TryGetString(record.slot_name_offset);
      const auto slot = slot_name ? platform::InputSlots::FindByName(*slot_name)
                                  : std::nullopt;
      DCHECK_F(
        slot.has_value(), "the input context loader refuses unknown slots");
      auto action = Action(record.action_asset_key);
      if (!action) {
        return nullptr;
      }
      auto mapping = std::make_shared<input::InputActionMapping>(action, *slot);
      mapping->SetScale({ .x = record.scale[0], .y = record.scale[1] });
      mapping->SetBias({ .x = record.bias[0], .y = record.bias[1] });
      const auto triggers = asset_->GetTriggers().subspan(
        record.trigger_start_index, record.trigger_count);
      for (const auto& trigger_record : triggers) {
        auto trigger = Trigger(trigger_record);
        if (!trigger) {
          return nullptr;
        }
        trigger->SetActuationThreshold(trigger_record.actuation_threshold);
        ApplyBehavior(*trigger, trigger_record.behavior);
        mapping->AddTrigger(std::move(trigger));
      }
      return mapping;
    }

    auto Trigger(const data::pak::input::InputTriggerRecord& record)
      -> std::shared_ptr<input::ActionTrigger>
    {
      using data::pak::input::InputTriggerType;
      switch (record.type) {
      case InputTriggerType::kPressed:
        return std::make_shared<input::ActionTriggerPressed>();
      case InputTriggerType::kReleased:
        return std::make_shared<input::ActionTriggerReleased>();
      case InputTriggerType::kDown:
        return std::make_shared<input::ActionTriggerDown>();
      case InputTriggerType::kHold: {
        auto hold = std::make_shared<input::ActionTriggerHold>();
        if (record.fparams[0] > 0.0F) {
          hold->SetHoldDurationThreshold(record.fparams[0]);
        }
        return hold;
      }
      case InputTriggerType::kHoldAndRelease: {
        auto hold = std::make_shared<input::ActionTriggerHoldAndRelease>();
        if (record.fparams[0] > 0.0F) {
          hold->SetHoldDurationThreshold(record.fparams[0]);
        }
        return hold;
      }
      case InputTriggerType::kPulse: {
        auto pulse = std::make_shared<input::ActionTriggerPulse>();
        if (record.fparams[1] > 0.0F) {
          pulse->SetInterval(record.fparams[1]);
        }
        return pulse;
      }
      case InputTriggerType::kTap: {
        auto tap = std::make_shared<input::ActionTriggerTap>();
        if (record.fparams[0] > 0.0F) {
          tap->SetTapTimeThreshold(record.fparams[0]);
        }
        return tap;
      }
      case InputTriggerType::kChord: {
        auto linked = Action(record.linked_action_asset_key);
        if (!linked) {
          return nullptr;
        }
        auto chord = std::make_shared<input::ActionTriggerChord>();
        chord->SetChordAction(std::move(linked));
        return chord;
      }
      case InputTriggerType::kActionChain: {
        auto linked = Action(record.linked_action_asset_key);
        if (!linked) {
          return nullptr;
        }
        auto chain = std::make_shared<input::ActionTriggerChain>();
        chain->SetLinkedAction(std::move(linked));
        return chain;
      }
      case InputTriggerType::kCombo:
        return Combo(record);
      }
      ABORT_F("the input context loader refuses trigger type {}",
        std::to_underlying(record.type));
    }

    auto Combo(const data::pak::input::InputTriggerRecord& record)
      -> std::shared_ptr<input::ActionTrigger>
    {
      auto combo = std::make_shared<input::ActionTriggerCombo>();
      const auto steps = asset_->GetTriggerAuxRecords().subspan(
        record.aux_start_index, record.aux_count);
      for (const auto& step : steps) {
        auto action = Action(step.action_asset_key);
        if (!action) {
          return nullptr;
        }
        combo->AddComboStep(std::move(action),
          static_cast<input::ActionState>(static_cast<uint8_t>(
            step.completion_states & kCompletionStatesMask)),
          NanosecondsToSeconds(step.time_to_complete_ns));
      }
      return combo;
    }

    static auto ApplyBehavior(input::ActionTrigger& trigger,
      const data::pak::input::InputTriggerBehavior behavior) -> void
    {
      using data::pak::input::InputTriggerBehavior;
      switch (behavior) {
      case InputTriggerBehavior::kExplicit:
        trigger.MakeExplicit();
        return;
      case InputTriggerBehavior::kImplicit:
        trigger.MakeImplicit();
        return;
      case InputTriggerBehavior::kBlocker:
        trigger.MakeBlocker();
        return;
      }
      ABORT_F("the input context loader refuses trigger behavior {}",
        std::to_underlying(behavior));
    }

    observer_ptr<const data::InputMappingContextAsset> asset_;
    observer_ptr<IAssetLoader> loader_;
    observer_ptr<const engine::InputSystem> input_system_;
    std::unordered_map<data::AssetKey, std::shared_ptr<input::Action>> created_;
  };

} // namespace

auto HydrateInputContext(const data::InputMappingContextAsset& asset,
  IAssetLoader& loader, engine::InputSystem& input_system)
  -> std::shared_ptr<input::InputMappingContext>
{
  if (auto existing
    = input_system.GetMappingContextByKey(asset.GetAssetKey())) {
    return existing;
  }
  auto builder = ContextBuilder(asset, loader, input_system);
  auto context = builder.Build();
  if (!context) {
    return nullptr;
  }
  for (const auto& action : builder.CreatedActions()) {
    input_system.AddAction(action);
  }
  return context;
}

} // namespace oxygen::content
