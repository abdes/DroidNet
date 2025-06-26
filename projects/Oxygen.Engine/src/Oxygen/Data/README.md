# Oxygen Engine Data Module

## Overview

The `Data` module provides definitions and management for all immutable,
shareable asset types used throughout the Oxygen Engine. This includes geometry,
materials, textures, and other data-centric resources that are referenced by
scene components, rendering systems, and asset pipelines.

---

## Design Principles

- **Immutability:** Assets are read-only after creation, enabling safe sharing
  and efficient caching. In-place mesh editing, dynamic LOD, or runtime asset
  mutation are not supported—any change requires creating a new asset instance.
  This is an intentional constraint to ensure safety and predictability.
- **Reference Counting:** Assets are managed via `std::shared_ptr` for robust
  lifetime management and deduplication.
- **Separation of Concerns:** Asset definitions are decoupled from scene,
  component, and runtime logic.
- **Extensibility:** The module is designed to accommodate new asset types
  (e.g., animation, audio) as the engine evolves.

---

## Key Asset Types

- **MeshAsset:**
  - Stores immutable geometry data (vertex/index buffers, submeshes, bounding
    volumes).
  - Referenced by `MeshComponent` or similar scene components.

- **MaterialAsset:**
  - Describes shader/material properties, textures, and render states.
  - Shared by many renderable instances.

- **TextureAsset:**
  - Represents immutable image data for use in materials, UI, etc.

---

## Typical Usage Pattern

1. **Asset Creation/Loading:**
   - Assets are created by importers, loaders, or procedural generators.
   - All data is provided at construction; assets are immutable after.

2. **Asset Management:**
   - Assets are registered in an asset table or manager for deduplication and
     streaming.
   - Referenced via `std::shared_ptr` from scene components or systems.

3. **Scene Integration:**
   - Scene nodes/components reference assets for rendering, animation, etc.
   - Asset data is extracted and batched for efficient rendering pipelines.

---

## MeshAsset

### MeshAsset Data Structure

MeshAsset holds all immutable mesh geometry and submesh descriptors for the
engine. Its data members are:

- `std::vector<Vertex> vertices_` — All mesh vertices, stored contiguously.
- `std::vector<uint32_t> indices_` — All mesh indices, stored contiguously.
- `std::vector<MeshView> views_` — Each MeshView describes a submesh (a range in
  the index buffer, typically grouped by material or logical part).
- `glm::vec3 bbox_min_`, `glm::vec3 bbox_max_` — The precomputed axis-aligned
  bounding box (AABB) corners for the mesh.

### Behavior and Usage

- MeshAsset is immutable: all geometry and submesh data is set at construction
  and never changes. Any mutation (e.g., mesh editing, procedural changes)
  requires creating a new MeshAsset instance.
- All mesh data is owned by the MeshAsset and is safe to share across systems
  via `std::shared_ptr`.
- Submeshes are described by MeshView objects, each referencing a valid,
  in-bounds range of the index buffer. The set of MeshViews is determined at
  construction, based on logical or material grouping.
- The bounding box is always precomputed from the vertex data and available for
  culling and spatial queries.
- All access to mesh data is read-only. Any mutation (e.g., mesh editing,
  procedural changes) requires creating a new MeshAsset instance.
- Use standard C++20 ranges and algorithms (e.g., `std::ranges::find_if`,
  range-for) for all non-mutating access to submeshes via the `views_` vector.
- MeshAsset guarantees safe, efficient sharing and deduplication across the
  engine, supporting robust asset management and rendering pipelines.

---

## Vertex

The `Vertex` structure defines the per-vertex attributes used for mesh geometry
in the Oxygen Engine. It is a standalone type, defined in its own header file,
to allow reuse and extension across different engine systems (e.g., procedural
generation, import/export, GPU upload, or physics). This design ensures that
`Vertex` is not tightly coupled to `MeshAsset` and can be referenced wherever
vertex data is needed.

```cpp
struct Vertex {
    glm::vec3 position;   // Object-space position
    glm::vec3 normal;     // Object-space normal
    glm::vec2 texcoord;   // Texture coordinates (UV)
    glm::vec3 tangent;    // Tangent vector (optional, for normal mapping)

    // Not implemented initially
    glm::vec3 bitangent;  // Bitangent vector (optional, for normal mapping)
    glm::vec4 color;      // Vertex color (optional, for per-vertex tinting)
    // Extend as needed: skin weights, bone indices, etc.
};
```

- **position**: Required for all meshes.
- **normal**: Required for lighting and shading.
- **texcoord**: Required for texturing.

> **Note:** The default equality operator (`operator==`) for Vertex is
  epsilon-based and suitable for geometric data and hash containers. If strict
  bitwise equality is required (e.g., for serialization or exact deduplication),
  use `StrictlyEqual` instead.

### Future Extensions

- **tangent/bitangent**: Needed for normal mapping and advanced shading
  (optional for simple meshes).
- **color**: Optional, used for per-vertex coloring or debugging.

This structure can be extended as your engine's needs grow (e.g., for skeletal
animation, morph targets, or custom attributes). All fields are stored in object
space and are suitable for direct upload to GPU buffers after layout packing.

---

## MeshView

The `MeshView` structure is a modern, encapsulated, memory-safe, and fully
immutable C++20 value type that describes a non-owning, read-only view into a
subset of a `MeshAsset`'s index buffer. MeshView is intentionally decoupled from
its parent mesh: it does not hold a pointer or reference to the parent
`MeshAsset`, and all validation or bounds checking must be performed externally
by the code that creates or uses it. MeshView does not provide an `IsValid()`
method by design; this ensures that validation responsibility is explicit and
external, supporting robust and predictable usage patterns.

To ensure safety and correctness, MeshView instances should only be created by
the owning `MeshAsset`, which guarantees that all views reference valid,
in-bounds ranges of its index buffer. This design follows C++ view conventions,
ensuring MeshView remains lightweight, copyable, and non-mutating, without
introducing ownership or lifetime complexity. It is designed for clarity,
safety, and efficient use in real-time rendering pipelines.

```cpp
class MeshAsset; // Forward declaration

class MeshView {
public:
    MeshView() = delete;
    // Only MeshAsset can construct MeshView instances
    friend class MeshAsset;

private:
    MeshView(uint32_t index_offset, uint32_t index_count)
        : index_offset_(index_offset), index_count_(index_count) {}

    uint32_t index_offset_ = 0;
    uint32_t index_count_ = 0;

public:
    [[nodiscard]] constexpr uint32_t IndexOffset() const noexcept { return index_offset_; }
    [[nodiscard]] constexpr uint32_t IndexCount() const noexcept { return index_count_; }

    auto operator==(const MeshView&) const noexcept -> bool = default;
    auto operator<=>(const MeshView&) const noexcept = default;
};
```

- **Encapsulation:** All members are private; access is via `constexpr`/`const`
  accessors.
- **Memory Safety:** No raw pointers or ownership; only value types.
- **Defaulted Comparison:** Supports equality and ordering for batching/sorting.
- **Modern C++:** Uses C++20 features (defaulted comparisons, `constexpr`,
  `[[nodiscard]]`).

> **Note:** MeshView does not provide an `IsValid()` method. All validation and
> bounds checking must be performed externally, as MeshView is intentionally
> decoupled from its parent mesh and cannot determine validity in isolation.

This interface is minimal, robust, and ready for use in high-performance, modern
C++ codebases.

---

## Procedural Mesh Generation

All procedural mesh factories (cube, sphere, plane, cylinder, cone, torus, quad,
arrow gizmo) are implemented as free functions, not as methods of MeshAsset.
This ensures MeshAsset remains immutable and decoupled from generation logic.
Each factory guarantees valid, manifold geometry and always returns a
fully-formed MeshAsset or `nullptr` on invalid input. All procedural mesh assets
use designated initializers, trailing commas, and are formatted to be
clang-format safe for maintainability.

---

## Error Handling and Validation

- All asset and view creation functions must validate input and fail fast
  (returning `nullptr` or throwing) on invalid parameters.
- All bounds checking and validation is the responsibility of the code that
  creates or uses MeshViews, not the view itself.
- MeshAsset construction is all-or-nothing; partial or invalid assets are never
  exposed.
- MeshView instances are only created by MeshAsset, ensuring all views reference
  valid, in-bounds ranges.

---

## Developer Guidance and Best Practices

- Always use `std::shared_ptr<MeshAsset>` for asset references; never use raw
  pointers or stack instances for shared assets.
- Never mutate asset data after creation; any change requires creating a new
  asset instance. This is a core design constraint, not a limitation.
- Use C++20 ranges and algorithms for all asset and submesh access (e.g.,
  `std::ranges::find_if`, range-for).
- Prefer procedural mesh factories for test assets, debugging, and editor tools.
- Use `AlmostEqual` for all floating-point vertex comparisons; never use
  `operator==` for geometric data.
- When extending asset types (e.g., animation, audio), follow the same
  immutability, encapsulation, and shared ownership patterns.
- Asset serialization, hot-reloading, and importers should operate only on
  immutable asset data, never mutating assets in place.

---

## Unit Testing

Unit tests for the Data module should focus on correctness, immutability, and
robustness of all core types and procedural mesh factories. Recommended test
areas are summarized below:

### Vertex (`Vertex_test.cpp`)

- *VertexBasicTest*
  - epsilon-based equality (AlmostEqual)
  - quantized hash consistency
- *VertexEdgeTest*
  - NaN
  - Inf
  - zero vectors
- *VertexHashTest*
  - Vertex in hash-based containers with custom hash/equality

### MeshAsset (`MeshAsset_test.cpp`)

- *MeshAssetBasicTest*
  - immutability
  - bounding box correctness
  - shared ownership
- *MeshAssetViewTest*
  - view validity
  - in-bounds checks

### MeshView (`MeshView_test.cpp`)

- *MeshViewBasicTest*
  - encapsulation
  - accessors
  - comparison
  - copy/move semantics

### Procedural Mesh Factories (`ProceduralMeshes_test.cpp`)

- *ProceduralMeshTest*
  - valid/invalid input
  - mesh validity
  - bounding box
  - default view
  - (one test per mesh type covering all scenarios)

Tests should follow scenario-based naming, AAA pattern, and use Google Test
matchers and fixtures as described in the project’s unit test instructions.

---

## Engine Data Future Extensions

- Animation assets (skeletons, clips)
- Audio assets
- Asset serialization and hot-reloading
- Asset pipeline tooling and importers
