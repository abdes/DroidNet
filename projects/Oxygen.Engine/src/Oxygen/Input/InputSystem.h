//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Core/EngineModule.h>
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
  [[nodiscard]] OXGN_NPUT_API auto GetActionByName(std::string_view name) const
    -> std::shared_ptr<input::Action>;

  OXGN_NPUT_API void AddMappingContext(
    const std::shared_ptr<input::InputMappingContext>& context,
    int32_t priority);
  OXGN_NPUT_API void RemoveMappingContext(
    const std::shared_ptr<input::InputMappingContext>& context);
  OXGN_NPUT_API void ClearAllMappingContexts();
  [[nodiscard]] OXGN_NPUT_API auto GetMappingContextByName(
    std::string_view name) const -> std::shared_ptr<input::InputMappingContext>;
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
