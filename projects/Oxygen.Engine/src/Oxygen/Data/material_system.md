# Material System Design

## Introduction

Modern real-time rendering engines, such as those powering AAA games and
advanced visualization tools, rely on a robust and flexible material system to
achieve high visual fidelity, efficient resource management, and scalable
performance. In the context of the Oxygen Engine, the material system is
designed to maximize GPU efficiency, support physically based rendering (PBR)
workflows, and seamlessly integrate with a bindless rendering architecture.

The Oxygen material system abstracts the complexity of resource binding and
asset management by representing all material properties and texture references
in a GPU-friendly format. Instead of using CPU-side pointers or engine-specific
handles, materials reference textures and buffers via indices into a global
descriptor heap. This approach, inspired by the architectures of engines like
Unreal Engine and modern in-house AAA solutions, enables the renderer to batch
draw calls, minimize state changes, and fully leverage the capabilities of APIs
such as DirectX 12 and Vulkan.

By decoupling material definitions from resource bindings, the Oxygen material
system allows for:

- **Efficient Asset Deduplication:** Textures and buffers are loaded once and
  referenced by index, reducing memory usage and load times.
- **Scalable Rendering:** The global descriptor heap enables thousands of unique
  materials and textures to be accessed in a single pass, without per-material
  binding overhead.
- **Flexible Material Authoring:** Materials can be defined using a combination
  of texture indices and scalar factors, supporting both simple and complex
  shading models.

This document outlines the core design principles, data structures, and
integration strategies for the Oxygen Engine’s material system, providing a
foundation for future extensions and optimizations.

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

A material asset is a data-driven description of how a surface should appear
when rendered. It defines how light interacts with a surface—whether it’s shiny,
rough, metallic, translucent, etc. Think of it as a recipe: the material asset
doesn’t do the rendering itself—it tells the engine what ingredients to use and
how to cook them.

Material assets are stored as compact, streamable binary files and are designed
for reuse, instancing, and runtime overrides.

---

### 🔖 Material Asset Properties

Material properties are grouped into the following categories:

#### A. Identification & Metadata

- `name` (string): For debugging, asset tracking, and editor display.
- `domain` (enum): E.g., Opaque, AlphaBlended, Masked. Controls pipeline
  behavior and render pass selection.
- `version` (uint): For compatibility and upgrade tracking.

#### B. Texture References

- Bindless indices into a global descriptor heap:
  - `base_color_texture_index`
  - `normal_texture_index`
  - `metallic_roughness_texture_index`
  - `emissive_texture_index`

✅ Indices are compact, GPU-friendly, and decouple asset management from rendering.

#### C. Scalar Factors (PBR)

Used to modulate or replace texture values:

- `base_color` (float3 or float4)
- `roughness` (float)
- `metalness` (float)
- `emissive_factor` (float3)
- `opacity` (float)

✅ Factors support simple materials without textures, and allow runtime overrides.

#### D. Flags & Options

- Boolean flags or bitfields for:
  - `double_sided`
  - `alpha_test`
  - `depth_write`
  - `transparent`
- These control render state configuration and shader branching.

---

### 🧩 Material Asset Structure

A typical material asset includes:

- **Shader References**: Vertex, pixel, and optionally hull/domain shaders.
- **Texture References**: As bindless indices into a global descriptor heap.
- **Scalar/Vector Parameters**: PBR factors and color/emissive overrides.
- **Render States**: Flags for transparency, culling, depth behavior, etc.
- **Metadata**: Name, domain, version.

---

### 🧬 Instancing and Parameter Overrides

Material instancing allows lightweight variation without duplicating the base
asset.

- **Swapping**: Replace one material with another at runtime.
- **Instancing**: Override specific parameters (e.g., color tint) per instance.

> Example:
> A tree model uses a shared leaf material. To add natural variation, each tree
> instance can override the leaf color using a per-instance parameter table. The
> renderer applies these overrides when building low-level render items, so each
> tree appears unique without duplicating the material asset or storing
> overrides in the asset file.

- Per-instance overrides are runtime-only and stored in a parameter override
  table or GPU buffer.
- The base material remains immutable and shared across instances.

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

## 🧩 Integration Strategy

This section outlines how the asset system, material system, and renderer
cooperate to support efficient, bindless rendering using a global descriptor
heap. It emphasizes deferred GPU resource creation to support streaming, memory
efficiency, and runtime flexibility.

---

### 📦 Asset Management

- The asset system is responsible for loading and tracking asset metadata and
  raw data (e.g., texture headers, image data, material definitions).
- The renderer is responsible for creating GPU resources (e.g., SRVs) from these
  assets on demand and assigning them indices in the global descriptor heap.
- GPU resource creation is deferred until the asset is actually needed by a
  visible render item or material.
- Each unique texture is loaded once and assigned a stable, unique index into
  the global descriptor heap when promoted to GPU memory.
- The asset system maintains a mapping:

  ```text
  TextureAssetID (e.g., hash of file path) → DescriptorHeapIndex (uint32)
  ```

- This enables:
  - Deduplication of resources
  - Fast lookup during material instancing
  - Consistent indexing across frames and materials
  - Streaming-friendly behavior (CPU-side assets can exist without GPU residency)

---

### 🔗 Bindless Rendering

- A single, global descriptor heap (SRV/UAV/CBV) is created and managed by the renderer.
- All GPU-resident textures and buffers are placed into this heap at fixed indices.
- The heap is bound once per frame or per pass using root descriptor tables.
- Shaders access resources using 32-bit indices passed via material or instance data:

  ```hlsl
  Texture2D baseColor = ResourceHeap[material.baseColorIndex];
  ```

- ✅ Benefits:
  - Eliminates per-material descriptor sets
  - Enables massive batching and material variation
  - Reduces CPU-GPU synchronization and descriptor rebinding
  - Supports streaming and dynamic resource residency

---

### 🎨 Material System

- The `Material` struct is GPU-friendly and compact:

  ```cpp
  enum class MaterialDomain : uint8_t
  {
      Opaque,         // Standard opaque material
      AlphaBlended,   // Transparent material (add more as needed)
  };

  struct Material
  {
    std::string name;                        // For debugging and asset tracking
    MaterialDomain domain = Opaque;          // Controls blending, depth, etc.

    // Texture indices into the global bindless descriptor heap
    uint32_t baseColorTextureIndex = 0;
    uint32_t normalTextureIndex = 0;
    uint32_t metallicRoughnessTextureIndex = 0;
    uint32_t emissiveTextureIndex = 0;

    // Scalar fallback factors (used if textures are missing)
    float3 baseColor = float3(1.0f);
    float roughness = 1.0f;
    float metalness = 0.0f;
    float3 emissiveFactor = float3(0.0f);
    float opacity = 1.0f;

    uint32_t flags = 0; // Bitfield for double-sided, alpha test, etc.
  };

  // No CPU-side pointers or handles to textures—only indices for GPU access.
  // The asset system manages texture lifetimes and deduplication.
  // The renderer and shaders use only these indices to fetch resources from the global heap.
  ```

- When a material is created or loaded:
  - The asset system resolves texture references to logical IDs.
  - The renderer resolves these IDs to descriptor heap indices on demand.
  - The material struct is populated with indices and scalar parameters.

- At render time:
  - The material struct is uploaded to a GPU-visible buffer (e.g., structured buffer or bindless array).
  - Shaders use the indices to fetch textures from the global heap.

---

### 🔄 Integration Workflow

1. **Asset Loading**
   - Asset system loads texture metadata and raw image data.
   - No GPU resources are created at this stage.

2. **Material Creation**
   - Material assets reference textures by logical ID or asset handle.
   - Texture indices are resolved only when the material is used by a visible render item.
   - The material struct is populated with indices and scalar parameters.

3. **Rendering**
   - Renderer binds the global descriptor heap once per frame or pass.
   - Material data is passed to shaders via a structured buffer or root constant.
   - Shaders use indices to access textures from the heap.

4. **Deferred GPU Resource Creation**
   - If a texture is referenced by a visible material and not yet resident on the GPU:
     - The renderer uploads it and assigns it a descriptor heap index.
     - The index is cached for future use.

5. **No Per-Material Bindings**
   - No descriptor sets or root signatures are created per material.
   - All binding is handled via indices and the global heap.

---

### 🛠 Implementation Notes

- Descriptor heap size should be large enough to accommodate all scene resources (e.g., 64K entries).
- Consider double-buffering or ring-buffering the heap if dynamic updates are needed.
- Material structs can be stored in a GPU-visible buffer and indexed per draw or per instance.
