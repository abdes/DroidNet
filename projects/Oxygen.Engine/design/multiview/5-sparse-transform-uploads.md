# Multi-View Implementation: Sparse Transform Uploads

This document details the implementation strategy for handling sparse transform uploads in a multi-view rendering context, ensuring efficient resource usage and stable bindless indexing.

## 1. Philosophy & Impact

### 1.1. Frame-Centric Uploads
The **Upload Module** remains strictly **frame-centric**. It is agnostic to the number of views.
-   **Invariant**: `UploadCoordinator::OnFrameStart` is called exactly **once per frame**.
-   **Invariant**: The Upload Module does not know which view is currently rendering.

### 1.2. The "Upload Once" Rule
With multi-view, we break the coupling between "Culling" and "Uploading". We upload a **superset** of data (all active objects) to ensure validity across all views.
-   **Impact**: Increased upload volume per frame (Active Set vs. Visible Set).
-   **Mitigation**: `RingBufferStaging` must be sized to accommodate the "worst-case" scene state.

### 1.3. Stable Indexing & Hazards
Bindless architecture relies on **Stable Indices** (e.g., Object 42 is always at Index 42).
-   **Challenge**: We cannot simply pack updated transforms into a dense array without breaking the index.
-   **Hazard**: Updating a persistent buffer in-place risks GPU race conditions (writing while the previous frame reads).

## 2. Implementation Strategy: "Transient Frame Buffer"

We adopt a **Transient Frame Buffer** strategy (Strategy A) as the robust MVP.

### 2.1. Mechanism
Instead of updating a persistent buffer, we allocate a **fresh, transient buffer** every frame that contains *all* active transforms.

1.  **Allocate**: At `OnFrameStart`, `TransformUploader` allocates a slice from `RingBufferStaging` equal to `MaxActiveIndex * sizeof(Transform)`.
2.  **Write**: CPU writes *all* active transforms to this mapped memory. Gaps (inactive indices) can be skipped or zeroed.
3.  **Bind**: We create a **Transient SRV** for this specific allocation (sub-range of the Staging Buffer).
4.  **Update Constants**: The `SceneConstants` for the current frame are updated to point to this new SRV index.

### 2.2. Why it works
-   **Synchronization**: `RingBufferStaging` guarantees the memory is valid for the current frame and won't be overwritten until the GPU is done with it (via `RetireCompleted`).
-   **Stable Indices**: Object 42 writes to offset `42 * 64`. The shader reads `Buffer[42]`. Since `Buffer` is the *new* SRV, it reads the correct data.
-   **Simplicity**: No complex compute shaders or sparse merging logic required.

## 3. New Component: `TransientStructuredBuffer`

To implement this, we introduce a lightweight wrapper around `RingBufferStaging` called `TransientStructuredBuffer`. It replaces `AtlasBuffer` for dynamic data.

### 3.1. Class Design

```cpp
class TransientStructuredBuffer {
public:
    // Allocates memory from RingBufferStaging and creates a Transient SRV
    auto Allocate(uint32_t element_count) -> void {
        allocation_ = staging_provider_->Allocate(element_count * stride_);

        // Create Transient SRV locally
        auto& allocator = gfx_->GetDescriptorAllocator();
        auto handle = allocator.Allocate(kStructuredBuffer_SRV, kShaderVisible);

        BufferViewDescription view_desc;
        view_desc.view_type = kStructuredBuffer_SRV;
        view_desc.range = { allocation_.offset.get(), allocation_.size.get() };
        view_desc.stride = stride_;

        // Create View on the Staging Buffer
        gfx_->GetResourceRegistry().CreateView(*allocation_.buffer, handle, view_desc);

        srv_index_ = allocator.GetShaderVisibleIndex(handle);
    }

    auto GetBinding() const -> Binding { return { srv_index_, stride_ }; }
    auto GetMappedPtr() -> void* { return allocation_.mapped_ptr; }

    // Called at frame end to release the SRV descriptor
    auto Reset() -> void {
        if (srv_index_ != kInvalid) {
             gfx_->GetDescriptorAllocator().Free(srv_index_);
             srv_index_ = kInvalid;
        }
    }
};
```

### 3.2. Required API Enhancements
1.  **`RingBufferStaging`**: Ensure internal buffer is registered with `ResourceRegistry` (in `EnsureCapacity`) so views can be created.

## 4. Comparison: Atlas vs. Transient

| Feature | Component | Usage Pattern | Why? |
| :--- | :--- | :--- | :--- |
| **Static / Persistent Data**<br>(Geometry, Materials) | **`AtlasBuffer`** | - Allocate once.<br>- Keep `ElementRef`.<br>- **Stable SRV**. | Data rarely changes. Manages fragmentation efficiently. |
| **Dynamic / Per-Frame Data**<br>(Transforms) | **`TransientStructuredBuffer`** | - Allocate every frame.<br>- **Transient SRV**.<br>- Auto-recycled. | Data changes every frame. Handles N-buffering and synchronization automatically. |

---

**Document Status**: Draft
**Last Updated**: 2025-12-02
**Part of**: Multi-View Rendering Design Series
