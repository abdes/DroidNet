//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/OxCo/Co.h>
#include <fmt/format.h>
// Phase mutation flags and helpers
#include <Oxygen/Core/PhaseRegistry.h>

namespace oxygen::engine {
class FrameContext;
}

namespace oxygen::engine::asyncsim {

class GraphicsLayer;
class AsyncEngineSimulator; // Forward declare for engine reference

//! Strong type for module execution priority (lower values = higher priority)
using ModulePriority
  = NamedType<uint32_t, struct ModulePriorityTag, Comparable>;

//! Predefined priority levels for convenience
namespace ModulePriorities {
  inline constexpr auto Critical
    = ModulePriority { 0 }; //!< System-critical modules (input, core systems)
  inline constexpr auto High
    = ModulePriority { 100 }; //!< High-priority gameplay modules
  inline constexpr auto Normal
    = ModulePriority { 500 }; //!< Standard gameplay modules
  inline constexpr auto Low
    = ModulePriority { 800 }; //!< Non-critical modules (debug, profiling)
  inline constexpr auto Background
    = ModulePriority { 900 }; //!< Background services
}

//! Convert ModulePriority to string for logging and debugging
inline std::string to_string(const ModulePriority& priority)
{
  const auto value = priority.get();

  // Check for predefined priorities
  if (value == ModulePriorities::Critical.get())
    return "Critical";
  if (value == ModulePriorities::High.get())
    return "High";
  if (value == ModulePriorities::Normal.get())
    return "Normal";
  if (value == ModulePriorities::Low.get())
    return "Low";
  if (value == ModulePriorities::Background.get())
    return "Background";

  // Return numeric value for custom priorities
  return std::to_string(value);
}

} // namespace oxygen::engine::asyncsim

//! fmt formatter for ModulePriority to support direct formatting in logs
template <> struct fmt::formatter<oxygen::engine::asyncsim::ModulePriority> {
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

  template <typename FormatContext>
  auto format(const oxygen::engine::asyncsim::ModulePriority& priority,
    FormatContext& ctx)
  {
    return fmt::format_to(
      ctx.out(), "{}", oxygen::engine::asyncsim::to_string(priority));
  }
};

namespace oxygen::engine::asyncsim {

//! Flags indicating which frame phases a module participates in
enum class ModulePhases : uint32_t {
  None = 0,

  // Ordered phases (Category A) - deterministic, sequential
  FrameStart = 1u << 0, //!< Frame start hook
  Input = 1u << 1, //!< Input sampling phase
  FixedSimulation = 1u << 2, //!< Fixed timestep simulation
  Gameplay = 1u << 3, //!< Variable gameplay logic
  NetworkReconciliation = 1u << 4, //!< Network packet reconciliation
  SceneMutation = 1u << 5, //!< Scene structural changes
  TransformPropagation = 1u << 6, //!< Transform hierarchy updates
  PostParallel = 1u << 7, //!< Integration after parallel work
  FrameGraph = 1u << 8, //!< Render graph assembly
  CommandRecord = 1u << 9, //!< Command list recording
  // NOTE: Present is engine-only and not available to modules

  // Parallel phases (Category B) - snapshot-based, concurrent
  ParallelWork = 1u << 10, //!< Parallel frame work

  // Async phases (Category C) - multi-frame pipelines
  AsyncWork = 1u << 11, //!< Async pipeline work

  // Detached phases (Category D) - fire-and-forget
  DetachedWork = 1u << 12, //!< Background services

  FrameEnd = 1u << 13, //!< Frame end hook

  // Common combinations
  CoreGameplay
  = Input | FixedSimulation | Gameplay | SceneMutation | TransformPropagation,
  Rendering = ParallelWork | PostParallel | FrameGraph | CommandRecord,
  AllPhases = 0xFFFFFFFF
};

constexpr ModulePhases operator|(ModulePhases a, ModulePhases b)
{
  return static_cast<ModulePhases>(
    static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr ModulePhases operator&(ModulePhases a, ModulePhases b)
{
  return static_cast<ModulePhases>(
    static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr bool HasPhase(ModulePhases flags, ModulePhases phase)
{
  return (flags & phase) != ModulePhases::None;
}

//! Abstract base interface for engine modules
//!
//! Modules are called at specific frame phases with clear data contracts:
//! - Ordered phases: Can mutate authoritative state, strict ordering
//! - Parallel phases: Read-only snapshot access, concurrent execution
//! - Async phases: Multi-frame operations, eventual consistency
//! - Detached phases: Fire-and-forget background work
//!
//! Mutation gating:
//! - The engine exposes `PhaseDesc::MutateFlags` via `PhaseRegistry` to
//!   indicate whether a phase may mutate GameState and/or EngineState.
//! - Modules and FrameContext validators should call the debug helpers
//!   `DebugAssertCanMutateGameState` or `DebugAssertCanMutateEngineState`
//!   (available in debug builds) before performing gated mutations. Engine
//!   operations that affect EngineState should still require `EngineTag`
//!   capability where applicable.
class EngineModule {
public:
  virtual ~EngineModule() = default;

  //! Module identification
  [[nodiscard]] virtual std::string_view GetName() const noexcept = 0;
  [[nodiscard]] virtual ModulePriority GetPriority() const noexcept
  {
    return ModulePriorities::Normal;
  }
  [[nodiscard]] virtual ModulePhases GetSupportedPhases() const noexcept = 0;

  //! Engine reference management - modules can store engine as observer_ptr
  //! Engine lifetime is guaranteed to be greater than any module it manages
  virtual void SetEngine(AsyncEngineSimulator* engine) noexcept
  {
    engine_ = observer_ptr { engine };
  }

protected:
  //! Engine reference stored as observer_ptr - NOT owned by module
  oxygen::observer_ptr<AsyncEngineSimulator> engine_ { nullptr };

public:
  //! Lifecycle management
  virtual auto Initialize(AsyncEngineSimulator& engine) -> co::Co<>
  {
    co_return;
  }
  virtual auto Shutdown() -> co::Co<> { co_return; }

  // === ORDERED PHASES (Category A) - Sequential, deterministic ===
  // Can mutate authoritative state, strict ordering enforced

  virtual void OnFrameStart(FrameContext& /*context*/) { }
  virtual void OnFrameEnd(FrameContext& /*context*/) { }

  //! Input sampling phase - produce immutable input snapshot
  virtual auto OnInput(FrameContext& context) -> co::Co<> { co_return; }

  //! Fixed timestep simulation - deterministic physics/gameplay
  virtual auto OnFixedSimulation(FrameContext& context) -> co::Co<>
  {
    co_return;
  }

  //! Variable gameplay logic - high-level game state mutations
  virtual auto OnGameplay(FrameContext& context) -> co::Co<> { co_return; }

  //! Network reconciliation - apply network updates to authoritative state
  virtual auto OnNetworkReconciliation(FrameContext& context) -> co::Co<>
  {
    co_return;
  }

  //! Scene mutations - structural changes (spawn/despawn, reparent)
  virtual auto OnSceneMutation(FrameContext& context) -> co::Co<> { co_return; }

  //! Transform propagation - hierarchy traversal and world transform updates
  virtual auto OnTransformPropagation(FrameContext& context) -> co::Co<>
  {
    co_return;
  }

  //! Post-parallel integration - merge results from parallel work
  virtual auto OnPostParallel(FrameContext& context) -> co::Co<> { co_return; }

  //! Frame graph assembly - build render pass dependency graph
  virtual auto OnFrameGraph(FrameContext& context) -> co::Co<> { co_return; }

  //! Command recording - record GPU command lists (may be parallel per surface)
  virtual auto OnCommandRecord(FrameContext& context) -> co::Co<>
  {
    // Example debug assertion: ensure command recording is allowed to mutate
    // EngineState (fence/descriptor bookkeeping) in the configured phase.
    // DebugAssertCanMutateEngineState(PhaseId::kCommandRecord);
    co_return;
  }

  // === FRAME BOUNDARY PHASES - Hooks at frame start/end ===
  // NOTE: Present is engine-only and not available to modules - the engine
  // handles presentation through the GraphicsLayer for platform abstraction

  // === PARALLEL PHASE (Category B) - Concurrent, snapshot-based ===
  // Read-only snapshot access, parallel execution safe

  //! Parallel work phase - concurrent processing on immutable snapshot
  //! Safe for parallel execution, no shared mutable state access
  virtual auto OnParallelWork(FrameContext& context) -> co::Co<> { co_return; }

  // === ASYNC PHASE (Category C) - Multi-frame pipelines ===
  // Eventual consistency, results integrated when ready

  //! Async work - multi-frame operations (asset loading, compilation, etc.)
  virtual auto OnAsyncWork(FrameContext& context) -> co::Co<> { co_return; }

  // === DETACHED PHASE (Category D) - Fire-and-forget ===
  // Background services, no frame dependencies

  //! Detached work - background services (telemetry, logging, etc.)
  virtual auto OnDetachedWork(FrameContext& context) -> co::Co<> { co_return; }
};

//! Convenience base class providing default implementations
class EngineModuleBase : public EngineModule {
public:
  explicit EngineModuleBase(std::string name, ModulePhases phases,
    ModulePriority priority = ModulePriorities::Normal)
    : name_(std::move(name))
    , phases_(phases)
    , priority_(priority)
  {
  }

  [[nodiscard]] std::string_view GetName() const noexcept override
  {
    return name_;
  }
  [[nodiscard]] ModulePriority GetPriority() const noexcept override
  {
    return priority_;
  }
  [[nodiscard]] ModulePhases GetSupportedPhases() const noexcept override
  {
    return phases_;
  }

private:
  std::string name_;
  ModulePhases phases_;
  ModulePriority priority_;
};

} // namespace oxygen::engine::asyncsim
