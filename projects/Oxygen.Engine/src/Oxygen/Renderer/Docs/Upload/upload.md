# 📘 Nexus Working Design Document (Updated)

## 0. Purpose and scope

This is a living design document for Nexus (the unified GPU resource manager)
and the renderer upload/bindless pipeline. It reflects the current
implementation while preserving future feature specifications. It specifies
contracts and behaviors and intentionally avoids embedding code.

## 1. Conceptual overview

**Nexus** is the conceptual umbrella for GPU residency and bindless access.
The current implementation is distributed across:

- Upload module (UploadCoordinator, UploadPlanner, UploadTracker,
  RingBufferStaging, InlineTransfersCoordinator).
- ScenePrep finalization pipeline (geometry, transform, material uploads,
  draw metadata, sorting/partitioning).
- Resource binders/uploader subsystems (GeometryUploader, MaterialBinder,
  TextureBinder, TransformUploader, DrawMetadataEmitter).
- Renderer and pass system (RenderPass, generated root signatures, binding
  discipline).

Finalization order is deterministic: geometry → transforms → materials →
draw metadata emit → sort/partition → draw metadata upload. Texture residency
is driven by TextureBinder and material binding.

## 2. GPU-facing struct layout and naming semantics

### 2.1 Alignment and layout rules

GPU-facing structs must have stable binary layout across C++ and shaders.
Current implementation uses explicit alignment in key structs (SceneConstants
GPU snapshot and per-draw metadata). Rules remain:

- 16-byte alignment for vector-heavy structs and GPU buffers.
- Constant buffer uploads honor 16-byte field packing; padding fields are used
  to preserve multiples of 16 bytes.
- Shader-facing indices use `ShaderVisibleIndex` and its invalid sentinel.

Future (not yet fully implemented): layout hashing and runtime validation
against shader reflection.

### 2.2 Naming conventions

Keep type names explicit and consistent across CPU and shader usage:

- `*Handle` or `*Index` for bindless indices or indirections.
- `*Slot` for SceneConstants fields that hold bindless indices.
- `*View` for transient views of resources (descriptor or registry view).

### 2.3 Current GPU-facing structs

These are authoritative in the current renderer:

- SceneConstants GPU snapshot (per-frame CBV): view/projection, camera,
  frame sequence/slot, time, and bindless slots for draw metadata, transforms,
  normals, materials, environment data, lights, shadows, and instance data.
- DrawMetadata structured buffer (per-draw records; dense indexed access).
- MaterialConstants structured buffer (per-material data, bindless texture
  indices).

## 3. Bindless binding contract

### 3.1 Root signature and binding order (generated)

Root bindings are generated from Bindless.yaml and shared across all render
passes. Current root parameters (order is significant):

1. Bindless SRV table: t0, space0 (unbounded SRV table)
2. Bindless sampler table: s0, space0
3. SceneConstants CBV: b1, space0 (direct root CBV)
4. Root constants: b2, space0 (2×32-bit values)
   - DWORD0: draw index (per-draw index into DrawMetadata)
   - DWORD1: pass-constants index (per-pass payload, typically bindless)
5. EnvironmentDynamicData CBV: b3, space0 (per-frame environment data)

All passes use the same root signature for simplicity, even if some bindings
are unused in a pass. The RenderPass base class and command recorder bind the
descriptor heaps and root tables during pipeline setup.

### 3.2 Bindless index types and sentinels

Use strong types from Bindless/Types.h:

- `BindlessHeapIndex` (backend allocator index)
- `ShaderVisibleIndex` (shader-visible bindless index)

Sentinel: `kInvalidBindlessIndex` (also exposed as `kInvalidShaderVisibleIndex`)
is the only invalid value. Index 0 is valid.

### 3.3 Stability expectations (as implemented)

Bindless index stability is resource-specific:

- **TextureBinder**: stable shader-visible index per ResourceKey; descriptors
  are repointed in-place to upgrade placeholder → final texture without
  changing the index.
- **GeometryUploader**: stable handle per geometry identity; SRV index is
  stable across buffer resize (Replace preserves SRV index) but may change
  after eviction/reload.
- **MaterialBinder**: stable SRV index for the material buffer; per-material
  indices are stable unless capacity is rebuilt or material is removed.
- **TransientStructuredBuffer (transforms, draw metadata, instance data)**:
  SRV indices are frame-local and change each frame by design.

Generation-based aliasing protection exists as a type (`VersionedBindlessHandle`)
but is not yet wired into all bindless allocation/reuse paths.

### 3.4 Bindless source-of-truth

Bindless mapping is generated from Bindless.yaml into:

- Generated.Constants.h (sentinels, constants)
- Generated.RootSignature.h (root parameter order)
- Generated.Heaps.D3D12.h (heap strategy JSON)
- BindingSlots headers for C++ and HLSL

## 4. Bindless indices management

### 4.1 Global indexing scheme

- Unified CBV/SRV/UAV heap for shader-visible resources.
- Unified SRV table (t0, space0) with logical domains (global, materials,
  textures) mapped by domain base indices.
- Sampler heap and table (s0, space0).

### 4.2 Index lifetime, reuse, and aliasing prevention

Current state:

- Stable indices are enforced per subsystem (TextureBinder, GeometryUploader,
  MaterialBinder), but global generation-based reuse is not yet implemented.
- The invalid sentinel is enforced; shaders must treat it as a safe fallback.

Future spec (keep):

- Reuse a bindless slot only after generation increment gated by fence/frame
  completion.
- Track generation counters in CPU shadow state and validate on handle use.
- Maintain per-domain pending-recycle queues and publish generation updates
  atomically before reuse.

### 4.3 Validation and debugging

Current:

- Runtime validation occurs in specific subsystems (mesh and material checks,
  texture layout validation, allocator bounds checks).

Future:

- Debug-only validation toggles for bindless bounds and root binding checks.
- Shader-side invalid-sentinel fallback standardization across all shaders.

## 5. Logical structuring of the unified descriptor heap

### 5.1 Descriptor ranges and logical tables

Current layout (as generated):

- SceneConstants CBV: b1, space0 (root CBV)
- EnvironmentDynamicData CBV: b3, space0 (root CBV)
- Unified SRV table: t0, space0 (unbounded), domains include global, materials,
  textures
- Sampler table: s0, space0

### 5.2 Resource placement (conceptual)

- Geometry buffers, transforms, draw metadata, instance data: SRV table
- Material constants: SRV table at materials domain base
- Textures: SRV table at textures domain base
- Samplers: sampler table

This structure enables stable bindings while allowing per-frame slot updates
through SceneConstants and other root parameters.

## 6. ScenePrep finalization

Finalization order (current implementation):

1. Geometry upload (GeometryUploader)
2. Transform upload (TransformUploader)
3. Material upload (MaterialBinder)
4. Per-item draw metadata emission (DrawMetadataEmitter)
5. Sort & partition draws (DrawMetadataEmitter)
6. Upload draw metadata (DrawMetadataEmitter)

SceneConstants is then updated with the bindless SRV indices produced by
these subsystems, including instance data if instancing was generated.

## 7. Upload system

### 7.1 Core components

- **UploadCoordinator**: staging allocation, copy recording, fence signaling,
  ticket registration, and shutdown coordination.
- **UploadPlanner**: buffer coalescing and texture footprint planning.
- **StagingProvider**: abstract staging allocation (RingBufferStaging is the
  default implementation).
- **UploadTracker**: fence-based completion tracking with coroutine support.
- **UploadPolicy**: alignment requirements, filler policy, queue selection.

### 7.2 Queue and state behavior

- Copy queue path tracks resources from COMMON and restores to COMMON.
- Graphics queue path transitions resources to usage-derived steady states.
- Texture uploads are aligned to row pitch and placement requirements.

### 7.3 Direct write vs staging + copy

- **Direct write**: TransientStructuredBuffer writes into upload heap memory
  and relies on InlineTransfersCoordinator to drive retirement.
- **Staging + copy**: UploadCoordinator plans and records CopyBuffer/CopyTexture
  operations into the chosen queue, then fences completion.

### 7.4 Staging provider behavior (RingBufferStaging)

- Partitioned per-frame slot; linear bump allocation.
- Growth with slack; idle trimming after sustained inactivity.
- Retirement tracking to avoid accidental overwrite.

### 7.5 UploadTracker lifecycle

- Tickets are registered against a monotonic fence value.
- OnFrameStart clears entries in the recycled slot; Shutdown waits using the
  last-registered fence value to ensure completion even after cleanup.

## 8. Resource-specific upload and binding behavior

### 8.1 GeometryUploader

- Interns geometry by (AssetKey, LOD index) and returns stable handles.
- Uses device-local buffers and bindless SRVs; uploads via UploadCoordinator.
- Publishes SRV indices only after successful upload completion.
- Handles asset eviction by invalidating SRVs until reload.

### 8.2 MaterialBinder

- Stores MaterialConstants in AtlasBuffer (device-local structured buffer).
- Deduplicates materials by content hash; dirty tracking controls uploads.
- Resolves texture indices via TextureBinder.

### 8.3 TextureBinder

- Stable shader-visible indices per ResourceKey.
- Descriptor repointing keeps SRV index stable while content changes.
- Handles placeholder and error textures; eviction repoints to placeholder.
- Validates layout against cooked payload metadata; supports uncompressed and
  BC7 formats; per-frame upload bytes capped.

### 8.4 TransformUploader

- Uses TransientStructuredBuffer for per-frame world and normal matrices.
- Handles are deterministic per frame based on call order.
- SRV indices are frame-local.

### 8.5 DrawMetadataEmitter

- Emits per-draw metadata into a dense CPU array, sorts and partitions.
- Uploads draw metadata into a transient structured buffer each frame.
- Supports GPU instancing with a separate instance data buffer.

## 9. SceneConstants contract

SceneConstants is the per-frame CBV bound at b1. It provides:

- View/projection matrices and camera position.
- Frame slot and sequence number, plus time in seconds.
- Bindless slots for draw metadata, transforms, normal matrices, material
  constants, environment data, directional lights, directional shadows,
  positional lights, and instance data.

SceneConstants uses a monotonic version and only rebuilds GPU snapshots on
change to avoid unnecessary uploads.

## 10. Caching, residency, and eviction

Current behavior:

- Geometry and materials are device-local buffers with explicit upload
  scheduling.
- Textures are loaded and uploaded via TextureBinder with stable indices and
  placeholder/error fallbacks.
- GeometryUploader subscribes to content eviction events and invalidates
  geometry entries on asset eviction. Evicted geometry releases its buffers and
  SRV indices; subsequent access yields invalid SRVs until the asset is
  reloaded and re-uploaded.
- TextureBinder integrates with the content cache via ResourceKey identity and
  eviction events. On eviction, the per-entry descriptor is repointed to the
  global placeholder texture while preserving the shader-visible index; the
  entry is marked evicted and will reload on the next GetOrAllocate call.
- Late upload completions are ignored when the entry generation no longer
  matches (eviction or replacement), preventing stale data from being
  published.
- MaterialBinder uses content-hash deduplication and dirty tracking; it does
  not currently evict individual materials from the atlas, but it can be
  refreshed on update or rebuild. Residency is therefore bounded by atlas
  capacity and explicit lifecycle management by the renderer.
- UploadCoordinator and UploadTracker provide fence-based completion that
  gates when data is considered resident. Callers must only publish bindless
  indices after successful completion.

Future spec (keep):

- Budget-aware residency across domains.
- Generation-gated slot reuse and formal eviction states.
- LRU- or priority-based eviction policies with GPU fence gating.

## 11. Validation, diagnostics, and tooling

Current validation:

- Mesh and material validation at ingestion.
- Texture layout validation against payload metadata.
- UploadPlanner rejects invalid requests and out-of-bounds regions.

Future spec (keep):

- Layout hashing and shader reflection validation.
- Debug overlays for bindless index usage and heap utilization.

## 12. Implementation status and future work

Implemented:

- Generated bindless layout and root signature metadata.
- Direct indexing flags in D3D12 root signatures.
- Shared root signature across passes (SRV table, sampler table, SceneConstants,
  root constants, EnvironmentDynamicData).
- Upload module with staging, coalescing, fence-based tracking, coroutine
  support, and inline-transfer retirement.
- TextureBinder stable SRV indices with descriptor repointing.
- GeometryUploader, MaterialBinder, TransformUploader, DrawMetadataEmitter.

Not yet implemented (keep as future specs):

- Generation-gated slot reuse for all bindless allocations.
- Debug-only validation toggles for bindless bounds and root binding checks.
- Shader-side invalid-sentinel fallback standardization across all shaders.
- Type-safe per-domain bindless handle wrappers.
- Budget-aware residency and eviction policy for non-texture resources.
- Parallel upload scheduling and per-domain staging queues.
- CI checks comparing shader reflection layout hashes against C++.

## 13. Testing and observability

Current tests cover Upload module and resource binder behaviors. Missing tests
include bindless generation handling, recycling after fences, and shader-visible
index bounds checks. Add integration tests that validate binding layout and
ScenePrep output consistency.

## 14. Extensibility

- Extend SceneConstants or per-draw buffers to add new data without changing
  root signatures.
- Prefer new bindless buffers and slots over new root parameters.
- Keep Bindless.yaml as the single source of truth for binding layout.
