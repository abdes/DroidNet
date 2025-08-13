# 🎮 Render Items Architecture

This document outlines how geometry and material assets are composed into
high-level and low-level render items in Oxygen. It also addresses best
practices for instancing, asset change tracking, parameter overrides, asset
streaming, error handling, and versioning to ensure a robust and future-proof
design.

---

## 🧱 Geometry Asset

- Represents the 3D shape of a model.
- Stored as a binary asset with metadata, vertex/index buffers, and submesh
  descriptors.
- Designed for efficient streaming and partial loading.

### Geometry Asset Structure

```text
Geometry Asset (AssetID: "character_knight")
├── LOD 0 (MeshID: "character_knight_LOD0")
│   ├── Submesh 0 → Material Slot 0
│   └── Submesh 1 → Material Slot 1
├── LOD 1 (MeshID: "character_knight_LOD1")
│   ├── Submesh 0 → Material Slot 0
│   └── Submesh 1 → Material Slot 1
└── LOD 2 (MeshID: "character_knight_LOD2")
    ├── Submesh 0 → Material Slot 0
    └── Submesh 1 → Material Slot 1
```

### Key Concepts

- **LODs**: Multiple levels of detail for performance scaling based on screen
  size or distance.
- **Submeshes**: Smallest drawable units, each mapped to a material slot.
- **Material Slots**: Logical bindings that allow material overrides without
  modifying geometry.

### Best Practices

- Use stable AssetIDs and MaterialIDs (hashed strings or GUIDs) for referencing.
- Support partial loading of LODs and submeshes for large scenes.
- Include asset versioning in the binary format for backward compatibility and
  upgrades.
- Store bounding volumes per LOD for efficient culling.

---

## 🎨 Material Asset

- Describes how a surface should appear when rendered.
- Stored as a compact, streamable binary format.
- Designed for reuse, instancing, and runtime overrides.

### Material Asset Structure

- **Shader References**: Vertex, pixel, and optionally hull/domain shaders.
- **Texture References**: Albedo, normal, roughness, metallic, AO, etc.
- **Parameters**: Scalars, vectors, colors (e.g., roughness = 0.5, tint = [1,
  0.8, 0.6]).
- **Render States**: Flags like transparency, double-sided, depth write, etc.
- **Version**: Material asset version for compatibility and upgrade tracking.

### Instancing and Overrides

- Materials can be:
  - **Swapped**: Replace one material with another at runtime.
  - **Instanced**: Override only a few parameters for variation. All per-instance
    overrides are managed as runtime-only data, not stored in the asset binary.
    At runtime, a parameter override table is maintained by the engine or scene
    system, allowing unique values for specific instances without duplicating
    the entire material asset.

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

- The engine provides a set of default materials and textures (e.g.,
  "DefaultMaterial", "DefaultTexture", "DefaultNormal") as part of its core
  asset package. These are always available and loaded at engine startup.
- If a material asset or referenced texture is missing or fails to load, the
  engine automatically substitutes the corresponding default asset to ensure
  visual continuity and avoid rendering errors.
- Fallback assets are visually distinct (e.g., checkerboard, magenta, or labeled
  "MISSING") to make missing or broken references easy to spot during
  development and testing.

---

## 🧩 Composition into Render Items

### High-Level Render Item

- Represents a renderable object in the scene.
- Created when a model is instantiated.
- Contains:
  - Reference to a **Geometry Asset**
  - A **Material Set** (1:1 with submeshes)
  - A **Transform** (world matrix)
  - **Instance Data**: Optional per-instance overrides (e.g., tint, animation
    state)
- Stable across frames.
- Used for LOD selection, material overrides, and scene management.

#### Responsibilities

- High-level render items are created or updated for scene nodes that are
  determined to be visible by the Scene system (e.g., via scene graph traversal
  or spatial partitioning and culling). Render items themselves do not manage
  culling or scene structure.
- Each high-level render item maintains references to the MeshAsset and
  MaterialAsset(s) used by its associated scene node. If an asset is
  hot-reloaded or updated, the asset system notifies all dependent render items
  (or their owning scene nodes) to update or rebuild as needed. This avoids
  circular dependencies and ensures live updates are reflected in rendering.

### Low-Level Render Item

- Represents a single draw call.
- Created per frame from high-level items.
- Contains:
  - A **Submesh** (from selected LOD)
  - A **Resolved Material** (GPU-ready, with overrides applied)
  - GPU draw parameters (index count, offsets, etc.)
  - **Packed Instance Data**: For GPU consumption
- Transient and rebuilt every frame.
- Used by the renderer for batching, sorting, and submission.
- Uses fallback data if referenced assets are not yet loaded.

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

- All identifiers should support versioning and change tracking for
  hot-reloading and live updates.

---

## 📦 Sample Binary Layout

```text
[ModelHeader]
- AssetID
- LOD count
- Material slot count
- Offsets to LOD blocks
- Version

[LODHeader]
- MeshID
- Submesh count
- Offset to vertex/index data
- Offset to submesh descriptors

[SubmeshDescriptor]
- SubmeshID
- Index offset/count
- MaterialSlotID

[MaterialBindingTable]
- MaterialSlotID → MaterialID
- Optional: ParameterOverrideTable (for per-instance overrides)

[VertexBuffer]
[IndexBuffer]
```

---

## 🔄 Lifecycle Best Practices

### High-Level Render Items

- Created when a scene or object is loaded.
- Persist across frames.
- Updated when materials or geometry change (e.g., LOD switch, material
  override).
- Owned by the scene graph or renderable component system.
- Track dependencies on assets for hot-reloading and live updates.
- Support partial loading and streaming for large scenes.

### Low-Level Render Items

- Rebuilt every frame (or every visibility pass).
- Created by the render graph or visibility system.
- Owned by the renderer or frame allocator.
- Freed or recycled after each frame.
- Use fallback data if referenced assets are not yet loaded.

---

## 🧩 Render Items List: State, Justification, and Update Flows

A dedicated, stateful `RenderItemsList` class is justified in real-world engines
for:

- **Incremental Updates & Caching:** Avoid rebuilding the entire list every
  frame by tracking dirty nodes/assets and only updating what has changed (as
  seen in Unreal Engine's and Unity's render graph systems).
- **Multi-View/Pass Support:** Maintain separate lists for different views (main
  camera, shadow maps, reflections) or passes, each with their own filtering and
  visibility.
- **Dependency Tracking:** Track which render items depend on which assets for
  efficient hot-reloading and live updates (used in engines like Frostbite and
  Unreal).
- **Custom Filtering/Batching:** Persist user/editor-driven filters, sorting, or
  batching state across frames.
- **Parallel/Asynchronous Extraction:** Coordinate work between threads or jobs
  for large scenes, as in AAA engines.

### Frame Update Flow

1. **Scene Traversal/Query:**
   - The `RenderItemsList` queries the Scene for visible and renderable nodes
     (using traversal, culling, and filtering APIs).
2. **Extraction:**
   - For each visible node with a MeshAsset and Material(s), extract the
     necessary data (geometry, materials, transform, instance data).
3. **Render Item Construction:**
   - Build or update a high-level render item for each node, storing references
     to assets and per-instance overrides.
4. **Caching/Reuse:**
   - Reuse existing render items where possible; only create or destroy items as
     needed.
5. **Dependency Registration:**
   - Register dependencies between render items and their assets for
     hot-reloading and live updates.
6. **List Finalization:**
   - The list is now ready for use by the renderer or further processing (e.g.,
     for low-level render item generation).

### Important Update Flows

- **Frame Update:**
  - On each frame, the `RenderItemsList` checks for changes in scene visibility,
    node transforms, or instance data. Only affected render items are updated.
- **Asset Hot-Reload:**
  - When an asset (geometry or material) is reloaded or changed, the asset
    system notifies the `RenderItemsList`, which updates or rebuilds only the
    affected render items.
- **Scene Edits:**
  - When nodes are added, removed, or modified (e.g., LOD switch, material
    override), the list updates accordingly, creating, updating, or removing
    render items as needed.
- **Multi-View/Pass Update:**
  - For each view or rendering pass, the list may be filtered or rebuilt to
    match the specific requirements (e.g., shadow casters only, editor
    overlays).

---

## ✅ Summary

- Geometry and materials are authored as assets and structured for efficient
  streaming and reuse.
- High-level render items bind geometry, materials, and transforms for scene
  logic.
- Low-level render items are transient GPU-ready draw calls derived from
  high-level items.
- The system supports instancing, parameter overrides, asset streaming, error
  handling, versioning, and hot-reloading.
- A stateful render item list enables efficient updates, multi-pass rendering,
  and robust asset tracking.
