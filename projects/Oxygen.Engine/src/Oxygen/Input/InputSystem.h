//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Input/InputSnapshot.h>
#include <Oxygen/Input/api_export.h>
#include <Oxygen/OxCo/BroadcastChannel.h>
#include <Oxygen/Platform/InputEvent.h>

namespace oxygen {
class IAsyncEngine;
}

namespace oxygen::input {
class Action;
class InputMappingContext;
} // namespace oxygen::input

namespace oxygen::engine {

class InputSystem : public EngineModule, public Composition, public Named {
  OXYGEN_TYPED(InputSystem)
public:
  struct InputMappingContextEntry {
    int32_t priority;
    bool is_active { false };
    std::shared_ptr<input::InputMappingContext> mapping_context;
  };

  OXGN_NPUT_API explicit InputSystem(
    co::ReaderContext<platform::InputEvent> input_reader);
  OXGN_NPUT_API ~InputSystem() override = default;

  OXYGEN_MAKE_NON_COPYABLE(InputSystem)
  OXYGEN_MAKE_NON_MOVABLE(InputSystem)

  // Metadata
  OXGN_NPUT_NDAPI auto GetName() const noexcept -> std::string_view override;

  [[nodiscard]] auto GetPriority() const noexcept -> ModulePriority override
  {
    return ModulePriority { kModulePriorityHighest };
  }
  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> ModulePhaseMask override
  {
    return MakeModuleMask<core::PhaseId::kInput, core::PhaseId::kSnapshot,
      core::PhaseId::kFrameStart, core::PhaseId::kFrameEnd>();
  }

  [[nodiscard]] auto IsCritical() const noexcept -> bool override
  {
    return true;
  }

  void SetName(std::string_view name) noexcept override;

  OXGN_NPUT_NDAPI auto OnAttached(observer_ptr<IAsyncEngine> engine) noexcept
    -> bool override;
  OXGN_NPUT_API auto RegisterConsoleBindings(
    observer_ptr<console::Console> console) noexcept -> void override;
  OXGN_NPUT_API auto ApplyConsoleCVars(
    observer_ptr<const console::Console> console) noexcept -> void override;

  //! Records the actions and contexts present and which contexts are active.
  OXGN_NPUT_NDAPI auto OnSessionBegin(
    observer_ptr<IAsyncEngine> engine) noexcept -> bool override;
  //! Removes every action and context added during the session, restores the
  //! recorded activation of the rest, and drops pending deactivations and
  //! latched fixed-step edges. A context meant to outlive sessions is added
  //! before the session begins.
  OXGN_NPUT_API auto OnSessionEnd(observer_ptr<IAsyncEngine> engine) noexcept
    -> void override;

  // Frame phase handlers
  OXGN_NPUT_API auto OnFrameStart(observer_ptr<FrameContext> context)
    -> void override;
  OXGN_NPUT_API auto OnInput(observer_ptr<FrameContext> context)
    -> co::Co<> override;
  OXGN_NPUT_API auto OnSnapshot(observer_ptr<FrameContext> context)
    -> void override;
  OXGN_NPUT_API auto OnFrameEnd(observer_ptr<FrameContext> context)
    -> void override;

  OXGN_NPUT_API void AddAction(const std::shared_ptr<input::Action>& action);
  OXGN_NPUT_API void RemoveAction(const std::shared_ptr<input::Action>& action);
  OXGN_NPUT_API void ClearAllActions();
  //! First action with this name. Names are labels: actions hydrated from
  //! different assets may share one; find those with GetActionByKey().
  [[nodiscard]] OXGN_NPUT_API auto GetActionByName(std::string_view name) const
    -> std::shared_ptr<input::Action>;
  //! The action hydrated from the asset with this key, if added.
  [[nodiscard]] OXGN_NPUT_API auto GetActionByKey(
    const data::AssetKey& key) const -> std::shared_ptr<input::Action>;

  OXGN_NPUT_API void AddMappingContext(
    const std::shared_ptr<input::InputMappingContext>& context,
    int32_t priority);
  OXGN_NPUT_API void RemoveMappingContext(
    const std::shared_ptr<input::InputMappingContext>& context);
  OXGN_NPUT_API void ClearAllMappingContexts();
  //! First context with this name; see GetActionByName().
  [[nodiscard]] OXGN_NPUT_API auto GetMappingContextByName(
    std::string_view name) const -> std::shared_ptr<input::InputMappingContext>;
  //! The context hydrated from the asset with this key, if added.
  [[nodiscard]] OXGN_NPUT_API auto GetMappingContextByKey(
    const data::AssetKey& key) const
    -> std::shared_ptr<input::InputMappingContext>;
  //! Actions and mapping contexts added, active or not.
  [[nodiscard]] auto GetActionCount() const noexcept -> std::size_t
  {
    return actions_.size();
  }
  [[nodiscard]] auto GetMappingContextCount() const noexcept -> std::size_t
  {
    return mapping_contexts_.size();
  }

  OXGN_NPUT_API void ActivateMappingContext(
    const std::shared_ptr<input::InputMappingContext>& context);
  OXGN_NPUT_API void DeactivateMappingContext(
    const std::shared_ptr<input::InputMappingContext>& context);

  // Accessor used by the engine coordinator after kInput to publish the
  // snapshot into FrameContext. Returns nullptr if no snapshot is available.
  [[nodiscard]] auto GetCurrentSnapshot() const
    -> std::shared_ptr<const input::InputSnapshot>
  {
    return current_snapshot_;
  }

private:
  auto DrainPendingInputEvents() -> void;
  //! Route one event to the active contexts; returns the slot it was routed
  //! on, or nothing if no slot reads it.
  auto RouteInputEvent(const platform::InputEvent& event)
    -> std::optional<platform::InputSlot>;
  //! Evaluate every active context once, highest priority first, carrying
  //! the physical sources claimed by consuming actions.
  void EvaluateContexts(
    time::CanonicalDuration delta_time, const platform::InputSlot* event_slot);
  //! End the activations of contexts deactivated since the last frame.
  void ApplyPendingDeactivations();

  std::vector<std::shared_ptr<input::Action>> actions_;
  std::list<InputMappingContextEntry> mapping_contexts_;
  // Deactivated contexts whose activations end at the next kInput, inside
  // that frame's edge window so the Canceled edges are visible.
  std::vector<std::shared_ptr<input::InputMappingContext>>
    pending_deactivations_;

  // What existed when the play session began; empty without a session.
  struct SessionBaseline {
    std::vector<std::shared_ptr<input::Action>> actions;
    std::vector<std::pair<std::shared_ptr<input::InputMappingContext>, bool>>
      contexts;
  };
  std::optional<SessionBaseline> session_baseline_;

  // Frame processing
  co::ReaderContext<platform::InputEvent> input_reader_;
  bool input_enabled_ { true };
  bool was_enabled_ { true };
  bool log_events_ { false };
  // Frozen per-frame snapshot at end of kInput; shared so the engine can
  // publish it into FrameContext for early access and later freezing.
  std::shared_ptr<const input::InputSnapshot> current_snapshot_;
};

} // namespace oxygen::engine
