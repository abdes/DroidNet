//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Scene/Internal/IMutationCollector.h>
#include <Oxygen/Scene/Internal/MutationCollector.h>

namespace oxygen::scene::internal {

namespace {

  class MutationCollector final : public IMutationCollector {
  public:
    auto SetEnabled(const bool enabled) -> void override { enabled_ = enabled; }

    [[nodiscard]] auto IsEnabled() const noexcept -> bool override
    {
      return enabled_;
    }

    auto ClearMutations() -> void override { mutations_.clear(); }

    auto CollectScriptSlotActivated(const NodeHandle& node_handle,
      const ScriptSlotIndex slot_index) -> void override
    {
      if (!enabled_) {
        return;
      }
      mutations_.push_back(MutationRecord {
        .sequence = next_sequence_++,
        .payload = ScriptSlotMutation {
          .type = ScriptSlotMutationType::kActivated,
          .node_handle = node_handle,
          .slot_index = slot_index,
        },
      });
    }

    auto CollectScriptSlotChanged(const NodeHandle& node_handle,
      const ScriptSlotIndex slot_index) -> void override
    {
      if (!enabled_) {
        return;
      }
      mutations_.push_back(MutationRecord {
        .sequence = next_sequence_++,
        .payload = ScriptSlotMutation {
          .type = ScriptSlotMutationType::kChanged,
          .node_handle = node_handle,
          .slot_index = slot_index,
        },
      });
    }

    auto CollectScriptSlotDeactivated(const NodeHandle& node_handle,
      const ScriptSlotIndex slot_index) -> void override
    {
      if (!enabled_) {
        return;
      }
      mutations_.push_back(MutationRecord {
        .sequence = next_sequence_++,
        .payload = ScriptSlotMutation {
          .type = ScriptSlotMutationType::kDeactivated,
          .node_handle = node_handle,
          .slot_index = slot_index,
        },
      });
    }

    auto CollectLightChanged(const NodeHandle& node_handle) -> void override
    {
      if (!enabled_) {
        return;
      }
      mutations_.push_back(MutationRecord {
        .sequence = next_sequence_++,
        .payload = LightMutation { .type = LightMutationType::kChanged,
          .node_handle = node_handle },
      });
    }

    auto CollectCameraChanged(const NodeHandle& node_handle) -> void override
    {
      if (!enabled_) {
        return;
      }
      mutations_.push_back(MutationRecord {
        .sequence = next_sequence_++,
        .payload = CameraMutation { .type = CameraMutationType::kChanged,
          .node_handle = node_handle },
      });
    }

    auto CollectTransformChanged(const NodeHandle& node_handle) -> void override
    {
      if (!enabled_) {
        return;
      }
      mutations_.push_back(MutationRecord {
        .sequence = next_sequence_++,
        .payload = TransformMutation { .type = TransformMutationType::kChanged,
          .node_handle = node_handle },
      });
    }

    auto CollectNodeDestroyed(const NodeHandle& node_handle) -> void override
    {
      if (!enabled_) {
        return;
      }
      mutations_.push_back(MutationRecord {
        .sequence = next_sequence_++,
        .payload
        = NodeDestroyedMutation { .type = NodeDestroyedMutationType::kDestroyed,
          .node_handle = node_handle },
      });
    }

    auto DrainMutations() -> std::vector<MutationRecord> override
    {
      auto out = std::move(mutations_);
      mutations_.clear();
      return out;
    }

  private:
    bool enabled_ { true };
    uint64_t next_sequence_ { 0 };
    std::vector<MutationRecord> mutations_;
  };

} // namespace

auto CreateMutationCollector() -> std::unique_ptr<IMutationCollector>
{
  return std::make_unique<MutationCollector>();
}

} // namespace oxygen::scene::internal
