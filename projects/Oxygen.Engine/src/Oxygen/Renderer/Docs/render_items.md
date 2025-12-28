# 🎮 Render Items Architecture

Status: Living design document (updated December 28, 2025). Tracks the current
`RenderItem` implementation and evolution toward low-level `DrawPacket`
representation (planned for Phase 6).

**Current Status**:

* High-level `RenderItem` abstraction: **Implemented and stable**
* Low-level `DrawPacket` abstraction: **Planned (Phase 6)**
* `RenderItemsList` container: **Implemented (Phase 3)**
* ScenePrep integration: **Fully implemented (Phase 4)**

Scope:

* Current high-level per-object snapshot: `oxygen::engine::RenderItem` (implemented)
* Planned low-level transient representation: `DrawPacket` / `LowLevelRenderItem` (pending Phase 6)
* Supporting systems: `RenderItemsList` (implemented), ScenePrep extraction (implemented),
  LOD selection (implemented), instancing (planned), hot-reload dependency tracking (planned)

Non‑Scope: Full render graph resource lifetime solving, material system authoring format, editor tooling.

Cross‑Refs:

* `implementation_plan.md` (current status and roadmap)
* `scene_prep.md` (collection/finalization phases - fully documented)
* `bindless_conventions.md` (GPU resource layout and binding)
* `gpu_resource_management.md` (pooling / lifetime - TBD)
* `data_flow.md` (pass data flow - to be updated with packet pipeline)

Legend (for embedded task lists below): [ ] pending, [~] in progress, [x] done,
[d] deferred.

---

## Implementation Status Summary

| Component | Status | Phase | Details |
| -- | -- | -- | -- |
| RenderItem (high-level) | [x] Complete | 1-4 | POD struct with transform, bounds, material/geometry refs; used in extraction |
| RenderItemsList | [x] Complete | 3 | Container in Renderer; validates bounds and properties |
| Scene Extraction | [x] Complete | 4 | ScenePrep Collection phase with frustum culling and LOD selection |
| GPU Resource Upload | [x] Complete | 4 | GeometryUploader, TransformUploader, MaterialBinder all integrated |
| Draw Metadata | [x] Complete | 4 | DrawMetadataEmitter produces per-draw metadata; all bindless slots defined |
| DrawPacket (low-level) | [ ] Pending | 6 | Design complete; implementation deferred; awaits Phase 6 refactor |
| Instancing Framework | [ ] Pending | 6+ | Per-draw instance buffer layout planned; not yet implemented |
| Versioned Handles | [ ] Pending | 6+ | Generation counters for stable external references; planned |
| SoA Culling Path | [ ] Pending | 10 | Optional parallel arrays for SIMD culling; research/benchmark deferred |

---

## 🧱 Geometry Asset

* Represents the 3D shape of a model.
* Stored as a binary asset with metadata, vertex/index buffers, and submesh
  descriptors.
* Designed for efficient streaming and partial loading.

### Key Concepts

* **LODs**: Multiple levels of detail for performance scaling based on screen
  size or distance.
* **Submeshes**: Smallest drawable units, each mapped to a material slot.
* **Material Slots**: Logical bindings that allow material overrides without
  modifying geometry.

---

## 🎨 Material Asset

* Describes how a surface should appear when rendered.
* Stored as a compact, streamable binary format.
* Designed for reuse, instancing, and runtime overrides.

### Instancing and Overrides

* Materials can be:
  * **Swapped**: Replace one material with another at runtime.
  * **Instanced**: Override only a few parameters for variation. All
    per-instance overrides are managed as runtime-only data, not stored in the
    asset binary. At runtime, a parameter override table is maintained by the
    engine or scene system, allowing unique values for specific instances
    without duplicating the entire material asset.

    Example scenario:
    > Suppose you have a tree model with a leaf material. You want to place 1000
    > tree instances in the scene, each with a slightly different leaf color for
    > natural variation. The base material asset defines the default leaf color.
    > At runtime, each tree instance can specify a unique color override (e.g.,
    > via a per-instance parameter table or buffer). The renderer applies these
    > overrides when building low-level render items, so each tree appears
    > unique without duplicating the material asset or storing overrides in the
    > asset file.

### Error Handling and Fallbacks

* The engine provides a set of default materials and textures (e.g.,
  "DefaultMaterial", "DefaultTexture", "DefaultNormal") as part of its core
  asset package. These are always available and loaded at engine startup.
* If a material asset or referenced texture is missing or fails to load, the
  engine automatically substitutes the corresponding default asset to ensure
  visual continuity and avoid rendering errors.
* Fallback assets are visually distinct (e.g., checkerboard, magenta, or labeled
  "MISSING") to make missing or broken references easy to spot during
  development and testing.

---

## 🧩 Composition into Render Items

### High-Level Render Item (`RenderItem` – In Code)

* Represents a renderable object in the scene.
* Created when a model is instantiated / scene loaded.
* Contains:
  * Reference to a **Geometry Asset**
  * A **Material** (or material set conceptually; current code single pointer)
  * A **Transform** (world matrix) and derived normal matrix
  * Rendering flags (shadow casting/receiving, layer bits, custom flags)
  * Bounding volumes (sphere + AABB)
* Stable across frames (only updated when scene/asset changes).
* Used for LOD selection, material overrides, culling, and scene management.

#### Responsibilities

* Produced by scene extraction code. Current pipeline culls on CPU using the
  `View` frustum before inserting into the list. A future SoA path may build
  arrays pre-culling.
  for SoA build).
* Supplies source data for building low-level `DrawPacket`s every frame.
* Per-submesh policy: current extraction builds one RenderItem per mesh (first
  submesh/material) for simplicity. A per-submesh item expansion is planned
  and documented as a follow-up.
* Asset hot-reload triggers refresh of dependent RenderItems (recompute bounds /
  materials as needed).
* Supplies source data for building low-level `DrawPacket`s every frame.

### Low-Level Render Item (`DrawPacket` – Planned)

Represents exactly one hardware draw (or instanced draw) prepared for fast
submission. Built fresh each frame from visible `RenderItem` objects and other
per-frame systems (LOD selection, instancing aggregator, material override
table).

Contains (conceptual layout):

| Field | Purpose | Notes |
| -- | -- | -- |
| sort_key (u64) | Packed ordering (material, mesh, depth, states) | Front-to-back for opaque, stable grouping |
| mesh_handle (u32) | Reference to mesh/geometry GPU data | Could be index into bindless arrays |
| material_handle (u32) | Reference to resolved material / pipeline state key | May drive PSO/shader variant selection |
| first_index (u32) | Index buffer start | From submesh descriptor |
| index_count (u32) | Number of indices to draw | — |
| vertex_offset (i32) | Added to indices | Optional if mesh uses base vertex |
| instance_data_offset (u32) | Offset into per-frame instance buffer | 0 if non-instanced |
| lod (u16) | Selected LOD for this packet | For metrics / debugging |
| flags (u16) | Blending, shadow, etc. | Mirrors subset of RenderItem flags |
| layer_bits (u32) | Render layer/filter mask snapshot | Allows per-pass filtering without re-build |
| submesh_id (u16) | Original submesh or implicit 0 (single-mesh) | Debug/profiling |

Excluded: Large matrices or materials (they live in shared/constant buffers or
bindless tables). Keep packet trivially copyable.

Lifecycle: Build → (sort / filter) → Submit → Discard.

* A **Resolved Material** (GPU-ready, with overrides applied)
* GPU draw parameters (index count, offsets, etc.)
* **Packed Instance Data**: For GPU consumption
* Transient and rebuilt every frame.
* Used by the renderer for batching, sorting, and submission.
* Uses fallback data if referenced assets are not yet loaded.

---

## 🧱 High-Level vs Low-Level Render Items

| Concept | High-Level Render Item | Low-Level Render Item |
| -- | -- | -- |
| 🧠 Purpose | Describes what to render | Describes how to render it |
| 🧩 Composition | Geometry + Material Set + Transform + Instance Data | Submesh + Material + GPU-ready state + Instance Data |
| 🧱 Abstraction Level | Asset-level abstraction (CPU-side) | Draw-call-level abstraction (GPU-side) |
| 🔁 Lifecycle | Created during scene loading or asset instantiation | Created per frame during render graph or culling phase |
| 📦 Ownership | References geometry and materials | Owns resolved GPU handles and draw parameters |
| 🔄 Mutability | Stable across frames | Transient, rebuilt every frame |
| 🎯 Use Case | Scene management, LOD switching, material overrides | Efficient GPU submission, batching, sorting |

---

## 🧩 Identification Strategy

| Entity | Identifier Type | Implementation | Notes |
| -- | -- | -- | -- |
| Asset (Geometry/Material) | `AssetKey` (128-bit GUID) | `data::AssetKey` struct (16 bytes) | Uniquely identifies any asset; randomly generated |
| Cached Resource (Texture/Buffer) | `ResourceKey` (64-bit encoded) | `content::ResourceKey` (NamedType) | Packs (pak_index, resource_type, resource_index); opaque at runtime |
| Rendering View | `ViewId` (64-bit) | `core::ViewId` (NamedType) | Unique per frame; allocated by FrameContext |
| Scene Node | `NodeHandle` (ResourceHandle) | `scene::NodeHandle` (extends ResourceHandle) | Zero-overhead wrapper; stores index + scene_id |
| Transform Entry | `TransformHandle` (uint32 index) | `sceneprep::TransformHandle` (NamedType) | Stable deduplicated transform index; recycled over time |
| Material Registry Entry | `MaterialHandle` (uint32 index) | `sceneprep::MaterialHandle` (NamedType) | Stable material registry entry; recycled over time |
| Geometry Resource | `GeometryHandle` (uint32 index) | `sceneprep::GeometryHandle` (NamedType) | Stable mesh/geometry entry; recycled over time |
| Submesh Descriptor | Implicit uint32 index | Index field in geometry asset | Per-mesh; from submesh table in GeometryAsset |
| Material Override Slot | Index into override table | `RenderItem.material` pointer + asset key | Per-item material reference; resolved at render time |

**Key Design Patterns**:

* **AssetKey** (128-bit GUID): Stable identifier across all systems; never changes for an asset
* **ResourceKey** (64-bit encoded): Runtime-only identifier for cached resources; opaque to renderer
* **Handles** (TransformHandle, MaterialHandle, GeometryHandle): Frame-local deduplication indices; recycled/reused
* **ViewId** (64-bit): Per-frame unique identifier for rendering views allocated by engine
* **NodeHandle** (from scene::NodeHandle): Zero-cost wrapper for scene graph navigation

---

## 🎯 Handle-Based Two-Phase Pipeline Architecture

Handles exist to decouple the **collection phase** (when render items are discovered and dependencies identified) from the **finalization phase** (when GPU resources are actually allocated and realized).

### Two-Phase Pattern

**Collection Phase** (parallel scene traversal):

1. Extractors traverse the scene and build `RenderItemProto` objects
2. For each item, extractors call `GetOrAllocate()` on managers to obtain **stable handles** for transforms, materials, and geometry
3. These handles are **assigned upfront** and embedded in `RenderItemData`, but GPU resources don't exist yet
4. Multiple collectors can work in parallel because handles are stable identities (not GPU resources)

**Finalization Phase** (sequential GPU setup):

1. Finalizers iterate collected items and ensure GPU resources are allocated for each unique handle
2. Uploaders transfer CPU data (matrices, material constants, geometry) to GPU buffers
3. Managers return shader-visible indices that map handles to GPU buffer locations
4. Passes use handles to fetch resources at draw time (indirection via bindless tables)

### TransformHandle — Deferred Transform Allocation

**Why it exists**: Enables collection to assign stable identities to transforms before GPU buffers are allocated.

* **Collection**: `uploader->GetOrAllocate(matrix)` → returns `TransformHandle` immediately
* **Finalization**: `uploader->EnsureFrameResources()` → allocates GPU buffer, maps handle → GPU index
* **Rendering**: Shader uses `worlds_buffer[handle]` to fetch matrix
* **Memory efficiency**: Handle is 4 bytes vs. 64 bytes for embedded matrix
* **Secondary benefit**: Early deduplication infrastructure (will be removed)

### MaterialHandle — Deferred Material Allocation

**Why it exists**: Separate collection from GPU residency for material data.

* **Collection** (where deduplication happens): `binder->GetOrAllocate(material)` → content hash checked → if identical material already registered, return existing handle; else create new handle
* **Finalization**: `binder->EnsureFrameResources()` → allocates GPU atlas, maps all unique handles → texture/constant slots
* **Rendering**: Shader uses handle to fetch material constants and texture descriptors
* **Result**: Multiple items with identical material content get same handle; one GPU allocation serves many logical requests

### GeometryHandle — Deferred Mesh Allocation

**Why it exists**: Decouple mesh collection from GPU buffer allocation.

* **Collection** (where deduplication happens): `uploader->GetOrAllocate(mesh)` → content hash checked → if identical mesh already registered, return existing handle; else create new handle
* **Finalization**: `uploader->EnsureFrameResources()` → allocates GPU vertex/index buffers for all unique handles, maps handle → SRV indices
* **Rendering**: Shader uses handle to fetch vertex/index SRV descriptors
* **Result**: 1000 instances of same tree mesh all get same handle; one GPU buffer serves 1000 draw calls

### Architectural Benefits

* **Parallel-safe collection**: Multiple threads can safely call `GetOrAllocate()` concurrently without GPU allocation overhead
* **Stable identifiers**: Handles are assigned during collection and flow through the entire pipeline unchanged
* **Deferred GPU work**: Collection doesn't block on GPU allocation; finalization happens sequentially after collection completes
* **Indirection foundation**: Handles provide stable references for future caching, versioning, and cross-frame optimization

---

**Versioning & Hot-Reload**:

* AssetKey supports direct lookups in asset registry for dependency tracking
* ResourceKey enables content-relative resource resolution without thread-local state
* Handles are tied to registries (TransformUploader, MaterialBinder, GeometryUploader) which can invalidate on reloads
* TODO: Versioned handles (generation counter) planned for Phase 6+ to support external tool references

---

## 🔄 Lifecycle & Thread-Safety

* DrawPacket build invalidated if any contributing RenderItem dirty (transform,
  material, LOD, instancing group composition change).

### Current Implementation (Single Struct)

`RenderItem` today is a POD-like struct with publicly writable fields (for fast
construction) but intended to be treated as immutable – once constructed and its
derived properties (`UpdateComputedProperties`) updated, no further mutation
should occur during that frame's render passes. A future change will enforce
immutability via:

* Private fields + factory / builder (planned)
* Or splitting into HighLevelRenderItem (mutable across frames) and a frozen
  DrawPacket (immutable, per frame)

Thread-Safety: Construction & property updates occur on the extraction thread(s)
prior to queuing for passes. After publication to `RenderContext` spans, only
const access is allowed. No internal synchronization is provided nor intended.

### Bounding Volumes & Derived Data

* `bounding_sphere`: Derived from source mesh local bounds applying max-scale
  uniform expansion for conservative correctness. Sentinel: (0,0,0,0) when no
  mesh.
* `bounding_box_min/max`: Eight-corner transform; axis-aligned in world space.
  Could be optimized later via SIMD / matrix column min/max.
* `normal_transform`: Inverse transpose of upper 3x3 of world transform.

Update Methods:

* `UpdatedTransformedProperties()`: Recomputes world-space bounds & normal
  matrix (fast path – geometry/material stable).
* `UpdateComputedProperties()`: Currently same behavior; reserved extension
  point for costlier derivations (e.g., surface area, LOD metrics, motion
  vectors) – DO NOT remove.

### Planned Migration Steps

1. [x] Introduce `RenderItemsList` orchestrating extraction & caching (Phase 3)
2. [ ] Split high vs low-level: high-level keeps asset/material references,
   transform, flags; low-level (DrawPacket) holds ready-to-submit geometry
   slices, GPU handles, packed constants (Phase 6)
3. [ ] Add versioning & handles: stable integer handle + generation counter for
   editor/tool references (Phase 6+)
4. [ ] Move per-frame transient data (bounds after animation skinning, motion
   vectors) into low-level packet (Phase 6+)
5. [ ] Optional SoA acceleration path for culling (separate arrays of centers,
   radii, layer bits) (Phase 10)

### High-Level Render Items [x Implemented]

* [x] Created when a scene or object is loaded
* [x] Persist across frames
* [x] Updated when materials or geometry change (e.g., LOD switch, material
  override)
* [x] Owned by the Renderer via `RenderItemsList` container
* [ ] Track dependencies on assets for hot-reloading and live updates (pending)
* [x] Support visibility filtering and culling via ScenePrep extraction
* [x] Integrated with texture binding and material binder for GPU resource tracking

### Low-Level Render Items [pending Phase 6]

* [ ] Rebuilt every frame (or every visibility pass)
* [ ] Created by the render graph or visibility system (planned)
* [ ] Owned by the renderer or frame allocator (planned)
* [ ] Freed or recycled after each frame (planned)
* [ ] Use fallback data if referenced assets are not yet loaded (planned)

**Status**: `DrawPacket` struct design complete (render_items.md). Implementation
deferred to Phase 6 pending `RenderItemsList` stabilization.

---

## 🧩 Render Items List: State, Justification, and Update Flows

A dedicated, stateful `RenderItemsList` class is justified in real-world engines
for:

* **Incremental Updates & Caching:** Avoid rebuilding the entire list every
  frame by tracking dirty nodes/assets and only updating what has changed (as
  seen in Unreal Engine's and Unity's render graph systems).
* **Multi-View/Pass Support:** Maintain separate lists for different views (main
  camera, shadow maps, reflections) or passes, each with their own filtering and
  visibility.
* **Dependency Tracking:** Track which render items depend on which assets for
  efficient hot-reloading and live updates (used in engines like Frostbite and
  Unreal).
* **Custom Filtering/Batching:** Persist user/editor-driven filters, sorting, or
  batching state across frames.
* **Parallel/Asynchronous Extraction:** Coordinate work between threads or jobs
  for large scenes, as in AAA engines.

### Frame Update Flow (Current State → Target State)

**Current Implementation (Phases 1–4 Complete)**:

1. [x] **Scene Traversal/Query**: Scene graph traversed by ScenePrep extraction
2. [x] **Collection**: Per-view visibility filtering and LOD selection via ScenePrep Collection phase
3. [x] **Render Item Construction**: High-level RenderItems created and stored in `RenderItemsList`
4. [x] **GPU Resource Preparation**: Geometry/transform/material uploads via ScenePrep Finalization phase
5. [x] **Finalization**: Draw metadata assembly and partition by pass via ScenePrep finalizers

**Pending Implementation (Phase 6+)**:

1. [ ] **DrawPacket Build**: Visible RenderItems converted into DrawPackets (with optional instancing & LOD selection)
2. [ ] **Sort & Partition**: Opaque/transparent sorting by sort key with per-pass partition ranges
3. [ ] **Publish to RenderContext**: Expose spans of DrawPackets (Renderer updates context)

**Full Target Flow** (planning for Phase 6):

1. Scene Traversal/Query: Query Scene for visible and renderable nodes (using traversal, culling, and filtering APIs)
2. Extraction: For each visible node with a MeshAsset and Material(s), extract necessary data (geometry, materials, transform, instance data)
3. Render Item Construction: Build or update a high-level render item for each node, storing references to assets and per-instance overrides
4. Caching/Reuse: Reuse existing render items where possible; only create or destroy items as needed
5. Dependency Registration: Register dependencies between render items and their assets for hot-reloading and live updates
6. List Finalization: The list is now ready for use by the renderer or further processing (e.g., for DrawPacket generation)
7. DrawPacket Build: Visible RenderItems converted into DrawPackets (with optional instancing & LOD selection)
8. Sort & Partition: Opaque: material/mesh grouping then depth buckets (front-to-back); Transparent: strict back-to-front by depth
9. Publish to RenderContext: Expose spans of DrawPackets (Renderer updates context)

### Important Update Flows

* **Frame Update:**
  * On each frame, the `RenderItemsList` checks for changes in scene visibility,
    node transforms, or instance data. Only affected render items are updated.
  * DrawPacket cache invalidated if any contributing RenderItem dirty
    (transform, material, LOD, instancing group change).
* **Asset Hot-Reload:**
  * When an asset (geometry or material) is reloaded or changed, the asset
    system notifies the `RenderItemsList`, which updates or rebuilds only the
    affected render items.
* **Scene Edits:**
  * When nodes are added, removed, or modified (e.g., LOD switch, material
    override), the list updates accordingly, creating, updating, or removing
    render items as needed.
* **Multi-View/Pass Update:**
  * For each view or rendering pass, the list may be filtered or rebuilt to
    match the specific requirements (e.g., shadow casters only, editor
    overlays).

---

## 🧪 Field Reference (Current `RenderItem`)

| Field | Type | Purpose | Notes |
| -- | -- | -- | -- |
| mesh | `shared_ptr<const Mesh>` | Geometry source (bounds, buffers) | Immutable asset pointer |
| material | `shared_ptr<const MaterialAsset>` | Material/shader params reference | Optional fallbacks handled upstream |
| world_transform | glm::mat4 | Object→World matrix | Column-major GLM convention |
| normal_transform | glm::mat4 | Inverse transpose (normals) | Recomputed on transform change |
| cast_shadows | bool | Participate as caster | Future: bitfield compression |
| receive_shadows | bool | Accept shadowing | — |
| render_layer | uint32 | Pass / filtering mask | Supports multi-pass selection |
| render_flags | uint32 | Custom per-item flags | Engine-defined bitmask (see future enum) |
| bounding_sphere | glm::vec4 | (center.xyz, radius) world-space | Sentinel radius=0 means undefined/empty |
| bounding_box_min/max | glm::vec3 | AABB in world space | Derived from mesh local bounds |

Validation Expectations:

* Mesh & material may be null during streaming: bounds become sentinel ⇒ item
  excluded by culling.
* After population, extraction must call `UpdateComputedProperties()` before
  publishing to passes.
* Future: add debug assert that radius >= 0 and min <= max per axis.

## 🚀 Roadmap & Best Practices Alignment

| Theme | Status | Planned Action | Rationale |
| -- | -- | -- | -- |
| LOD Selection | [x] Implemented | Integrated per-item chosen LOD index before DrawPacket build | Avoid per-pass recompute |
| Material Overrides | [x] Implemented | Runtime override table applied during low-level build | Avoid duplicating material assets |
| Instancing | [ ] Planned | Batch identical mesh/material transforms into instance buffer | Reduce draw call count |
| Dependency Tracking | [ ] Pending | Asset hot-reload hooks invalidating cached packets | Live editing support |
| SoA Layout | [ ] Pending | Optional parallel culling arrays | Improve SIMD & cache efficiency |
| Handle/Generation | [ ] Pending | Stable integer handle + generation counter | External references safety |
| GPU DrawPacket | [ ] Pending (Phase 6) | Compact struct (indices, resource handles, constants indices) | Fast submission |
| Multi-View Support | [x] Implemented | Per-view filtered extraction and draw lists | Shadows, reflections, portals |
| Motion Vectors | [ ] Pending | Store prev frame transform in high-level; packet holds both | TAA, motion blur |
| Streaming Fallbacks | [~] Partial | Deferred substitution with default assets early | Visual continuity |
| Validation / Tests | [x] Implemented | Unit tests for bounding volume transforms & flag logic | Robustness |

## 🧰 Potential Future Optimizations

* Quantized normals / transforms for static DrawPackets.
* CPU BVH build over high-level items (phase 7 option) feeding hierarchical
  culling.
* GPU-driven culling path (compute producing visible packet indices buffer).
* Per-material or per-mesh sort keys (64-bit) precomputed for stable sorting.

## ✅ Summary

* Geometry and materials are authored as assets and structured for efficient
  streaming and reuse.
* High-level render items bind geometry, materials, and transforms for scene
  logic.
* Low-level render items are transient GPU-ready draw calls derived from
  high-level items.
* The system supports instancing, parameter overrides, asset streaming, error
  handling, versioning, and hot-reloading.
* A stateful render item list enables efficient updates, multi-pass rendering,
  and robust asset tracking.

### Material Override Timing (Phase 1)

* Per-frame material snapshot is provided via
  `Renderer::SetMaterialConstants(...)` before `ExecuteRenderGraph`.
* Passes that use material shading (e.g., ShaderPass) will see the last snapshot
  set that frame.
* Future phases move toward per-item material resolution at packet build time;
  the Phase 1 snapshot is a temporary global override.

## 🗂 Revision History

* **December 28, 2025**: Updated to reflect current implementation state. Tables reformatted with proper spacing. Marked completed implementations: Phase 3 `RenderItemsList`, Phase 4 ScenePrep integration, multi-view support. Phase 6 `DrawPacket` design documented but marked pending.
* **August 13, 2025**: Added DrawPacket abstraction & cleanup for implementation start.
* **August 13, 2025**: Expanded to align with current `RenderItem` struct, added field
  reference, lifecycle & roadmap, inserted planned migration tasks.
* **August 13, 2025 (initial)**: Original conceptual high vs low-level description.

---

## 📦 Container Semantics (Phase 3 – Implemented)

Render items are now managed by `RenderItemsList` owned by `Renderer` (Phase 3).

* [x] Insert via `RenderItemsList::Add(RenderItem)` after visibility filtering
* [x] Container validates bounds (sphere radius ≥ 0; AABB min ≤ max) and calls
  `UpdateComputedProperties()` automatically
* [x] Read access for passes is via `PreparedSceneFrame` published to `RenderContext`
  per-view (set by ScenePrep finalization)
* [x] Renderer ensures GPU resources for the published draw list via ScenePrep
* [ ] DrawPacket-based mutation flow (pending Phase 6): Update via `DrawPacket` builder
  to ensure validation and recomputation

**Planned Enhancements (Phase 6+)**:

* [ ] Direct `Update(index, RenderItem)` path with automatic validation
* [ ] Expose mutation through packet builder to prevent corruption
