//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Engine/FrameContext.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen {
class AsyncEngine;
} // namespace oxygen

namespace oxygen::engine {

struct FrameSnapshot;

//! Strong type for module execution priority (lower values = higher priority)
using ModulePriority = NamedType<std::uint32_t, struct ModulePriorityTag>;

constexpr ModulePriority kModulePriorityLowest { // Executes last
  (std::numeric_limits<std::uint32_t>::max)()
};

constexpr ModulePriority kModulePriorityHighest { // Executes first
  0U
};

// Alias the canonical PhaseMask from the phase registry for semantic clarity.
using ModulePhaseMask = core::PhaseMask;

// Constexpr helper to produce a ModulePhaseMask from a list of PhaseId
// values. Usage: `MakeModuleMask<PhaseId::kInput, PhaseId::kGameplay>()`.
template <core::PhaseId... Ids>
consteval auto MakeModuleMask() -> ModulePhaseMask
{
  return ((MakePhaseMask(Ids)) | ...);
}

// Module-facing interface. Module authors implement this interface and
// override only the phase handlers they need. Default implementations are
// no-ops. Lifecycle methods are present so modules can perform init/shutdown.
class EngineModule : public Object {
public:
  EngineModule() = default;
  virtual ~EngineModule() = default;

  OXYGEN_MAKE_NON_COPYABLE(EngineModule)
  OXYGEN_MAKE_NON_MOVABLE(EngineModule)

  // Metadata
  [[nodiscard]] virtual auto GetName() const noexcept -> std::string_view = 0;
  [[nodiscard]] virtual auto GetPriority() const noexcept -> ModulePriority = 0;
  [[nodiscard]] virtual auto GetSupportedPhases() const noexcept
    -> ModulePhaseMask
    = 0;
  [[nodiscard]] virtual auto IsCritical() const noexcept -> bool
  {
    return false;
  }

  // Lifecycle management

  //! Called when a module is attached to the engine.
  /*!
   @return True if the module initialization after being attached to the
   engine was successful, false otherwise. When false is returned, the
   module will be unregistered and will no longer be used.
  */
  virtual auto OnAttached(observer_ptr<AsyncEngine> /*engine*/) noexcept -> bool
  {
    return true;
  }

  //! Called when a module is being unregistered from the engine. Provides a
  //! hook for the module to perform cleanup.
  /*!
   This lifecycle hook is not allowed to fail. The module will be removed no
   matter what.
  */
  virtual auto OnShutdown() noexcept -> void { }

  virtual auto OnUnload() noexcept -> void { }

  // === Module-facing phase handlers ===
  // Expose handlers for all phases except the explicitly engine-only ones:
  // kNetworkReconciliation, kRandomSeedManagement, kPresent, kBudgetAdapt.

  // Ordered phases
  virtual auto OnFrameStart(FrameContext& /*context*/) -> void { }
  virtual auto OnFrameEnd(FrameContext& /*context*/) -> void { }

  // Synchronous snapshot phase (modules participate; engine publishes last)
  // Must not spawn threads or coroutines; runs on the main thread.
  virtual auto OnSnapshot(FrameContext& /*context*/) -> void { }

  virtual auto OnInput(FrameContext& /*context*/) -> co::Co<> { co_return; }
  virtual auto OnNetworkReconciliation(FrameContext& /*context*/) -> co::Co<>
  {
    co_return;
  }
  virtual auto OnFixedSimulation(FrameContext& /*context*/) -> co::Co<>
  {
    co_return;
  }
  virtual auto OnGameplay(FrameContext& /*context*/) -> co::Co<> { co_return; }
  virtual auto OnSceneMutation(FrameContext& /*context*/) -> co::Co<>
  {
    co_return;
  }
  virtual auto OnTransformPropagation(FrameContext& /*context*/) -> co::Co<>
  {
    co_return;
  }

  virtual auto OnPostParallel(FrameContext& /*context*/) -> co::Co<>
  {
    co_return;
  }
  virtual auto OnFrameGraph(FrameContext& /*context*/) -> co::Co<>
  {
    co_return;
  }
  virtual auto OnCommandRecord(FrameContext& /*context*/) -> co::Co<>
  {
    co_return;
  }

  // Parallel phase (snapshot-based)
  // Parallel phase (snapshot-based): modules receive a read-only
  // `FrameSnapshot` (a view over the authoritative GameState). Handlers
  // must not mutate GameState or EngineState directly; any per-job outputs
  // must be written into module-owned staging buffers (FrameState) and
  // integrated later during the ordered PostParallel phase.
  virtual auto OnParallelTasks(const UnifiedSnapshot& /*snapshot*/) -> co::Co<>
  {
    co_return;
  }

  virtual auto OnAsyncPoll(FrameContext& /*context*/) -> co::Co<> { co_return; }

  virtual auto OnDetachedService(FrameContext& /*context*/) -> co::Co<>
  {
    co_return;
  }

protected:
  //! Report an error with this module's name automatically set as the source
  //! key
  /*!
   This helper ensures that modules always report errors with proper
   attribution. All modules should use this method instead of calling
   FrameContext::ReportError directly.

   @param context The frame context to report the error to
   @param message The error message to report
   */
  auto ReportError(FrameContext& context, std::string_view message) const
    -> void
  {
    context.ReportError(
      GetTypeId(), std::string { message }, std::string { GetName() });
  }
};

} // namespace oxygen::engine
