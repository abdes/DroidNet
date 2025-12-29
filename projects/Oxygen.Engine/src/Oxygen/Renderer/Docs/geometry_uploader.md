# GeometryUploader — Improvements Task List

This document tracks robustness and contract-compliance improvements for
`GeometryUploader` (GPU residency management + stable handles + stable shader-visible indices).

Scope: `src/Oxygen/Renderer/Resources/GeometryUploader.{h,cpp}` and its interaction with
`UploadCoordinator`, `ResourceRegistry`, and ScenePrep handle contracts.

## Contracts (Must Not Drift)

### Geometry Identity Input

Current (Dec 2025) contract:

- `GeometryUploader` operates at the **LOD mesh** level.
  - A `data::GeometryAsset` provides one `data::Mesh` per LOD.
- Geometry identity is the pair `(data::AssetKey, lod_index)`.
  - `AssetKey` comes from the owning `data::GeometryAsset` header.
  - `lod_index` comes from the scene's LOD resolution path
    (`scene::ActiveMesh::lod`).
- The returned `GeometryHandle` must be stable for the same
  `(AssetKey, lod_index)` across frames.

Important: mesh pointer identity is *not* part of the contract.
Pointers may change on hot-reload; the identity must not.

### GeometryHandle Meaning and Lifetime

- `GeometryHandle` is an index into an internal stable table of geometry
  entries owned by `GeometryUploader`.
- Handles are scoped to Renderer/ScenePrep usage.
- Handles may be recycled **only** after explicit asset-eviction notification
  (future hook). Until streaming/eviction exists, handles remain valid for the
  renderer lifetime.

### Update / Hot-Reload Contract

- `Update(handle, mesh)` is a hot-reload/editing path.
- It must preserve the handle and rebuild GPU buffers as needed.
- It must not be used to "rebind" a handle to unrelated geometry.
- Requirement: `Update()` must only be used to update the same
  `(AssetKey, lod_index)`.
  - The caller must ensure the updated mesh belongs to the same geometry
    identity as the handle.

## Final Runtime Behavior (Agreed)

When geometry is invalid, not yet resident, or upload fails:

- Render **nothing** for that item (drop the draw or emit a zero-count draw).
- Do **not** fail the frame; other draws and shader execution must proceed normally.
- Do **not** substitute placeholder/error meshes (no reliable proxy exists).
- Log clearly on detection (invalid mesh / upload failure / still-not-resident).

Implementation implication:

- The renderer must never issue draw calls that reference invalid bindless indices.
  If geometry is not resident (or invalid), the draw is skipped/zeroed so shaders are not invoked with bad SRVs.

## Ownership / Responsibilities

- Asset caching, streaming, and eviction are owned by the asset loader and its cache.
- `GeometryUploader` must not implement its own caching/eviction/LRU policies.
- Asset deduplication ("these two meshes are the same") is also an asset-loader
  concern. The loader should ensure identical content resolves to the same
  asset identity (or stable key) and handle lifetime/eviction.
- `GeometryUploader` is responsible only for:
  - creating/maintaining GPU buffers + bindless SRV indices for meshes it is asked to prepare,
  - scheduling uploads when data is first needed or changes,
  - ensuring it never causes a frame failure when data is invalid or not resident.
- `GeometryUploader` may perform lightweight *interning*:
  - mapping a stable mesh identity/key to a stable `GeometryHandle`,
  - avoiding duplicate GPU work for repeated requests of the same identity.
  It must not attempt runtime content hashing of vertex/index data.
- Future enhancement (not in scope here): react to asset-loader eviction notifications to invalidate/rebuild GPU state.

## Instancing Considerations (Future-Proofing)

Instancing in Oxygen means multiple `SceneNode`s can reference the same geometry
asset while retaining node-local renderable properties (transform, visibility,
material overrides, etc.). This has specific implications for `GeometryUploader`:

- GeometryUploader is **asset-scoped**, not instance-scoped.
  - The same geometry asset (or resolved LOD mesh identity) must map to the same
    `GeometryHandle` and the same SRV indices, regardless of how many nodes
    reference it.
- Per-node/per-instance properties must **not** influence geometry identity.
  - Do not key geometry handles by node, transform, per-node flags, or material.
- Instancing/batching is built on top of GeometryUploader outputs.
  - ScenePrep/emitters may group draw records by `(geometry_handle, material,
    pipeline key, ...)` and then emit an instanced draw.
  - GeometryUploader only provides the VB/IB SRV indices and remains oblivious
    to instance count.
- Error handling and logging must be **per asset**, not per instance.
  - A missing/non-resident asset can affect many instances; avoid logging per
    instance.

## Invariants / Acceptance Criteria (Debug + Release)

These are the non-negotiable correctness properties for implementation.

- Never fail the frame (Release): invalid/not-resident/failed-upload geometry
  must not crash or abort rendering.
- Assert on logic errors (Debug): use DCHECK/ASSERT to detect incorrect API
  usage and programmer mistakes.
- Never submit a draw that references invalid bindless indices.
- Logging: clear and actionable, and never emitted per instance (may be
  de-duplicated per asset event).
- Upload scheduling: geometry uploads only on first need or content change.
  Merely being referenced this frame must not schedule an upload.

## Tracked Tasks

### P0 — Correctness bugs (must-fix, implement first)

- [ ] Fix upload ticket retirement logic to handle `std::expected<bool, UploadError>` correctly.
  - `UploadCoordinator::IsComplete()` returns `expected<bool>`; only retire a ticket when `has_value() && value() == true`.
  - Decide policy for error case (log + keep ticket vs log + retry).
  - Touchpoints: `GeometryUploader::RetireCompletedUploads()`, `UploadCoordinator::IsComplete()` contract.

- [ ] Return the correct invalid handle sentinel on failure.
  - Use `engine::sceneprep::kInvalidGeometryHandle` (not a bindless-index constant) for invalid meshes.
  - Consider changing `GetOrAllocate()` to return `std::expected<GeometryHandle, Error>` to avoid silent invalid-handle propagation.

- [ ] Normalize invalid SRV sentinel usage.
  - Use `kInvalidShaderVisibleIndex` consistently for `ShaderVisibleIndex` fields (avoid mixing `kInvalidBindlessIndex`).
  - Ensure header defaults match implementation checks.

- [ ] Make `Update(handle, mesh)` unconditionally schedule a rebuild/upload for the entry.
  - Must mark dirty even if called in the same epoch.
  - Must not allow "handle rebinding" to unrelated geometry.
  - Done when: calling `Update()` results in upload work being scheduled on the
    next `EnsureFrameResources()`.

### P1 — Contract alignment (handles / residency / stability)

- [ ] Separate “touched this frame” from “dirty content”.
  - Do not treat “referenced this frame” as “needs reupload”.
  - Add `dirty_epoch` or `dirty` boolean that only flips on:
    - first-time allocation (no GPU buffer yet),
    - content change (`Update()` or changed mesh key),
    - buffer resize/replacement requiring reupload.
  - Outcome: stop per-frame reuploads of stable geometry.
  - Done when: a mesh referenced every frame does not produce repeated upload
    tickets unless its content changes or buffers must grow.

- [ ] Align handle and API docs with asset-loader-owned deduplication.
  - GeometryUploader only interns by mesh identity; asset loader owns
    deduplication.
  - Done when: public docs and comments do not claim content-hash dedupe.

- [ ] Ensure geometry identity remains instance-agnostic (instancing-safe).
  - Geometry handles and SRVs must depend only on geometry asset identity (and
    resolved LOD), not on node-local properties.
  - Done when: changing per-node properties does not change the returned handle
    for the same mesh identity.

- [ ] Fix mesh lifetime assumptions.
  - Assumption: mesh identities remain valid while the asset is in the cache.
  - Ensure code is ready for the future eviction notification hook.
  - Done when: no long-lived references remain that would outlive asset cache
    guarantees, and eviction hook can invalidate entries safely.

- [ ] (Future, not in scope) Add an eviction-notification hook from the asset loader.
  - On notification: invalidate affected GPU buffers/SRVs and mark the entry dirty for rebuild.
  - Must be fence-safe (defer release / registry + deferred reclaimer).

- [ ] Map “critical” geometry to actual upload prioritization.
  - Set `UploadRequest::priority` based on `is_critical`.
  - Ensure priority is preserved through `SubmitMany()`.

### P2 — Error handling / resilience

- [ ] Implement the agreed missing-residency / failed-upload policy.
  - Behavior: show nothing (skip draw or emit zero-count), never fail the frame.
  - Hard rule: do not submit draw calls that reference invalid SRV indices.
  - Logging: clear and actionable.
  - Done when: invalid/not-ready geometry produces no invalid SRV usage and
    emits clear logs describing the asset/mesh.

- [ ] Ensure shutdown/destruction is GPU-safe.
  - Before unregistering or replacing buffers, ensure in-flight uploads are completed or resources are deferred-released.
  - Consider calling `UploadCoordinator::Shutdown()` at renderer shutdown and/or ensuring all tickets are awaited.

### P3 — Tests (regressions + contract verification)

- [ ] Add unit tests mirroring `TransformUploader_test` patterns for `GeometryUploader`.
  - Ticket retirement: tickets are retained until completion.
  - Dirty vs used: stable mesh does not reupload every frame.
  - `Update()` in-frame marks dirty and schedules upload.
  - Invalid mesh returns `kInvalidGeometryHandle`.
  - Interning behavior matches documented semantics (same mesh identity returns
    same handle).

## Notes / References

- ScenePrep contract overview: `src/Oxygen/Renderer/Docs/scene_prep.md`
- Handle definitions: `src/Oxygen/Renderer/ScenePrep/Handles.h`
- Upload helper: `src/Oxygen/Renderer/Upload/UploadHelpers.{h,cpp}`
- Reference patterns:
  - `TransformUploader` (frame lifecycle + lazy ensure)
  - `MaterialBinder` (dirty-epoch tracking + atlas-based stable indices)
