# GPU Timestamp Profiling System for Oxygen.Engine

Date: March 21, 2026
Status: `implemented` (code complete 2026-03-21; focused profiler-unit validation complete, live D3D12 scene validation still in progress after zero-timestamp failure observed in DemoShell on 2026-03-21)
Owner: Renderer + Graphics
Scope: Engine-wide GPU timestamp profiling; D3D12 implementation target for v1

Cross-references (engine-wide):

- `src/Oxygen/Graphics/Common/Graphics.h`
- `src/Oxygen/Graphics/Common/Graphics.cpp`
- `src/Oxygen/Graphics/Common/CommandQueue.h`
- `src/Oxygen/Graphics/Common/GpuEventScope.h`
- `src/Oxygen/Renderer/Renderer.cpp`
- `src/Oxygen/Renderer/RenderContext.h`

Cross-references (D3D12 implementation, see Section 19):

- `src/Oxygen/Graphics/Direct3D12/CommandQueue.h`
- `src/Oxygen/Graphics/Direct3D12/CommandRecorder.h`

## 1. Purpose

Define an implementable, backend-agnostic GPU timestamp profiling system for Oxygen that:

1. Records hierarchical GPU scopes around arbitrary GPU workloads.
2. Uses one per-frame timestamp query pool of fixed capacity, provided by the backend.
3. Reuses all timestamp storage every frame (no multi-frame ring buffering).
4. Resolves all queries once per frame into CPU-readable tick storage, provided by the backend.
5. Relies on Oxygen's existing per-frame queue flush/fence model for validity.
6. Produces a complete per-frame hierarchical GPU timeline consumable by profiler UI and export.
7. Extends the existing `GpuEventScope` as the single engine-wide scope API for both debug markers and timestamps.

This document specifies the backend-agnostic system design and, in Section 19, the D3D12-specific implementation.
No implementation is claimed outside of Section 19 and the phased plan in Section 14.

A future Vulkan implementation would fulfill the same abstract contracts in Section 4–13 using Vulkan query pools and calibrated timestamps, without changing any renderer code.

## 1.1 Canonical Terms

This document uses one name per concept:

1. `scope`: one profiled begin/end region.
2. `scope_id`: integer id of a scope record in the current frame.
3. `query_slot`: integer position inside the per-frame timestamp query pool (backend-managed, fixed capacity).
4. `begin_query_slot`: query slot written at scope begin.
5. `end_query_slot`: query slot written at scope end.
6. `used_query_slots`: number of query slots written in the frame.
7. `boundary queries`: the two queries that define a scope duration (begin + end).

Naming rule:

1. We use `slot` for timestamp query positions.
2. We use `id` for logical object identity (scope, stream, parent).
3. We do not mix `index` and `slot` for query pool positions.

Scope query cardinality rule (v1):

1. One scope has exactly two boundary queries.
2. A scope does not own extra queries in v1.
3. If we need intermediate timing points later, they are modeled as child scopes or explicit checkpoint events, not as extra queries attached to one scope.

## 2. Existing Oxygen Anchors (Facts)

The design aligns to already-existing behavior and types:

1. `Graphics::BeginFrame(...)` calls `FlushCommandQueues()` before frame work, which CPU-signals and waits each queue via `CommandQueue::Flush()`.
2. `CommandQueue` already exposes queue fence operations (`Signal`, `Wait`, `GetCompletedValue`, `GetCurrentValue`).
3. D3D12-specific recorder escape hatch exists through `d3d12::CommandRecorder::GetD3D12CommandList()`.
4. Renderer already wraps pass/view execution with GPU debug scopes (`GpuEventScope`), giving a proven RAII instrumentation pattern.
5. Renderer frame orchestration has clear phase boundaries (`OnFrameStart`, `OnPreRender`, `OnRender`, `OnCompositing`, `OnFrameEnd`).

These are sufficient to implement flush-per-frame timestamp profiling without introducing additional in-flight-frame buffering.

## 3. Goals and Non-Goals

### 3.1 Goals

1. High-fidelity per-frame GPU timing tree.
2. Minimal runtime overhead when enabled and near-zero overhead when disabled.
3. Deterministic behavior under Oxygen's flush-per-frame model.
4. Clean failure behavior when query budget is exceeded.
5. Backend boundary discipline: renderer consumes a clear profiling interface; D3D12 details remain in graphics backend implementation.

### 3.2 Non-Goals

1. Cross-frame overlap analysis (not needed with flush-per-frame).
2. Cross-queue correlation (first version targets graphics queue timeline).
3. Vendor counter integration (PIX/NSight/etc.) in this phase.

## 4. Functional Requirement Coverage

### FR1: GPU Profiling Scopes (hierarchical)

Design:

1. Reuse the existing engine-wide RAII scope helper (`GpuEventScope`) as the canonical scope primitive.
2. `GpuEventScope` emits debug events and, when enabled, timestamp begin/end through the same recorder API.
3. Each scope records exactly two boundary queries when timestamp mode is enabled:
   - one query at begin,
   - one query at end.
4. CPU side tracks:
   - `scope_name` (string_view hashed to `scope_name_hash`, optional retained display string)
   - `begin_query_slot`
   - `end_query_slot`
   - `parent_scope_id` (or `kInvalidScope` for root)
   - `stream_id` (command stream/recorder identity)

### FR2: Timestamp Query Allocation

Design:

1. One per-frame timestamp query pool of fixed capacity, owned and managed by the backend.
   - Dedicated to timestamp queries only; not shared with descriptor binding, resource allocation, or any other subsystem.
   - Opaque to the renderer; the renderer never holds a direct reference to it.
2. One CPU-readable tick buffer, also owned by the backend.
   - Populated by the backend's single bulk-resolve command at frame tail.
   - Persistently accessible (no per-frame map/unmap); the backend exposes a typed read-only span to the profiler.
3. Fixed config: `max_scopes_per_frame` and derived `max_queries_per_frame = max_scopes_per_frame * 2`.
4. Single-frame storage reused every frame; no ring of query pools or tick buffers.
5. Frame allocator is a monotonic counter reset at frame start once previous frame validity is guaranteed by fence/flush.

### FR3: Timestamp Recording

Design:

1. Begin scope: write a timestamp at `begin_query_slot` into the query pool.
2. End scope: write a timestamp at `end_query_slot` into the query pool.
3. Recording is issued inline in the command stream at the exact point of the scope boundary.
4. Ordering guarantee: timestamps are ordered in the command stream at the point they are issued, providing bottom-of-pipe boundary measurements for queue timeline profiling.
5. The backend implements the actual write; the recorder calls its virtual profile-scope API. See Section 19.3 for the D3D12 implementation.

### FR4: Timestamp Resolution

Design:

1. At frame command-recording tail, issue one bulk-resolve command covering `[0, used_query_slots)` — **only if `used_query_slots > 0`**.
   - If no scopes were recorded in this frame, skip the resolve command entirely; no GPU work is emitted.
2. Destination: the backend's CPU-readable tick buffer, persistently accessible for the backend's lifetime.
3. The resolve command is recorded after all scoped command lists for the frame have been submitted; it is enqueued last on the same queue.
4. The backend records and submits the resolve; the renderer calls the engine-level `ResolveTimestamps` API. See Section 19.4 for the D3D12 implementation.

### FR5: GPU Synchronization

Design:

1. Primary validity model: existing engine per-frame flush/fence.
2. Record `frame_resolve_fence_value` at frame finalization (`queue->Signal()` after resolve submission).
3. Data valid when `queue->GetCompletedValue() >= frame_resolve_fence_value`.
4. Under current engine contract (flush before next frame), this condition is expected true at or before next frame start.

### FR6: CPU Readback

Design:

1. CPU readback (reading the tick buffer and building the timeline frame) is **demand-driven**: it is only performed when at least one active sink is registered.
   - If no sinks are registered, the fence is still validated (to keep the validity state current) but the tick buffer is not read and no `GpuTimelineFrame` is constructed.
2. Once fence-valid and at least one sink is active, the profiler reads resolved tick values directly from the backend's CPU-accessible tick buffer.
3. No extra map/unmap and no extra CPU synchronization primitives are needed after fence validation.

### FR7: Timestamp Interpretation

Design:

1. Cache queue timestamp frequency `freq_hz` obtained from the backend via `CommandQueue::TryGetTimestampFrequency`.
2. Convert ticks to milliseconds:

$$
t_{ms} = \frac{ticks \times 1000.0}{freq_{hz}}
$$

1. For each scope:
   - `start_ms = ticks_to_ms(start_tick)`
   - `end_ms = ticks_to_ms(end_tick)`
   - `duration_ms = max(0, end_ms - start_ms)`

### FR8: Profiler Integration

Design:

1. Emit per-frame `GpuTimelineFrame` containing hierarchical scope nodes — **only when at least one sink is active**.

   - A sink is any registered consumer of timeline data: a profiler UI panel, an in-progress CSV/JSON export, or an automated test listener.
   - The profiler service maintains a count of active sinks. When count reaches zero, CPU readback and timeline construction are suppressed.
   - Sink registration/deregistration is explicit: UI open/close, export start/end, test setup/teardown.

2. Include enough info for:

   - tree rendering (parent/children)
   - timeline bars (start/end relative to frame root)
   - per-scope metrics (duration, inclusive duration, depth)

3. Provide optional export (CSV/JSON) of resolved frame timing data.

### FR9: Error Handling and Limits

Design:

1. On query budget overflow for frame N:

   - stop allocating new scope queries,
   - mark frame profiling state `overflowed = true`,
   - disable profiling for remainder of frame N,
   - report diagnostic once per frame.

2. Incomplete scope handling:

   - any scope with missing end query is marked invalid and excluded from timeline duration aggregation,
   - subtree remains structurally intact for diagnostics but not counted as valid timing.

### FR10: Runtime Control

Design:

1. Runtime toggle cvar (proposed): `rndr.gpu_timestamps`.
2. **Disabled** (`rndr.gpu_timestamps = false`) path must not:
   - allocate scope entries,
   - write timestamp queries,
   - issue resolve.
   - This is the true zero-overhead state (single branch at scope entry).
3. **Enabled but no active sinks** is a distinct middle state with non-zero GPU overhead:
   - Scope writes and resolve still occur so data is available the moment a sink registers.
   - CPU readback and `GpuTimelineFrame` construction are suppressed (no sinks = no consumers).
   - The GPU cost is 2 × N timestamp writes + 1 `ResolveQueryData` per frame; this is acceptable and intentional.
4. Transition behavior:
   - disabling mid-frame applies immediately to new scopes,
   - scopes opened while enabled will still receive their end timestamp from the RAII destructor; no force-close is triggered by mid-frame disable,
   - frame output marked disabled/partial accordingly.
   - a sink registering mid-frame will see data from the next complete frame.

## 5. Proposed Architecture

## 5.1 Components

### A) Engine-wide profiling scope contract

Proposed: keep `graphics::GpuEventScope` in Graphics Common as the only scope helper used by render code.

Responsibilities:

1. Begin one logical profile scope in constructor, end in destructor.
2. Route to recorder profile APIs that can emit debug markers and optional timestamps.
3. Keep call sites stable regardless of backend or profiling mode.

### B) Renderer-facing timeline service

Proposed: `renderer::profiling::GpuTimelineProfiler` (renderer-owned service).

Responsibilities:

1. Scope stack management (hierarchy bookkeeping).
2. Per-frame query-slot allocation and overflow policy.
3. Lifecycle hooks:
   - `OnFrameStart(frame_seq, frame_slot)`
   - `OnScopeBegin(recorder, name)`
   - `OnScopeEnd(recorder, scope_token)`
   - `OnFrameRecordTailResolve(recorder_or_internal_submit)`
   - `TryBuildTimelineIfReady()`
4. Conversion to profiler DTO (`GpuTimelineFrame`).
5. Own enable/disable policy and publish diagnostics.

### C) Timestamp query backend (implementation boundary)

Concept: a backend-owned component that owns:

1. The per-frame timestamp query pool (fixed capacity, reused each frame).
2. The CPU-readable tick buffer (persistently accessible, populated by the bulk-resolve command).
3. The timestamp write logic (called by the recorder's virtual profile-scope methods).
4. The single bulk-resolve command per frame.
5. Queue frequency for tick-to-millisecond conversion.

The backend is opaque to both the renderer and `CommandRecorder`. The recorder calls virtual profile-scope methods; the backend implementation performs the actual query writes using its own resources.

For v1, `graphics::d3d12::TimestampQueryBackend` is the D3D12 implementation. A future `graphics::vk::TimestampQueryBackend` would fulfill the same contract using Vulkan query pools and calibrated timestamps. See Section 19 for the D3D12 implementation design.

### D) Unified recorder profile API

`GpuEventScopeOptions` (defined in `Graphics/Common/GpuEventScope.h`, engine-wide):

```cpp
struct GpuEventScopeOptions {
  bool timestamp_enabled = false; // request timestamp boundary writes for this scope
  // reserved for future: marker color, category tag, etc.
};
```

Proposed additions in `CommandRecorder`:

1. `virtual auto BeginProfileScope(std::string_view name, const GpuEventScopeOptions& opts) -> GpuEventScopeToken;`
2. `virtual auto EndProfileScope(const GpuEventScopeToken& token) -> void;`

Behavior:

1. Debug events are always emitted when marker mode is enabled.
2. Timestamp writes are emitted only when `opts.timestamp_enabled` is true.
3. One scope call path serves both concerns and avoids disconnected systems.

Compatibility rule:

1. `GpuEventScope` is retained, not discarded.
2. Existing call sites remain valid and gain timestamp support through backend/profile configuration.
3. No second public scope type is introduced in renderer code.

## 5.2 Data Model

```cpp
struct GpuScopeRecord {
  uint64_t scope_name_hash;
  // display_name is an interned pointer into a per-frame string arena
  // (deduplicated by scope_name_hash). It is NEVER a view into a transient
  // string; all call sites MUST pass a string literal or an arena-resident
  // pointer. Passing fmt::format() or any temporary string is disallowed.
  const char* display_name;      // non-owning, arena-backed; nullptr if not retained
  uint32_t parent_scope_id;      // kInvalidScope for root
  uint32_t begin_query_slot;
  uint32_t end_query_slot;
  uint16_t depth;
  uint16_t stream_id;
  uint8_t flags;
  // flags field bits:
  //   bit 0: complete     — end query slot was written (scope was properly closed)
  //   bit 1: valid        — complete && begin_tick <= end_tick
  //   bits 2-7: reserved  — must be zero; do not use without updating this spec
};

struct GpuEventScopeToken {
  uint32_t scope_id;
  uint16_t stream_id;
  uint8_t flags;
  // flags field bits:
  //   bit 0: timestamp_enabled — timestamp queries were allocated for this scope
  //   bit 1: overflow_at_creation — scope was opened after budget overflow; no slot allocated
  //   bits 2-7: reserved — must be zero
  // GpuEventScopeToken is 8 bytes total (+ 1 byte padding); safe to store in GpuEventScope stack object.
};

struct GpuFrameCapture {
  uint64_t frame_sequence;
  uint64_t resolve_fence_value;  // queue fence value signalled after resolve submission
  uint32_t used_query_slots;
  bool profiling_enabled;
  bool overflowed;
  // scopes is pre-reserved to max_scopes_per_frame at GpuTimelineProfiler construction
  // and .clear()'d (not destructed) at each frame reset to avoid per-frame allocations.
  // No scope record is appended after overflowed becomes true.
  std::vector<GpuScopeRecord> scopes;
};

struct GpuTimelineNode {
  // Identity and hierarchy
  uint32_t scope_id;             // index into GpuFrameCapture::scopes for cross-reference
  uint32_t parent_scope_id;      // kInvalidScope for root
  std::vector<uint32_t> child_scope_ids; // pre-built child list; avoids re-walking at render time
  // Naming
  uint64_t name_hash;
  const char* display_name;      // same arena-backed pointer as GpuScopeRecord::display_name
  // Timing (relative to frame root start)
  float start_ms;
  float end_ms;
  float duration_ms;
  // Classification
  uint16_t depth;
  uint16_t stream_id;            // for per-stream timeline lane rendering
  bool valid;
};
```

**String arena invariant:** `GpuTimelineProfiler` owns a per-frame flat string arena that is cleared (not freed) at frame reset. Scope names are inserted once per unique hash and never copied again. All `display_name` pointers in `GpuScopeRecord` and `GpuTimelineNode` point into this arena. This eliminates dangling-view risk without per-scope heap allocation.

## 5.3 Query Budget Math

Given configured `max_scopes_per_frame = S`:

1. `max_queries_per_frame = 2 * S`
2. Readback size bytes:

$$
readback\_bytes = max\_queries\_per\_frame \times sizeof(uint64\_t)
$$

Example:

1. `S = 4096`
2. `max_queries = 8192`
3. `readback = 64 KiB`

## 6. Frame Lifecycle Integration in Oxygen

## 6.1 OnFrameStart

Decision tree (in order):

1. **Profiling disabled?** → skip all readback work; discard previous `GpuFrameCapture`; go to reset.
2. **`used_query_slots == 0` for previous frame?** → no resolve was issued; discard previous `GpuFrameCapture`; go to reset.
3. **Fence check** — evaluate `queue->GetCompletedValue() >= frame_resolve_fence_value` once and cache the result.
   - If **not complete**: this must not happen under the flush-per-frame contract; log a `gpu.timestamp.fence_miss` diagnostic; discard previous `GpuFrameCapture`; go to reset.
4. **No active sinks?** → fence is already confirmed complete (step 3); skip tick-buffer read and `GpuTimelineFrame` construction; discard previous `GpuFrameCapture`; go to reset.
5. **Fence complete + sinks active** → read tick buffer, build `GpuTimelineFrame`, publish to all registered sinks; the previous `GpuFrameCapture` is consumed and its `scopes` vector cleared (not freed).
6. **Reset:** clear `scopes` vector (reuse capacity), reset per-frame string arena, reset scope stack, set `used_query_slots = 0`.

Key invariants:

- The fence is checked **exactly once** in step 3; steps 4 and 5 use the cached result — no redundant `GetCompletedValue()` calls.
- `GpuFrameCapture` is either published to sinks (step 5) or explicitly discarded (steps 1–4) before reset — a stale capture never survives across frame boundaries.
- `BeginFrame()` calls `FlushCommandQueues()` before `OnFrameStart` runs, so step 3 is expected never to trigger in normal operation.

## 6.2 During OnRender / OnCompositing

Instrumentation points:

1. View-level scope around each view render loop.
2. Pass-level scopes in each pass (`PrepareResources`, `Execute`) if enabled.
3. Optional sub-scopes around expensive draw/dispatch blocks.

All instrumentation uses the existing `GpuEventScope` only. No separate timestamp-only scope is introduced.

## 6.3 End of frame recording

1. After all frame command lists have been recorded/submitted on graphics queue, **if `used_query_slots > 0`**, issue one bulk-resolve command covering `[0, used_query_slots)` by calling `TimestampQueryBackend::RecordResolve(recorder, used_query_slots)` (see §7.1 and §19.4).
2. Signal queue and store `resolve_fence_value` (always — the fence is used to track whether a resolve was issued this frame).
3. Persist capture metadata (`used_query_slots`, scope table, overflow flag).

The D3D12-specific resolve call (`ResolveQueryData`) is described in Section 19.4.

## 6.4 OnFrameEnd

1. Final bookkeeping only.
2. No CPU blocking wait required here; next frame start/flush model guarantees readiness.

## 7. API and Ownership Changes (Proposed)

## 7.1 Graphics API additions

To keep renderer backend-agnostic while still D3D12-specific in implementation:

1. `CommandQueue`:
   - `virtual auto TryGetTimestampFrequency(uint64_t& out_hz) const -> bool;`
   - Returns `true` and populates `out_hz` if the queue supports timestamp queries; returns `false` for backends/queues that do not (e.g., headless, copy-only queues).

2. `CommandRecorder`:
   - `virtual auto BeginProfileScope(std::string_view name, const GpuEventScopeOptions& opts) -> GpuEventScopeToken;`
   - `virtual auto EndProfileScope(const GpuEventScopeToken& token) -> void;`

3. `TimestampQueryBackend` (backend-owned, **not** on `CommandRecorder`):
   - `auto RecordResolve(CommandRecorder& recorder, uint32_t used_query_slots) -> bool;`
   - Called by `GpuTimelineProfiler::OnFrameRecordTailResolve()`.
   - The backend uses its own internally-held query heap and readback buffer — the renderer never touches or sees these resources.
   - The D3D12 implementation records `ResolveQueryData` onto the provided recorder's command list.
   - Returns `false` on failure (triggers §9.3 resolve-failure path).

**Why `ResolveTimestamps` is NOT on `CommandRecorder`:** The resolve operation requires the query heap and the readback `ID3D12Resource`, both of which are owned by `TimestampQueryBackend` and must remain opaque to the renderer. Placing resolve on the recorder would force the renderer to hold and pass backend-owned resource handles, violating the backend opacity requirement. Vulkan's `vkCmdCopyQueryPoolResults` has the same constraint — query pool and destination buffer are backend-internal — confirming this boundary is necessary for future backend portability.

D3D12 overrides of `BeginProfileScope`/`EndProfileScope` perform real timestamp writes in one recorder path.
Headless/other backends can keep debug-event behavior and return timestamp-disabled tokens (with `bit 0 = 0`).

Required architecture rule: renderer code never casts recorders to backend types for profiling behavior.

## 7.2 Renderer integration points

1. Renderer owns profiler service lifetime.
2. Hook points:
   - `Renderer::OnFrameStart` -> read previous frame + reset allocator.
   - `Renderer::OnRender` / `Renderer::OnCompositing` -> scope instrumentation.
   - Frame tail before completion -> resolve + fence stamp.
3. Timeline product exposed through renderer stats/debug interfaces.
4. Renderer passes use `GpuEventScope` uniformly; they do not call low-level timestamp APIs directly.

## 8. Hierarchy and Timeline Construction

Algorithm:

1. During command recording:
   - Maintain a CPU stack of active scope ids **per stream** (one stack per `stream_id`).
   - `parent_scope_id = stack.top()` (or `kInvalidScope` if stack is empty) at begin.
   - Push on begin, pop on end.
   - Allocate exactly two query slots per scope (begin/end) from the shared atomic slot counter.
   - v1 has one stream; the stack degenerates to a single list with no synchronization needed.
2. During readback:
   - The readback loop is **index-driven**: for each `GpuScopeRecord`, read `ticks[scope.begin_query_slot]` and `ticks[scope.end_query_slot]`. It does **not** iterate slots in sequential order.
   - This is safe even when multiple streams interleave their slot allocations (query slots from different streams are interleaved in the query heap but each scope still references its own pair by index).
   - Invalidate scope if `end_tick < begin_tick` or `complete` flag is not set (incomplete scope).
   - Convert valid ticks to ms and normalize to frame origin (`frame_root_start_ms = ticks_to_ms(frame_root_begin_tick)`).
3. Build child lists from parent indices to populate `GpuTimelineNode::child_scope_ids`.

Output is a `std::vector<GpuTimelineNode>` (tree structure with pre-built child lists) plus the flat `GpuFrameCapture::scopes` array for direct CSV/JSON export.

## 9. Overflow, Invalidity, and Diagnostics

## 9.1 Overflow policy

At scope begin, **before** any allocation, check whether two free slots are available:

```text
// next_free_slot is the monotonic counter (index of the next unallocated slot)
if (next_free_slot + 2 > max_queries_per_frame) {
    overflowed = true;
    // stop appending to scopes; return overflow token
}
being_query_slot = next_free_slot++;
end_query_slot   = next_free_slot++;
```

Each scope consumes **exactly two** slots. The check reserves both atomically: `next_free_slot + 2 > max_queries_per_frame` is the correct gate (equivalently `next_free_slot + 1 >= max_queries_per_frame` for unsigned integers, but the `+2` form is unambiguous and preferred).

On overflow:

1. Set `GpuFrameCapture::overflowed = true`.
2. **Stop appending** new `GpuScopeRecord` entries; the `scopes` vector is frozen.
3. Return a token with `bit 1 (overflow_at_creation) = 1`; the caller's RAII destructor skips the end-query write when it sees this bit.
4. Emit diagnostic once per frame:
   - code: `gpu.timestamp.overflow`
   - payload: frame_seq, used_query_slots, max_queries_per_frame

## 9.2 Incomplete scopes

If scope stack non-empty at frame end:

1. Force-close bookkeeping state on CPU (pop remaining stack entries).
2. Mark affected scopes `complete = false` (bit 0 = 0), `valid = false` (bit 1 = 0).
3. Emit `gpu.timestamp.incomplete_scope` diagnostic.

**Mid-frame disable invariant:** If `rndr.gpu_timestamps` is set to `false` mid-frame, any scope opened while enabled still has a valid begin slot. Its RAII destructor will still execute `EndProfileScope`; since the token has `timestamp_enabled = true` (bit 0), the end query is written. The scope is therefore complete and valid. No force-close is triggered by a mid-frame disable — only by an unmatched begin with no destructor run.

## 9.3 Resolve failures

On failure to write resolve command:

1. mark frame capture invalid
2. skip readback conversion for that frame
3. keep system enabled for subsequent frames unless repeated failures exceed threshold.

## 10. Runtime Control and UX

Proposed cvars/commands:

1. `rndr.gpu_timestamps` (bool, default false)
2. `rndr.gpu_timestamps.max_scopes` (int, default 4096)
3. `rndr.gpu_timestamps.export_next_frame <path>` (one-shot)

Profiler UI expectations:

1. Tree view of scopes with inclusive duration.
2. Timeline bars (start offset + duration).
3. Warning badge on overflow/invalid frames.

## 11. Export Format (Offline Analysis)

Export is a first-class consumption path. A running export is an active sink (see FR8); it registers with `GpuTimelineProfiler` at export-start and deregisters when complete or cancelled.

The one-shot trigger is `rndr.gpu_timestamps.export_next_frame <path>`. This registers a single-frame export sink; after one `GpuTimelineFrame` is consumed and written to disk, the sink automatically deregisters.

### 11.1 Threading model

Export I/O must not block the render thread. The profiler pushes a completed `GpuTimelineFrame` into a bounded single-producer/single-consumer queue serviced by a dedicated export worker thread. The worker thread is started when the first export sink registers and stopped when the last deregisters.

**Backpressure policy by sink type:**

- **Continuous / multi-frame sinks** (e.g., profiler UI capturing many frames): if the queue is full (worker is behind), the frame is dropped and a `gpu.timestamp.export_drop` diagnostic is emitted. Dropping preserves render-thread responsiveness.
- **One-shot sinks** (`export_next_frame`): the user explicitly requested exactly one frame. A drop would silently fail the export. Instead, the one-shot sink uses a **retry strategy**: if the push fails this frame (queue full), the sink remains registered and retries on the next frame until a frame is successfully enqueued. A `gpu.timestamp.export_retry` diagnostic is emitted per skipped frame. The retry count is bounded (max 16 frames); after exhaustion the sink deregisters and emits `gpu.timestamp.export_failed`.

### 11.2 CSV format

One row per scope per frame. Suitable for spreadsheet ingestion and scripted analysis.

Columns (in this order):

```text
frame_seq, scope_id, parent_scope_id, depth, stream_id, name_hash, name,
start_ms, end_ms, duration_ms, valid, flags
```

- `name` is the arena-backed `display_name` if available; empty string otherwise.
- `flags` is the raw `uint8_t` flags byte from `GpuScopeRecord`.
- One header row with `# timestamp_freq_hz=<value>` as the first line (metadata comment) so offline tools can re-derive raw ticks from millisecond values.
- Then the column header row, then one data row per scope in `scope_id` order.
- One file per captured frame, or all frames appended into one file depending on trigger mode.

### 11.3 JSON format

Full-fidelity single-frame document. Suitable for tooling, automated tests, and visualization.

Top-level schema:

```json
{
  "version": 1,
  "frame_seq": <uint64>,
  "timestamp_freq_hz": <uint64>,
  "profiling_enabled": <bool>,
  "overflowed": <bool>,
  "used_query_slots": <uint32>,
  "scopes": [
    {
      "scope_id": <uint32>,
      "parent_scope_id": <uint32>,
      "name_hash": <uint64>,
      "name": <string>,
      "depth": <uint16>,
      "stream_id": <uint16>,
      "begin_query_slot": <uint32>,
      "end_query_slot": <uint32>,
      "start_ms": <float>,
      "end_ms": <float>,
      "duration_ms": <float>,
      "valid": <bool>,
      "flags": <uint8>
    }
  ],
  "diagnostics": [
    {
      "code": <string>,
      "payload": <object>
    }
  ]
}
```

- `scopes` is a flat array in `scope_id` order; hierarchy is reconstructed from `parent_scope_id`.
- `valid` is an explicit boolean (not just derivable from `flags`) for automated test assertions and direct filter expressions.
- `diagnostics` carries any overflow, incomplete-scope, or resolve-failure events for this frame.
- `timestamp_freq_hz` is included so offline tools can re-derive raw tick values from millisecond values if needed.

## 12. Performance Characteristics

Three operating states with distinct overhead profiles:

| State | GPU cost | CPU cost |
| --- | --- | --- |
| **Disabled** (`rndr.gpu_timestamps = false`) | zero (no query writes, no resolve) | one branch per scope entry |
| **Enabled, no active sinks** | 2 `EndQuery` per scope + 1 `ResolveQueryData` per frame | scope allocation + hierarchy bookkeeping; no tick-buffer read, no DTO construction |
| **Enabled, sinks active** | same as above | same as above + O(N) tick-buffer read, ms conversion, tree build, zero-copy DTO push |

The "enabled, no sinks" state is the intended warm-standby state: GPU profiling data is always current so a newly registered sink sees the next complete frame without a warm-up gap.

Memory footprint is bounded and small: query pool (64 KiB at S=4096) + readback buffer (64 KiB) + CPU metadata vectors (pre-reserved, no per-frame allocation).

## 13. Threading and Correctness Notes

1. Scope allocator and hierarchy bookkeeping are frame-local and should be recorder-thread safe if command recording becomes parallel.
2. If multiple recording threads are used, each recorder stream gets its own scope stack and `stream_id`, while sharing a lock-free/atomic query index allocator through the same profiler service.
3. All timestamp writes and resolve must target the same queue used for workload timing (graphics queue for phase 1).
4. Scope lifetime is RAII-enforced through one tokenized begin/end API to prevent mismatched event/timestamp boundaries.

## 14. Implementation Plan (Phased)

Legend: `[ ]` pending | `[~]` in progress | `[x]` done

### Phase A: Core timestamp backend plumbing

1. [x] Implement `d3d12::TimestampQueryBackend`: query pool + CPU-readable tick buffer (see Section 19.1, 19.2).
2. [x] Implement queue frequency retrieval (`CommandQueue::TryGetTimestampFrequency` + D3D12 override, see Section 19.5).
3. [x] Add unified recorder scope methods (`BeginProfileScope` / `EndProfileScope`) and resolve support (see Section 19.3, 19.4).

Exit criteria: A single `GpuEventScope` emits both debug marker boundaries and valid timestamp duration.

### Phase B: Renderer integration and hierarchy

1. [x] Add `GpuTimelineProfiler` service to renderer.
2. [x] Extend existing `GpuEventScope` usage in renderer passes so profiling is enabled through the same scope calls.
3. [x] Add frame-tail resolve and fence capture.

Exit criteria: Per-frame hierarchy appears for views/passes without overflow at default budget using one scope API.

### Phase C: UI and export

1. [x] Integrate timeline/tree into profiler/debug UI.
2. [x] Add one-shot export command (CSV/JSON).
3. [x] Add overflow/incomplete diagnostics surfacing.

Exit criteria: Exported frame data matches on-screen hierarchy and timing metrics.

### Phase D: Hardening

1. [x] Add validation tests for budget overflow, incomplete scope handling, and disabled fast-path.
2. [x] Add stress test with many nested scopes.
3. [x] Add docs for instrumentation conventions.

Exit criteria: Stable operation across sustained frame runs with profiling enabled.
Current status: focused compile/unit/profiler validation is complete; live D3D12 scene validation is not complete because DemoShell currently reproduces an all-zero timestamp presentation failure. Scene-level runtime overhead characterization was not executed in this iteration.

## 15. Validation Strategy

## 15.1 Functional validation

1. Verify start/end/duration per scope are monotonic and non-negative.
2. Verify parent-child relationships for nested scopes.
3. Verify overflow frame reports and disables remainder of frame instrumentation.
4. Verify disabling profiler emits zero query writes/resolves.

## 15.2 Integration validation

1. Confirm no additional frame latency introduced beyond existing flush model.
2. Confirm mapped readback access is fence-gated and race-free.
3. Confirm no corruption when scopes are imbalanced (error path).

## 15.3 Performance validation

1. Measure overhead with profiling off and on across representative scenes.
2. Validate default query budget for worst expected scope depth/width.

## 15.4 Validation Evidence (2026-03-21)

Build / compile evidence:

1. `cmake --build out/build-ninja --target Oxygen.Renderer.GpuTimelineProfiler.Tests --parallel 8`
   - Passed; rebuilt `oxygen-graphics-common`, `oxygen-engine`, `oxygen-renderer`, and the dedicated test binary.
2. `cmake --build out/build-ninja --target oxygen-graphics-direct3d12 --parallel 8`
   - Passed; compiled and linked the D3D12 timestamp backend integration (`TimestampQueryBackend.cpp`, `Graphics.cpp`, `CommandQueue.cpp`).

Focused test evidence:

1. `ctest --test-dir out/build-ninja -C Debug --output-on-failure -R "Oxygen.Renderer.GpuTimelineProfiler.Tests"`
   - Passed.
2. Covered behaviors:
   - disabled fast path emits zero timestamp writes/resolves,
   - nested scope hierarchy publishes valid parent/child structure,
   - retained latest-frame path publishes timeline data for the live viewer,
   - overflow emits a diagnostic and stops further scope allocation,
   - incomplete scope is marked invalid and diagnosed,
   - one-shot JSON export writes the resolved frame,
   - deeper nesting preserves the expected parent chain and query budget accounting.
3. Existing automated non-zero timing evidence is limited to the synthetic profiler test backend, where `GpuTimelineProfiler_test.cpp` uses incrementing fake tick values and asserts positive scope durations. This does not validate the live D3D12 query/resolve path.

Remaining validation delta:

1. No representative-scene runtime perf capture was run in-editor or in example apps.
2. No GPU-driver/runtime capture tool validation (PIX/NSight) was run in this iteration.
3. Live DemoShell validation on 2026-03-21 exposed a zero-timestamp failure in the D3D12 runtime path, so end-to-end timestamp correctness remains open.

## 15.5 Instrumentation Conventions

1. Use `graphics::GpuEventScope` as the only renderer-facing GPU scope primitive; do not add a parallel timestamp-only RAII type.
2. Renderer call sites should obtain options from `Renderer::MakeGpuEventScopeOptions()` so timestamp enablement remains centralized.
3. Scope names must be stable, human-readable labels for pass/view/subsystem work; prefer literals or long-lived strings over transient formatted temporaries.
4. New backend-specific profiling behavior must stay behind `CommandRecorder`, `CommandQueue`, and `TimestampQueryProvider` interfaces; renderer code must not cast to D3D12 types for profiling.

## 16. Risks and Mitigations

1. Risk: Query budget too small in debug-heavy frames.
   Mitigation: configurable budget + explicit overflow diagnostics.

2. Risk: Renderer/backend boundary erosion through backend-specific casts.
   Mitigation: enforce unified `CommandRecorder` profile-scope API and prohibit direct backend casts in profiling code.

3. Risk: Partial/incomplete scope instrumentation from exceptions/early returns.
   Mitigation: strict RAII scopes and frame-end incomplete-scope sanitation.

4. Risk: Multiple command streams complicate hierarchy readability.
   Mitigation: include `stream_id` and optionally render per-stream lanes in timeline.

## 17. Acceptance Criteria

This design is accepted for implementation when:

1. All FR1-FR10 mappings above remain satisfied without multi-frame buffering.
2. Design keeps ownership boundaries clean (Renderer consumes one engine-wide scope API and service, D3D12 owns query mechanics).
3. Overflow and invalid-scope paths are explicit and testable.
4. Runtime toggle guarantees zero timestamp GPU work when disabled.
5. No parallel disconnected profiling scope system exists in renderer code.

## 18. Existing Scope Fate (Explicit)

`GpuEventScope` remains the engine scope type.

1. It is not removed.
2. It is not replaced at call sites by a second scope class.
3. It is extended so one RAII scope can drive both GPU event markers and optional timestamp capture.
4. Existing renderer code that already uses `GpuEventScope` remains the primary instrumentation path.

### 18.1 Proposed GpuEventScope modification

The modification replaces the `BeginEvent`/`EndEvent` calls with `BeginProfileScope`/`EndProfileScope` and stores the returned token:

```cpp
class GpuEventScope {
public:
  explicit GpuEventScope(CommandRecorder& recorder, std::string_view name,
                         const GpuEventScopeOptions& opts = {})
    : recorder_(&recorder)
    , token_(recorder_->BeginProfileScope(name, opts))  // replaces BeginEvent + optional timestamp
  {}

  ~GpuEventScope()
  {
    if (recorder_ != nullptr) {
      recorder_->EndProfileScope(token_);  // replaces EndEvent + optional timestamp
    }
  }

  OXYGEN_MAKE_NON_COPYABLE(GpuEventScope)
  OXYGEN_MAKE_NON_MOVABLE(GpuEventScope)

private:
  CommandRecorder* recorder_;
  GpuEventScopeToken token_;  // 8 bytes; fits on the stack — no heap allocation
};
```

Existing call sites with no `opts` argument receive a default-constructed `GpuEventScopeOptions{ .timestamp_enabled = false }`, so the behavior is identical to the current `BeginEvent`/`EndEvent` path until the profiler explicitly sets `timestamp_enabled = true` through the options.

The `GpuEventScopeToken` is 8 bytes (4 + 2 + 1 + 1 padding), safe to store as a stack member.

---

## 19. D3D12 Implementation Design

This section is the only place D3D12 API types and calls appear in this document.
Everything above Sections 1–18 is backend-agnostic and applies equally to a future Vulkan implementation.

### 19.1 Query pool → `ID3D12QueryHeap`

- D3D12 type: `ID3D12QueryHeap`, created with `D3D12_QUERY_HEAP_DESC { Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP, Count = max_queries_per_frame }`.
- This is **not** a descriptor heap. It is **not** the bindless resource heap. It is **not** a memory allocator heap.
  It is a dedicated, fixed-size array of GPU-side slots; the GPU writes raw tick values into it via `EndQuery`.
- One instance per device. Reused every frame; no ring of heaps.
- Owned by `graphics::d3d12::TimestampQueryBackend`, private to the D3D12 backend module.

### 19.2 CPU-readable tick buffer → readback `ID3D12Resource`

- D3D12 resource created with `D3D12_HEAP_TYPE_READBACK` (CPU-visible GPU memory) and `D3D12_RESOURCE_STATE_COPY_DEST`.
  `D3D12_HEAP_TYPE_READBACK` is simply the D3D12 memory category for the buffer — it has no relation to query heap slots.
- Size: `max_queries_per_frame * sizeof(uint64_t)` bytes.
- Persistently mapped via `resource->Map(0, nullptr, &mapped_ptr)` at creation; stays mapped until the backend is destroyed.
- CPU reads resolved tick values as `const uint64_t* mapped_ticks`.
- Owned by the same `graphics::d3d12::TimestampQueryBackend`.
- **Resource state invariant:** Readback heap resources are created in `D3D12_RESOURCE_STATE_COPY_DEST` and **must remain in that state for their entire lifetime**. D3D12 forbids transitioning readback heap resources to any other state. No `ResourceBarrier` calls are ever issued for this buffer; `ResolveQueryData` writes to it directly as copy-destination.

### 19.3 Timestamp write → `ID3D12GraphicsCommandList::EndQuery`

```cpp
command_list->EndQuery(query_heap, D3D12_QUERY_TYPE_TIMESTAMP, query_slot);
```

- Called inline in the command list at the exact point of each scope boundary (once at begin, once at end).
- No pipeline barrier or resource state transition is needed.
- The timestamp captures the bottom-of-pipe position in the command stream at that point.

### 19.4 Bulk resolve → `ID3D12GraphicsCommandList::ResolveQueryData`

```cpp
command_list->ResolveQueryData(
    query_heap,
    D3D12_QUERY_TYPE_TIMESTAMP,
    /*StartIndex=*/ 0,
    /*NumQueries=*/ used_query_slots,
    readback_resource,
    /*AlignedDestinationBufferOffset=*/ 0);
```

- Issued once per frame via `TimestampQueryBackend::RecordResolve(recorder, used_query_slots)`, called from `GpuTimelineProfiler::OnFrameRecordTailResolve()`.
- **Command list acquisition:** `GpuTimelineProfiler` acquires a `CommandRecorder` from `Graphics::AcquireCommandRecorder()` (same pool used by renderer passes). The recorder's RAII deleter submits it through the standard deferred submission path (`SubmitDeferredCommandLists()`), keeping the resolve within the existing submission infrastructure — no direct queue access needed by the backend.
- The recorder is passed to `TimestampQueryBackend::RecordResolve(recorder, used_query_slots)`, which calls the D3D12 command list's `ResolveQueryData` using the backend's internally-held `query_heap` and `readback_resource`. These resources are never exposed to the caller.
- Copies all written query slots from the query heap into the readback buffer as contiguous `uint64_t` values.
- Must be submitted **after** all rendering command lists for the current frame so all `EndQuery` calls are already recorded.

### 19.5 Queue frequency → `ID3D12CommandQueue::GetTimestampFrequency`

```cpp
UINT64 freq_hz = 0;
d3d12_queue->GetTimestampFrequency(&freq_hz);
```

- Queried once at backend initialization and cached for the session (frequency does not change at runtime).
- Exposed through the engine-wide virtual `CommandQueue::TryGetTimestampFrequency(uint64_t& out_hz) -> bool`,
  which the D3D12 `CommandQueue` override delegates to `GetTimestampFrequency`.

### 19.6 Fence and validity model

The resolve command list is submitted through the standard deferred path (§19.4). After `SubmitDeferredCommandLists()` completes, `GpuTimelineProfiler` records the **current queue fence value** as the validity threshold:

```cpp
// After deferred submit flushes the resolve command list:
frame_resolve_fence_value = graphics_queue->GetCurrentValue();
```

No separate fence object is introduced. The existing `d3d12::CommandQueue` already owns a fence (created in `CommandQueue::CreateFence`); `GetCurrentValue()` returns the last CPU-signalled value, which is incremented with each `Signal()` call made by `Submit()`. The `frame_resolve_fence_value` simply captures this value after the resolve is enqueued.

Data is valid when:

```cpp
graphics_queue->GetCompletedValue() >= frame_resolve_fence_value
```

This check uses the engine's existing virtual `CommandQueue::GetCompletedValue()` — no D3D12-specific fence handle needed in the profiler layer.

Under Oxygen's flush-per-frame contract (`Graphics::BeginFrame()` calls `FlushCommandQueues()`, which calls `CommandQueue::Flush()` on each queue), this condition is guaranteed satisfied before the next frame's `OnFrameStart` reads back the tick values. The `frame_resolve_fence_value` check in §6.1 step 3 is therefore a defensive guard against contract violations, not a routine synchronization point.
