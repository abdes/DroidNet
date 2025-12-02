# Multi-View Implementation: Sparse Transform Uploads

This document details the implementation strategy for handling sparse transform uploads in a multi-view rendering context, ensuring efficient resource usage and stable bindless indexing.

## 1. Philosophy & Impact

### 1.1. Frame-Centric Uploads

The **Upload Module** remains strictly **frame-centric**. It is agnostic to the number of views.

- **Invariant**: `OnFrameStart` is called exactly **once per frame**.
- **Invariant**: The Upload Module does not know which view is currently rendering.

### 1.2. The "Upload Once" Rule

With multi-view, we break the coupling between "Culling" and "Uploading". We upload a **superset** of data (all active objects) to ensure validity across all views.

- **Impact**: Increased upload volume per frame (Active Set vs. Visible Set).
- **Mitigation**: `RingBufferStaging` sized to accommodate worst-case scene state.

### 1.3. Stable Indexing & Hazards

Bindless architecture relies on **Stable Indices** (e.g., Object 42 is always at Index 42).

- **Challenge**: Cannot pack updated transforms into a dense array without breaking the index.
- **Hazard**: Updating a persistent buffer in-place risks GPU race conditions (writing while the previous frame reads).

## 2. Implementation Strategy: "Transient Frame Buffer"

We adopt a **Transient Frame Buffer** strategy as the robust MVP.

### 2.1. Mechanism

Instead of updating a persistent buffer, we allocate a **fresh, transient buffer** every frame that contains *all* active transforms.

1. **Allocate**: `TransformUploader` allocates from `RingBufferStaging` for all transforms.
2. **Write**: CPU writes transforms to mapped memory via direct `memcpy`.
3. **Bind**: Create a **Transient SRV** for this allocation (sub-range of Staging Buffer).
4. **Coordinate**: `InlineTransfersCoordinator` tracks writes and manages retirement via synthetic fence values.

### 2.2. Why it works

- **Synchronization**: `RingBufferStaging` partitions memory by frame slot; guarantees memory is valid for current frame.
- **Stable Indices**: Object at index 42 writes to offset `42 * stride`. Shader reads `Buffer[42]` from the new SRV.
- **N-Buffering**: Ring buffer automatically cycles through partitions (Frame 0, Frame 1, ...).
- **Retirement**: `InlineTransfersCoordinator` advances synthetic fence counter; `RingBufferStaging` retires completed work.

## 3. Core Components

### 3.1. `TransientStructuredBuffer`

Wrapper around `StagingProvider` for per-frame transient structured buffers. Implements "Direct Write" strategy (CPU-visible Upload Heap directly readable by GPU over PCIe).

**Key Features**:

- Allocates from `RingBufferStaging` and creates transient SRV per frame/view
- N-buffering handled by underlying ring buffer (no manual fence tracking)
- Per-slot data: maintains allocation, SRV index, and native view per frame slot
- Automatic cleanup via `ResetSlot()` and descriptor unregistration

**Usage Pattern**: `OnFrameStart(slot)` → `Allocate(count)` → write to `GetMappedPtr()` → bind via `GetBinding().srv`

### 3.2. `InlineTransfersCoordinator`

Manages inline write coordination across frame boundaries without actual GPU copy commands.

**Responsibilities**:

- Tracks pending inline write bytes (via `NotifyInlineWrite`)
- Generates synthetic fence values (monotonic counter, no GPU query)
- Broadcasts `OnFrameStart` and `RetireCompleted` to registered `StagingProvider` instances
- Manages weak references to providers (auto-cleanup on provider destruction)

**Integration**: `TransformUploader`, `DrawMetadataEmitter` notify coordinator of writes; coordinator cycles retirement to advance ring buffer partitions.

### 3.3. `RingBufferStaging`

Ring allocator over a single persistently-mapped upload buffer with partition-based N-buffering.

**Key Features**:

- Partitions buffer by frame slot count (e.g., 3 slots for triple buffering)
- Linear bump allocation within active partition; resets head on slot change
- Auto-grows with slack factor when capacity insufficient
- Registers backing buffer with `ResourceRegistry` (required for `TransientStructuredBuffer` view creation)
- Tracks retirement via fence values; warns on partition reuse without retirement

**Alignment Requirement**: Stride must be multiple of alignment for structured buffers (D3D12 SRV FirstElement constraint).

### 3.4. `TransformUploader` & `DrawMetadataEmitter`

Both use `TransientStructuredBuffer` for per-frame uploads:

- **TransformUploader**: Worlds and normals buffers allocated each frame, writes via `memcpy`, caches SRV indices
- **DrawMetadataEmitter**: Draw metadata buffer allocated per frame/view, sorts and partitions draws, uploads sorted array

**Common Pattern**: `OnFrameStart` → collect data → `EnsureFrameResources` (allocate + write) → provide SRV index to renderer

## 4. Comparison: Atlas vs. Transient

| Feature | Component | Usage Pattern | Why? |
| :--- | :--- | :--- | :--- |
| **Static / Persistent Data** (Geometry, Materials) | **`AtlasBuffer`** | Allocate once, Keep `ElementRef`, **Stable SRV**. | Data rarely changes. Manages fragmentation efficiently. |
| **Dynamic / Per-Frame Data** (Transforms, Draws) | **`TransientStructuredBuffer`** | - Allocate every frame., **Transient SRV**, Auto-recycled. | Data changes every frame. N-buffering and synchronization automatic. |

---

## Implementation Status

| Component/Feature | Status | Location | Notes |
|-------------------|--------|----------|-------|
| **TransientStructuredBuffer** | ✅ Complete | `src/Oxygen/Renderer/Upload/TransientStructuredBuffer.{h,cpp}` | Full implementation with per-slot allocation, SRV management, and cleanup |
| **RingBufferStaging** | ✅ Complete | `src/Oxygen/Renderer/Upload/RingBufferStaging.{h,cpp}` | Partition-based ring allocator with N-buffering, auto-growth, ResourceRegistry integration |
| **InlineTransfersCoordinator** | ✅ Complete | `src/Oxygen/Renderer/Upload/InlineTransfersCoordinator.{h,cpp}` | Synthetic fence management, provider lifecycle, frame start/retirement broadcast |
| **TransformUploader using Transient** | ✅ Complete | `src/Oxygen/Renderer/Resources/TransformUploader.cpp` | Uses `TransientStructuredBuffer` for worlds/normals, direct memcpy, SRV caching |
| **DrawMetadataEmitter using Transient** | ✅ Complete | `src/Oxygen/Renderer/Resources/DrawMetadataEmitter.cpp` | Uses `TransientStructuredBuffer` for draw metadata, sort/partition, per-frame upload |
| **Frame-Centric Upload Invariants** | ✅ Complete | N/A | OnFrameStart called once per frame, view-agnostic upload confirmed |
| **Stable Indexing** | ✅ Complete | N/A | Transform/draw indices stable across frames, bindless compatible |
| **N-Buffering & Synchronization** | ✅ Complete | N/A | Ring buffer partitions + synthetic fences handle multi-frame overlap |
| **ResourceRegistry Integration** | ✅ Complete | `RingBufferStaging.cpp:131` | Backing buffer registered in `EnsureCapacity()` for view creation |
| **Direct Write Strategy** | ✅ Complete | N/A | CPU-visible upload heap, zero-copy, PCIe reads confirmed |

**Summary**: Feature is **100% complete**. All core components implemented and integrated. The design document now accurately reflects the actual implementation including `InlineTransfersCoordinator`, per-slot management, and ResourceRegistry integration.

**Architecture Highlights**:

- **Zero-Copy Pipeline**: CPU writes directly to mapped upload heap; GPU reads over PCIe (no CopyBufferRegion needed)
- **Automatic Synchronization**: Ring buffer partitioning + synthetic fence retirement eliminates manual fence tracking
- **Flexible N-Buffering**: Configurable slot count (e.g., triple buffering) via `frame::SlotCount`
- **Descriptor Lifecycle**: Per-slot SRV allocation/release with proper ResourceRegistry integration

---

**Document Status**: Draft
**Last Updated**: 2025-12-02
**Part of**: Multi-View Rendering Design Series
