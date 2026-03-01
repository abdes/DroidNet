//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/InputContextHydration.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/InputActionAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
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

  [[nodiscard]] auto CanonicalizeInputSlotName(
    const std::string_view authored_name) -> std::string_view
  {
    static constexpr std::array<std::pair<std::string_view, std::string_view>,
      17>
      kAliasTable {
        std::pair { "UpArrow", "Up" },
        std::pair { "DownArrow", "Down" },
        std::pair { "RightArrow", "Right" },
        std::pair { "LeftArrow", "Left" },
        std::pair { "Alpha0", "0" },
        std::pair { "Alpha1", "1" },
        std::pair { "Alpha2", "2" },
        std::pair { "Alpha3", "3" },
        std::pair { "Alpha4", "4" },
        std::pair { "Alpha5", "5" },
        std::pair { "Alpha6", "6" },
        std::pair { "Alpha7", "7" },
        std::pair { "Alpha8", "8" },
        std::pair { "Alpha9", "9" },
        std::pair { "Exclaim", "!" },
        std::pair { "RightControl", "RightCtrl" },
        std::pair { "LeftControl", "LeftCtrl" },
      };
    for (const auto& [alias_name, canonical_name] : kAliasTable) {
      if (authored_name == alias_name) {
        return canonical_name;
      }
    }
    if (authored_name == "Print") {
      return "PrintScreen";
    }
    return authored_name;
  }

  auto ToActionValueType(const uint8_t value_type_id)
    -> std::optional<input::ActionValueType>
  {
    switch (value_type_id) {
    case static_cast<uint8_t>(input::ActionValueType::kBool):
      return input::ActionValueType::kBool;
    case static_cast<uint8_t>(input::ActionValueType::kAxis1D):
      return input::ActionValueType::kAxis1D;
    case static_cast<uint8_t>(input::ActionValueType::kAxis2D):
      return input::ActionValueType::kAxis2D;
    default:
      return std::nullopt;
    }
  }

  auto ResolveInputSlot(const std::string_view slot_name)
    -> const platform::InputSlot*
  {
    if (slot_name.empty()) {
      return nullptr;
    }

    platform::InputSlots::Initialize();
    static const std::vector<platform::InputSlot> all_slots = [] {
      auto slots = std::vector<platform::InputSlot> {};
      platform::InputSlots::GetAllInputSlots(slots);
      return slots;
    }();

    const auto canonical_name = CanonicalizeInputSlotName(slot_name);
    const auto it
      = std::ranges::find_if(all_slots, [canonical_name](const auto& slot) {
          return slot.GetName() == canonical_name;
        });
    return it != all_slots.end() ? &(*it) : nullptr;
  }

  auto NanosecondsToSecondsClamped(const uint64_t nanoseconds) -> float
  {
    constexpr auto kNanosPerSecond = 1000000000.0L;
    const auto seconds
      = static_cast<long double>(nanoseconds) / kNanosPerSecond;
    if (seconds
      > static_cast<long double>((std::numeric_limits<float>::max)())) {
      return (std::numeric_limits<float>::max)();
    }
    return static_cast<float>(seconds);
  }

} // namespace

auto HydrateInputContext(const data::InputMappingContextAsset& asset,
  IAssetLoader& loader, engine::InputSystem& input_system)
  -> std::shared_ptr<input::InputMappingContext>
{
  try {
    const auto context_name = [&asset] {
      const auto name = asset.GetAssetName();
      if (!name.empty()) {
        return std::string(name);
      }
      return data::to_string(asset.GetAssetKey());
    }();

    auto context = input_system.GetMappingContextByName(context_name);
    if (context) {
      return context;
    }
    context = std::make_shared<input::InputMappingContext>(context_name);

    const auto mappings = asset.GetMappings();
    const auto triggers = asset.GetTriggers();
    const auto trigger_aux = asset.GetTriggerAuxRecords();

    auto action_cache
      = std::unordered_map<data::AssetKey, std::shared_ptr<input::Action>> {};
    action_cache.reserve(mappings.size());

    const auto resolve_action
      = [&](const data::AssetKey& action_asset_key,
          const std::string_view path) -> std::shared_ptr<input::Action> {
      if (action_asset_key.IsNil()) {
        LOG_F(WARNING,
          "WARNING [input.context.action_unresolved]: missing action key at {} "
          "while hydrating context '{}'",
          path, context_name);
        return nullptr;
      }
      if (const auto it = action_cache.find(action_asset_key);
        it != action_cache.end()) {
        return it->second;
      }

      auto action_asset = loader.GetInputActionAsset(action_asset_key);
      if (!action_asset) {
        LOG_F(WARNING,
          "WARNING [input.context.action_unresolved]: action asset {} missing "
          "at {} while hydrating context '{}'",
          data::to_string(action_asset_key), path, context_name);
        return nullptr;
      }

      const auto action_name = std::string(action_asset->GetAssetName());
      if (action_name.empty()) {
        LOG_F(ERROR,
          "ERROR [input.context.hydration_failed]: empty action name for "
          "action asset {} at {} while hydrating context '{}'",
          data::to_string(action_asset_key), path, context_name);
        return nullptr;
      }

      const auto value_type_opt
        = ToActionValueType(action_asset->GetValueTypeId());
      if (!value_type_opt.has_value()) {
        LOG_F(ERROR,
          "ERROR [input.context.hydration_failed]: invalid value_type {} for "
          "action '{}' ({})",
          action_asset->GetValueTypeId(), action_name,
          data::to_string(action_asset_key));
        return nullptr;
      }

      auto existing = input_system.GetActionByName(action_name);
      if (existing) {
        if (existing->GetValueType() != *value_type_opt) {
          LOG_F(ERROR,
            "ERROR [input.context.hydration_failed]: action '{}' value type "
            "mismatch (existing={} incoming={})",
            action_name, static_cast<uint8_t>(existing->GetValueType()),
            static_cast<uint8_t>(*value_type_opt));
          return nullptr;
        }
        if (action_asset->ConsumesInput()) {
          existing->SetConsumesInput(true);
        }
        action_cache.emplace(action_asset_key, existing);
        return existing;
      }

      auto action
        = std::make_shared<input::Action>(action_name, *value_type_opt);
      action->SetConsumesInput(action_asset->ConsumesInput());
      input_system.AddAction(action);
      action_cache.emplace(action_asset_key, action);
      return action;
    };

    for (size_t mapping_index = 0; mapping_index < mappings.size();
      ++mapping_index) {
      const auto& mapping = mappings[mapping_index];

      const auto trigger_start
        = static_cast<size_t>(mapping.trigger_start_index);
      const auto trigger_count = static_cast<size_t>(mapping.trigger_count);
      if (trigger_start > triggers.size()
        || trigger_count > (triggers.size() - trigger_start)) {
        LOG_F(ERROR,
          "ERROR [input.context.hydration_failed]: trigger range out of "
          "bounds for mapping index {} in context '{}'",
          mapping_index, context_name);
        continue;
      }

      const auto slot_name_opt = asset.TryGetString(mapping.slot_name_offset);
      if (!slot_name_opt.has_value()) {
        LOG_F(ERROR,
          "ERROR [input.context.hydration_failed]: invalid slot string offset "
          "{} for mapping index {} in context '{}'",
          mapping.slot_name_offset, mapping_index, context_name);
        continue;
      }

      const auto* slot = ResolveInputSlot(*slot_name_opt);
      if (!slot) {
        LOG_F(ERROR,
          "ERROR [input.context.hydration_failed]: unknown slot '{}' for "
          "mapping index {} in context '{}'",
          *slot_name_opt, mapping_index, context_name);
        continue;
      }

      auto action = resolve_action(mapping.action_asset_key,
        "mappings[" + std::to_string(mapping_index) + "].action");
      if (!action) {
        continue;
      }

      auto hydrated_mapping
        = std::make_shared<input::InputActionMapping>(action, *slot);

      const auto trigger_end = trigger_start + trigger_count;
      for (size_t trigger_index = trigger_start; trigger_index < trigger_end;
        ++trigger_index) {
        const auto& trigger_record = triggers[trigger_index];
        std::shared_ptr<input::ActionTrigger> trigger;

        using data::pak::input::InputTriggerType;
        switch (trigger_record.type) {
        case InputTriggerType::kPressed:
          trigger = std::make_shared<input::ActionTriggerPressed>();
          break;
        case InputTriggerType::kReleased:
          trigger = std::make_shared<input::ActionTriggerReleased>();
          break;
        case InputTriggerType::kDown:
          trigger = std::make_shared<input::ActionTriggerDown>();
          break;
        case InputTriggerType::kHold: {
          auto typed = std::make_shared<input::ActionTriggerHold>();
          if (trigger_record.fparams[0] > 0.0F) {
            typed->SetHoldDurationThreshold(trigger_record.fparams[0]);
          }
          trigger = std::move(typed);
          break;
        }
        case InputTriggerType::kHoldAndRelease: {
          auto typed = std::make_shared<input::ActionTriggerHoldAndRelease>();
          if (trigger_record.fparams[0] > 0.0F) {
            typed->SetHoldDurationThreshold(trigger_record.fparams[0]);
          }
          trigger = std::move(typed);
          break;
        }
        case InputTriggerType::kPulse: {
          auto typed = std::make_shared<input::ActionTriggerPulse>();
          if (trigger_record.fparams[1] > 0.0F) {
            typed->SetInterval(trigger_record.fparams[1]);
          }
          trigger = std::move(typed);
          break;
        }
        case InputTriggerType::kTap: {
          auto typed = std::make_shared<input::ActionTriggerTap>();
          if (trigger_record.fparams[0] > 0.0F) {
            typed->SetTapTimeThreshold(trigger_record.fparams[0]);
          }
          trigger = std::move(typed);
          break;
        }
        case InputTriggerType::kChord:
        case InputTriggerType::kActionChain: {
          auto typed = std::make_shared<input::ActionTriggerChain>();
          if (trigger_record.linked_action_asset_key.IsNil()) {
            LOG_F(ERROR,
              "ERROR [input.context.hydration_failed]: trigger type {} "
              "requires linked action at mapping index {} trigger index {} in "
              "context '{}'",
              trigger_record.type == InputTriggerType::kChord ? "Chord"
                                                              : "ActionChain",
              mapping_index, trigger_index, context_name);
            continue;
          }
          {
            auto linked = resolve_action(trigger_record.linked_action_asset_key,
              "mappings[" + std::to_string(mapping_index) + "].triggers["
                + std::to_string(trigger_index - trigger_start)
                + "].linked_action");
            if (!linked) {
              continue;
            }
            typed->SetLinkedAction(linked);
          }
          trigger = std::move(typed);
          break;
        }
        case InputTriggerType::kCombo: {
          auto typed = std::make_shared<input::ActionTriggerCombo>();
          const auto aux_start
            = static_cast<size_t>(trigger_record.aux_start_index);
          const auto aux_count = static_cast<size_t>(trigger_record.aux_count);
          if (aux_start > trigger_aux.size()
            || aux_count > (trigger_aux.size() - aux_start)) {
            LOG_F(ERROR,
              "ERROR [input.context.hydration_failed]: combo aux range out of "
              "bounds for mapping index {} trigger index {} in context '{}'",
              mapping_index, trigger_index, context_name);
            continue;
          }
          const auto aux_end = aux_start + aux_count;
          for (size_t aux_index = aux_start; aux_index < aux_end; ++aux_index) {
            const auto& aux_record = trigger_aux[aux_index];
            auto aux_action = resolve_action(aux_record.action_asset_key,
              "mappings[" + std::to_string(mapping_index) + "].triggers["
                + std::to_string(trigger_index - trigger_start) + "].aux["
                + std::to_string(aux_index - aux_start) + "].action");
            if (!aux_action) {
              continue;
            }
            typed->AddComboStep(aux_action,
              static_cast<input::ActionState>(
                static_cast<uint8_t>(aux_record.completion_states & 0xFFU)),
              NanosecondsToSecondsClamped(aux_record.time_to_complete_ns));
          }
          trigger = std::move(typed);
          break;
        }
        default:
          LOG_F(ERROR,
            "ERROR [input.context.hydration_failed]: unknown trigger type {} "
            "at "
            "mapping index {} trigger index {} in context '{}'",
            static_cast<uint8_t>(trigger_record.type), mapping_index,
            trigger_index, context_name);
          continue;
        }

        trigger->SetActuationThreshold(trigger_record.actuation_threshold);
        switch (trigger_record.behavior) {
        case data::pak::input::InputTriggerBehavior::kExplicit:
          trigger->MakeExplicit();
          break;
        case data::pak::input::InputTriggerBehavior::kBlocker:
          trigger->MakeBlocker();
          break;
        case data::pak::input::InputTriggerBehavior::kImplicit:
        default:
          trigger->MakeImplicit();
          break;
        }

        hydrated_mapping->AddTrigger(std::move(trigger));
      }

      context->AddMapping(std::move(hydrated_mapping));
    }

    return context;
  } catch (const std::exception& ex) {
    LOG_F(ERROR,
      "ERROR [input.context.hydration_failed]: failed to hydrate input context "
      "asset {}: {}",
      data::to_string(asset.GetAssetKey()), ex.what());
  } catch (...) {
    LOG_F(ERROR,
      "ERROR [input.context.hydration_failed]: unknown failure while hydrating "
      "input context asset {}",
      data::to_string(asset.GetAssetKey()));
  }
  return nullptr;
}

} // namespace oxygen::content
