# Texture Binder Design

This document specifies the design for the runtime texture binding system that enables full texture support for PBR material rendering in the Oxygen Engine.

## Purpose

Enable runtime binding of texture resources (from PAK files or loose cooked filesystem) to shader-visible descriptor heap indices, allowing materials to reference textures that can be sampled in shaders.

## Responsibilities

- Map texture resource indices (from PAK or loose cooked files) to stable shader-visible SRV indices
- Manage GPU texture resource lifecycle (creation, upload, replacement)
- Coordinate with existing upload and descriptor allocation infrastructure
- Provide error-handling defaults when texture loading fails

## Design Decisions

### Storage Agnostic

The TextureBinder is agnostic to asset storage format:

- Works with PAK files (packed archives)
- Works with loose cooked files (filesystem-based)
- Uses `pak::ResourceIndexT` as a generic resource identifier (just `uint32_t`)
- Depends on `AssetLoader` abstraction to provide `TextureResource` data regardless of source

**Note**: "PAK index" in this document refers generically to the resource table index used by the asset system, whether that table is stored in a PAK file or a separate index file for loose cooked assets.

### Index Semantics

`ShaderVisibleIndex` represents an offset into the currently bound shader-visible descriptor heap, matching the convention used by `DescriptorAllocator::GetShaderVisibleIndex()` and existing buffer SRV allocation (`EnsureBufferAndSrv`).

**Current Implementation (Dec 2025)**:

- `pak::v1::kFallbackResourceIndex` (currently `0`) is a **reserved, valid**
  index. The asset pipeline/packer must store the **fallback texture** at
  index `0` so the meaning of "fallback" is consistent across tooling and
  runtime.
- The current runtime implementation treats `0` as a special case and returns
  the binder's placeholder texture immediately (an acceptable stand-in while
  async loading is TODO).
- A **forced-error sentinel** (`UINT32_MAX`) is used by examples to deterministically select the error texture path without requiring the async loader.

Only `kInvalidBindlessIndex` (0xFFFFFFFF) indicates an invalid *bindless heap index*; this is separate from the resource-index conventions above.

## 1. TextureBinder Component

### Contract

**Interface**:

```cpp
class TextureBinder {
public:
  // Get or allocate shader-visible SRV index for texture resource
  // Returns: Stable SRV index usable in MaterialConstants
  // Guarantee: Same resource index always returns same SRV index
  auto GetOrAllocate(pak::ResourceIndexT resource_index) -> ShaderVisibleIndex;

  // Get error-indicator texture for loading failures
  auto GetErrorTextureIndex() const -> ShaderVisibleIndex;
};
```

**Behavior**:

- `GetOrAllocate()` must return immediately with a valid SRV index (may reference placeholder texture initially)
- Returned SRV index must remain stable for the lifetime of that texture resource
- Texture data loading and upload occurs asynchronously after index allocation
- Resource index `0` is reserved for the fallback texture (valid in the PAK)
  and may be special-cased by the runtime as a fast fallback path

### Stable Index Guarantee

The SRV index returned by `GetOrAllocate()` must never change for a given texture resource index. This is achieved through:

1. Allocate SRV descriptor once during first call
2. Use `ResourceRegistry::Replace()` pattern to swap placeholder with final texture while preserving the descriptor
3. Follow the same pattern as `EnsureBufferAndSrv` in `UploadHelpers.cpp`

### Integration Requirements

**Dependencies**:

- `Graphics` (descriptor allocation, resource registry, texture creation)
- `UploadCoordinator` (texture data upload)
- `AssetLoader` (texture resource loading from PAK or loose files)

**Called By**:

- `MaterialBinder` during `SerializeMaterialConstants()`

**Lifecycle**:

- Created during `Renderer` initialization
- `OnFrameStart()` / `OnFrameEnd()` called by renderer

## 2. Error-Indicator Texture

### Purpose

Provide a visually obvious texture to indicate texture loading failures, preventing crashes and making errors immediately apparent during rendering.

### Rationale

The texture system does NOT provide semantic-specific defaults (white, flat normal, etc.). The material system already defines fallback PBR scalar values in `MaterialAsset::CreateDefault()` for cases where textures are intentionally absent. The texture system's responsibility is ONLY to indicate loading/creation failures with an obvious visual marker.

### Specification

**Error Texture**: 256×256 magenta and black checkerboard pattern

- **Format**: RGBA8_UNORM, Texture2D
- **Size**: 256×256 pixels (single mip level)
- **Pattern**: Alternating magenta (255, 0, 255, 255) and black (0, 0, 0, 255) checkerboard (32×32 pixel tiles)
- **Generation**: Programmatic (see Implementation Notes)

### Implementation Notes

**Implemented Approach**: Programmatic generation during `TextureBinder` initialization

- Generate RGBA8 checkerboard pattern in CPU memory
- Create D3D12 committed resource
- Upload via `UploadCoordinator::SubmitTexture2D()`
- Register SRV via `ResourceRegistry::Register()`

**Why Programmatic**:

- Always available (no asset loading dependency)
- Trivial generation (64-pixel pattern)
- Avoids chicken-and-egg problem (can't use asset loader to load the fallback for when asset loader fails)
- Single texture, not multiple semantic-specific assets

**Alternative**: Engine PAK asset (NOT recommended due to complexity and circular dependency)

### Usage Contract

- Error texture used ONLY when texture loading/creation fails
- Resource index 0 is a valid index and NOT mapped to error texture
- Error texture must be created and uploaded during `TextureBinder` initialization before any material binding

## 3. Texture Loading and Upload

### Input Contract

**Source**: `data::TextureResource` loaded via `AssetLoader` (from PAK or loose cooked files)

**Guaranteed Properties** (from asset cooker):

- Row pitch: 256-byte aligned
- Mip placement: 512-byte aligned
- Formats: RGBA8_UNORM, RGBA8_UNORM_SRGB
- Compression: NONE
- Type: 2D textures

### Lifecycle Phases

**Phase 1 - Immediate Response**:

- `GetOrAllocate()` creates small placeholder texture (1×1 recommended)
- Allocates and registers shader-visible SRV immediately
- Returns stable SRV index to caller
- Shaders can sample placeholder during loading

**Phase 2 - Async Loading**:

- **Status**: TODO (scaffolded but not implemented).
- `InitiateAsyncLoad()` currently logs a warning and keeps the placeholder.

**Phase 3 - Upload**:

- Create final GPU texture with correct dimensions/format/mips from `TextureResourceDesc`
- Submit upload via `UploadCoordinator::SubmitTexture2D()` with entire mip chain blob
- `UploadPlanner` handles per-mip region calculation automatically

**Phase 4 - Replacement**:

- Use `ResourceRegistry::UpdateView(texture, bindless_handle, view_desc)` to
  repoint the already-allocated shader-visible descriptor to the new texture.
  This preserves the stable SRV index returned by `GetOrAllocate()`.
- Same SRV index now references final texture
- Defer-reclaim old placeholder via `DeferredReclaimer` (TODO for async path;
  the demo override path replaces the shared_ptr directly)

### Error Handling Contract

**Current Implementation (Dec 2025)**:

- Async load failures are not yet applicable (async loading is TODO).
- Placeholder/descriptor allocation failures immediately return the global
  error texture SRV index.
- Demo force-error sentinel repoints the allocated descriptor to the error
  texture while keeping the per-resource stable SRV index.

## Material Binder Integration

### Current Problem

`MaterialBinder::SerializeMaterialConstants()` copies asset resource-table indices directly into `MaterialConstants`, but shaders require shader-visible SRV heap indices.

### Required Changes

**1. Dependency**: Add `TextureBinder` as constructor parameter and member

**2. Index Resolution**: Replace direct resource index copies with `texture_binder.GetOrAllocate(resource_index)` calls for all five texture fields

**3. Key Stability**: `MakeMaterialKey()` must continue hashing resource indices (NOT SRV indices) to ensure material handle stability across runs and independent of descriptor allocation order

### Contract

```cpp
// MaterialBinder must call TextureBinder during serialization
auto SerializeMaterialConstants(
  const MaterialAsset& material,
  TextureBinder& texture_binder
) -> MaterialConstants;

// Result: MaterialConstants contains shader-usable SRV indices
```

### Testing Requirements

- Same material asset always produces same material handle
- `MaterialConstants` texture fields contain valid SRV indices after serialization
- Error texture only used for resource load failures (not resource index 0)

## Trackable Implementation Tasks

This checklist captures all remaining implementation work implied by this
design (beyond the current scaffolding and placeholder/error texture creation).

### A. Binding + Index Semantics

- [x] A1. Define index semantics:
  - `kFallbackResourceIndex` returns placeholder
  - `UINT32_MAX` is reserved as an examples-only forced-error sentinel
- [x] A2. Ensure `GetOrAllocate()` always returns immediately with a valid SRV
  index (placeholder or error)
- [x] A3. Ensure SRV index stability: the SRV index allocated for a
  `resource_index` never changes for the lifetime of that mapping

### B. Placeholder / Error Textures

- [x] B1. Verify error texture pixel byte ordering produces the intended
  magenta/black checkerboard on all supported backends
- [x] B2. Confirm error texture is created and uploaded during binder
  initialization before any material binding

### C. Async Load (AssetLoader Integration)

- [ ] C1. Implement `InitiateAsyncLoad()` to request `data::TextureResource`
  from `AssetLoader` for a given `resource_index`
- [ ] C2. Define completion path (callback/future/job) that returns loaded
  texture metadata (dimensions, format, mip count) and the mip payload
- [ ] C3. Handle `AssetLoader` unavailable: keep placeholder without spamming
  logs (rate-limit or one-time warning per index)

### D. GPU Texture Creation + Upload (Mip Chain)

- [ ] D1. Create final `graphics::Texture` from `TextureResource` descriptor
  (type 2D, width/height, mip_levels, array_size, format)
- [ ] D2. Support formats: `RGBA8_UNORM` and `RGBA8_UNORM_SRGB` (SRV view format
  must match the created resource)
- [ ] D3. Upload full mip chain blob (not only mip 0)
- [ ] D4. Validate asset assumptions: row pitch alignment (256B) and mip
  placement (512B); reject/log if violated

### E. Stable Replacement (Keep Descriptor Index)

- [x] E1. Allocate the SRV descriptor exactly once at first `GetOrAllocate()`
  for each `resource_index`
- [x] E2. Preserve descriptor index by updating the view in-place
  (`ResourceRegistry::UpdateView`) rather than allocating a new descriptor
- [x] E3. Update the registered SRV view description as needed (format, mip
  levels, subresource range)
- [ ] E4. Ensure the old placeholder resource is deferred-reclaimed after the
  swap (via existing deferred reclaim infrastructure)

### F. Error Handling + Retry Semantics

- [x] F1. Forced-error sentinel keeps the allocated SRV index stable and
  switches the descriptor to reference the error texture
- [ ] F2. Define upload failure behavior: keep placeholder active, log error,
  and allow retry (explicit or opportunistic)
- [ ] F3. Define creation/format mismatch behavior: reject, log, and use error
  indicator texture via stable index

### G. Frame Lifecycle + Work Pumping

- [ ] G1. Implement `OnFrameStart()` to pump completed loads and schedule GPU
  creation/uploads for newly-ready textures
- [ ] G2. Implement `OnFrameEnd()` to finalize per-frame work and perform any
  safe cleanup/deferred releases

### H. Threading + Synchronization

- [ ] H1. Define thread ownership rules for `texture_map_` and registry
  operations (render thread only vs multi-threaded)
- [ ] H2. If async completions occur off-thread, marshal registry/graphics
  calls back onto the render thread (or add proper synchronization)

### I. MaterialBinder Contract (Verification)

- [x] I1. Keep `MakeMaterialKey()` hashing resource indices (NOT SRV indices)
  to preserve material handle stability
- [x] I2. Ensure `MaterialConstants` always contain shader-visible SRV indices
  after serialization for all five texture slots

### J. Tests

- [ ] J1. Add a test that the same `MaterialAsset` produces the same material
  handle across calls
- [ ] J2. Add a test that `MaterialConstants` texture fields are not raw
  resource indices (i.e., they come from `TextureBinder`)
- [ ] J3. Add a test that resource index `0` does not map to the error texture
  and remains valid

## Summary

**Key Guarantees**:

1. Stable SRV indices: same PAK index always returns same SRV index
2. PAK index 0 is valid and maps to a real texture resource
3. Only `0xFFFFFFFF` indicates invalid bindless index
4. Async loading with immediate placeholder availability
5. Material handle stability via PAK-index-based keying

**Integration Points**:

- TextureBinder created during Renderer init
- MaterialBinder depends on TextureBinder
- Both use existing UploadCoordinator/ResourceRegistry/DescriptorAllocator
- Follow EnsureBufferAndSrv pattern for resource replacement
