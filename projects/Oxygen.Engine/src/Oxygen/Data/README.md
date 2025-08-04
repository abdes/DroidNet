# Oxygen Engine Data Module

## Overview

The `Data` module defines and manages all immutable, shareable asset types used
throughout the Oxygen Engine. This includes geometry, materials, textures, and
other data-centric resources referenced by scene components, rendering systems,
and asset pipelines.

---

## Design Principles

- **Immutability:** All asset types (`Mesh`, `SubMesh`, `MeshView`,
  `GeometryAsset`, `MaterialAsset`, `TextureResource`, etc.) are immutable after
  creation. Any modification requires creating a new instance. This ensures
  thread safety, predictable behavior, and efficient sharing.
- **Reference Counting:** Assets are managed via `std::shared_ptr` for robust
  lifetime management and deduplication.
- **Separation of Concerns:** Asset definitions are decoupled from scene,
  component, and runtime logic.
- **Extensibility:** The module is designed to accommodate new asset types
  (e.g., animation, audio) as the engine evolves.

---

## Key Asset Types

- **Mesh:**
  - Stores immutable geometry data: vertex buffer (`std::vector<Vertex>`), index
    buffer (`std::vector<uint32_t>`), submesh descriptors
    (`std::vector<SubMesh>`), and precomputed bounding box (`glm::vec3
    bbox_min_`, `glm::vec3 bbox_max_`).
  - Referenced by scene components such as `MeshComponent`.
  - Construction is all-or-nothing; partial or invalid meshes are never exposed.

- **SubMesh:**
  - Groups one or more contiguous `MeshView` instances and associates them with
    a `MaterialAsset`.
  - Logical partitions of a mesh for rendering, material binding, and culling.

- **MeshView:**
  - Non-owning, value-type view into a contiguous subrange of a mesh's vertex
    and index data.
  - Only `Mesh` can construct `MeshView` instances, ensuring safe, non-owning
    access.

- **GeometryAsset:**
  - Owns one or more LOD meshes (`std::vector<std::shared_ptr<Mesh>>`).
  - Provides asset-level metadata and bounding volumes.

- **MaterialAsset:**
  - Describes shader/material properties, textures, and render states.
  - Shared by many renderable instances.

- **TextureResource:**
  - Represents immutable image data for use in materials, UI, etc.

See the corresponding header files for details: `MeshType.h`, `Vertex.h`,
`MaterialAsset.h`, `TextureResource.h`, `GeometryAsset.h`, `ProceduralMeshes.h`.

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

## Mesh, SubMesh, MeshView: Implementation Details

**Mesh** is the core immutable geometry container. It owns all vertex and index
data, manages submeshes, and precomputes bounding volumes. Meshes are
constructed with all data provided up front; partial or invalid meshes are never
exposed. All access is read-only, and sharing is via `std::shared_ptr`.

**SubMesh** groups one or more contiguous MeshView instances and associates them
with a MaterialAsset. SubMeshes are logical partitions for rendering, material
binding, and culling. Only Mesh can construct SubMesh instances, ensuring
correct ownership and encapsulation.

**MeshView** is a non-owning, value-type view into a contiguous subrange of a
mesh's vertex and index data. MeshView does not own memory and is only
constructible by Mesh, guaranteeing safe, in-bounds access. All validation and
bounds checking is performed externally by the code that creates or uses
MeshView.

**GeometryAsset** owns one or more LOD meshes and provides asset-level metadata
and bounding volumes. It is the top-level container for geometry resources
referenced by scene components and rendering systems.

**MaterialAsset** and **TextureResource** describe shader/material properties
and immutable image data, respectively. These are shared by many renderable
instances and referenced by submeshes and materials.

Procedural mesh generation is implemented via free functions, not as methods of
Mesh. These factories guarantee valid, manifold geometry and always return
complete vertex/index buffers suitable for mesh construction. All procedural
assets use designated initializers and are formatted for maintainability.

All asset and view creation functions validate input and fail fast on invalid
parameters. Mesh construction is all-or-nothing; partial or invalid assets are
never exposed. MeshView instances are only created by Mesh, ensuring all views
reference valid, in-bounds ranges.

For asset references, always use `std::shared_ptr`; never use raw pointers or
stack instances for shared assets. Never mutate asset data after creation; any
change requires creating a new asset instance. Use C++20 ranges and algorithms
for all asset and submesh access. Asset serialization, hot-reloading, and
importers operate only on immutable asset data.

---

## Vertex: Implementation Details

The Vertex structure defines the per-vertex attributes used for mesh geometry.
It is standalone and reusable across engine systems (procedural generation,
import/export, GPU upload, or physics). Vertex is not tightly coupled to Mesh
and can be referenced wherever vertex data is needed. Attributes include
position, normal, texcoord, tangent, bitangent, and color. The structure is
extensible for future needs such as skin weights or bone indices. Equality and
hashing are epsilon-based for geometric data; strict bitwise equality is
available for serialization or deduplication.

---

## MeshView: Implementation Details

MeshView is a modern, encapsulated, memory-safe, and fully immutable value type
that describes a non-owning, read-only view into a subset of a Mesh's index
buffer. MeshView is intentionally decoupled from its parent mesh; it does not
hold a pointer or reference to the parent Mesh, and all validation or bounds
checking must be performed externally. MeshView is lightweight, copyable, and
non-mutating, designed for clarity, safety, and efficient use in real-time
rendering pipelines. Only Mesh can construct MeshView instances, ensuring all
views reference valid, in-bounds ranges.

---

## Procedural Mesh Generation

Procedural mesh factories (cube, sphere, plane, cylinder, cone, torus, quad,
arrow gizmo) are implemented as free functions, not as methods of Mesh. This
ensures Mesh remains immutable and decoupled from generation logic. Each factory
guarantees valid, manifold geometry and always returns complete vertex/index
buffers suitable for mesh construction. All procedural mesh assets use
designated initializers and are formatted for maintainability.

---

## Error Handling and Validation

All asset and view creation functions validate input and fail fast on invalid
parameters. Mesh construction is all-or-nothing; partial or invalid assets are
never exposed. MeshView instances are only created by Mesh, ensuring all views
reference valid, in-bounds ranges. All bounds checking and validation is the
responsibility of the code that creates or uses MeshView, not the view itself.

---

## Developer Guidance and Best Practices

Always use `std::shared_ptr<Mesh>` for asset references; never use raw pointers
or stack instances for shared assets. Never mutate asset data after creation;
any change requires creating a new asset instance. Use C++20 ranges and
algorithms for all asset and submesh access. Prefer procedural mesh factories
for test assets, debugging, and editor tools. When extending asset types (e.g.,
animation, audio), follow the same immutability, encapsulation, and shared
ownership patterns. Asset serialization, hot-reloading, and importers should
operate only on immutable asset data.

---

## Unit Testing

Unit tests for the Data module focus on correctness, immutability, and
robustness of all core types and procedural mesh factories. Tests cover Vertex
(equality, hashing, edge cases), Mesh (immutability, bounding box, shared
ownership), MeshView (encapsulation, accessors, comparison), SubMesh (material
association, view grouping), GeometryAsset (LOD mesh management, bounding
volumes), and procedural mesh factories (valid/invalid input, mesh validity,
bounding box). Tests follow scenario-based naming, AAA pattern, and use Google
Test matchers and fixtures as described in the project’s unit test instructions.

---

## Engine Data Future Extensions

- Animation assets (skeletons, clips)
- Audio assets
- Asset serialization and hot-reloading
- Asset pipeline tooling and importers
