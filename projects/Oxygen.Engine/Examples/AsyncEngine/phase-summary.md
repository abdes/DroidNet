# AsyncEngine Phase Summary

This file summarizes the engine phases and maps each phase to the execution
category (A/B/C/D), whether it is ordered or parallel, whether structured
concurrency is allowed, whether multi-threaded execution is allowed, and whether
the phase may mutate the frame context.

| Phase | Category | Ordered or Parallel | Structured Concurrency Allowed | Multi-threaded Allowed | Mutates GameState | Mutates EngineState | Barrier |
|---|---:|---:|:---:|:---:|:---:|:---:|:---:|
| PhaseFrameStart / Frame Start & Epoch Maintenance | A | Ordered | No | No (main/coordinator) | No | Yes — advances frame index, performs epoch/fence reclamation, runs deferred reclamation using epoch/fence markers | B0 (Barrier_InputSnapshot) |
| Input | A | Ordered | No | No | Yes — updates per-frame input snapshot used by gameplay | No | B0 (Barrier_InputSnapshot) |
| PhaseNetworkReconciliation | A | Ordered | No | No | Yes — applies server authoritative state, rewinds client predictions, replays unacknowledged inputs | No | B1 (Barrier_NetworkReconciled) |
| PhaseRandomSeedManagement | A | Ordered | No | No | Yes — manages RNG/seed state for determinism; must happen BEFORE any systems that consume randomness | No | B1 (Barrier_NetworkReconciled) |
| PhaseFixedSim (Fixed timestep physics) | A | Ordered | No | No | Yes — authoritative physics integration, deterministic state mutation | No | B2 (Barrier_SimulationComplete) |
| Game Play | A | Ordered | No | No | Yes — mutates authoritative game state; may stage structural changes | No | B2 (Barrier_SimulationComplete) / B3 (Barrier_SceneStable) |
| PhaseSceneMutation (structural changes, spawn/despawn) | A | Ordered | No | No | Yes — structural mutations and handle allocations (B3 barrier referenced) | No | B3 (Barrier_SceneStable) |
| PhaseTransforms (hierarchy propagation) | A | Ordered | No | No | Yes — updates transforms; ensures B4 before parallel | No | B4 (Barrier_SnapshotReady) |
| PhaseSnapshot (build immutable frame snapshot) | A | Ordered | No | No | No — produces immutable snapshot for parallel readers (publishes snapshot) | No | B4 (Barrier_SnapshotReady) |
| Parallel Tasks | B | Parallel | Yes — barriered coroutines (FrameTaskGroup / co::AllOf) | Yes — worker thread pool expected | No — must not mutate GameState; write to per-job outputs | No — must not mutate EngineState directly; write per-job outputs / staging buffers integrated later | B5 (Barrier_ParallelComplete) |
| PhasePostParallel (integrate parallel outputs) | A (post-barrier) | Ordered (barrier join) | No | No | Yes — integrates per-job outputs into GameState or FrameOutputs | Conditional — may publish descriptors, merge staging uploads or update EngineState metadata during integration | B5 (Barrier_ParallelComplete) |
| PhaseFrameGraph / Frame Graph & Render Pass Assembly | A | Ordered (but may use helpers) | Limited — helper tasks allowed, core assembly ordered | Limited — helper tasks on workers, core assembly on coordinator | No — consumes snapshot and build draw/dispatch plans (does not change gameplay) | Yes — constructs frame graph, plans resource state transitions and may update EngineState planning metadata | B6 (Barrier_CommandReady) |
| PhaseCommandRecord / Command Recording & Submission | B | Parallel recording, ordered submission | Yes — structured/RAII task groups for recording; ordered submission on coordinator | Yes — recording on worker threads, submission ordering on coordinator | No — recording is pure command list construction from snapshot and must not mutate GameState | Yes — records command lists into FrameOutputs (transient), then updates GPU fences/timestamps, records fence/epoch markers into EngineState (used for reclamation/epoch tracking), manages queue ordering and deferred reclaims | B6 (Barrier_CommandReady) |
| PhasePresent (synchronous presentation) | A | Ordered | No | No | No — present happens on coordinator | Conditional — may update EngineState (swapchain/timing) | B6 (Barrier_CommandReady) |
| PhaseAsyncPoll / Polling async jobs (Category C) | C | Ordered poll/integration step | Yes — multi-frame coroutines allowed; integration gated | Yes — background work is multi-threaded; polling is coordinator-side | Conditional — may integrate assets/PSOs/BLAS that affect GameState or renderables (validate generation) | Yes — publishes resources into EngineState registries and updates EngineState metadata (fence/handles) | B7 (Barrier_AsyncPublishReady) |
| PhaseBudgetAdapt / Adaptive scheduling | A | Ordered | No | No | No — does not directly mutate GameState | Yes — mutates EngineState budgets and scheduling parameters | B7 (Barrier_AsyncPublishReady) |
| PhaseFrameEnd / End-of-frame bookkeeping | A | Ordered | No | No | No — does not mutate GameState (finalization only) | Yes — finalizes EngineState counters, deferred releases and prepares epoch markers for next frame | B7 (Barrier_AsyncPublishReady) |
| Detached Services (Category D: logging, telemetry, background compaction) | D | Detached / Opportunistic | No | Yes — background threads/services | No — should not mutate GameState; interact via message queues | No — should not mutate EngineState directly; use thread-safe channels for reporting | - |

Notes

- Categories A/B/C/D are taken from the AsyncEngine execution model: A = ordered
  main-thread deterministic phases; B = structured parallel barriered frame
  tasks; C = multi-frame async pipelines polled and integrated; D = detached
  background services.
- "Structured Concurrency Allowed" indicates phases that are designed to launch
  barriered coroutines/tasks tied to a join point (Category B) or multi-frame
  coroutines (Category C). Integration must still be validated at commit points.
- "Multi-threaded Allowed" indicates whether significant work for the phase may
  run on worker threads. Category A phases run on the coordinator thread but may
  offload subtasks and co_await their completion (offloaded ordered background
  tasks pattern).
- "Mutates Frame Context" flags phases that perform authoritative state
  mutations that must be ordered. Parallel tasks should not mutate authoritative
  frame context directly; they must write to per-job outputs merged after the
  barrier.

- Fence values and GPU/CPU epoch markers: Command Submission may update GPU
  fence values; those fence timestamps/values are commonly stored in the frame
  context or epoch tracking structures so that deferred reclamation (resource
  retire) and epoch-based safety checks can run at `PhaseFrameStart`.

Generated from: `Examples/AsyncEngine/README.md`,
`Examples/AsyncEngine/execution_model.md`, and
`Examples/AsyncEngine/AsyncEngineSimulator.h`.

## Parallel Tasks — contents

The following lists the canonical kinds of work launched during the "Parallel
Tasks" phase (Category B). Each task runs on the immutable frame snapshot and
must write results to per-job outputs which are integrated at the post-parallel
barrier.

- Animation / IK: Skeletal pose evaluation, retargeting and inverse-kinematics;
  produces per-entity pose buffers.
- Particles: Per-system particle simulation producing GPU upload buffers or
  merged particle outputs.
- Visibility & Culling: Frustum/portal/BVH/occlusion culling to produce
  visibility sets or draw lists.
- LOD Selection & Impostors: Per-object LOD decision, impostor generation or
  selection for draw submission.
- Light Clustering / Tiled Culling: CPU-side light culling and cluster
  generation used by render passes.
- Material & Uniform Prep: Bake per-object dynamic uniform blocks, parameter
  packing, and descriptor staging.
- AI Batch Queries: Batched perception or pathfinding queries that operate on
  read-only world snapshot.
- GPU Upload Preparation: Populate staging buffers or copy regions that will be
  submitted to copy queue (writes into reserved allocations).
- Occlusion Query Reduction: Reduce occlusion query results from previous frames
  into usable occlusion bitsets.

## Barrier definitions

- B0 (Barrier_InputSnapshot) — Stable input snapshot and epoch maintenance.
  Ensures input sampling is complete and GPU/CPU epoch reclamation ran;
  upstream: OS/Input; downstream: NetworkReconciliation.
- B1 (Barrier_NetworkReconciled) — Server authoritative state applied. Ensures
  network reconciliation (server state application, client prediction
  rewind/replay) is complete before simulation; upstream: NetworkReconciliation;
  downstream: FixedSim/Gameplay.
- B2 (Barrier_SimulationComplete) — Deterministic physics/simulation completion.
  Ensures fixed-timestep physics and other deterministic updates are finished
  before gameplay; upstream: FixedSim; downstream: Gameplay/RandomSeed.
- B3 (Barrier_SceneStable) — Structural integrity / scene mutations applied.
  Ensures spawns, despawns, handle allocations, and structural scene edits are
  visible and stable before transform propagation; upstream:
  Gameplay/SceneMutation; downstream: Transforms.
- B4 (Barrier_SnapshotReady) — Transform propagation completed and immutable
  snapshot published. Ensures world transforms are finalized and an immutable
  frame snapshot is available to parallel readers; upstream:
  Transforms/Snapshot; downstream: Parallel tasks (B5).
- B5 (Barrier_ParallelComplete) — All parallel outputs ready (parallel barrier
  join). Ensures all Category B tasks (animation, culling, particles, LOD, etc.)
  have completed so the frame graph can be built.
- B6 (Barrier_CommandReady) — Command recording & submission readiness. Ensures
  valid command lists, resource states and submission ordering constraints are
  satisfied before present.
- B7 (Barrier_AsyncPublishReady) — Async pipeline publish readiness. Ensures
  multi-frame async pipelines produced ready resources which may be atomically
  published into resource registries on safe integration.

## Frame Context (precise definition)

FrameContext is the central coordination mechanism for the AsyncEngine's frame
execution model, implementing strict access control and phase-dependent mutation
restrictions to ensure deterministic execution across all subsystems.

### Design Philosophy

**Capability-Based Access Control**: Critical operations require EngineTag
capability tokens, preventing external modules from bypassing engine
coordination. Only engine-internal code can construct EngineTag instances via
the internal factory pattern.

**Phase-Dependent Validation**: Operations are restricted based on current
execution phase. Authoritative state mutations are only allowed during Category
A ordered phases, while parallel phases must use immutable snapshots
exclusively.

**Fail-Safe Defaults**: Invalid operations (wrong phase, missing capabilities)
are silently ignored rather than causing runtime errors, preventing disruption
of frame execution flow.

### Access Control Model

Three distinct data layers with different access patterns:

- **Immutable Layer**: Application-lifetime read-only data (engine config, asset
  registry, shader database)
- **EngineState Layer**: Engine-internal bookkeeping requiring EngineTag
  capability (graphics backend, fences, profiler, thread pool)
- **GameState Layer**: Cross-module authoritative data with phase-dependent
  mutation rules (entities, transforms, animation, particles, materials,
  physics, AI)

### Atomic Snapshot Architecture

**Dual Snapshot Design**: Two complementary snapshot types published atomically:

- **GameStateSnapshot**: Heavy application data storage layer - owns actual game
  data containers, provides thread-safe sharing via shared_ptr&lt;const&gt;,
  designed for module-level access to complete game state
- **FrameSnapshot**: Lightweight coordination access layer - contains efficient
  views (spans) into GameStateSnapshot data, optimized for parallel task
  consumption with Structure-of-Arrays layout

- **Double-Buffered Lock-Free Access**: Snapshot buffers are double-buffered
  with atomic index swapping, enabling zero-contention parallel access during
  snapshot transitions.

### Lifecycle Contract Enforcement

- **Monotonic Progression**: Frame indices and epochs advance strictly
  monotonically via EngineTag-protected operations, preventing accidental state
  jumps.

- **Phase Validation**: All mutation operations validate current execution phase
  against allowed operations, with detailed assertion points for development
  debugging.

- **Resource Lifecycle Coordination**: GPU fence values and epoch markers enable
  safe deferred resource reclamation coordinated with frame boundaries.

### Concurrent Access Strategy

- **Ordered Phase Pattern**: Single-threaded coordinator mutations during
  Category A phases with immediate consistency guarantees.

- **Parallel Phase Pattern**: Lock-free snapshot access during Category B phases
  - parallel tasks receive immutable views, write to private output buffers
  integrated at barriers.

- **Snapshot Publication**: Engine coordinator publishes unified snapshots
  atomically after ordered mutations, enabling immediate lock-free access by
  parallel workers.

### High-Level FrameContext Layout

```cpp
class FrameContext {
  // Application-lifetime immutable dependencies
  Immutable immutable_;           // config, assets, shader DB

  // Engine-managed state (EngineTag required)
  EngineState engine_state_;      // graphics, fences, profiler, surfaces

  // Cross-module authoritative game data
  GameState game_state_;          // entities, transforms, animation, physics

  // Lock-free snapshot distribution
  UnifiedSnapshot buffers_[2];    // double-buffered atomic snapshots
  atomic<uint32_t> visible_index_; // lock-free buffer selection

  // Performance and coordination metadata
  Metrics metrics_;               // timing, budget, adaptation stats
  shared_mutex snapshot_lock_;    // snapshot creation coordination
};
```

**Key Design Invariants**: GameState mutations only during ordered phases,
parallel access exclusively via snapshots, capability tokens for engine
operations, atomic snapshot publication with monotonic versioning.
