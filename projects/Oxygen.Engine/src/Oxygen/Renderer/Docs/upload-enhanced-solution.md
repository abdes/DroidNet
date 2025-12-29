# Upload-enhanced solution — resident atlas + upload ring + dynamic SRV

This document summarizes a hybrid design for managing transforms, materials,
and similar per-draw data in a bindless renderer. It favors a simple C++
runtime and relies on the existing N-partitioned upload ring for delta
uploads and safe reclamation.

## At a glance

- [Goals](#goals)
- [Architecture](#architecture)
- [Implementation notes (D3D12)](#implementation-notes-d3d12-specifics)
- [Phase 1 progress](#phase-1-progress-current-state)
- [Workflow](#workflow)
- [Indirection and shader access](#indirection-and-shader-access)
- Lifetime & memory:
  - [Reclamation](#reclamation-simple-and-fence-keyed)
  - [Stability heuristics](#stability-heuristics-practical-minimal-tuning)
  - [Atlas growth](#atlas-growth-hybrid-primary-resize-then-overflow-chunks)
- [Detailed migration plan](#detailed-migration-plan-transformuploader)
- [Provider/API hardening](#providerapi-hardening)

## Goals

- Minimize per-frame CPU→GPU upload bandwidth for stable entries (transforms
  and material constants).
- Preserve absolute bindless indices exposed to shaders.
- Keep shaders simple; prefer C++ bookkeeping and straight SRV reads.
- Keep implementation KISS: correct first, optimize when measurable wins
  justify complexity.

## Remaining TODOs summary

Phase 2 (Dynamic SRV + mixing)

| Item | Status |
| ---- | ------ |
| Implement dynamic per-frame buffers and descriptor update strategy | Pending |
| Enable LUT mixing (bit flag) and promotion/demotion heuristics | Pending |
| Extend tests for promotion/demotion and dynamic path | Pending |

## Architecture

### Core components

- Atlas (resident buffer): DEFAULT-heap structured buffer, indexable by
  absolute bindless index. Holds stable entries (transform or material structs).
- Upload ring (N-partitioned UPLOAD heap): existing per-frame staging buffer.
  Used only for delta uploads (entries created/changed this frame). Atlas slot
  allocation and reclamation are owned by the atlas allocator using retire
  lists keyed to frame fences (synchronized with the same frame cycle).
- Dynamic buffer (per-frame SRV): small per-frame DEFAULT-heap buffer for
  entries updated every frame (e.g., skinned bones, animated parameters) to
  avoid heavy atlas churn. Exposed via the bindless SRV table as well; update
  its descriptor once per frame to point at the current buffer.
- Indirection table (small SRV): maps logical/bindless index → (location,
  slot). Location indicates atlas or dynamic slot. Kept stable and updated by
  C++ when entries move.

### Reusable component: AtlasBuffer

- Purpose: Own one primary DEFAULT-heap structured buffer (single SRV index)
  and optionally additional overflow chunks (each with its own SRV index)
  when growing past thresholds. Provide slot allocation with fence-keyed
  retire lists; expose capacity/stride and compute dst offsets for
  UploadRequests. It does not handle staging or command recording.
- Creation/resize: Uses `renderer::resources::internal::EnsureBufferAndSrv()`
  to create or grow the buffer and (re)establish its SRV while preserving the
  descriptor index. Returns `EnsureBufferResult` (unchanged/created/resized).
- Growth (Hybrid): Start with a single primary buffer. Prefer replacement
  growth (resize+reupload) up to a configurable threshold; beyond that, add
  overflow chunk(s). Phase 1 can keep using only the primary (no overflow)
  while the API remains future-proof.
- Slot allocator: Returns stable slot indices; maintains N retire lists keyed
  to frame slots; `OnFrameStart(slot)` recycles retirees into the free list.
- Minimal API (sketch):
  - `EnsureCapacity(min_elements, slack) -> EnsureBufferResult`
  - `Allocate(count=1) -> Allocation{ ElementRef first, uint32_t count }`
    - `ElementRef { ShaderVisibleIndex srv; uint32_t element; }`
    - `Release(ElementRef ref, frame_slot)` (accepts any chunk)
  - `OnFrameStart(frame_slot)`
  - `Stride()`, `CapacityElements()` (total across chunks), diagnostics
  - Upload-intent API for building requests:
    - `MakeUploadDesc(ElementRef, size_bytes) -> UploadBufferDesc`
  - (No staging/submission APIs; clients build UploadRequests and call
    UploadCoordinator.)

## Implementation notes (D3D12 specifics)

- Atlas buffers: HEAP_TYPE_DEFAULT with SRV as `StructuredBuffer<T>` (or
  `ByteAddressBuffer` when packing). Sampling states:
  `RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | RESOURCE_STATE_PIXEL_SHADER_RESOURCE`.
- Upload ring: HEAP_TYPE_UPLOAD, persistently mapped. All CPU writes go here;
  copies go Upload → DEFAULT via CopyBufferRegion (4-byte alignment rule).
- Dynamic buffers: triple-buffered DEFAULT resources sized for the expected
  per-frame dynamic traffic. One SRV descriptor per kind is updated per
  frame to the current buffer.
  - This SRV is a root-signature bound descriptor in the single global
    CBV_SRV_UAV heap (100% bindless). It is never an UPLOAD resource; data is
    uploaded into DEFAULT memory, and only the SRV descriptor is switched
    after the relevant fences signal.
  - Descriptor lifetime: either allocate N descriptor slots (one per frame in
    flight) and index by frame, or update a single slot only after the fence
    for the previous frame signals. Do not overwrite a descriptor while it may
    still be referenced by in-flight work.
- Barriers: for each destination buffer, do two transitions per frame:
  SRV → COPY_DEST, batch all copies, then COPY_DEST → SRV. Avoid per-entry
  transitions (buffers transition as a whole in D3D12).
- Copy queue (optional): Start with DIRECT. Consider COPY queue and queue
  synchronization only if profiling shows a DIRECT bottleneck.

## Phase 1 progress (current state)

### What’s done

- Reusable AtlasBuffer in place for worlds and normals (DEFAULT-heap, stable
  SRV index). Stats exposed (ensure/alloc/release/capacity/free list/next).
- TransformUploader builds naive per-dirty-element UploadRequests and relies
  on centralized batching/merging in UploadPlanner/UploadCoordinator.
- UploadPlanner groups by destination buffer, sorts by dst offset, assigns
  aligned src offsets, and merges contiguous regions.
- UploadCoordinator records per-destination transitions (CopyDest → batched
  copies → steady) and emits a single signal per batch; renderer owns queue
  sync. `Flush()` retained.
- StagingProvider interface extended with `OnFrameStart(slot)` (default no-op);
  `RingBufferStaging` overrides it to set the active partition without RTTI.
- Telemetry integrated: AtlasBuffer and StagingProvider expose Stats;
  TransformUploader prints aligned, scoped summaries.

### Impact

- Fewer CopyBufferRegion calls (contiguous merges), minimal barriers (two per
  destination buffer per batch), and reduced signal spam (one per batch).
  Stable entries upload only on change.

## Workflow

1. Create entry (logical index assigned): allocate an atlas element index from
   the atlas allocator. Build an UploadRequest targeting the atlas at
   `element_index * stride` and submit via UploadCoordinator (which stages,
   records, and submits the copy).
2. Update entry (infrequent): build and submit an UploadRequest to the atlas at
   `element_index * stride`. No per-frame copies for stable entries.
3. Dynamic per-frame updates (Phase 2): build and submit UploadRequests to the
   current-frame dynamic buffer; update the dynamic SRV descriptor accordingly.
4. Read: shaders index indirection table by logical index and then SRV-read
   the atlas or dynamic buffer slot indicated.

## Indirection and shader access

### Indirection table (packed and minimal)

- Use one table per data kind (transforms, materials, …) to keep entries
  small and avoid a “kind” field.
- Phase 1 (exact current path): No LUT required. Keep a single transforms SRV
  bound in the global descriptor table and index it directly with the
  existing transform index.
- Optional Phase 1 LUT (if desired): 32-bit entry (location|element) still
  selects the same single SRV (location=0) and conveys `element`. This is not
  required and can be deferred.
- 32-bit entry layout (single-buffer atlas): bit 31 = location (0 = atlas,
  1 = dynamic), bits 0–30 = element index. If a table may exceed 2^31 elements,
  use `uint2{location, element}`.
- Hybrid (multi-chunk) variant: LUT must encode both SRV index and element.
  Options:
  - `uint2{ srv_index, element }` (simple and flexible), or
  - packed 64-bit (requires bit budgeting constraints), or
  - two tables (srv table and element table). Phase 1 may keep the simple
    32-bit atlas-only form and upgrade later when overflow chunks are enabled.
- The indirection table lives in DEFAULT memory and is updated via the upload
  ring using the same copy scheduling. Optional CPU mirror is not required.

### Shader read pattern (HLSL)

```hlsl
StructuredBuffer<Transform> g_Transforms;     // atlas (single-buffer)
StructuredBuffer<Transform> g_DynTransforms;  // dynamic per-frame (Phase 2)
StructuredBuffer<uint>      g_TransformLUT;   // packed entry (location|element)

Transform LoadTransform(uint logicalIndex)
{
    const uint e = g_TransformLUT[logicalIndex];
    const bool isDyn = (e & 0x80000000u) != 0u;
    const uint elem  = (e & 0x7fffffffu); // element index within the buffer
    return isDyn ? g_DynTransforms[elem] : g_Transforms[elem];
}
```

### Hybrid shader (descriptor indexing) example

```hlsl
// If/when overflow chunks are enabled, use a descriptor-indexed array:
//   StructuredBuffer<Transform> g_AtlasBuffers[];
// And a LUT of uint2 { srv_index, element }
StructuredBuffer<Transform> g_AtlasBuffers[];
StructuredBuffer<uint2>     g_TransformLUT2; // {srv, elem}

Transform LoadTransform2(uint logicalIndex)
{
    const uint2 e = g_TransformLUT2[logicalIndex];
    return g_AtlasBuffers[e.x][e.y];
}
```

## Lifetime and memory management

### Reclamation (simple and fence-keyed)

- Use frame-fence keyed retire lists instead of per-element fences: when an
  entry is destroyed at frame F, push its atlas element index to
  `retire_list[F % N]`. When the renderer advances to frame K and the fence for
  K completes, move `retire_list[K % N]` to the free pool. This leverages the
  same lifetime as the upload ring without coupling allocation to ring internals.
- Avoid immediate reuse of slots in the same frame by construction.
- **Memory Leak Prevention**: UploadTracker implements frame-slot-based cleanup
  using `creation_slot` tracking and `std::erase_if` to remove all entries
  created in cycling slots. This bounds memory to the number of frames in flight
  worth instead of infinite growth, preventing performance degradation from hash
  table bloat.

### Stability heuristics (practical, minimal tuning)

- Explicit hint: creators may declare entries static or dynamic at allocation
  time. Static ⇒ place in atlas and never auto-reclaim unless explicitly destroyed.
- Age promotion: default policy for runtime entries.
  - Start as dynamic (no atlas allocation) or allocate in atlas but mark
    transient. Track `last_update_frame` per entry.
  - Promote to stable (eligible for long residency) if not updated for
    `promote_threshold` frames (example: 30 frames ≈ 0.5s at 60Hz).
  - Demote to dynamic if updated frequently: if `updates_in_window > demote_threshold`
    (example: >5 updates in last 8 frames).
- Shared-use bias: prefer stability for entries referenced by many instances
  (maintain `instance_count` per logical entry). Larger counts lower promotion
  threshold.

### When to reclaim stable entries

- Reclaim only when explicitly released or when eviction is needed to free
  atlas capacity. For eviction, prefer LRU or a cost-based policy that
  considers: `instance_count` (lower preferred for reclaim), `last_update_frame`,
  and allocation cost.
- Do not automatically evict solely based on age. Prefer explicit release or
  eviction under memory pressure.

### Atlas growth (hybrid: primary resize then overflow chunks)

- Prefer primary-buffer replacement up to a threshold: allocate a larger
  primary, copy live slots (or re-upload), then update the primary SRV
  descriptor in-place after fences. This is rare; schedule during a
  maintenance phase.
- Beyond threshold, add overflow chunk(s) with their own SRV indices. New
  allocations may land in overflow. The LUT must then carry SRV+element.
- Optional maintenance pass can compact from overflow back into a larger
  primary during idle windows and update LUT entries for moved elements.

### Simple allocation / reclaim pseudocode (C++ sketch)

```cpp
// Called by user/renderer to create or update an entry (conceptual)
void UpdateEntry(LogicalIndex id, const EntryData &data) {
  if (entryTable[id].state == State::Dynamic) { // Phase 2
    UploadRequest req = MakeBufferUpload(dynamicBuffer[frameIndex],
                                         slotOffset(id),
                                         std::as_bytes(std::span{&data, 1}));
    uploadCoordinator.SubmitMany(std::span{&req, 1}, stagingProvider);
    entryTable[id].last_update_frame = frameIndex;
  } else {
    UploadRequest req = MakeBufferUpload(atlasBuffer,
                                         slotOffset(id),
                                         std::as_bytes(std::span{&data, 1}));
    uploadCoordinator.SubmitMany(std::span{&req, 1}, stagingProvider);
  }
}

// Release entry
void ReleaseEntry(LogicalIndex id) {
  auto slot = entryTable[id].slot;
  entryTable[id].state = State::Released;
  // retire on the fence of the frame in which this is destroyed
  retireLists[frameIndex % N].push_back(slot);
}
```

## Rationale

- Bindless indices remain absolute and stable: the indirection table keeps
  shader-visible indices small and stable while allowing C++ to remap or
  reuse atlas storage under the hood.
- Minimal per-frame CPU work for stable entries: uploads only when changed.
- The upload ring already tracks per-frame lifetimes and fences — reuse that
  for safe reclamation instead of building separate per-slot fences.
- Dynamic update path is fully bindless: dynamic buffers are exposed via the
  same descriptor table; the engine only updates a small number of SRV
  descriptors per frame. Shaders branch on a single bit.
- KISS: keep complex logic in allocator and indirection table in C++.

## Operational parameters

- `promote_threshold`: number of frames without updates before promoting to
  stable (default 30).
- `demote_threshold`: updates per window to demote a stable entry (default 5 in
  8 frames).
- `eviction_policy`: LRU or cost-based; invoked only under memory pressure.

## Operational guidance and metrics

- Schedule uploads early in the frame; batch per-buffer barriers (2 per
  buffer per frame) and coalesce adjacent CopyBufferRegion calls.
- Track per-frame metrics: bytes uploaded, number of copies, number of
  barriers, promotion/demotion counts, atlas free capacity, dynamic buffer
  occupancy.
- Start without a COPY queue; adopt it only if profiling shows DIRECT queue
  contention during the upload phase.

## Detailed migration plan: TransformUploader

### Phasing

- Phase 1 (now): Treat all transforms as stable and store in the resident
  atlas. Defer the dynamic per-frame SRV path and LUT mixing semantics to
  Phase 2. No LUT is required in Phase 1; shaders may keep indexing the single
  transforms SRV directly. Keep APIs and structures future-proof but only the
  atlas path is exercised.
- Phase 2 (later): Add dynamic SRV buffers and descriptor switching; enable
  LUT mixing (atlas vs dynamic) and promotion/demotion heuristics.

This migration makes `TransformUploader` use the reusable `AtlasBuffer`
component for worlds and normals in Phase 1 (and later other data kinds).
In Phase 1 it may optionally own an indirection table; in Phase 2 it adds
dynamic buffers. The staging provider is only to obtain mapped UPLOAD memory
for copies. Bindless SRV indices remain stable and are managed by
`TransformUploader` via `ResourceRegistry`.

### 1) Resources and descriptors (via AtlasBuffer)

- Worlds and normals are each backed by an `AtlasBuffer` instance.
- Strides: `sizeof(glm::mat4)` for worlds; `sizeof(glm::mat3)` (or packed
  3x4/quat) for normals. AtlasBuffer computes offsets as
  `element_index * stride`.
- Initial capacity: power-of-two. `AtlasBuffer::EnsureCapacity()` calls
  `EnsureBufferAndSrv()` and returns `EnsureBufferResult`.
- On `kResized`/`kCreated`, TransformUploader re-uploads live content via
  UploadCoordinator (descriptor index remains stable as SRV is updated
  in-place by `EnsureBufferAndSrv()`).
- Indirection table:
  - DEFAULT-heap `StructuredBuffer<uint>` (packed entry as described above).
    One table per kind (transforms). Optional CPU mirror.
  - Bindless SRV index reserved and kept stable; updates go through the
    upload path like any other buffer.
  - Hybrid note: Phase 1 may keep the 32-bit (location|element) form since
    only the primary atlas buffer is used. When overflow chunks are enabled,
    migrate to `uint2{srv_index, element}` (or a packed 64-bit) and use a
    descriptor-indexed SRV array in shaders.
- Dynamic buffers (per-frame): deferred to Phase 2.

### 2) Element allocation and lifecycle (atlas via AtlasBuffer)

- `AtlasBuffer` maintains free list + N retire lists keyed by frame slot.
- `AtlasBuffer::OnFrameStart(frame_slot)` recycles retirees into free list.
- Allocation returns stable element indices; clients may track metadata like
  `last_update_frame` or `instance_count` as needed.
- Growth: Phase 1 uses full re-upload after `kResized`. Phase 2 may add a
  maintenance path for device-to-device copies of live ranges.

### 3) Frame partitioning and staging

- Continue using the existing N-partitioned staging provider:
  `RingBufferStaging::SetActivePartition(frame_slot)` must be called at
  frame start. The staging alignment must equal the destination element
  stride (power-of-two enforced by provider ctor).
- The atlas itself is not partitioned. Only the staging upload memory is.
- Dynamic buffers selection is deferred to Phase 2.

### 4) Update and submit path (using UploadCoordinator)

- Stable entries (atlas):
  - When created/changed, call `atlasBuffer.MakeUploadDesc(ref, stride)` to
    obtain an `UploadBufferDesc`, then build an `UploadRequest` with the
    returned desc and a data view containing `stride` bytes.
    - `ref` is `ElementRef{ srv, element }`. In Phase 1, `srv` equals the
      primary SRV; with overflow chunks, `srv` may point to a different SRV.
  - Do not perform barriers or command recording in `TransformUploader`.
    Submit requests to `UploadCoordinator`, which owns planning, staging via
    `StagingProvider::Allocate`, recording, and barriers.
- Dynamic entries: deferred to Phase 2.
- Indirection table updates:
  - If used in Phase 1, entries always point to the atlas (bit=0). Stage a
    4-byte LUT write as needed exactly like other buffer updates. Using a LUT
    in Phase 1 is optional; the current single-SRV pattern can be kept.
- Submission: Call `UploadCoordinator::SubmitMany(requests, provider)`.

### 5) Frame start and retirement

- `OnFrameStart(frame_slot)` responsibilities:
  - Set the active staging partition on `RingBufferStaging`.
  - Recycle `retire[frame_slot]` element indices into the atlas free list.
  - Call `UploadCoordinator::RetireCompleted()` once per frame to advance
    fences, complete tickets, and recycle staging (which calls
    `StagingProvider::RetireCompleted()` internally).
  - Optionally update telemetry snapshots.

### 6) Telemetry and diagnostics

- Track per-frame: bytes staged, copies recorded, barriers issued, atlas
  capacity and free elements, dynamic buffer occupancy, promotions/demotions.
- Expose provider diagnostics via getters (alignment, per-partition/total
  capacity). AtlasBuffer exposes stride, capacity, and allocator counters.

## Concrete TODO checklist (authoritative)

Phase 2 (Dynamic SRV + mixing)

1) Implement dynamic per-frame DEFAULT buffers and per-frame descriptor update
strategy per data kind.
2) Enable LUT mixing (location bit) and promotion/demotion heuristics as
outlined in Stability heuristics.
3) Extend tests for promotion/demotion and dynamic path (including descriptor
update lifetime rules).

## Provider/API hardening

1) RingBufferStaging
   - Constructor already requires `alignment` and `partitions`.
   - Validate `alignment` is power-of-two (enforced); document that callers
     must pass element stride for structured buffers.
   - Expose diagnostics: `CapacityPerPartition()`, `TotalCapacity()`,
     `Alignment()`.
2) UploadCoordinator
   - Document current behavior: a single provider per `SubmitMany` batch is
     required. If mixed providers are passed, behavior is undefined.
   - Expand texture steady-state handling when policy is extended.
3) Tests
   - Add unit tests: partition isolation, growth behavior preserving old
     allocations, alignment correctness, and coordinator integration.
