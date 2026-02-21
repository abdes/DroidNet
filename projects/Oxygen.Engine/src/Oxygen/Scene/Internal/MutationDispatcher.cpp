//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <type_traits>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/Internal/IMutationCollector.h>
#include <Oxygen/Scene/Internal/MutationDispatcher.h>

namespace oxygen::scene::internal {

namespace {
  template <typename> inline constexpr bool kAlwaysFalse = false;

  static_assert(std::variant_size_v<MutationPayload> == 5,
    "MutationPayload changed: update MutationDispatcher visitor and tests.");

  template <SceneMutation MutationT> struct CoalescedState final {
    using KeyType = typename MutationTraits<MutationT>::KeyType;

    std::unordered_map<KeyType, MutationT> by_key {};
    std::vector<KeyType> first_seen_order {};
  };

  class MutationDispatcher final : public IMutationDispatcher {
  public:
    explicit MutationDispatcher(
      observer_ptr<IScriptSlotMutationProcessor> script_slot_processor)
      : script_slot_processor_(script_slot_processor)
    {
    }

    auto Dispatch(IMutationCollector& mutation_collector,
      const DispatchContext& context) -> void override
    {
      ++counters_.sync_calls;

      auto mutations = mutation_collector.DrainMutations();
      if (mutations.empty()) {
        return;
      }

      ++counters_.frames_with_mutations;
      counters_.drained_records += mutations.size();

      for (const auto& record : mutations) {
        std::visit(
          [this, &context](const auto& mutation) {
            using MutationT = std::decay_t<decltype(mutation)>;
            if constexpr (std::same_as<MutationT, ScriptSlotMutation>) {
              HandleScriptMutation(mutation, context);
            } else if constexpr (std::same_as<MutationT, LightMutation>) {
              HandleLightMutation(mutation);
            } else if constexpr (std::same_as<MutationT, CameraMutation>) {
              HandleCameraMutation(mutation);
            } else if constexpr (std::same_as<MutationT, TransformMutation>) {
              HandleTransformMutation(mutation);
            } else if constexpr (std::same_as<MutationT,
                                   NodeDestroyedMutation>) {
              HandleNodeDestroyedMutation(mutation, context);
            } else {
              static_assert(kAlwaysFalse<MutationT>,
                "Unhandled MutationPayload alternative in dispatcher.");
            }
          },
          record.payload);
      }

      FlushCoalesced(context);
    }

    [[nodiscard]] auto GetCounters() const noexcept -> Counters override
    {
      return counters_;
    }

  private:
    auto HandleScriptMutation(const ScriptSlotMutation& mutation,
      const DispatchContext& context) -> void
    {
      DCHECK_NOTNULL_F(script_slot_processor_.get());
      ++counters_.script_records_dispatched;
      script_slot_processor_->Process(
        mutation, context.resolve_script_slot, context.notify_script_observers);
    }

    auto HandleLightMutation(const LightMutation& mutation) -> void
    {
      ++counters_.light_records_coalesced_in;
      CoalesceMutation(mutation);
    }

    auto HandleCameraMutation(const CameraMutation& mutation) -> void
    {
      ++counters_.camera_records_coalesced_in;
      CoalesceMutation(mutation);
    }

    auto HandleTransformMutation(const TransformMutation& mutation) -> void
    {
      ++counters_.transform_records_coalesced_in;
      CoalesceMutation(mutation);
    }

    auto HandleNodeDestroyedMutation(const NodeDestroyedMutation& mutation,
      const DispatchContext& context) -> void
    {
      ++counters_.node_destroyed_records_dispatched;
      if (context.notify_node_destroyed_mutation) {
        context.notify_node_destroyed_mutation(mutation);
      }
    }

    template <SceneMutation MutationT>
      requires MutationTraits<MutationT>::kCoalescible
    auto CoalesceMutation(const MutationT& mutation) -> void
    {
      auto& state = Coalesced<MutationT>();
      const auto key = MutationTraits<MutationT>::Key(mutation);
      const auto [it, inserted] = state.by_key.try_emplace(key, mutation);
      if (!inserted) {
        MutationTraits<MutationT>::Merge(it->second, mutation);
        return;
      }
      state.first_seen_order.push_back(key);
    }

    template <SceneMutation MutationT>
      requires MutationTraits<MutationT>::kCoalescible
    [[nodiscard]] auto Coalesced() -> CoalescedState<MutationT>&
    {
      if constexpr (std::same_as<MutationT, LightMutation>) {
        return coalesced_light_;
      } else if constexpr (std::same_as<MutationT, CameraMutation>) {
        return coalesced_camera_;
      } else {
        static_assert(std::same_as<MutationT, TransformMutation>);
        return coalesced_transform_;
      }
    }

    auto FlushTransformMutations(const DispatchContext& context) -> void
    {
      for (const auto& key : coalesced_transform_.first_seen_order) {
        const auto it = coalesced_transform_.by_key.find(key);
        DCHECK_F(it != coalesced_transform_.by_key.end());
        ++counters_.transform_records_dispatched;
        if (context.notify_transform_mutation) {
          context.notify_transform_mutation(it->second);
        }
      }
      coalesced_transform_.by_key.clear();
      coalesced_transform_.first_seen_order.clear();
    }

    auto FlushLightMutations(const DispatchContext& context) -> void
    {
      for (const auto& key : coalesced_light_.first_seen_order) {
        const auto it = coalesced_light_.by_key.find(key);
        DCHECK_F(it != coalesced_light_.by_key.end());
        ++counters_.light_records_dispatched;
        if (context.notify_light_mutation) {
          context.notify_light_mutation(it->second);
        }
      }
      coalesced_light_.by_key.clear();
      coalesced_light_.first_seen_order.clear();
    }

    auto FlushCameraMutations(const DispatchContext& context) -> void
    {
      for (const auto& key : coalesced_camera_.first_seen_order) {
        const auto it = coalesced_camera_.by_key.find(key);
        DCHECK_F(it != coalesced_camera_.by_key.end());
        ++counters_.camera_records_dispatched;
        if (context.notify_camera_mutation) {
          context.notify_camera_mutation(it->second);
        }
      }
      coalesced_camera_.by_key.clear();
      coalesced_camera_.first_seen_order.clear();
    }

    auto FlushCoalesced(const DispatchContext& context) -> void
    {
      FlushTransformMutations(context);
      FlushLightMutations(context);
      FlushCameraMutations(context);
    }

    observer_ptr<IScriptSlotMutationProcessor> script_slot_processor_;
    CoalescedState<LightMutation> coalesced_light_ {};
    CoalescedState<CameraMutation> coalesced_camera_ {};
    CoalescedState<TransformMutation> coalesced_transform_ {};
    Counters counters_ {};
  };

} // namespace

auto CreateMutationDispatcher(
  observer_ptr<IScriptSlotMutationProcessor> script_slot_processor)
  -> std::unique_ptr<IMutationDispatcher>
{
  DCHECK_NOTNULL_F(script_slot_processor.get());
  return std::make_unique<MutationDispatcher>(script_slot_processor);
}

} // namespace oxygen::scene::internal
