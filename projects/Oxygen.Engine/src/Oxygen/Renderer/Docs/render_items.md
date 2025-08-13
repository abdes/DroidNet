# 🎮 Render Items Architecture

Status: Active (implementation in progress). This document is a living design
that must track the actual `RenderItem` implementation (`RenderItem.h/.cpp`),
and the migration path toward a split High-Level vs Low-Level representation.

Scope:

* Current immutable(ish) per-object snapshot: `oxygen::engine::RenderItem`.
* Planned separation: HighLevelRenderItem (stable, scene owned) ⇒ DrawPacket /
  LowLevelRenderItem (frame transient, GPU–ready) (see Roadmap section).
* Supporting systems (RenderItemsList container, LOD selection, instancing,
  hot-reload dependency tracking) are referenced but not all implemented yet.

Non‑Scope: Full render graph resource lifetime solving, material system
authoring format, editor tooling.

Cross‑Refs:

* `implementation_plan.md` (Phase 1+ tasks)
* `gpu_resource_management.md` (pooling / lifetime)
* `data_flow.md` (pass data flow; to be updated as low-level split lands)
* `view_abstraction.md` (view snapshot consumed during extraction)

Legend (for embedded task lists below): [ ] pending, [~] in progress, [x] done,
[d] deferred.

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

* Produced post-culling by scene extraction code (future: also before culling
  for SoA build).
* Decoupled from scene graph nodes once constructed (no back pointers).
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
|-------|---------|-------|
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

| Concept                | High-Level Render Item                                 | Low-Level Render Item                                   |
|------------------------|--------------------------------------------------------|---------------------------------------------------------|
| 🧠 Purpose             | Describes what to render                               | Describes how to render it                              |
| 🧩 Composition         | Geometry + Material Set + Transform + Instance Data    | Submesh + Material + GPU-ready state + Instance Data    |
| 🧱 Abstraction Level   | Asset-level abstraction (CPU-side)                     | Draw-call-level abstraction (GPU-side)                  |
| 🔁 Lifecycle           | Created during scene loading or asset instantiation    | Created per frame during render graph or culling phase  |
| 📦 Ownership           | References geometry and materials                      | Owns resolved GPU handles and draw parameters           |
| 🔄 Mutability          | Stable across frames                                   | Transient, rebuilt every frame                          |
| 🎯 Use Case            | Scene management, LOD switching, material overrides    | Efficient GPU submission, batching, sorting             |

---

## 🧩 Identification Strategy

| Entity         | Identifier Type                  | Description |
|----------------|----------------------------------|-------------|
| Model Asset    | `AssetID` (string hash or GUID)  | Unique identifier for the model asset |
| Mesh (LOD)     | `MeshID` (AssetID + LOD index)   | Identifies a specific LOD mesh |
| Submesh        | `SubmeshID` (uint32 index)       | Index within the LOD mesh |
| Material Slot  | `MaterialSlotID` (uint32 index)  | 1:1 mapping with submeshes |
| Material Asset | `MaterialID` (string hash or GUID) | Unique identifier for a material asset |

* All identifiers should support versioning and change tracking for
  hot-reloading and live updates.

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

1. Introduce `RenderItemsList` orchestrating extraction & caching. [ ]
2. Split high vs low-level: high-level keeps asset/material references,
   transform, flags; low-level (DrawPacket) holds ready-to-submit geometry
   slices, GPU handles, packed constants. [ ]
3. Add versioning & handles: stable integer handle + generation counter for
   editor/tool references. [ ]
4. Move per-frame transient data (bounds after animation skinning, motion
   vectors) into low-level packet. [ ]
5. Optional SoA acceleration path for culling (separate arrays of centers,
   radii, layer bits). [ ]

### High-Level Render Items

* Created when a scene or object is loaded.
* Persist across frames.
* Updated when materials or geometry change (e.g., LOD switch, material
  override).
* Owned by the scene graph or renderable component system.
* Track dependencies on assets for hot-reloading and live updates.
* Support partial loading and streaming for large scenes.

### Low-Level Render Items

* Rebuilt every frame (or every visibility pass).
* Created by the render graph or visibility system.
* Owned by the renderer or frame allocator.
* Freed or recycled after each frame.
* Use fallback data if referenced assets are not yet loaded.

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

### Frame Update Flow (Target State)

1. **Scene Traversal/Query:**
   * The `RenderItemsList` queries the Scene for visible and renderable nodes
     (using traversal, culling, and filtering APIs).
2. **Extraction:**
   * For each visible node with a MeshAsset and Material(s), extract the
     necessary data (geometry, materials, transform, instance data).
3. **Render Item Construction:**
   * Build or update a high-level render item for each node, storing references
     to assets and per-instance overrides.
4. **Caching/Reuse:**
   * Reuse existing render items where possible; only create or destroy items as
     needed.
5. **Dependency Registration:**
   * Register dependencies between render items and their assets for
     hot-reloading and live updates.
6. **List Finalization:**
   * The list is now ready for use by the renderer or further processing (e.g.,
     for DrawPacket generation).

7. **DrawPacket Build:**
   * Visible RenderItems converted into DrawPackets (with optional instancing &
     LOD selection). [ ]

8. **Sort & Partition:**
   * Opaque: material/mesh grouping then depth buckets (front-to-back)
   * Transparent: strict back-to-front by depth

9. **Publish to RenderContext:**
   * Expose spans of DrawPackets (Renderer updates context). [ ]

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
|-------|------|---------|-------|
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

| Theme | Planned Action | Rationale |
|-------|----------------|-----------|
| LOD Selection | Integrate per-item chosen LOD index before DrawPacket build | Avoid per-pass recompute |
| Instancing | Batch identical mesh/material transforms into instance buffer | Reduce draw call count |
| Material Overrides | Runtime override table applied during low-level build | Avoid duplicating material assets |
| Dependency Tracking | Asset hot-reload hooks invalidating cached packets | Live editing support |
| SoA Layout | Optional parallel culling arrays | Improve SIMD & cache efficiency |
| Handle/Generation | Stable integer handle + generation counter | External references safety |
| GPU DrawPacket | Compact struct (indices, resource handles, constants indices) | Fast submission |
| Multi-View Support | Per-view filtered packet lists | Shadows, reflections, portals |
| Motion Vectors | Store prev frame transform in high-level; packet holds both | TAA, motion blur |
| Streaming Fallbacks | Deferred substitution with default assets early | Visual continuity |
| Validation / Tests | Unit tests for bounding volume transforms & flag logic | Robustness |

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

## 🗂 Revision History

* 2025-08-13: Added DrawPacket abstraction & cleanup for implementation start.
* 2025-08-13: Expanded to align with current `RenderItem` struct, added field
  reference, lifecycle & roadmap, inserted planned migration tasks.
* 2025-08-13 (initial): Original conceptual high vs low-level description.
