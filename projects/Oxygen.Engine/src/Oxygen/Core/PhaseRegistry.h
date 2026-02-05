//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cinttypes>
#include <type_traits>

#include <Oxygen/Base/EnumIndexedArray.h>
#include <Oxygen/Base/Macros.h>

// ReSharper disable CppClangTidyPerformanceEnumSize

namespace oxygen::core {

//! ExecutionModel classification
/*!
 ExecutionModel describes which of the ABCD execution classes a phase belongs to
 (see Examples/AsyncEngine/README.md). Keep this enum small and
 constexpr-friendly so it can be used in static phase descriptors and
 compile-time predicates.

 @note Values are stable and used for code-path selection in the engine's
 coordinator and scheduler.
*/
enum class ExecutionModel : std::uint32_t {
  //! A: Synchronous ordered phases that must run on coordinator thread.
  //! AsyncEngine coordinator calls foundational subsystems directly;
  //! application modules execute in deterministic sequence, exclusively on the
  //! coordinator thread. Use of Structured concurrency execution model is
  //! allowed.
  kSynchronousOrdered = 0,

  //! B: Barriered Concurrency - Synchronous Outcome, Parallelizable tasks.
  //! Application modules and engine services execute concurrently on awaited
  //! coroutines. Engine coordinator will not transition to next phase until
  //! all modules complete execution. Supports all execution models.
  kBarrieredConcurrency = 1,

  //! C: Async ordered across frames. Foundational subsystems and application
  //! modules manage async pipelines internally; engine polls readiness every
  //! frame and integrates results when available.
  kDeferredPipelines = 2,

  //! D: Detached services. Cross-cutting utility services with fire-and-forget
  //! semantics; no frame synchronization or subsystem integration required.
  kDetached = 3,

  //! Small engine-internal sync phases executed on the coordinator (not
  //! intended for module handlers).
  kEngineInternal = 4,
};

//! Ordered list of engine frame phases
/*!
 Canonical, ordered list of engine frame phases. The numeric values are
 intentionally stable and must match the engine frame loop ordering. These
 enumerators are used for PhaseMask bit operations and indexing into the phase
 registry tables.

 @see PhaseDesc, kPhaseRegistry
*/
enum class PhaseId : std::uint32_t {
  kFirst = 0,

  kFrameStart = kFirst,
  kInput = 1,
  kNetworkReconciliation = 2,
  kRandomSeedManagement = 3,
  kFixedSimulation = 4,
  kGameplay = 5,
  kSceneMutation = 6,
  kTransformPropagation = 7,
  kSnapshot = 8,
  kParallelTasks = 9,
  kPostParallel = 10,
  kGuiUpdate = 11,
  kPreRender = 12,
  kRender = 13,
  kCompositing = 14,
  kPresent = 15,
  kAsyncPoll = 16,
  kBudgetAdapt = 17,
  kFrameEnd = 18,
  kDetachedServices = 19,

  kCount = 20, // Must be last
};
using PhaseIndex = EnumAsIndex<PhaseId>;

//! Bitmask type for PhaseId values
/*!
 PhaseMask is the compact integer type used for bitmask operations that
 represent sets of phases. It is intentionally a 32-bit unsigned type to allow
 efficient bit operations across the commonly-sized PhaseId enum.
*/
using PhaseMask = std::uint32_t;

//! MakePhaseMask helper
/*!
 Build a PhaseMask for a single PhaseId at compile time.

 @param id PhaseId enumerator to construct the mask for.
 @return PhaseMask with the bit for `id` set.
*/
constexpr auto MakePhaseMask(PhaseId id) noexcept
{
  return 1u << static_cast<std::underlying_type_t<PhaseId>>(id);
}

//! Barrier identifiers
/*!
 Engine-level synchronization barrier identifiers. Barriers are declared here to
 provide constexpr visibility for engine coordination logic and for use in the
 `kBarrierRegistry` table.
*/
enum class BarrierId : std::uint32_t {
  kFirst = 0,

  kInputSnapshot = kFirst, // B0
  kNetworkReconciled = 1, // B1
  kSimulationComplete = 2, // B2
  kSceneStable = 3, // B3
  kSnapshotReady = 4, // B4
  kParallelComplete = 5, // B5
  kCommandReady = 6, // B6
  kAsyncPublishReady = 7, // B7

  kCount = 8, // Must be last
};
using BarrierIndex = EnumAsIndex<BarrierId>;

//! AllowMutation — bitflags for permitted mutation targets
/*!
 A compact set of bitflags that express which engine state layers a phase may
 mutate. Values are intended for use in static phase descriptors and runtime
 validation logic. Flags are created with the `OXYGEN_FLAG` macro and may be
 combined with bitwise operators (see `OXYGEN_DEFINE_FLAGS_OPERATORS`).

 Use the `PhaseDesc::allowed_mutations` member or the helpers in `meta::` (for
 example `meta::PhaseCanMutateGameState`) to query permissions.

 @note
 - `kGameState` denotes authoritative, cross-frame game data that requires
   strict coordination and is only safe to mutate from allowed phases.
 - `kFrameState` denotes transient, per-frame artifacts (draw lists, per-job
   outputs, command buffers, etc.) that may be produced during the frame and
   integrated later.
 - `kEngineState` denotes coordinator-visible engine registries and metadata
   such as swapchain state, resource registries, and scheduling bookkeeping.
*/
enum class AllowMutation : std::uint32_t {
  kNone = 0,
  kGameState = OXYGEN_FLAG(0),
  kFrameState = OXYGEN_FLAG(1),
  kEngineState = OXYGEN_FLAG(2),
};

// Enable bitwise operators for AllowMutation
OXYGEN_DEFINE_FLAGS_OPERATORS(AllowMutation)

// Forward declarations for detail docstring accessors so PhaseDesc can be
// declared above the detail namespace where the tables live.
namespace detail {
  constexpr auto PhaseName(PhaseId id) noexcept -> const char*;
  constexpr auto PhaseDescription(PhaseId id) noexcept -> const char*;
  constexpr auto BarrierName(BarrierId id) noexcept -> const char*;
  constexpr auto BarrierDescription(BarrierId id) noexcept -> const char*;
} // namespace detail

//! Phase descriptor
/*!
 A small, constexpr-friendly descriptor that records execution semantics and
 mutation permissions for a given phase. This structure is intended to be stored
 in `kPhaseRegistry` and used by validation code and the engine coordinator to
 decide how to execute module handlers for each phase.

 Members:
 - `id`: PhaseId enumerator for this descriptor.
 - `category`: ExecutionModel classification.
 - `thread_safe`: true when module handlers may run on worker threads.
 - `allowed_mutations`: AllowMutation bitflags describing allowed mutations.

 See also: `kPhaseRegistry`, `meta::PhaseCanMutate*` helpers.
*/
struct PhaseDesc {
  PhaseId id;
  ExecutionModel category;
  AllowMutation allowed_mutations;
  //! If true, modules are allowed to use multithreaded tasks during this phase.
  bool thread_safe;

  //! True when this phase uses barriered concurrency (Category B) and
  //! handlers are implemented as awaitable coroutines.
  constexpr auto UsesCoroutines() const noexcept
  {
    return category == ExecutionModel::kBarrieredConcurrency;
  }
  //! True when this phase is permitted to mutate authoritative GameState.
  constexpr auto CanMutateGameState() const noexcept
  {
    return (allowed_mutations & AllowMutation::kGameState)
      == AllowMutation::kGameState;
  }
  //! True when this phase is permitted to mutate transient per-frame
  //! FrameState outputs.
  constexpr auto CanMutateFrameState() const noexcept
  {
    return (allowed_mutations & AllowMutation::kFrameState)
      == AllowMutation::kFrameState;
  }
  //! True when this phase is permitted to mutate EngineState registries and
  //! metadata.
  constexpr auto CanMutateEngineState() const noexcept
  {
    return (allowed_mutations & AllowMutation::kEngineState)
      == AllowMutation::kEngineState;
  }

  constexpr auto Name() const noexcept { return detail::PhaseName(id); }

  constexpr auto Description() const noexcept
  {
    return detail::PhaseDescription(id);
  }
};

namespace detail {
  //! Compact storage for documentation strings separated from metadata so the
  //! PhaseDesc/BarrierDesc remain small and constexpr-friendly.
  struct DocStrings {
    const char* name;
    const char* description;
  };

  //! Phase docstrings table (indexed by PhaseId underlying value).
  constexpr std::array<DocStrings, static_cast<std::size_t>(PhaseId::kCount)>
    kPhaseDocStrings = {
      DocStrings {
        .name = R"(FrameStart)",
        .description
        = R"(Advance the global frame index and perform coordinator-side
epoch and fence reclamation. Runs deferred resource reclamation and other
engine bookkeeping that prepare the coordinator-visible EngineState for the
upcoming frame. Does not publish a GameState snapshot.)",
      },
      DocStrings {
        .name = R"(Input)",
        .description
        = R"(Sample platform and user input and publish a stabilized per-frame
input snapshot consisting of captured input events and sampling state.
This snapshot contains input data only (captured events/state) and is not
the engine's FrameSnapshot or a view over GameState or EngineState.)",
      },
      DocStrings {
        .name = R"(NetworkReconciliation)",
        .description
        = R"(Apply authoritative network updates and reconcile client-side
predictions. Mutations performed here update authoritative GameState so that
subsequent simulation phases observe the reconciled state.)",
      },
      DocStrings {
        .name = R"(RandomSeedManagement)",
        .description
        = R"(Manage deterministic RNG and per-frame seed state used by gameplay
and simulation systems. This updates EngineState RNG bookkeeping and does
not mutate the GameState.)",
      },
      DocStrings {
        .name = R"(FixedSimulation)",
        .description
        = R"(Execute fixed-timestep deterministic physics and simulation
integrations that produce authoritative GameState updates. Results are
authoritative and will be visible to downstream ordered phases.)",
      },
      DocStrings {
        .name = R"(Gameplay)",
        .description
        = R"(Run high-level game logic that mutates authoritative GameState.
Gameplay may stage structural edits (spawn/despawn) that are later applied
in SceneMutation.)",
      },
      DocStrings {
        .name = R"(SceneMutation)",
        .description
        = R"(Apply structural scene edits (spawns, despawns, handle and
component allocations). These changes modify GameState topology and are
required to be visible before transform propagation.)",
      },
      DocStrings {
        .name = R"(TransformPropagation)",
        .description
        = R"(Propagate hierarchical transforms and finalize spatial
relationships. After this phase the engine will publish an immutable
FrameSnapshot that parallel readers may consume; the snapshot reflects the
current GameState.)",
      },
      DocStrings {
        .name = R"(Snapshot)",
        .description
        = R"(Publish an immutable FrameSnapshot (a lightweight view over the
GameState) for parallel readers. The snapshot is intended for read-only
consumption by Category C tasks and does not permit direct GameState
mutations. FrameState (transient per-frame outputs) is produced after this
phase and integrated later.)",
      },
      DocStrings {
        .name = R"(ParallelTasks)",
        .description
        = R"(Run parallel Category C tasks that consume the immutable
FrameSnapshot (read-only GameState view). Tasks must not mutate GameState or
EngineState directly; they write results into per-job outputs (FrameState)
for later integration at the post-parallel barrier.)",
      },
      DocStrings {
        .name = R"(PostParallel)",
        .description
        = R"(Integrate per-job FrameState outputs produced by parallel tasks
into authoritative GameState and FrameOutputs. This phase may also perform
EngineState updates required to publish descriptors, resource transitions,
or other cross-frame metadata.)",
      },
      DocStrings {
        .name = R"(UIUpdate)",
        .description
        = R"(Process UI systems including ImGui, game UI, and debug overlays.
Generates UI rendering artifacts (draw lists, vertex buffers, textures) that
will be consumed by the frame graph. May perform async UI work such as
layout calculations, text rendering, and animation updates. Should not
mutate authoritative GameState; UI interactions queue events for the next
frame's gameplay phase.)",
      },
      DocStrings {
        .name = R"(PreRender)",
        .description
        = R"(Prepare per-frame and per-view rendering data. This phase is
      responsible for renderer-owned preparation such as culling, draw-metadata
      emission and upload staging. App modules may also perform work here to
      prepare render-pass inputs. No command lists should be recorded in this
      phase.)",
      },
      DocStrings {
        .name = R"(Render)",
        .description
        = R"(Execute rendering: modules record command lists and run per-view
      rendering logic. Render consumes prepared per-view snapshots produced in
      PreRender. This phase may submit command lists. Modules should not mutate
      authoritative GameState.)",
      },
      DocStrings {
        .name = R"(Compositing)",
        .description
        = R"(Perform post-rendering composition and full-screen effects.
Modules can access rendered outputs from previous phases and combine them
or apply effects before presentation. This phase produces final presentable
surfaces.)",
      },
      DocStrings {
        .name = R"(Present)",
        .description
        = R"(Perform swapchain present and finalize platform submission
bookkeeping. Presentation is a coordinator-side operation that touches
EngineState (swapchain/timing) but does not modify GameState.)",
      },
      DocStrings {
        .name = R"(AsyncPoll)",
        .description
        = R"(Poll long-running multi-frame async pipelines and integrate
completed results. Async pipelines should publish ready resources and
transient FrameState artifacts into thread-safe staging areas; they must not
mutate authoritative GameState from background threads. When the coordinator
detects readiness, it performs coordinator-side integration during an ordered
integration phase where controlled GameState updates (if required) may be
applied.)",
      },
      DocStrings {
        .name = R"(BudgetAdapt)",
        .description
        = R"(Adjust per-frame budgets and scheduling heuristics to adapt
performance and pacing. This phase updates EngineState scheduling metadata
and does not directly mutate GameState.)",
      },
      DocStrings {
        .name = R"(FrameEnd)",
        .description
        = R"(Finalize end-of-frame bookkeeping, perform deferred resource
releases, and prepare epoch markers for the next frame. These operations
update EngineState reclamation and do not mutate GameState.)",
      },
      DocStrings {
        .name = R"(DetachedServices)",
        .description
        = R"(Run opportunistic background services (logging, telemetry,
compaction) that operate outside the frame-critical path. Detached
services must not mutate GameState; EngineState-side diagnostics are
allowed through thread-safe channels.)",
      },
    };

  // Barrier docstrings table (indexed by BarrierId underlying value).
  constexpr std::array<DocStrings, static_cast<std::size_t>(BarrierId::kCount)>
    kBarrierDocStrings = {
      DocStrings {
        .name = R"(B0_InputSnapshot)",
        .description
        = R"(Stable input and epoch reclamation point. Ensures platform
and user input sampling is complete and that any GPU/CPU epoch-based
reclamation ran so downstream phases observe consistent, coordinator-
visible input state. The snapshot here is the input/FrameSnapshot used by
downstream GameState consumers.)",
      },
      DocStrings {
        .name = R"(B1_NetworkReconciled)",
        .description
        = R"(Network reconciliation completion. Authoritative network updates
and client prediction replay are applied so subsequent simulation phases
observe the reconciled GameState.)",
      },
      DocStrings {
        .name = R"(B2_SimulationComplete)",
        .description
        = R"(Simulation completion barrier. Guarantees that deterministic
physics and simulation integrations have finished and that authoritative
GameState updates are visible to later phases.)",
      },
      DocStrings {
        .name = R"(B3_SceneStable)",
        .description
        = R"(Scene stability barrier. Structural edits (spawns/despawns and
handle allocations) are applied and made visible before transform
propagation and snapshot publication.)",
      },
      DocStrings {
        .name = R"(B4_SnapshotReady)",
        .description
        = R"(Frame snapshot published. Indicates transforms are finalized and
an immutable FrameSnapshot (read-only view over GameState) is available
for parallel Category C tasks. Downstream tasks produce FrameState outputs
based on this snapshot.)",
      },
      DocStrings {
        .name = R"(B5_ParallelComplete)",
        .description
        = R"(Parallel join barrier. All Category C parallel tasks have
completed and their per-job FrameState outputs are ready to be integrated
into authoritative GameState or FrameOutputs at the post-parallel phase.)",
      },
      DocStrings {
        .name = R"(B6_CommandReady)",
        .description
        = R"(Command readiness barrier. Command recording and resource-state
preparation are complete; submission metadata (fence/epoch markers) are
captured into EngineState for reclamation and ordering guarantees.)",
      },
      DocStrings {
        .name = R"(B7_AsyncPublishReady)",
        .description
        = R"(Async publish readiness. Multi-frame async pipelines have
produced ready resources that can be atomically published into EngineState
registries during coordinator-side integration.)",
      },
    };

  constexpr auto PhaseName(PhaseId id) noexcept -> const char*
  {
    auto index = static_cast<std::size_t>(
      static_cast<std::underlying_type_t<PhaseId>>(id));
    return kPhaseDocStrings[index].name;
  }

  constexpr auto PhaseDescription(PhaseId id) noexcept -> const char*
  {
    auto index = static_cast<std::size_t>(
      static_cast<std::underlying_type_t<PhaseId>>(id));
    return kPhaseDocStrings[index].description;
  }

  constexpr auto BarrierName(BarrierId id) noexcept -> const char*
  {
    auto index = static_cast<std::size_t>(
      static_cast<std::underlying_type_t<BarrierId>>(id));
    return kBarrierDocStrings[index].name;
  }

  constexpr auto BarrierDescription(BarrierId id) noexcept -> const char*
  {
    auto index = static_cast<std::size_t>(
      static_cast<std::underlying_type_t<BarrierId>>(id));
    return kBarrierDocStrings[index].description;
  }
} // namespace detail

//! Canonical, constexpr phase registry.
/*!
  The `kPhaseRegistry` table defines the engine's canonical frame-phase order
  and records per-phase execution semantics via `PhaseDesc`. Entries are indexed
  by `PhaseId` (via `EnumIndexedArray`) and are intentionally kept constexpr and
  compact so they can be used in compile-time validation, static predicates, and
  early engine initialization paths.

  Usage:
  - Query `kPhaseRegistry[id].category` to determine the `ExecutionModel` for
    scheduling decisions.
  - Use `kPhaseRegistry[id].allowed_mutations` or the helpers in `meta::` to
    check whether a phase may mutate GameState, FrameState, or EngineState.

  Notes:
  - Maintain the physical ordering of entries to match the runtime frame loop
    ordering; the numeric values in `PhaseId` must remain stable.
  - Keep entries constexpr; avoid runtime initializers that would prevent
    compile-time evaluation or static analysis.
*/
constexpr EnumIndexedArray<PhaseId, PhaseDesc> kPhaseRegistry = {
  PhaseDesc {
    .id = PhaseId::kFrameStart,
    .category = ExecutionModel::kSynchronousOrdered,
    .allowed_mutations = AllowMutation::kEngineState,
    .thread_safe = false,
  },
  PhaseDesc {
    .id = PhaseId::kInput,
    .category = ExecutionModel::kBarrieredConcurrency,
    .allowed_mutations = AllowMutation::kGameState,
    .thread_safe = false,
  },
  PhaseDesc {
    .id = PhaseId::kNetworkReconciliation,
    .category = ExecutionModel::kBarrieredConcurrency,
    .allowed_mutations = AllowMutation::kGameState,
    .thread_safe = false,
  },
  PhaseDesc {
    .id = PhaseId::kRandomSeedManagement,
    .category = ExecutionModel::kEngineInternal,
    .allowed_mutations = AllowMutation::kGameState,
    .thread_safe = false,
  },
  PhaseDesc {
    .id = PhaseId::kFixedSimulation,
    .category = ExecutionModel::kBarrieredConcurrency,
    .allowed_mutations = AllowMutation::kGameState,
    .thread_safe = false,
  },
  PhaseDesc {
    .id = PhaseId::kGameplay,
    .category = ExecutionModel::kBarrieredConcurrency,
    .allowed_mutations = AllowMutation::kGameState,
    .thread_safe = false,
  },
  PhaseDesc {
    .id = PhaseId::kSceneMutation,
    .category = ExecutionModel::kBarrieredConcurrency,
    .allowed_mutations = AllowMutation::kGameState,
    .thread_safe = false,
  },
  PhaseDesc {
    .id = PhaseId::kTransformPropagation,
    .category = ExecutionModel::kBarrieredConcurrency,
    .allowed_mutations = AllowMutation::kGameState,
    .thread_safe = false,
  },
  PhaseDesc {
    .id = PhaseId::kSnapshot,
    .category = ExecutionModel::kEngineInternal,
    .allowed_mutations = AllowMutation::kFrameState,
    .thread_safe = false,
  },
  PhaseDesc {
    .id = PhaseId::kParallelTasks,
    .category = ExecutionModel::kDeferredPipelines,
    .allowed_mutations = AllowMutation::kNone,
    .thread_safe = true,
  },
  PhaseDesc {
    .id = PhaseId::kPostParallel,
    .category = ExecutionModel::kBarrieredConcurrency,
    .allowed_mutations = AllowMutation::kGameState | AllowMutation::kFrameState,
    .thread_safe = false,
  },
  PhaseDesc {
    .id = PhaseId::kGuiUpdate,
    .category = ExecutionModel::kBarrieredConcurrency,
    .allowed_mutations
    = AllowMutation::kFrameState | AllowMutation::kEngineState,
    .thread_safe = false,
  },
  PhaseDesc {
    .id = PhaseId::kPreRender,
    .category = ExecutionModel::kBarrieredConcurrency,
    .allowed_mutations
    = AllowMutation::kFrameState | AllowMutation::kEngineState,
    .thread_safe = false,
  },
  PhaseDesc {
    .id = PhaseId::kRender,
    .category = ExecutionModel::kBarrieredConcurrency,
    .allowed_mutations
    = AllowMutation::kFrameState | AllowMutation::kEngineState,
    .thread_safe = true,
  },
  PhaseDesc {
    .id = PhaseId::kCompositing,
    .category = ExecutionModel::kSynchronousOrdered,
    .allowed_mutations = AllowMutation::kFrameState,
    .thread_safe = true,
  },
  PhaseDesc {
    .id = PhaseId::kPresent,
    .category = ExecutionModel::kEngineInternal,
    .allowed_mutations = AllowMutation::kNone,
    .thread_safe = false,
  },
  PhaseDesc {
    .id = PhaseId::kAsyncPoll,
    .category = ExecutionModel::kBarrieredConcurrency,
    .allowed_mutations = AllowMutation::kEngineState,
    .thread_safe = true,
  },
  PhaseDesc {
    .id = PhaseId::kBudgetAdapt,
    .category = ExecutionModel::kEngineInternal,
    .allowed_mutations = AllowMutation::kNone,
    .thread_safe = false,
  },
  PhaseDesc {
    .id = PhaseId::kFrameEnd,
    .category = ExecutionModel::kSynchronousOrdered,
    .allowed_mutations = AllowMutation::kNone,
    .thread_safe = false,
  },
  PhaseDesc {
    .id = PhaseId::kDetachedServices,
    .category = ExecutionModel::kDetached,
    .allowed_mutations = AllowMutation::kNone,
    .thread_safe = true,
  },
};

//! Central ordered barrier registry.
/*!
  The `kBarrierRegistry` table contains constexpr `BarrierDesc` entries that
  declare canonical engine synchronization barriers and the phase after which
  they occur. Entries are indexed by `BarrierId` (via `EnumIndexedArray`) and
  are intended to be small, constexpr-friendly, and stable across releases.

  Usage:
  - Use `kBarrierRegistry[id].after_phase` to query where the barrier must be
    enforced relative to the frame order defined in `kPhaseRegistry`.
  - The table is constexpr so it may be used in compile-time predicates and
    static validation.

  @note Keep entries in logical order and avoid non-constexpr runtime
  initialization; these descriptors are used by coordinator and scheduler logic
  that may run during engine startup and compile-time checks.
*/
struct BarrierDesc {
  BarrierId id;
  PhaseId after_phase;

  constexpr auto Name() const noexcept { return detail::BarrierName(id); }

  constexpr auto Description() const noexcept
  {
    return detail::BarrierDescription(id);
  }
};

constexpr EnumIndexedArray<BarrierId, BarrierDesc> kBarrierRegistry = {
  BarrierDesc {
    .id = BarrierId::kInputSnapshot,
    .after_phase = PhaseId::kFrameStart,
  },
  BarrierDesc {
    .id = BarrierId::kNetworkReconciled,
    .after_phase = PhaseId::kNetworkReconciliation,
  },
  BarrierDesc {
    .id = BarrierId::kSimulationComplete,
    .after_phase = PhaseId::kFixedSimulation,
  },
  BarrierDesc {
    .id = BarrierId::kSceneStable,
    .after_phase = PhaseId::kSceneMutation,
  },
  BarrierDesc {
    .id = BarrierId::kSnapshotReady,
    .after_phase = PhaseId::kTransformPropagation,
  },
  BarrierDesc {
    .id = BarrierId::kParallelComplete,
    .after_phase = PhaseId::kParallelTasks,
  },
  BarrierDesc {
    .id = BarrierId::kCommandReady,
    .after_phase = PhaseId::kPreRender,
  },
  BarrierDesc {
    .id = BarrierId::kAsyncPublishReady,
    .after_phase = PhaseId::kAsyncPoll,
  },
};

namespace meta {
  //! Query whether the given phase is permitted to mutate authoritative
  //! GameState according to the registry `allowed_mutations`.
  constexpr auto PhaseCanMutateGameState(PhaseId id) noexcept
  {
    return (kPhaseRegistry[id].allowed_mutations & AllowMutation::kGameState)
      == AllowMutation::kGameState;
  }

  //! Query whether the given phase is permitted to mutate transient per-frame
  //! FrameState outputs according to the registry `allowed_mutations`.
  constexpr auto PhaseCanMutateFrameState(PhaseId id) noexcept
  {
    return (kPhaseRegistry[id].allowed_mutations & AllowMutation::kFrameState)
      == AllowMutation::kFrameState;
  }

  //! Query whether the given phase is permitted to mutate EngineState
  //! registries and metadata according to the registry `allowed_mutations`.
  constexpr auto PhaseCanMutateEngineState(PhaseId id) noexcept
  {
    return (kPhaseRegistry[id].allowed_mutations & AllowMutation::kEngineState)
      == AllowMutation::kEngineState;
  }
} // namespace meta

} // namespace oxygen::core
