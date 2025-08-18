# 📘 Nexus Working Design Document

## 1. Introduction

**Nexus** is the Unified GPU Resource Manager (UGRM) and the orchestration point
for ScenePrep Finalization. It is the authoritative gatekeeper for GPU
residency: it decides what enters the GPU, what remains resident, and what gets
evicted. Nexus is bindless-first, batch-oriented, and supports both GPU-backed
and CPU-only configurations. Finalization priority and upload order are
deterministic: transforms → materials → geometry → textures.

## 2. ✅ Struct Layout and Naming Semantics

This section defines the rules for struct design, naming, and validation within
Nexus. The goal is to ensure absolute consistency between CPU and GPU
representations, maintain semantic clarity across the codebase, and enable
future‑proof extensibility.

### 2.1 Alignment Rules and Cross‑Language Consistency

All GPU‑facing data structures must have a stable binary layout across **C++**,
**HLSL**, and **GLSL**.

- **Alignment Targets**
  - Vectors (`float4`, `int4`): 16‑byte alignment minimum.
  - Root constants / CBVs (D3D12): 256‑byte alignment per hardware constant
    buffer requirements.
  - SSBOs / UAVs: Use `std430`‑like packing for Vulkan/GLSL to avoid unexpected
    padding. For D3D12, follow its alignment rules, which are similar but not
    identical to `std430`.

- **Size Enforcement**
  - Apply `static_assert(sizeof(T) % 16 == 0)` to all GPU‑facing structs.
  - Use `alignas(16)` or higher where required to match shader expectations.

- **Binary Layout Identification**
  - At build time, generate a `LayoutId` hash for each GPU‑facing struct.
  - Embed the hash in both shader and C++ definitions.
  - Validate at runtime (debug builds only) to detect ABI mismatches before
    rendering.

- **Platform Notes**
  - Pad explicitly to account for platform or compiler ABI quirks.
  - Keep layouts endian‑safe if data will be serialized or streamed
    cross‑platform.

### 2.2 Root Constants vs. Descriptor Tables

Define a strict separation between lightweight root constants and bindless
descriptor resources. The `RenderPass` base class enforces this separation
across all passes, ensuring consistency and adherence to best practices.

- **Root Constants**
  - Used for small, frequently updated values (e.g., frame constants, draw
    indices).
  - The `kDrawIndexConstant` is a key example, used to bind per-draw indices
    efficiently.
  - The `RenderPass` base class ensures root constants are bound correctly
    before each draw call.

- **Descriptor Tables**
  - All buffers, textures, and samplers live in bindless descriptor tables.
  - Descriptor tables are bound in the root signature and accessed via shaders.
  - The `RenderPass` base class enforces the use of descriptor tables for SRVs,
    ensuring stable descriptor ranges and efficient GPU access patterns.

#### Binding Discipline

- Keep descriptor ranges stable across frames to minimize driver work.
- Separate per-frame constants from per-draw constants in distinct root slots.
- The `RenderPass` base class validates root signature layouts and ensures
  alignment with the Nexus binding model.

### 2.3 Semantic Naming Conventions

Type and member names must communicate **role**, **lifetime**, and **scope**
unambiguously.

- **Prefixes**
  - `Gpu*` → GPU‑facing structures mirrored in shaders.
  - `*Handle` → Bindless index into a GPU resource table.
  - `*Id` → Immutable registry identifier or asset key.
  - `*View` → Transient, read‑only view of a resource.
- **Suffixes**
  - `Buffer`, `Texture`, `Sampler`, `Table` to indicate resource type
    explicitly.
- **Guidelines**
  - Avoid abbreviations in public or shared headers.
  - Use consistent terminology between CPU and GPU code for equivalent data.

### 2.4 Validation and Enforcement

Automated validation ensures struct safety and semantic correctness throughout
the pipeline.

- **Static Analysis**
  - static_assert when applicable to enforce contracts at compile time.
  - Clang‑Tidy rules enforce naming patterns and banned abbreviations.
- **Build‑Time Checks**
  - Generated tooling compares struct fields and offsets against shader
    reflection data.
  - CI fails on any mismatch.
- **Runtime Debugging**
  - Optional debug pass in the first draw validates `LayoutId` matches. Enabled
    only in debug builds to avoid performance overhead.
  - Logs show expected vs. actual layout hash, plus offending struct details.

### 2.5 Best‑Practice Considerations

- **Forward/Backward Compatibility**
  - Use version tags for structs that may evolve.
  - Retain deprecated fields until all dependent shaders are updated. Delete
    them as soon as the shaders are migrated.
- **Cache Behavior**
  - Cluster hot fields for coherent access; move cold fields to the end.
- **Serialization**
  - Match on‑disk or streamed layouts to GPU layout when possible to avoid
    costly repacking.
- **Cross‑Compiler Safety**
  - Run startup layout verification shaders to detect compiler/driver packing
    deviations.

## 3. Bindless Indices Management

Nexus provides a unified, stable indexing system for all GPU‑visible resources
to support a bindless‑first rendering model. The system guarantees that any
shader‑visible index remains valid for the lifetime of its resource and prevents
aliasing between active and freed slots.

### 3.1 Global Indexing Scheme

- Maintain a **unified descriptor heap** for all resource types (SRV/UAV/CBV),
  as required by D3D12.
- Logical segmentation is achieved through descriptor ranges within a single
  descriptor table at root parameter 0.
  - Range 0: A single CBV at register `b1` (heap index 0).
  - Range 1: Unbounded SRVs at register `t0`, space0 (heap indices 1+).
- The flag `D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED` enables
  direct indexing into the descriptor heap.
- All shaders reference resources via stable indices provided by the allocator.
- Indices are **stable** for the lifetime of an object.
- Reuse occurs **only** after a **generation counter** increment to detect stale
  references.
- The allocator ensures efficient resource management and stable indexing
  without requiring physically separate tables.

### SSoT: `Spec.yaml` (authoritative)

The single source-of-truth for bindless slot/register mapping is
`src/Oxygen/Core/Bindless/Spec.yaml`. Key facts taken from that file:

- Domains and domain bases:
  - `scene` (CBV) : register `b1`, heap index 0, domain_base=0, capacity=1
  - `srv_global` (SRV) : register `t0, space0`, domain_base=1, capacity=2048
  - `materials` (SRV) : register `t1, space0`, domain_base=2049, capacity=3047
  - `textures` (SRV) : register `t2, space0`, domain_base=5096, capacity=65536
  - `samplers` (SAMPLER) : register `s0, space0`, domain_base=0, capacity=256

- Heaps (mirror D3D12 allocator defaults):
  - `heap_cbv_srv_uav_cpu` (index 0, cpu-only)
  - `heap_cbv_srv_uav_gpu` (index 1, shader-visible)
  - `heap_sampler_cpu` (index 2, cpu-only)
  - `heap_sampler_gpu` (index 3, shader-visible)
  - `heap_rtv_cpu` (index 4, cpu-only)
  - `heap_dsv_cpu` (index 5, cpu-only)
  - Note: These heaps are configured at runtime from a generated JSON spec
    embedded in `Generated.Heaps.D3D12.h` and loaded by
    `D3D12HeapAllocationStrategy` (see Section 3.7). The generator derives this
    JSON from `Spec.yaml` to keep CPU, tooling, and shaders in sync.

- Mappings tie domains → heaps and provide local base indices. The generator
  consumes this file and emits `BindingSlots.h`, `BindingSlots.hlsl` and a
  runtime JSON descriptor used by the engine/tooling. The JSON is embedded in
  `src/Oxygen/Core/Bindless/Generated.Heaps.D3D12.h` as `kD3D12HeapStrategyJson`
  and consumed by `D3D12HeapAllocationStrategy::InitFromJson(...)`.

### 3.2 Index Lifetime, Reuse, and Aliasing Prevention

- Use a clear separation of concerns between the shader-visible index and a
  CPU-only generation counter:
  - `BindlessHandle` (shader-visible) is a strongly-typed 32-bit index exposed
    to shaders and stored in draw/dispatch data.
  - `VersionedBindlessHandle` (CPU-side) pairs the `BindlessHandle` with a
    CPU-only `generation` value. The runtime increments the generation when a
    slot is actually recycled.
- Shader code sees only the 32-bit `BindlessHandle` (no packing of generation
  into shader-visible bits). CPU-side validation compares the handle's stored
  generation against a shadow per-slot generation to detect stale references.
- Generation-based safety is the primary mechanism to prevent aliasing:
  - On Release, the allocator/registry may return the descriptor index to a free
    pool (implementation dependent). The runtime must increment the slot's
    generation only when the slot becomes safe to reuse (for example, after a
    fence or when the renderer confirms no in-flight references). Reused slots
    carry a new generation. Any `VersionedBindlessHandle` with an older
    generation is treated as stale and rejected in debug validation.
- **Sentinel values**:
  - Reserve only `oxygen::engine::kInvalidBindlessIndex` (equal to
    `std::numeric_limits<uint32_t>::max()`) for invalid handles. `0` is a valid
    index.
  - Shaders must safely handle invalid indices by branching to default
    resources. **Deferred slot reuse (generation-based policy)**:
- Slots freed mid‑frame may be returned to allocator free lists by low-level
  segments (see `FixedDescriptorHeapSegment::Release`). Relying on immediate
  free-list recycling alone risks aliasing; therefore the canonical safety
  mechanism is a CPU-only generation counter per slot.
- On release, the runtime must ensure a slot's `generation` is incremented only
  when it is safe to reuse (typically when a pending fence/frame completes).
  Implementations may either delay free-list insertion until that point or
  record the freed index in a pending queue and apply the generation bump
  atomically when safe. In either case, the generation transition is the
  authoritative event that enables reuse.
- `PerFrameResourceManager` / `DeferredObjectRelease.h` complements generation
  tracking by deferring object destruction; use it to ensure native resources
  remain valid until GPU work that references them has completed.

```cpp
// Strongly typed 32‑bit shader‑visible handle
using BindlessHandle = oxygen::NamedType<uint32_t, struct _BindlessHandleTag>;

// CPU-side versioned handle for validation and safe reuse tracking.
struct VersionedBindlessHandle {
  BindlessHandle index;     // shader-visible 32-bit index
  uint32_t       generation; // CPU-only generation counter

  VersionedBindlessHandle() noexcept : index(BindlessHandle{0u}), generation(0u) {}
  VersionedBindlessHandle(BindlessHandle idx, uint32_t gen) noexcept
    : index(idx), generation(gen) {}
};
```

### 3.3 Shader Contract Integration and Validation

- **Contract**:
  - All shaders access resources via **bindless global indices**.
  - Root constants and descriptor tables are validated and managed by the
    `RenderPass` base class.
  - Passes like `DepthPrePass` and `ShaderPass` inherit these best practices,
    ensuring consistent shader access to resources.

- **Pipeline Initialization Checks**:
  - Validate descriptor heap sizes match Nexus configuration.
  - Verify root signature layouts against Nexus binding model.
  - Emit a clear failure if any shader requests an index outside the configured
    range.

### 3.4 Debugging Strategies for Index Mismatches

- **Optional Validation Layer**:
  - Validate the binding of root constants and descriptor tables during pipeline
    initialization.
  - Ensure that `kDrawIndexConstant` and descriptor tables are correctly set up
    before execution.

### 3.5 Additional Considerations

- **Thread Safety**:
  - Allocation/free operations must be lock‑free or fine‑grained locked for
    Phase 2 parallel uploads.
- **Streaming Scenarios**:
  - Consider pre‑allocating index ranges for streaming systems to minimize
    contention.
- **Type Safety**:
  - Distinguish index types at compile time (e.g., `TextureHandle` vs
    `BufferHandle`) to prevent cross‑table misuse.
- **Future Extension**:
  - Potential move to type‑erased unified resource table with runtime type
    tagging.

### 3.6 Implementation status & TODO (bindless indices management)

The codebase contains foundational pieces for bindless rendering (descriptor
allocator, D3D12 root-signature flags, single-table usage), but several items
are missing to fully meet Section 3’s contract around stable indices,
lifetime/generation, validation, and type safety.

- [x] Bindless mapping specification and single source-of-truth
  - Action: Specify the bindless slot/register mapping once and generate headers
    for both C++ and HLSL. The generator now emits the canonical artifacts
    consumed by engine and shaders.
    - Align authoritative slot mapping to `b1` for `SceneConstants` across code
      and shaders (remove lingering `b0`).
    - Emit generated artifacts used at runtime: `Generated.Constants.h`,
      `Generated.RootSignature.h`, `Generated.Meta.h`, `Generated.Heaps.D3D12.h`
      and `Generated.All.json` (embedded JSON). These are the authoritative
      outputs of the BindlessCodeGen tool.
    - Tool: BindlessCodeGen v1.1.0 — adds rich root-signature metadata and
      timestamp strategies (preserve/omit/git-sha).
  - Files: `src/Oxygen/Renderer/RenderPass.h/.cpp`,
    `src/Oxygen/Renderer/Renderer.cpp`,
    `src/Oxygen/Graphics/Direct3D12/Shaders/FullScreenTriangle.hlsl`,
    `src/Oxygen/Core/Bindless/Generated.Constants.h`,
    `src/Oxygen/Core/Bindless/Generated.RootSignature.h`,
    `src/Oxygen/Core/Bindless/Generated.Meta.h`,
    `src/Oxygen/Core/Bindless/Generated.Heaps.D3D12.h`.

- [x] Unified direct indexing flags (CBV/SRV/UAV + Sampler)
  - Status: Done. All D3D12 root signatures now set both direct-indexing flags
    (CBV/SRV/UAV and Sampler). A debug-time assertion validates the presence of
    these flags at pipeline creation.
  - Action: Enforced flags in central RS creation and added DCHECK in debug
    builds. Future passes must use the centralized creation helpers or mirror
    this invariant.
  - Files: `src/Oxygen/Graphics/Direct3D12/CommandRecorder.cpp`,
    `src/Oxygen/Graphics/Direct3D12/Detail/PipelineStateCache.cpp`.

- [x] Single shader-visible heap + descriptor table binding discipline
  - Status: Partial. Command recorder binds descriptor heaps and a single
    unbounded SRV descriptor table (`t0`, space0). The generator now emits
    machine-readable JSON and modern C++ headers so passes can validate and
    consume the canonical layout. Renderer passes have been updated to build
    root bindings from the generated `kRootParamTable` (via
    `Detail/RootParamToBindings`), but explicit descriptor-heap
    SetDescriptorHeaps and per-pass sampler-table binding remain to be completed
    in some paths.
  - Action: In `RenderPass::Execute`, bind the CBV/SRV/UAV heap and the sampler
    heap exactly once per pass begin (bind-on-change later). Validate that the
    SRV table (`t0`, space0) and sampler table are set before any draw/dispatch.
  - Files: `src/Oxygen/Renderer/RenderPass.cpp`,
    `src/Oxygen/Graphics/Common/CommandRecorder.*`,
    `src/Oxygen/Renderer/Detail/RootParamToBindings.{h,cpp}`.

- [x] Stable global indices: domain bases and capacity budgeting
  - Status: Implemented. Allocator APIs expose stable per-domain base indices
    and reservation-based capacity budgeting; Reserve(...) returns
    `std::nullopt` for unsupported or over-capacity requests.
  - Action: Use the allocator APIs for domain budgeting and to compute
    shader-visible indices as `base + slot`.
  - Files (implementation & tests):
    - `src/Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h`
    - `src/Oxygen/Graphics/Common/Test/Bindless/BaseDescriptorAllocator_Domain_test.cpp`
    - `src/Oxygen/Graphics/Direct3D12/Test/HeapAllocationStrategy_domain_test.cpp`
    - `src/Oxygen/Graphics/Direct3D12/Bindless/D3D12HeapAllocationStrategy.h`
      (provider wiring)
  - Notes: The D3D12 provider test demonstrates honoring `base_index` from
    embedded JSON; tests assert Reserve-exceeding-capacity returns no value.

- [ ] BindlessHandle type and invalid sentinel
  - Status: Partial. The generated headers already provide the invalid sentinel
    `oxygen::engine::kInvalidBindlessIndex` in `Generated.Constants.h`. A
    strongly-typed `BindlessHandle` alias/wrapper is still missing and should be
    introduced to improve type-safety across renderer and ScenePrep.
  - Action: Introduce `using BindlessHandle = oxygen::NamedType<uint32_t, struct
    _BindlessHandleTag>;` and adopt it in new APIs. Files: new header (e.g.,
    `src/Oxygen/Renderer/Binding/BindlessHandle.h`) and updates in ScenePrep and
    renderer code.

- [ ] Deferred slot reuse and generation increment policy
  - Status: Partial. The design requires that recycled slots carry a new
    generation so CPU-side handles can detect staleness. The codebase provides
    `PerFrameResourceManager` to defer object destruction; allocator segments
    currently recycle indices immediately (see `FixedDescriptorHeapSegment`).
  - Action: Define the authoritative transition point where a slot's generation
    is incremented (for example, on fence/frame completion). Then either delay
    insertion of freed indices into the segment free list until that transition,
    or atomically record a generation bump at reuse and ensure CPU-side
    `VersionedBindlessHandle` validation will reject older generations.
  - Files: `src/Oxygen/Graphics/Common/ResourceRegistry.h/.cpp`,
    `src/Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h`,
    `src/Oxygen/Graphics/Common/Detail/FixedDescriptorHeapSegment.*`,
    renderer/frame sync in `src/Oxygen/Renderer/Renderer.cpp` (or
    coordinator/maestro).

    Option A — Fence-gated delayed recycle (design) Description:
    - Freed slots are not immediately visible for reuse. Instead, ReleaseSlot
      records the freed index along with the current completed fence/frame value
      (a `pending_fence` / `pending_frame_index`) and places the entry into a
      per-domain pending-recycle queue. When the renderer observes that the
      associated fence/frame has completed, it performs a generation increment
      for the slot and moves the index into the allocator's free list.

    Data shape (per-slot metadata):
    - index: uint32_t (shader-visible index)
    - generation: uint32_t (CPU-only generation)
    - state: enum {Invalid, Staged, Resident, PendingEvict, Evicted, Free}
    - pending_fence_or_frame: optional<uint64_t> (fence or completed-frame id)
    - last_used_frame, size_bytes, resident_mip_mask, resource_key

    API sketches (minimal):
    - ResourceRegistry::ReleaseSlot(VersionedBindlessHandle h, uint64_t
      pending_fence)
      - Marks slot PendingEvict, records pending_fence_or_frame :=
        pending_fence, and appends (index, pending_fence) to a per-domain
        pending queue. Does not immediately push to segment free list.
    - Renderer::OnFenceCompleted(uint64_t completed_fence) /
      PerFrameResourceManager::OnBeginFrame(frame_index)
      - Iterate pending queues and for each entry where pending_fence_or_frame
        <= completed_fence, atomically increment slot.generation, clear
        pending_fence_or_frame, and insert index into allocator/segment free
        list.

    Concurrency & correctness notes:
    - All modifications to slot metadata and pending queues should be protected
      by a fine-grained lock per-domain or done on the renderer thread during
      OnBeginFrame to avoid contention.
    - Incrementing generation must be atomic with respect to making the slot
      available; publish the new generation before any consumer may allocate the
      index and create a new VersionedBindlessHandle with that generation.
    - Pair with `PerFrameResourceManager` to defer actual object destruction
      until the same completed_fence/frame has been observed; this prevents the
      native resource from being destroyed while GPU may still reference it.

    Tests to add:
    - Unit: Allocate → ReleaseSlot(pending_fence=F) → before fence completion,
      Allocate must not return the same index. After simulated fence completion
      and OnFenceCompleted(F), allocation should be able to return the index
      with incremented generation.
    - Integration: Submit a command that references a slot, ReleaseSlot in the
      same frame, ensure slot is not reused until frame/fence observed and that
      CPU-side validation detects stale handles when generation mismatches.

- [ ] Shader invalid-sentinel handling and fallbacks
  - Status: Mixed. Shaders use bindless tables; explicit invalid-sentinel branch
    and default resources are not consistently enforced.
  - Action: Define a shared sentinel in shader headers; ensure all shaders
    branch to defaults when the handle equals `kInvalidBindlessIndex`. Provide
    default resources (white/black textures, null buffers) and keep them
    permanently resident.
  - Files: shader includes under `src/Oxygen/Graphics/Direct3D12/Shaders/`,
    shared binding header.

- [ ] Type-safe handle wrappers per resource category
  - Status: Missing. Cross-table misuse prevention (e.g., TextureHandle vs
    BufferHandle) not enforced at compile time.
  - Action: Add lightweight `NamedType<uint32_t, Tag>` wrappers for texture,
    buffer, sampler indices. Conversions to/from `BindlessHandle` are explicit.
    Use in draw metadata and scene-prep.
  - Files: new header (e.g., `src/Oxygen/Renderer/Binding/ResourceHandles.h`),
    updates in `ScenePrep` and passes.

- [ ] Thread-safety and streaming pre-allocation
  - Status: Partial. Allocator segments use mutexes; no per-domain pre-reserved
    index ranges for streaming systems.
  - Action: For Phase 1, use fine-grained locks per domain in the
    registry/allocator; add an optional API to pre-reserve contiguous index
    ranges for high-churn streaming domains to minimize contention.
  - Files: `src/Oxygen/Graphics/Common/DescriptorAllocator.h`,
    `src/Oxygen/Graphics/Common/ResourceRegistry.h`.

- [ ] Optional validation layer toggles
  - Status: Missing. No runtime toggles to enable/disable validation of indices
    and bindings.
  - Action: Add debug-only configuration (env var or engine flag) to enable:
    generation checks, heap/range checks, and root-binding validation during
    pipeline init and first draw.
  - Files: small config in `src/Oxygen/Renderer/`, plumbed through `RenderPass`
    and `ResourceRegistry`.

- [ ] Tests for indices, generation, and recycling
  - Status: Missing.
  - Action: Add unit tests for: allocate/release with generation increment;
    deferred recycling after fences; bounds validation; typed-handle misuse
    (compile-time/static-assert where applicable). Include simple integration
    test that publishes indices into a mock draw and verifies shader index
    bounds.
  - Files: `src/Oxygen/Graphics/Common/Test/`, `src/Oxygen/Renderer/tests`
    (new).

## 4. Logical Structuring of the Unified Descriptor Heap

The unified descriptor heap in Nexus is designed to accommodate all GPU-visible
resources required for rendering, including vertex buffers, index buffers,
structured buffers, material constants, and transform data. Logical segmentation
within the heap ensures efficient resource management and compatibility with the
bindless rendering model.

### 4.1 Descriptor Ranges and Logical Tables

The unified descriptor heap in Nexus is logically segmented into descriptor
ranges, each serving a specific purpose. These ranges ensure efficient resource
management and compatibility with the bindless rendering model. Below is a
detailed breakdown of each range, along with real-world scenarios from game
engine development:

- **Range 0: Constant Buffers (CBV)** (Completed)
  - **Register**: `b1`
  - **Heap Index**: 0
  - **Usage**: Stores per-frame constants, such as scene-wide parameters, or
    fallback constants for legacy systems. Additionally, it holds the **bindless
    indices table**, which is critical for accessing all other resources in the
    unified descriptor heap. The `SceneConstants` buffer is updated dynamically
    in Oxygen, ensuring that renderer-managed fields are refreshed every frame
    and uploaded to the GPU only when changes occur.
  - **Example**: In Oxygen, this range holds the `SceneConstants` buffer, which
    includes the view matrix, projection matrix, and camera position. The
    **bindless indices table** is used by Oxygen's shaders to dynamically fetch
    resources like material constants, textures, and structured buffers. This
    design ensures efficient resource access and eliminates the need for
    frequent CPU-GPU synchronization for resource bindings.

- **Range 1: Shader Resource Views (SRV)** (Completed)
  - **Register**: `t0`, space0
  - **Heap Indices**: 1+
  - **Usage**: Allocated for structured buffers essential for geometry
    processing, such as vertex buffers, index buffers, and transform data.
  - **Example**: When rendering a complex 3D model, the vertex buffer
    (containing positions, normals, and texture coordinates) and the index
    buffer (defining the mesh topology) are stored in this range. These buffers
    are accessed by the vertex shader to transform vertices into screen space.

- **Range 2: Material Constants (domain within SRV t0)** (Partial)
  - **Register**: `t0`, space0 (unified SRV table; addressed via a domain base
    index)
  - **Heap Indices**: Occupies slots inside the unified SRV table starting at
    the materials domain base.
  - **Usage**: Stores per-material data (base color, roughness, flags, etc.) in
    a SoA/packed structured buffer. Materials are indexed by material ID and
    fetched bindlessly by shaders. Materials generally have different residency
    and update characteristics from textures, so keeping them separate at the
    index-space level (via domain bases) helps eviction and upload policies.

- **Range 3: Textures (domain within SRV t0)** (Partial)
  - **Register**: `t0`, space0 (unified SRV table; addressed via a domain base
    index)
  - **Heap Indices**: Dynamically allocated for texture SRVs (per-texture
    descriptors / shader views) within the unified SRV table, starting at the
    textures domain base.
  - **Usage**: Stores GPU texture resources (2D/3D/array views). Textures are
    usually large, streamed per-mip, and have different eviction/streaming
    behavior than material constants.

- **Range 4: Samplers** (New)
  - **Register**: `s0`, space0
  - **Heap Indices**: Dedicated to sampler states.
  - **Usage**: Stores sampler configurations for texture sampling, such as
    filtering and addressing modes.
  - **Example**: A sampler in this range might define trilinear filtering and
    wrap addressing for a diffuse texture, ensuring smooth texture mapping on 3D
    models.

### Summary Table for Common Resource Placements

| Resource Type       | Logical Range | Register / Slot        | Notes / Indexing                         |
|---------------------|---------------|------------------------|------------------------------------------|
| Vertex Buffers      | Range 1       | SRV (t0, space0)       | Stable per-VB index; accessed by vertex shader |
| Index Buffers       | Range 1       | SRV (t0, space0)       | Stable per-IB index; used with VB pairs  |
| Material Constants  | Range 2       | SRV (t0, space0)       | Structured buffer lives in unified SRV table at materials domain base |
| Transforms          | Range 1       | SRV (t0, space0)       | Per-object buffers or SoA arrays indexed by object ID |
| Textures            | Range 3       | SRV (t0, space0)       | Texture SRVs live in unified SRV table at textures domain base |
| Samplers            | Range 4       | Sampler (s0)           | Unique sampler indices; stable states    |

### Real-World Scenario: Rendering a Character

Consider a scenario where a character model is rendered in a game:

1. **Constant Buffers (Range 0)**:
   - The `SceneConstants` buffer provides the view and projection matrices,
     ensuring the character is rendered in the correct position relative to the
     camera.

2. **Structured Buffers (Range 1)**:
   - The vertex buffer contains the character's mesh data, including positions,
     normals, and texture coordinates.
   - The index buffer defines how the vertices are connected to form triangles.
   - A transform buffer stores the character's world matrix, which is used to
     position and animate the character in the scene.

3. **Material Constants (Range 2)**:
   - Material constants for the character's skin and clothing are stored in a
     large structured buffer in Range 2.
   - The pixel shader fetches these constants dynamically using a material
     index, applying properties like base color, roughness, and metalness.

4. **Textures (Range 3)**:
   - Diffuse, normal, and specular maps live in the unified SRV table (`t0`,
     space0) within the textures domain (base + slot).
   - These textures are sampled in the pixel shader to apply detailed surface
     properties to the character.

5. **Samplers (Range 4)**:
   - A sampler with anisotropic filtering ensures high-quality texture sampling,
     even at oblique viewing angles.

By organizing resources into these logical ranges, the engine ensures efficient
access and management, enabling high-performance rendering for complex scenes in
a bindless-first architecture.

### 4.2 Shader Integration

Shaders access these resources using the bindless model, referencing them by
their indices within the unified heap. Below is an example of a vertex and pixel
shader that integrates with the unified descriptor heap, following the
conventions used in the engine:

- Vertex Shader:

  ```hlsl
  // Vertex shader example
  struct VSOutput {
      float4 position : SV_POSITION;
      float3 color : COLOR;
  };

  [shader("vertex")]
  VSOutput VS(uint vertexID : SV_VertexID) {
      VSOutput output;

      // Access per-draw metadata buffer through dynamic slot
      StructuredBuffer<DrawMetadata> draw_meta_buffer = ResourceDescriptorHeap[bindless_indices_slot];
      DrawMetadata meta = draw_meta_buffer[g_DrawIndex];

      // Access vertex buffer
      StructuredBuffer<Vertex> vertex_buffer = ResourceDescriptorHeap[meta.vertex_buffer_index];
      Vertex vertex = vertex_buffer[vertexID];

      // Apply world, view, and projection transforms
      float4 world_pos = mul(world_matrix, float4(vertex.position, 1.0));
      float4 view_pos = mul(view_matrix, world_pos);
      float4 proj_pos = mul(projection_matrix, view_pos);

      output.position = proj_pos;
      output.color = vertex.color.rgb;
      return output;
  }
  ```

- Pixel Shader:

  ```hlsl
  // Pixel shader example
  [shader("pixel")]
  float4 PS(VSOutput input) : SV_Target0 {
      // Default: just vertex color if materials buffer is not available
      float4 result = float4(input.color, 1.0);

      // Access per-draw metadata to find the material index for this draw
      StructuredBuffer<DrawMetadata> draw_meta_buffer = ResourceDescriptorHeap[bindless_indices_slot];
      DrawMetadata meta = draw_meta_buffer[g_DrawIndex];

      // Read material constants for this draw
      StructuredBuffer<MaterialConstants> materials = ResourceDescriptorHeap[bindless_material_constants_slot];
      MaterialConstants mat = materials[meta.material_index];

      // Simple unlit shading: vertex color modulated by material base color
      float3 base_rgb = mat.base_color.rgb;
      float  base_a   = mat.base_color.a;
      result = float4(input.color * base_rgb, base_a);

      return result;
  }
  ```

These shaders demonstrate how to fetch vertex data, apply transforms, and use
material constants for shading. They align with the conventions and structures
defined in the engine, ensuring compatibility with the unified descriptor heap.

#### Shader slot mapping (quick reference)

Below are the symbolic shader-slot names used in examples and a suggested
register mapping. Update your shader headers and renderer constants to match
these names for clarity:

- `bindless_indices_slot`          -> `b1` (Range 0, Scene constants / indices
  table)  # Implementation note: the current codebase uses `b1` (see
  `src/Oxygen/Renderer/Renderer.cpp`) and shaders (e.g.
  `src/Oxygen/Graphics/Direct3D12/Shaders/FullScreenTriangle.hlsl`). Update both
  code and shaders via a single source-of-truth header to avoid drift.
- `bindless_geometry_slot`         -> `t0` (Range 1, VB/IB/Transforms; unified
  SRV table)
- `bindless_materials_slot`        -> `t0` (Range 2, Materials domain within
  unified SRV table)
- `bindless_textures_slot`         -> `t0` (Range 3, Textures domain within
  unified SRV table)
- `bindless_samplers_slot`         -> `s0` (Range 4, Samplers)

When updating shaders, prefer symbolic slot names (above) and keep a single
source-of-truth mapping in your renderer so changes are applied consistently.

> Note on logical domains (geometry/materials/textures)
>
> You can represent domains in two equivalent ways while preserving the
> single-table-per-heap rule:
>
> - Option A: Single SRV range at `t0, space0` and compute the shader-visible
>   index as `domain_base + local_slot` for each domain
>   (geometry/materials/textures).
> - Option B: Multiple SRV ranges under the same CBV/SRV/UAV table using
>   register spaces (e.g., `t0, space0/space1/space2`). Pipelines may declare
>   multiple SRV ranges, but they are merged into one descriptor table per heap
>   type.
>
> Recommendation: Option A aligns best with the current `DescriptorAllocator`
> design (one shader-visible heap per type, global indexing), keeping domains as
> base-indexed subranges within a single SRV range. Option B remains compatible
> and is useful when you prefer explicit range delineation in pipeline
> descriptions.

### 4.3 Implementation status & TODO (render passes / root signatures)

The repository contains partial infrastructure for bindless root signatures and
pipeline descriptions, but several pieces remain unimplemented or inconsistent
with the logical ranges described above. The list below marks the current status
and actionable changes required to fully implement Section 4. Items are ordered
for a practical implementation sequence; stretch/automation items are last.

- [x] Descriptor-table binding in render passes
  - Status: Implemented. `RenderPass::Execute` calls the command recorder to set
    the pipeline state; the D3D12 `CommandRecorder::SetPipelineState(...)`
    implementation binds the shader-visible descriptor heaps and sets the root
    descriptor table(s) (via `SetupDescriptorTables`) as part of pipeline setup.
    `BindIndicesBuffer` remains a no-op because the indices buffer is accessed
    through the bound descriptor table.
  - Note (samplers): The command recorder will log a warning if a shader-visible
    sampler heap is present but the pipeline's root signature does not include a
    sampler table. If you need per-pass sampler-table binding, ensure the
    generated root signature includes an `s0` sampler table parameter so the
    recorder will bind it during `SetPipelineState`.
  - Files: `src/Oxygen/Renderer/RenderPass.cpp`,
    `src/Oxygen/Graphics/Direct3D12/CommandRecorder.cpp`,
    `src/Oxygen/Renderer/Detail/RootParamToBindings.{h,cpp}`.

- [x] Populate root signature / pipeline description with bindless ranges
  - Status: Implemented for current passes.
     `ShaderPass::CreatePipelineStateDesc()` and
     `DepthPrePass::CreatePipelineStateDesc()` build their `RootBindings`
     directly from the generator's authoritative `kRootParamTable` (via
     `Detail::BuildRootBindingItemsFromGenerated()`), so pipelines carry a
     descriptor-table entry for the unified SRV table (`t0`, space0), a direct
     CBV for `SceneConstants` (`b1`) and a root-constant for the draw index.
  - Action: For new or custom passes, continue using
     `BuildRootBindingItemsFromGenerated()` when constructing
     `GraphicsPipelineDesc::Builder().SetRootBindings(...)` so the canonical
     layout is preserved.
  - Files: `src/Oxygen/Renderer/ShaderPass.cpp`,
     `src/Oxygen/Renderer/DepthPrePass.cpp`, `src/Oxygen/Renderer/*Pass.cpp`.

- [x] Sampler table handling (binding-stage only)
  - Status: Completed. The generated `kRootParamTable` includes a sampler table
     descriptor (see `Generated.RootSignature.h`) and
     `BuildRootBindingItemsFromGenerated()` makes it available to pipeline
     descriptions. However, the engine must ensure the pipeline root signature
     includes the sampler-table root parameter so the command recorder can bind
     the sampler heap at pipeline setup.
  - Action: Ensure pipeline/root-signature creation uses the generated root
     signature entries so the sampler root parameter (`s0`) is present. The
     command recorder will perform the actual binding during
     `SetPipelineState(...)`. Sampler descriptor population (the table contents)
     is a separate follow-up task and is intentionally out of scope for this
     item.
  - Files: `src/Oxygen/Renderer/ShaderPass.cpp`,
     `src/Oxygen/Renderer/DepthPrePass.cpp`,
     `src/Oxygen/Graphics/Direct3D12/CommandRecorder.cpp`.
  - Note: PSO and root-signature creation in this project is driven from the
     authoritative `Spec.yaml` and produced by the BindlessCodeGen generator. Do
     not hardcode layout expectations in runtime code. Instead, ensure the
     generator emits the correct sampler-table root parameter (`s0`) when the
     spec requests a shader-visible sampler heap, and validate the generated
     artifacts during generator/unit tests (generator-driven validation). The
     command recorder will bind heaps/tables according to the generated layout
     during `SetPipelineState(...)`.

- [x] DescriptorAllocator API additions (per-range base indices) and validation
  - Status: Implemented. The allocator exposes per-domain base queries,
    reservation, and remaining-descriptor reporting used by budgeting and
    higher-level code. Unit tests exercise these APIs.
  - Action: Use the existing APIs when computing shader-visible indices and
    during admission/budgeting. The public APIs include:
    - `GetDomainBaseIndex(ResourceViewType, DescriptorVisibility)` → base index
    - `Reserve(ResourceViewType, DescriptorVisibility, uint32_t count)` → base
      index or failure
    - `GetRemainingDescriptorsCount(view_type, visibility)` → available slots
  - Files / Tests: `src/Oxygen/Graphics/Common/DescriptorAllocator.h`,
    `src/Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h`,
    `src/Oxygen/Graphics/Direct3D12/Test/HeapAllocationStrategy_domain_test.cpp`,
    `src/Oxygen/Graphics/Common/Test/Bindless/BaseDescriptorAllocator_Domain_test.cpp`.

- [ ] CPU-only generation validation for handles
  - Status: Missing in headers and validation.
  - Action: Keep `BindlessHandle` as a strongly-typed 32-bit shader-visible
    index with no bit packing. Do not emit index/generation bit masks to
    shaders. Add debug-only validation comparing CPU-side handle.generation vs
    slot.generation on submit. Use a single invalid sentinel:
    `NEXUS_HANDLE_INVALID = oxygen::engine::kInvalidBindlessIndex =
    std::numeric_limits<uint32_t>::max()`.
  - Files: generated BindingSlots headers (sentinels only), `ResourceRegistry`,
    renderer code.

- [ ] Tests & CI hooks
  - Status: Missing.
  - Action: Add unit tests for:
    - Allocate/Release and generation increment (DescriptorAllocator /
      ResourceRegistry)
    - Reserve contiguous indices for multi-slot geometry
    - Shader-visible index bounds validation (startup check) Add a CI startup
      check (debug-only) verifying that `GraphicsPipelineDesc.RootBindings()`
      agrees with the produced `BindingSlots` header.
  - Files: tests under `Testing/` and/or `src/Oxygen/Renderer/tests`.

### 4.4 Validation and Debugging

To ensure correctness, the following validation steps are performed:

1. **Descriptor Range Validation**:
   - Ensure that all resources fit within their designated ranges.
   - Validate that heap indices are stable and do not overlap. The
     `D3D12HeapAllocationStrategy` enforces non‑overlapping ranges and throws on
     invalid JSON or overlaps at startup.

2. **Shader Compatibility Checks**:
   - Verify that shaders reference the correct registers and spaces.
   - Ensure that the root signature matches the expected layout.

3. **Debugging Tools**:
   - Provide overlays to visualize resource bindings and heap usage.
   - Log detailed information about descriptor allocations and bindings.

This logical structuring ensures that the unified descriptor heap can
efficiently manage all required resources while maintaining compatibility with
the bindless rendering model.

## 5. Caching, Residency, and Upload in D3D12

This section specifies how Nexus caches resources, keeps them resident on the
GPU, assigns stable bindless indices, and moves bytes efficiently using Direct3D
12. It consolidates the data model, memory/heap strategy,
upload/synchronization, budgeting/eviction, and validation into a single,
actionable contract.

### 5.1 Goals and Contract

What Nexus guarantees to the rest of the engine:

- Stable bindless indices for the lifetime of each resource instance (Section 4
  defines descriptor ABI). Indices only change when a resource is truly
  destroyed and its slot is recycled after a fence.
- Deterministic upload and residency: transforms → materials → geometry →
  textures. Each domain has a clear memory backing and admission policy.
- D3D12‑correct resource states and synchronization: resources are transitioned
  for copy and consumption; cross‑queue work is serialized by fences.
- Graceful failure: when budgets are exceeded or uploads are not yet complete,
  Nexus returns fallback handles that remain valid and shader‑safe.

Success criteria:

- No descriptor aliasing or out‑of‑bounds access in debug builds; validation
  asserts early.
- No CPU stalls on the graphics queue for uploads; staging uses upload heaps and
  copy operations with proper fencing.
- Stable frame time: uploads and evictions amortized; no large heap
  re‑creations, no descriptor heap thrash.

Error modes and handling:

- Out‑of‑budget: return fallback handles, schedule retry, emit structured logs
  with domain and bytes requested.
- Late data: keep previous contents or fallback bound; never publish a
  half‑resident descriptor to shaders.
- Stale handles: generation mismatch detected in debug; draw is skipped or
  downgraded with a default.

### 5.2 Data Model and Handles

Domains: Transforms, Materials, Geometry, Textures. Each domain has an
allocation policy, budget, and descriptor range (see Section 4 for binding
slots).

- Handle shape (CPU): a versioned handle carries both the shader‑visible index
  and a CPU‑only generation counter. Default invalid is
  `oxygen::engine::kInvalidBindlessIndex`.
  - Index: full 32‑bit, strongly‑typed BindlessHandle used directly by shaders.
  - Generation: maintained only on the CPU (not encoded in the 32‑bit index),
    used for validation and safe reuse.
  - Shaders only see the 32‑bit index; there are no bit masks or packing in
    shader code.

CPU‑side types (sketch):

```cpp
using BindlessIndex  = oxy::NamedType<uint32_t, struct _BindlessIndexTag>;
using Generation     = oxy::NamedType<uint32_t, struct _GenerationTag>;

struct VersionedBindlessHandle {
  BindlessIndex index;     // shader‑visible 32‑bit index
  Generation    generation; // CPU‑only generation counter

  VersionedBindlessHandle() noexcept
    : index(BindlessIndex{0u}), generation(Generation{0u}) {}
  VersionedBindlessHandle(BindlessIndex idx, Generation gen) noexcept
    : index(idx), generation(gen) {}

  // Optional CPU‑side packing for maps/keys; shaders never see this
  static VersionedBindlessHandle FromPacked(uint64_t p) noexcept {
    return { BindlessIndex{static_cast<uint32_t>(p & 0xFFFFFFFFu)},
             Generation{static_cast<uint32_t>(p >> 32)} };
  }
  uint64_t ToPacked() const noexcept {
    return (static_cast<uint64_t>(static_cast<uint32_t>(generation)) << 32) |
            static_cast<uint32_t>(index);
  }
  BindlessHandle ToBindlessHandle() const noexcept {
    return BindlessHandle{static_cast<uint32_t>(index)};
  }
};
```

- Slot metadata (per allocation): { handle, state, shader_index, generation,
  last_used_frame, size_bytes, resident_mip_mask (textures), optional
  pending_fence_or_frame, resource_key }.
- Descriptor index: computed once by the DescriptorAllocator as shader_index =
  domain_base + slot_index; shaders must not add base again.

States: Invalid → Staged → Resident → PendingEvict → Evicted → Free. Transitions
are driven by uploads, fences, and budgeting.

### 5.3 Memory and Heaps (D3D12)

Resource heaps and placements:

- Upload staging: D3D12_HEAP_TYPE_UPLOAD buffers, persistently mapped (Map once,
  never Unmap). Ring allocators per queue with at least N frames of headroom.
  Align to D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT (for textures) and natural
  alignment for buffers.
- GPU resident: D3D12_HEAP_TYPE_DEFAULT committed resources or placed
  suballocations in larger arenas (preferred for geometry). Transforms and small
  materials are typically packed in large structured buffers; geometry lives in
  vertex/index arenas; textures use default heap resources (whole‑resource
  streaming in Phase A; per‑mip/tiled in Phase B).
- Readback (optional): D3D12_HEAP_TYPE_READBACK for diagnostics and tools.

Descriptor heaps and root binding:

- One shader‑visible CBV/SRV/UAV heap for all SRV/UAV/CBV (up to 1M
  descriptors).
- One shader‑visible SAMPLER heap (up to 2048 samplers).
- Root signature uses
  D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED and
  D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED. Passes index into
  ResourceDescriptorHeap[] and SamplerDescriptorHeap[] from shaders.
- Keep shader‑visible heaps persistent for the life of the device. Use a
  CPU‑only staging heap for descriptor edits and
  CopyDescriptors(…)/CopyDescriptorsSimple(…) into the GPU heap to avoid
  fragmentation and churn.

Resource states and transitions (typical):

- Upload: Transition(default) → COPY_DEST, perform
  CopyBufferRegion/CopyTextureRegion, Transition to target read state:
  - CBV buffers: D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER.
  - SRV buffers: D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE.
  - VB/IB: D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER /
    D3D12_RESOURCE_STATE_INDEX_BUFFER.
  - Textures (SRV): NON_PIXEL_SHADER_RESOURCE | PIXEL_SHADER_RESOURCE.

Queues and synchronization:

- Prefer a dedicated copy queue for staging heavy uploads. Signal a fence on the
  copy queue; make the graphics queue Wait on that fence before the first use of
  the resource. If using a single direct queue in Phase A, the same fence still
  gates slot visibility.
- N‑frames‑in‑flight fences are commonly used to determine when it is safe to
  increment a slot's generation and therefore allow reuse. The authoritative
  decision is the generation increment: a slot is considered safe to reuse only
  after its generation has been advanced (for example, when a pending fence or
  frame completes and the renderer performs the generation transition). CPU-
  side `VersionedBindlessHandle` comparisons detect stale handles by comparing
  stored generation vs the slot's current generation.

### 5.4 Upload Pipeline and Synchronization

Deterministic flow per frame (Phase A): ScenePrep → Allocate (slot) → Stage
(upload heap ring) → Record copy (copy or direct queue) → Transition to read
state → Publish descriptor → Mark Resident when fence passes.

Practical notes:

- Footprints: Use GetCopyableFootprints for texture uploads to compute
  subresource footprints and row pitches. Respect
  D3D12_TEXTURE_DATA_PITCH_ALIGNMENT.
- Batching: Coalesce multiple small uploads per domain to minimize Copy/Barrier
  count. Group by destination heap and target state.
- Persistent mapping: Each ring has a CPU pointer; writes are memcpy only. Track
  write head with alignment padding; wraparound requires fence‑based wait.
- Bindless publication: The descriptor’s GPU slot is written only when backing
  memory is fully uploaded and transitioned. Before that, a fallback descriptor
  lives at the slot, guaranteeing shader safety.
- On failure (budget/full ring): allocate a default/fallback handle and keep the
  request queued for retry in subsequent frames.

Phase B evolution (parallel): move staging to per‑domain SPSC rings and uploader
threads; record copies on the copy queue; graphics queue waits on a per‑batch
fence. The external contract remains unchanged.

### 5.5 Budgets, Residency, and Eviction

Budgets:

- Per‑domain byte budgets are tracked against allocations in default heaps. Use
  IDXGIAdapter3::QueryVideoMemoryInfo to observe local memory usage/available
  budget and, optionally, SetVideoMemoryReservation to cap usage.
- Admission control via ReserveBudget(domain, bytes): succeeds and commits or
  fails and returns a fallback handle. Budgets are soft in development, hard in
  CI.

Residency and reuse:

- A slot becomes Resident once its upload fence has completed and the descriptor
  points at the final view. When a slot is freed, the authoritative signal that
  allows reuse is a generation increment for that slot: the renderer must
  advance the slot's CPU-side `generation` only once it considers the slot safe
  to reuse (commonly when a pending fence/frame completes). Any
  `VersionedBindlessHandle` carrying an older generation is considered stale and
  must not be used for draws (debug validation will catch mismatches and fall
  back to defaults). Practically, implementations may either delay placing freed
  indices into a free queue until fence/frame completion or record a
  pending-generation bump and apply it atomically at the same safe point.

Eviction policy:

- Triggered by budget pressure, OS/driver notifications, or policy
  (age/priority). Primary heuristic is LRU with optional priority boosts.
  Textures prefer whole‑resource eviction in Phase A; per‑mip eviction is
  introduced with tiled resources in Phase B (maintain a minimum mip floor to
  avoid shimmering).
- Eviction sequence (generation-driven): select candidate → mark PendingEvict →
  ensure not in‑flight (fence/frame completion) → replace descriptor with
  fallback → free GPU bytes → advance slot generation → make slot available for
  reuse (either by inserting into free list or atomically updating the allocator
  state). The generation advance is the authoritative event that protects
  against aliasing; fences/frames are commonly used to gate that advance.

### 5.6 Bindless Indices and Validation

Index assignment:

- Each domain has a base index and capacity inside the unified descriptor heap.
  Final shader index is shader_index = base + slot_index. These values are
  emitted to C++ and HLSL from a single source (BindingSlots).

Validation (debug‑only):

- Bounds: assert that shader_index falls within the domain’s [base,
  base+capacity) before publishing.
- Generation check: when a handle is used for a draw, compare handle.generation
  against slot.generation in a CPU shadow map; mismatch triggers a warning and
  forces a default.
- Root signature/heap checks at startup: verify descriptor heap sizes and root
  parameter layout match Section 4; fail fast if not. Heap sizes/capacities are
  sourced from the generated JSON (Section 3.7); parse/validation failures throw
  exceptions for graceful shutdown.

Invalid/sentinel handling:

- Only `oxygen::engine::kInvalidBindlessIndex`
  (=`std::numeric_limits<uint32_t>::max()`) is reserved as invalid. Index `0` is
  valid. Shaders branch to default resources on the invalid sentinel.

### 5.7 API Surface (sketch)

Nexus exposes minimal, explicit calls; implementations may be backend‑specific
behind the same interface.

```cpp
class NexusRegistry {
public:
  // Admission and allocation
  bool ReserveBudget(Domain domain, size_t bytes);
  BindlessHandle Allocate(ResourceKey key, Domain domain, size_t sizeBytes, UploadIntent intent);
  void UpdateViewInPlace(BindlessHandle h, const ViewDesc& view); // keep index stable on hot‑reload
  void Release(BindlessHandle h); // deferred reuse; increments generation on recycle

  // Queries
  std::optional<BindlessHandle> FindResident(ResourceKey key) const;
  ResidencyState QueryResidency(BindlessHandle h) const; // textures: QueryResidencyMip(h, mip) in Phase B

  // Frame orchestration
  void ProcessUploadsAndSubmit(CommandRecorder& copy, CommandRecorder& gfx);
  void TickEviction(uint64_t frameIndex);
};
```

### 5.8 Testing and Observability

- Unit tests: allocation/reuse/generation; contiguous allocation for multi‑slot
  geometry; bounds/handle validation; fallback on budget failure.
- Integration tests: deterministic upload order; eviction under pressure;
  descriptor heap layout matches BindingSlots.
- Runtime tools: residency heatmaps, eviction and thrash logs, allocation
  traces; optional ValidateHandlesOnUse in debug. Name all D3D12 objects and
  emit PIX markers around upload/transition blocks; enable DRED for post‑mortem
  analysis.

Notes on scope

- Section 4 defines the descriptor ABI and symbolic slot mapping used here.
  Section 6 details Phase A/Phase B pipelines and threading; this section
  defines the invariant contract that those pipelines must uphold.

## 6. Upload System

### Phase 1: Basic Upload Pipeline

- Single-threaded staging and submission One staging ring per queue; coalesce
  via `StageBufferUploads`, group by destination to minimize copy calls.
- Deterministic upload order (e.g. transforms → materials → geometry → textures)
  ScenePrep calls domain uploaders in fixed order; results are cached in
  per-frame state.
- Explicit synchronization points between subsystems Per-domain fences; N=3
  frames in flight by default. CPU never recycles a suballoc until signaled.
- Integration with ScenePrep finalization (sequential orchestration) Uploaders:
  TransformUploader, MaterialUploader, GeometryUploader, TextureUploader.
  Assemblers consume handles and build the final draw lists.
- Logging and validation hooks for resource readiness On failure, bind default
  handles and queue retry; emit structured logs with resource keys and budgets.

### Phase 2: Parallel Upload Evolution

- Multi-threaded staging across resource domains Per-domain job queues;
  lock-free SPSC rings for producer → Nexus staging.
- Dependency graph for upload ordering and conflict resolution DAG ties
  materials → textures, geometry → buffers; scheduler respects edges while
  maximizing parallelism.
- Thread-safe residency map updates and dirty region tracking Sharded maps per
  category with epoch-based reclamation; range merges performed on worker
  threads.
- GPU submission batching with minimal stalls Batch copy/compute submissions by
  resource and state; compact barriers.
- Integration with render graph or submission queue abstraction Optional backend
  plug that records uploads into a render graph stage or a submission queue
  object.
- Debugging tools for upload race detection and profiling Timeline events for
  staging/submit/fence; race-detection mode asserts if two writers touch the
  same range in a frame.

## 7. Eviction Strategy

- Residency tracking and scoring heuristics LRU with score boosts for
  proximity/importance; textures honor a minimum resident mip floor.
- Dirty flag propagation and eviction triggers Evict only clean ranges; dirty
  uploads keep items pinned until committed.
- Streaming policies and asset lifetime management On-demand, ahead-of-time, and
  pinned policies; per-asset residency budgets.
- Open questions: eviction granularity, predictive models Geometry eviction at
  submesh vs whole-mesh; per-mip eviction vs atlas; explore ML/heuristic
  predictors under pressure.

## 8. ScenePrep Finalization

- Finalization boundaries and orchestration scope Filter → Uploaders →
  Sort/Partition → Assemblers → Outputs (RenderItemsList, DrawMetadata,
  partitions).
- Resource readiness and upload completion guarantees Assemblers read only from
  per-frame caches validated by Nexus fences; defaults applied on miss.
- Integration with render graph or submission pipeline Finalization can emit
  resource barriers and late uploads into a graph/queue stage.
- Testability and validation hooks CPU-only mode bypasses GPU work;
  deterministic outputs validated by unit tests and property tests.

## 9. Tooling and Workflow Integration

- VSCode/IntelliSense hygiene in multi-target workspaces Use per-config
  compile_commands and conditional includes; keep headers shader-friendly.
- Automated analysis for naming and layout correctness CI checks naming rules
  and struct/shader layout hashes.
- Debug overlays and GPU buffer inspection Optional overlays visualize bindless
  indices, residency heatmaps, and eviction events.
- Wishlist: context-aware code navigation Jump-to-definition between shader
  defines and C++ structs; hover showing layout/hash.

## 10. Testing and Simulation Modes

- CPU-only mode for deterministic validation Heap-backed buffers/images; integer
  handles; no GPU calls.
- GPU-backed mode for performance profiling D3D12/Vulkan backends with identical
  contracts; timings exposed for profiling.
- Unit test scaffolding and mock resource injection Scenario-based tests for
  each uploader/assembler; mocks for residency and bindless tables.
- Notes on reproducibility and test coverage Fixed seeds for generators; stable
  sort keys; property tests for idempotent batches.

## 11. Extensibility and Future Considerations

- Leverage the modularity of the `RenderPass` base class to introduce additional
  root constants or descriptor tables for new rendering techniques.
- Explore GPU-driven pipelines or advanced culling methods that can benefit from
  the existing bindless-first design.
