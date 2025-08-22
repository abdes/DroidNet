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
#include <Oxygen/OxCo/Co.h>
#include <fmt/format.h>

namespace oxygen::examples::asyncsim {

class ModuleContext;
class GraphicsLayer;

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

} // namespace oxygen::examples::asyncsim

//! fmt formatter for ModulePriority to support direct formatting in logs
template <> struct fmt::formatter<oxygen::examples::asyncsim::ModulePriority> {
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

  template <typename FormatContext>
  auto format(const oxygen::examples::asyncsim::ModulePriority& priority,
    FormatContext& ctx)
  {
    return fmt::format_to(
      ctx.out(), "{}", oxygen::examples::asyncsim::to_string(priority));
  }
};

namespace oxygen::examples::asyncsim {

//! Flags indicating which frame phases a module participates in
enum class ModulePhases : uint32_t {
  None = 0,

  // Ordered phases (Category A) - deterministic, sequential
  Input = 1 << 0, //!< Input sampling phase
  FixedSimulation = 1 << 1, //!< Fixed timestep simulation
  Gameplay = 1 << 2, //!< Variable gameplay logic
  NetworkReconciliation = 1 << 3, //!< Network packet reconciliation
  SceneMutation = 1 << 4, //!< Scene structural changes
  TransformPropagation = 1 << 5, //!< Transform hierarchy updates
  SnapshotBuild = 1 << 6, //!< Immutable snapshot creation
  PostParallel = 1 << 7, //!< Integration after parallel work
  FrameGraph = 1 << 8, //!< Render graph assembly
  DescriptorPublication = 1 << 9, //!< Bindless descriptor updates
  ResourceTransitions = 1 << 10, //!< GPU resource state planning
  CommandRecord = 1 << 11, //!< Command list recording
  Present = 1 << 12, //!< Final presentation

  // Parallel phases (Category B) - snapshot-based, concurrent
  ParallelWork = 1 << 16, //!< Parallel frame work

  // Async phases (Category C) - multi-frame pipelines
  AsyncWork = 1 << 20, //!< Async pipeline work

  // Detached phases (Category D) - fire-and-forget
  DetachedWork = 1 << 24, //!< Background services

  // Common combinations
  CoreGameplay
  = Input | FixedSimulation | Gameplay | SceneMutation | TransformPropagation,
  Rendering = SnapshotBuild | ParallelWork | PostParallel | FrameGraph
    | CommandRecord | Present,
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
class IEngineModule {
public:
  virtual ~IEngineModule() = default;

  //! Module identification
  [[nodiscard]] virtual std::string_view GetName() const noexcept = 0;
  [[nodiscard]] virtual ModulePriority GetPriority() const noexcept
  {
    return ModulePriorities::Normal;
  }
  [[nodiscard]] virtual ModulePhases GetSupportedPhases() const noexcept = 0;

  //! Lifecycle management
  virtual auto Initialize(ModuleContext& context) -> co::Co<> { co_return; }
  virtual auto Shutdown(ModuleContext& context) -> co::Co<> { co_return; }

  // === ORDERED PHASES (Category A) - Sequential, deterministic ===
  // Can mutate authoritative state, strict ordering enforced

  //! Input sampling phase - produce immutable input snapshot
  virtual auto OnInput(ModuleContext& context) -> co::Co<> { co_return; }

  //! Fixed timestep simulation - deterministic physics/gameplay
  virtual auto OnFixedSimulation(ModuleContext& context) -> co::Co<>
  {
    co_return;
  }

  //! Variable gameplay logic - high-level game state mutations
  virtual auto OnGameplay(ModuleContext& context) -> co::Co<> { co_return; }

  //! Network reconciliation - apply network updates to authoritative state
  virtual auto OnNetworkReconciliation(ModuleContext& context) -> co::Co<>
  {
    co_return;
  }

  //! Scene mutations - structural changes (spawn/despawn, reparent)
  virtual auto OnSceneMutation(ModuleContext& context) -> co::Co<>
  {
    co_return;
  }

  //! Transform propagation - hierarchy traversal and world transform updates
  virtual auto OnTransformPropagation(ModuleContext& context) -> co::Co<>
  {
    co_return;
  }

  //! Snapshot build - create immutable views for parallel work
  virtual auto OnSnapshotBuild(ModuleContext& context) -> co::Co<>
  {
    co_return;
  }

  //! Post-parallel integration - merge results from parallel work
  virtual auto OnPostParallel(ModuleContext& context) -> co::Co<> { co_return; }

  //! Frame graph assembly - build render pass dependency graph
  virtual auto OnFrameGraph(ModuleContext& context) -> co::Co<> { co_return; }

  //! Descriptor publication - update bindless descriptor tables
  virtual auto OnDescriptorPublication(ModuleContext& context) -> co::Co<>
  {
    co_return;
  }

  //! Resource transitions - plan GPU resource state changes
  virtual auto OnResourceTransitions(ModuleContext& context) -> co::Co<>
  {
    co_return;
  }

  //! Command recording - record GPU command lists (may be parallel per surface)
  virtual auto OnCommandRecord(ModuleContext& context) -> co::Co<>
  {
    co_return;
  }

  //! Present - final surface presentation (synchronous)
  virtual auto OnPresent(ModuleContext& context) -> co::Co<> { co_return; }

  // === PARALLEL PHASE (Category B) - Concurrent, snapshot-based ===
  // Read-only snapshot access, parallel execution safe

  //! Parallel work phase - concurrent processing on immutable snapshot
  //! Safe for parallel execution, no shared mutable state access
  virtual auto OnParallelWork(ModuleContext& context) -> co::Co<> { co_return; }

  // === ASYNC PHASE (Category C) - Multi-frame pipelines ===
  // Eventual consistency, results integrated when ready

  //! Async work - multi-frame operations (asset loading, compilation, etc.)
  virtual auto OnAsyncWork(ModuleContext& context) -> co::Co<> { co_return; }

  // === DETACHED PHASE (Category D) - Fire-and-forget ===
  // Background services, no frame dependencies

  //! Detached work - background services (telemetry, logging, etc.)
  virtual auto OnDetachedWork(ModuleContext& context) -> co::Co<> { co_return; }
};

//! Convenience base class providing default implementations
class EngineModuleBase : public IEngineModule {
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

} // namespace oxygen::examples::asyncsim
