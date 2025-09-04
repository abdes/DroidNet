
# AsyncEngine execution model

This document summarizes the execution patterns implemented by the frame loop in
`AsyncEngineSimulator.cpp` and other files in `AsyncEngine` module. Each section
is dedicated to a specific pattern.

## Frame loop guarantees

- Macro-ordering: the frame loop invokes phases in a canonical sequence. The
  documented patterns assume that the frame loop drives this high-level order.
- Phase completion: the frame loop advances past a phase only after the phase
  has completed (either by returning or by an awaited coroutine completing).
- Snapshot & epoch semantics: snapshots intended for parallel readers are
  produced before parallel work starts; resource reclamation is gated by
  epoch/fence semantics to avoid use-after-free across CPU/GPU boundaries.
- Integration points: the frame loop exposes well-defined integration points
  (post-parallel, async-poll, etc.) where subsystems may safely publish or
  consume results.

## Potential enhancements (not yest implemented)

- Provide budget monitoring and fallback strategies (task reduction, LOD drop,
  defer non-critical async jobs) to protect frame latency.

## 1. Offloaded ordered background tasks (thread-pool + await)

### Purpose (scope)

This pattern covers work that is dispatched to background worker threads and
awaited by the calling phase so ordering is preserved while offloading heavy CPU
work.

### Pre-conditions (guarantees)

- Dispatch occurs from a coroutine or frame-thread phase that will await
  completion.
- Access target data only if it is immutable or protected by thread-safe APIs.
- Prepare and pin any snapshots required by background workers before dispatch.

### Post-conditions (expectations)

- Background work completes before the calling phase resumes.
- Results are published or copied into frame-owned outputs before continuing.
- Any locks held by the dispatcher are released before suspension points.

### Examples

- `co::ThreadPool::Run`, `SimulateWork`, `co_await`

### Applicable scenarios

- Pre-processing that affects later phases.
- Heavy AI or physics batch computations required this frame.
- Any CPU-bound task that must finish before command recording.

### Implementation notes / Recommendations

- Dispatch using thread-pool primitives; await completion from the phase.
- Use immutable snapshots or copy-on-write outputs for worker reads.
- Enforce per-task time budgets and provide cancellation tokens.

- Budgeting: instrument and deal with tasks that exceed configured per-frame
  time budget; provide fallback results.
- Ownership: returned results must be moved into `FrameContext` containers;
  avoid returning raw pointers to transient resources.
- Failure modes: background task failures must surface an error token to the
  awaiting phase and leave fallback state usable.

## 2. Module-driven parallel work

### Purpose (scope)

This pattern covers module-provided parallel coroutines that the simulator
schedules alongside its own jobs and synchronizes at the same barrier point.

### Pre-conditions (guarantees)

- Modules receive immutable snapshots or access only thread-safe module APIs.
- Modules must not reference frame-only mutable resources directly.
- Modules expose clear completion and lifetime semantics for published outputs.

### Post-conditions (expectations)

- Module parallel work completes before the shared AllOf barrier releases.
- Module outputs are published in a frame-safe manner (epoch-bound if needed).
- Modules do not hold frame-thread locks across suspension points.

### Examples

- Module coroutine APIs, renderer/physics module parallel tasks

### Applicable scenarios

- Renderer culling and GPU upload preparation.
- Physics broad-phase or asynchronous internal tasks.
- Any subsystem that parallelizes internal work required this frame.

### Implementation notes / Recommendations

- Standardize module contracts for snapshot access and output publication.
- Validate module-provided coroutines do not touch frame-only state.
- Require modules to support cancellation and per-frame budgeting.

Additional precise guidance:

- Module contract: modules must document required snapshot types, their expected lifetime, and allowed mutation patterns.
- API shape: module coroutines should accept a read-only FrameSnapshot and a FrameOutputs reference for result writes.
- Publication: modules publish results via coordinator-provided APIs that validate epoch/generation before committing.

## 3. Synchronous main-thread ordered phases (Category A)

### Purpose (scope)

This section describes phases that the frame loop executes on the main/frame
thread and which are intended to provide ordered, deterministic updates where
the frame-thread is the authority. It covers only the behavior, contracts, and
examples that are specific to these synchronous, frame-thread phases. Broader
frame-loop guarantees and parallel patterns are documented elsewhere.

### Pre-conditions (guarantees)

- Phase is entered on the frame/main thread.
- Any thread-shared data accessed by the phase is either immutable or protected
  by proper synchronization. The code performing the access must acquire the
  appropriate synchronization (owner API may do this on behalf of callers); any
  held locks MUST be released before any suspension point (co_await); prefer
  obtaining an immutable snapshot or using owner-provided thread-safe APIs
  instead of holding locks across awaits.
- If downstream parallel readers require an immutable snapshot, the snapshot's
  backing data structures are prepared and pinned for publication.
- Resources required by subsequent phases are allocated or have valid published
  handles.

### Post-conditions (expectations)

- Phase completion occurs on the frame/main thread.
- All authoritative state changes performed by the phase are visible and ordered
  for later phases.
- The snapshot (if required) is published before parallel readers start.
- Resources that the phase creates or publishes remain valid until consumers
  observe them (lifetime tied to epoch/fence semantics where applicable).

### Non-coroutine (void) examples

- `PhaseFrameStart()` — declared `void` in `AsyncEngineSimulator.h` and used to
  perform minimal, immediate per-frame initialization (timestamping, graphics
  BeginFrame). This function is intentionally synchronous and must be cheap.

### Coroutine-based phase examples

- Many phases are implemented as coroutines (`-> co::Co<>`) because they need to
  `co_await` module-provided coroutine entry points or offloaded work. Examples
  (from `AsyncEngineSimulator.h`): `PhaseInput`, `PhaseFixedSim`,
  `PhaseSceneMutation`, `PhaseSnapshot`, `PhasePostParallel`, `PhaseFrameGraph`,
  `PhaseDescriptorTablePublication`, `PhaseResourceStateTransitions`, and
  `PhaseCommandRecord`.

### Applicable scenarios (concise)

- Lightweight per-frame initialization (timestamps, counters, BeginFrame).
- Input sampling and immediate input-to-state updates.
- Deterministic, in-order simulation steps that must execute synchronously.
- Scene structural mutations that must complete before transform propagation.
- Snapshot creation and any direct state commits that must be visible to later
  phases.
- Descriptor publication and other short operations that serialize resource
  visibility for command recording.

### Implementation notes

- Keep `void` phases minimal. Use coroutine phases (`co::Co<>`) when you need to
  await background work, but ensure awaited coroutines finish before the phase
  returns to preserve ordering.
- Prefer offloading CPU-heavy tasks to the thread pool and `co_await` their
  completion from a coroutine phase instead of blocking the frame thread.

## 4. Parallel barriered coroutines (Category B)

### Purpose (scope)

This pattern covers spawning many read-only or snapshot-based coroutines that
run concurrently and synchronize at a barrier so the frame resumes only after
all finish.

### Pre-conditions (guarantees)

- Publish an immutable snapshot before spawning parallel coroutines.
- Ensure per-job outputs are pre-allocated and thread-safe to write.
- Avoid holding frame-thread locks across the barrier or suspension points.

### Post-conditions (expectations)

- All parallel coroutines complete before the frame resumes past the barrier.
- Per-job side-effects are integrated into frame state only after the barrier.
- Snapshots remain valid for the duration of parallel execution (epoch pinned).

### Examples

- `co::AllOf`, `parallel_specs_`, `SimulateWork`

### Applicable scenarios

- Pose evaluation, culling, and LOD selection.
- Particle and GPU data preparation that is read-only.
- Large AI or animation batch evaluations.

### Implementation notes / Recommendations

- Tune task granularity to balance load across workers.
- Record per-job outputs locally and merge after the barrier.
- Provide budget checks and fallback (reduce tasks or defer work).

Additional precise guidance:

- Snapshot semantics: snapshot creation must pin generation counters and any GPU-visible resource indices; the barrier must guarantee snapshot immutability until merge.
- Locking: prohibit acquisition of frame-thread locks inside worker coroutines; use lock-free or per-job local buffers.
- Integration: merging outputs must validate generation counters and perform atomic swaps where external handles are exposed.

## 5. Fire-and-forget async jobs (Category C)

### Purpose (scope)

This pattern covers long-running, multi-frame background jobs that are launched
without blocking the frame and are polled and integrated when they complete.

### Pre-conditions (guarantees)

- Launch jobs with explicit ownership and lifetime handles (`async_jobs_`).
- Ensure background jobs avoid referencing resources scheduled for reclamation.
- Provide cancellation tokens and priority metadata at job creation.

### Post-conditions (expectations)

- The frame loop polls job readiness without blocking frame progress.
- Completed job results are integrated at designated synchronization points.
- Jobs can be cancelled or deprioritized to protect frame latency.

### Examples

- `async_jobs_`, `TickAsyncJobs()`, module fire-and-forget calls

### Applicable scenarios

- Asset streaming and asynchronous shader compilation.
- BLAS/TLAS builds and GPU readbacks.
- Any multi-frame workload that must not block the frame.

### Implementation notes / Recommendations

- Track job ownership and attach cancellation tokens.
- Prioritize jobs and provide throttling to preserve frame budget.
- Integrate results only at safe integration points (post-parallel, async-poll).

Additional precise guidance:

- Handle validation: when integrating results, validate resource generation/version to avoid applying stale data.
- Backpressure: implement a bounded queue for enqueued async completions and drop or deprioritize low-priority completions under high load.
- Graceful shutdown: ensure outstanding async jobs are cancelled or completed on shutdown; provide a draining timeout.

## 6. Detached long-lived services (Category D)

### Purpose (scope)

This pattern covers detached, long-lived services that run independently of the
frame loop and provide monitoring, logging, or maintenance without blocking
frames.

### Pre-conditions (guarantees)

- Start services with well-defined message queues or channels for communication.
- Ensure services do not directly mutate frame-only state.
- Provide robust error handling and restart policies for services.

### Post-conditions (expectations)

- Services run independently and do not impede frame progress.
- Interactions occur via thread-safe channels or persisted logs.
- Service-side failures are isolated and do not crash the frame thread.

### Examples

- Logging threads, telemetry daemons, crash-reporting services

### Applicable scenarios

- Crash reporting and minidump collection.
- Telemetry aggregation and symbolication.
- Background analytics and periodic maintenance.

### Implementation notes / Recommendations

- Isolate side effects and restrict shared state to thread-safe queues.
- Persist critical logs and fail gracefully on service errors.
- Minimize synchronous handoffs to the frame thread.

Additional precise guidance:

- Communication: use lock-free MPSC queues for enqueuing logs/metrics; avoid blocking calls on the frame thread.
- Restart policy: implement supervisor behavior for services that crash; limit restart frequency.
- Resource usage: cap memory and file descriptors used by detached services to avoid affecting game process.

## 7. Background fire-and-forget module work (no barrier)

### Purpose (scope)

This pattern covers module-initiated background work that is started without
awaiting and is polled or observed across later frames for completion.

### Pre-conditions (guarantees)

- Start background work with explicit completion flags or handles.
- Ensure background tasks avoid referencing imminently reclaimed resources.
- Provide notification mechanisms for readiness (flags, callbacks, futures).

### Post-conditions (expectations)

- Frame continues while module work executes in the background.
- Completion is detected via polling or notification on later frames.
- Background outputs are integrated only when safe and lifetime guarantees hold.

### Examples

- `ExecutePresent`, `ExecuteAsyncWork`, module background tasks

### Applicable scenarios

- Presentation handoff and asynchronous GPU uploads.
- Background data uploads and asynchronous submissions.

### Implementation notes / Recommendations

- Add explicit completion APIs and robust lifetime handling.
- Provide cancellation and resource-ownership semantics for background work.
- Poll or notify at designated integration points to avoid races.

Additional precise guidance:

- Ownership model: background tasks must capture strong references to any resources they touch and release them only after publication or timeout.
- Notification: prefer future/promise or generation-tagged flags over raw booleans to avoid missed state transitions.
- Staleness: integrate only when handle generation matches the snapshot to avoid applying stale updates.
