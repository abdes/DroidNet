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

This section distinguishes between what is IMPLEMENTED in the current PAK
format (v1) and what is PLANNED / FUTURE. Earlier revisions of this document
were aspirational and mentioned fields not yet present in
`MaterialAssetDesc`. The snapshot below reflects the actual layout of
`MaterialAssetDesc` (256 bytes) as of this commit.

Material properties are grouped into the following categories:

#### A. Identification & Metadata

- `name` (string): For debugging, asset tracking, and editor display.
- `domain` (enum): E.g., Opaque, AlphaBlended, Masked. Controls pipeline
  behavior and render pass selection.

#### B. Texture References

Current implementation exposes EXACTLY five core texture indices:

| Texture (PAK Field)         | Implemented | Notes |
|-----------------------------|-------------|-------|
| base_color_texture          | Yes         | Fallback to `base_color[4]` if `kNoResourceIndex (0)` |
| normal_texture              | Yes         | Scaled by `normal_scale` |
| metallic_texture            | Yes         | Metalness channel (no packing with roughness) |
| roughness_texture           | Yes         | Roughness channel (separate from metallic) |
| ambient_occlusion_texture   | Yes         | Multiplies lighting; fallback scalar `ambient_occlusion` |
| reserved_textures[0..7]     | Reserved    | Future extensions (emissive, opacity, etc.) |

Future (not yet in descriptor – treat as roadmap, not available at runtime):

| Potential Texture Slot | Rationale |
|------------------------|-----------|
| emissive_texture       | Glow / unlit contribution |
| opacity_texture        | Cutout / blended alpha control |
| height_parallax_tex    | Parallax / displacement mapping |
| clearcoat_texture      | Layered specular (automotive paint) |
| sheen_texture          | Cloth / fabric response |
| transmission_texture   | Thin transparency / glass |
| subsurface_texture     | SSS mask or color |

#### C. Scalar Factors (PBR)

Implemented scalars (in `MaterialAssetDesc`):

| Field              | Type   | Purpose |
|--------------------|--------|---------|
| base_color[4]      | float4 | Fallback RGBA (alpha currently not used for blending in core code) |
| normal_scale       | float  | Scales sampled normal map (0 = flat) |
| metalness          | float  | Fallback if metallic_texture == 0 |
| roughness          | float  | Fallback if roughness_texture == 0 |
| ambient_occlusion  | float  | Fallback if ambient_occlusion_texture == 0 |

Planned (NOT in current binary layout): emissive_factor, opacity, height_scale,
subsurface, clearcoat, sheen, transmission. Adding any of these requires a
format version bump or usage of reserved bytes.

#### D. Flags & Options

`flags` (uint32) is a generic bitfield. The current codebase does not define a
public enum for individual bits yet; proposed semantics (subject to change):

| Bit (Conceptual) | Meaning |
|------------------|---------|
| 0                | Double-sided (disable backface culling) |
| 1                | Alpha test (cutout) |
| 2                | Receives shadows |
| 3                | Casts shadows |
| 4                | Unlit (skip PBR lighting) |
| 5                | Wireframe (debug) |
| 6..31            | Reserved / engine-specific |

Until codified in a shared header, treat these as advisory only.

---

### 🧩 Material Asset Structure

Current PAK v1 layout (summarized):

| Offset | Size | Field | Notes |
|--------|------|-------|-------|
| 0x00   | 96   | header | `AssetHeader` (name, type, version) |
| 0x60   | 1    | material_domain | Cast to `MaterialDomain` |
| 0x61   | 4    | flags | Bitfield (see advisory table) |
| 0x65   | 4    | shader_stages | Bitfield population count → number of `ShaderReferenceDesc` following |
| 0x69   | 16   | base_color | float[4] RGBA |
| 0x79   | 4    | normal_scale | float |
| 0x7D   | 4    | metalness | float |
| 0x81   | 4    | roughness | float |
| 0x85   | 4    | ambient_occlusion | float |
| 0x89   | 4    | base_color_texture | Resource index (0 = none) |
| 0x8D   | 4    | normal_texture | Resource index |
| 0x91   | 4    | metallic_texture | Resource index |
| 0x95   | 4    | roughness_texture | Resource index |
| 0x99   | 4    | ambient_occlusion_texture | Resource index |
| 0x9D   | 32   | reserved_textures[8] | Future texture indices |
| 0xBD   | 68   | reserved | Future scalar/flags expansion |
| 0x100  | ...  | ShaderReferenceDesc[] | One per set bit in `shader_stages` |

`static_assert(sizeof(MaterialAssetDesc)==256)` ensures binary stability.

---

### 🧬 Instancing and Parameter Overrides

Status: NOT YET IMPLEMENTED in code (conceptual design only). Current runtime
representation (`MaterialAsset`) is immutable and does not expose an override
API. Planned approach:

1. Maintain a GPU-visible structured buffer of per-instance override records.
2. Each record references a base material index + packed override mask.
3. Shader blends fallback scalars with override values at draw time.

Until implemented, variation should be achieved by distinct material assets or
via vertex/instance data (e.g., color tints).

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

### 🎨 Material System (Runtime Access Pattern)

Rather than a bespoke CPU `Material` struct, the engine currently exposes an
immutable `MaterialAsset` wrapper over the binary descriptor. Example usage:

```cpp
using oxygen::data::MaterialAsset;

auto mat = MaterialAsset::CreateDefault();

auto domain = mat->GetMaterialDomain();
auto baseColor = mat->GetBaseColor(); // span<float,4>
auto baseColorTex = mat->GetBaseColorTexture();
if (baseColorTex == 0) {
  // Use fallback scalar base_color[]
}
auto roughnessTex = mat->GetRoughnessTexture();
float roughness = mat->GetRoughness(); // fallback if texture missing
```

Shader-side, a tightly packed GPU struct will mirror only the fields actually
needed; that packing step (and potential compression) is a renderer concern and
not part of the `MaterialAsset` API.

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

- Only five texture indices are consumed today; plan headroom before choosing a
  packing / compression strategy for future additions.
- `reserved_textures` and `reserved` bytes in the descriptor allow additive
  evolution without immediate format break; larger semantic changes should
  still bump a version constant (not yet present—recommend adding when first
  extension lands).
- Default & debug materials are factory-created helpers (`CreateDefault`,
  `CreateDebug`) returning shared immutable assets.
- Define a canonical bit layout for `flags` before shipping public builds to
  avoid content divergence.
- Consider a small indirection table for GPU-packed materials to allow hot
  re-packing without touching all instance records.

---

## 📌 Extension Guide (Adding Emissive Support Example)

1. Repurpose `reserved_textures[0]` as `emissive_texture` (retain 0 as none).
2. Allocate 12 bytes inside `reserved` for `float emissive_color[3]` + padding.
3. Add getters to `MaterialAsset` (e.g., `GetEmissiveTexture()` /
   `GetEmissiveColor()`).
4. Update PAK writer to emit new fields; increment internal material format
   minor revision (introduce a version in descriptor if absent).
5. Update shaders to sample emissive when texture or non-zero color present.
6. Add unit tests: default emissive zero, fallback path, texture index
   propagation.

Repeat analogous steps for opacity / transmission, ensuring careful reservation
consumption order documented in the descriptor comment block.
