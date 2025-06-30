# Asset Entity Relationships: Summary and Diagram

## Relationship Summary Table

| From      | To        | Cardinality | Notes                                                                 |
|-----------|-----------|-------------|-----------------------------------------------------------------------|
| Geometry  | Mesh      | 1 : N       | 🌳 Geometry is the root structure; it maps to multiple Meshes for LODs or variants. |
| Mesh      | SubMesh   | 1 : N       | 🧩 A Mesh is subdivided into SubMeshes — logical partitions for rendering. |
| SubMesh   | MeshView  | 1 : N       | 📏 A SubMesh groups one or more contiguous MeshViews (range slices of the Mesh). |
| SubMesh   | Material  | 1 : 1       | 🎚️ Each SubMesh is rendered with a single Material instance.         |
| Material  | Texture   | 0 : N       | 🖼️ A Material can have zero or more Textures (e.g., color maps, normal maps). |
| Material  | Shader    | 1 : N       | 🧠 A Material can reference multiple Shaders, at most one per stage (see ShaderStageFlags). |

## Entity Dependency Flowchart

```mermaid
flowchart TD
    Geometry["Geometry (1..N Meshes, LODs)"]
    Mesh["Mesh (LOD) (1..N SubMeshes)"]
    SubMesh["SubMesh (1..N MeshViews, 1 Material)"]
    MeshView["MeshView"]
    Material["Material (0..N Textures, 1..N Shaders)"]
    Texture["Texture"]
    Shader["Shader"]

    Geometry -- "1..N" --> Mesh
    Mesh -- "1..N" --> SubMesh
    SubMesh -- "1..N" --> MeshView
    SubMesh -- "1" --> Material
    Material -- "0..N" --> Texture
    Material -- "1..N" --> Shader
```
