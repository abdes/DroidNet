# Chunking / Loading

This document defines a binary `.pak` container format optimized for GPU asset
streaming, alignment, and fast loading. Inspired by Unreal Engine, Frostbite,
and modern GPU requirements.

---

## 1. Asset Classification Principles

This PAK format uses a three-tier architecture to balance performance,
flexibility, and maintainability:

**First-Class Assets** have independent existence with AssetKeys and can be
loaded individually across PAK files. They contain complex hierarchical data and
reference other assets or resources through embedded descriptors.

**Resources** are simple data blobs (textures, buffers, audio) grouped by type
for optimal memory mapping and GPU streaming. They are referenced by ID within
resource tables and cannot exist independently.

**Embedded Descriptors** are metadata structures that exist only within asset
descriptors, providing hierarchical organization without independent loading
capability.

### Asset & Resource Summary

#### First-Class Assets

| Type | Status | Purpose |
|------|--------|---------|
| **GeometryAsset** | ✅ Current | Multi-LOD mesh hierarchies |
| **MaterialAsset** | ✅ Current | Shader + texture combinations |
| **SceneAsset** | ✅ Current | Scene composition |
| **PrefabAsset** | 🔄 Future | Reusable object templates |
| **AnimationAsset** | 🔄 Future | Animation sequences |
| **ParticleSystemAsset** | 🔄 Future | Particle behavior definitions |

#### Resources

| Type | Status | Purpose |
|------|--------|---------|
| **TextureResource** | ✅ Current | GPU texture data |
| **BufferResource** | ✅ Current | Vertex/index/constant buffers |
| **ShaderResource** | ✅ Current | Compiled shader bytecode (stored in separate binary file, not in PAK) |
| **AudioResource** | 🔄 Future | Compressed audio data |
| **AnimationDataResource** | 🔄 Future | Bone weights, keyframes |
| **CollisionMeshResource** | 🔄 Future | Physics collision data |

#### Embedded Descriptors

| Type | Status | Purpose |
|------|--------|---------|
| **MeshDesc** | ✅ Current | LOD-specific mesh data |
| **SubMeshDesc** | ✅ Current | Material-specific submesh data |
| **MeshViewDesc** | ✅ Current | Draw call specifications |

---

## 📦 2. Asset Data Blobs

- Raw binary data for all resources, grouped by type for optimal memory mapping.
- Each resource is aligned to its required boundary
- Resources are stored in GPU-native formats:
  - Textures: BCn, ASTC, etc.
  - Buffers: tightly packed vertex/index data, aligned constant buffer data
  - Audio: compressed audio data (PCM, Vorbis, etc.)

> **Shader Bytecode Storage:**
>
> Compiled shader bytecode blobs are not stored in the PAK file. Instead, all
> shaders are pre-compiled at engine startup (if needed) and written to a
> dedicated binary file separate from the PAK. This allows for platform-specific
> optimization, rapid iteration, and avoids unnecessary duplication in asset
> bundles. The engine loads shader bytecode from this external file as needed.
>
> **Direct Mapping Rationale:**
>
> Asset data in the PAK file is padded and aligned to its required boundary so
> that it can be directly mapped into memory (e.g., via memory-mapped files) and
> used in-place by the engine or uploaded to the GPU without additional copying
> or realignment. This enables zero-copy streaming and the most efficient asset
> loading strategies, especially on platforms or engines that support direct
> file mapping to GPU resources. If the data were not aligned in the file,
> direct mapping would not be possible, and extra memory copies or realignment
> would be required at load time.
>
> **Summary:** Padding/alignment in the PAK file is not strictly required for
> all loading strategies, but it is essential for supporting direct mapping and
> zero-copy streaming, which are important for high-performance engines and
> future-proofing the format.

---

## 🧠 3. GPU Alignment Guidelines

| Resource Type    | Alignment | Notes                          |
|------------------|-----------|--------------------------------|
| Constant Buffer  | 256 bytes | Required by D3D12/Vulkan       |
| Vertex Buffer    | 16 bytes  | For vec4 alignment             |
| Index Buffer     | 4 bytes   | Typically uint32_t             |
| Texture Data     | 256 bytes | For optimal copy granularity   |
| Audio Data       | 16 bytes  | For cache line alignment       |

---

## 📜 4. High-Level File Layout

The binary layout of a `.pak` file is as follows:

```text
[ PakHeader ]
[ Textures (Data Blobs) ]
    [ Texture 0 Data (aligned)]
    [ Texture 1 Data (aligned)]
    ...
[ Buffers (Data Blobs) ]
    [ Buffer 0 Data (aligned)]
    [ Buffer 1 Data (aligned)]
    ...
[ Audio Blobs (Data Blobs) ]
    [ Audio Blob 0 Data (aligned)]
    ...

... Other data blob types (excluding shaders)

[ Global Resource Tables ]
    [ TextureTable[] ]
        [ TextureTableEntry 0 ]
        [ TextureTableEntry 1 ]
        ...
    [ BufferTable[] ]
        [ BufferTableEntry 0 ]
        [ BufferTableEntry 1 ]
        ...
    [ AudioTable[] ]
        [ AudioTableEntry 0 ]
        ...
[ Asset Descriptors ]
    [ Asset 0 Descriptor (e.g. GeometryAsset, MaterialAsset, ...) ]
    [ Asset 1 Descriptor ]
    ...
    [ Asset N-1 Descriptor (aligned) ]
[ AssetDirectory[] ]
    [ AssetDirectoryEntry 0 ]
    [ AssetDirectoryEntry 1 ]
    ...
    [ AssetDirectoryEntry N-1 ]
[ PakFooter ]
```

## Loading Procedure

1. **Bootstrap Phase**: Load PakFooter and AssetDirectory to build asset lookup table
2. **Resource Resolution**: Load required Global Resource Tables based on asset types needed
3. **Asset Loading**: For each requested asset:
   a. Load asset descriptor using AssetKey -> directory entry -> descriptor mapping
   b. Parse embedded asset (AssetKey) and resource (ResourceIndexT) references within the descriptor
   c. Recursively resolve embedded AssetKey dependencies
   d. Resolve resource IDs to actual data offsets using resource tables
   e. Memory map or load required data blob regions

**Loading Strategy Benefits:**

- **Partial Loading**: Only load resource tables and data regions actually needed
- **Memory Mapping**: Entire resource regions can be memory mapped efficiently
- **Stable References**: Resource IDs remain valid regardless of loading order
- **Deduplication**: Multiple assets can reference the same resource ID
- **Platform Optimization**: Skip resource types not supported on target platform
