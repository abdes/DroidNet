# MaterialBinder — Design and Operational Contract

## Purpose

MaterialBinder is the renderer-facing component that owns GPU-resident material
constants and exposes stable bindless access to them. It transforms authored
material data into shader-ready `MaterialConstants`, resolves texture bindings
via TextureBinder, and uploads only the changes each frame.

## Scope and Non-Goals

### In scope

- Stable material handles and content-based deduplication.
- GPU storage of material constants in a device-local structured buffer.
- Per-frame dirty tracking and batched uploads via UploadCoordinator.
- Texture index resolution via TextureBinder (bindless SRV indices).

### Out of scope (owned elsewhere)

- Global material streaming and cache budgets.
- Per-material eviction policies and LRU management.
- Shader fallback behavior beyond invalid-sentinel handling.

## Core Contracts (Must Not Drift)

### 1) Handle and identity

- `engine::sceneprep::MaterialHandle` is a stable index into internal state.
- Identity is derived from a content hash of authored material parameters and
 texture identities (ResourceKey), not from SRV indices.
- Handles are stable for the renderer lifetime; explicit recycling is a future
 policy decision.

### 2) Bindless indices and sentinel

- The material constants buffer is bound as a single SRV in the unified SRV
 table.
- The invalid sentinel is `kInvalidShaderVisibleIndex`.

### 3) Texture binding semantics

- MaterialConstants store shader-visible SRV indices, never ResourceKey or
 authored table indices.
- `kInvalidShaderVisibleIndex` means “do not sample” in shaders; sampling must
 branch to scalar defaults.
- Base-color uses a fallback/placeholder when authored data indicates
 “fallback texture”; normal/ORM slots use “do not sample” unless explicitly
 provided.

## Runtime Behavior

### Lifecycle

- `OnFrameStart(tag, slot)` resets per-frame dirty tracking.
- `GetOrAllocate(material_ref)` interns or reuses a handle; deduplicates by
 content hash; marks the entry dirty when new or changed.
- `Update(handle, material_asset)` updates an existing handle, validates the
 identity, and marks the entry dirty when content changes.
- `EnsureFrameResources()` uploads only dirty entries and publishes the SRV
 index for the material buffer once ready.
- `GetMaterialsSrvIndex()` returns the SRV index for the material constants
 buffer and is valid only after a successful EnsureFrameResources pass.

### Atlas buffer behavior

- Material constants are stored in an AtlasBuffer (device-local structured
 buffer) with a stable SRV index.
- Capacity growth can trigger a resize of the underlying buffer; the binder
 treats this as a data invalidation event and re-uploads all live materials.
- Material entries are dense and indexed by handle; no per-entry eviction is
 implemented at this time.

### Dirty tracking and retries

- Dirty entries are uploaded via UploadCoordinator using a staging provider.
- Upload failures keep entries dirty and retry in subsequent frames.
- Published indices only change when uploads complete successfully.

### Runtime overrides

- `OverrideUvTransform(...)` updates UV scale/offset for a material and marks
 the entry dirty. This is intended for editor/runtime authoring and does not
 change the material identity.

## Integration Points

- TextureBinder is used to resolve ResourceKey values into shader-visible SRV
 indices.
- UploadCoordinator provides batching, scheduling, and completion tracking for
 material uploads.
- ScenePrep uses MaterialBinder during finalization to ensure material
 resources are ready before draw metadata is uploaded.

## Validation and Diagnostics

- Material data is validated for non-finite values and reasonable ranges.
- Deduplication hashes ignore SRV indices to preserve handle stability.
- Errors are logged and material uploads are retried on subsequent frames.

## Future Work (Keep)

- Per-material eviction policies and cache budgets.
- Generation-gated slot reuse for material entries.
- Debug-only validation for bindless index bounds and residency state.
