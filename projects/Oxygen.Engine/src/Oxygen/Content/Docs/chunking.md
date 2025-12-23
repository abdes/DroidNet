# Chunking / Loading

> Defines the PAK file format (layout, alignment, classification). For implementation status see `implementation_plan.md#detailed-feature-matrix`.

This document defines a binary `.pak` container format optimized for GPU asset
streaming, alignment, and fast loading. Inspired by Unreal Engine, Frostbite,
and modern GPU requirements.

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

| Type | Purpose |
| ---- | ------- |
| **GeometryAsset** | Multi-LOD mesh hierarchies |
| **MaterialAsset** | Shader + texture combinations |
| **SceneAsset** | Scene composition |
| **PrefabAsset** | Reusable object templates (future) |
| **AnimationAsset** | Animation sequences (future) |
| **ParticleSystemAsset** | Particle behavior definitions (future) |

#### Resources

| Type | Purpose |
| ---- | ------- |
| **TextureResource** | GPU texture data |
| **BufferResource** | Vertex/index/constant buffers |
| **ShaderResource** | Compiled shader bytecode (stored separately, not in PAK) |
| **AudioResource** | Compressed audio data (future) |
| **AnimationDataResource** | Bone weights, keyframes (future) |
| **CollisionMeshResource** | Physics collision data (future) |

#### Embedded Descriptors

| Type | Purpose |
| ---- | ------- |
| **MeshDesc** | LOD-specific mesh data |
| **SubMeshDesc** | Material-specific submesh data |
| **MeshViewDesc** | Draw call specifications |

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

| Resource Type | PAK File Alignment | GPU Upload Alignment (D3D12 Example) | Notes |
| ------------- | ------------------ | ------------------------------------ | ----- |
| Constant Buffer | 256 bytes | 256 bytes (CBV upload) | Required by D3D12/Vulkan for CBV; PAK file aligns for direct mapping |
| Vertex Buffer | 16 bytes | 16 bytes (VB upload) | D3D12 requires 16-byte alignment for vertex buffers; PAK file matches for zero-copy |
| Index Buffer | 4 bytes | 4 bytes (IB upload) | D3D12 requires 4-byte alignment for index buffers |
| Texture Data | 256 bytes | 256 bytes (copy/upload) | D3D12 optimal copy granularity; PAK file aligns for direct mapping |
| Audio Data | 16 bytes | 16 bytes | For cache line alignment; not directly uploaded to GPU |

### Alignment Clarification: Storage vs GPU Upload

**PAK File Alignment:** All resources in the PAK file are padded and aligned to
their required boundaries for direct memory mapping. This means buffer and
texture data can be mapped into memory without additional copying or
realignment, enabling zero-copy streaming and efficient asset loading.

**GPU Upload Alignment (D3D12 Example):** When uploading resources to the GPU
(e.g., via Direct3D12), the engine must respect the alignment requirements of
the graphics API:

- **Constant Buffers:** Must be 256-byte aligned for CBV creation and upload.
- **Vertex Buffers:** Must be 16-byte aligned for VB upload.
- **Index Buffers:** Must be 4-byte aligned for IB upload.
- **Textures:** Must be 256-byte aligned for optimal copy granularity.

The engine ensures that the alignment in the PAK file matches the GPU upload
requirements, allowing direct mapping or efficient copying. If the PAK file were
not aligned, additional memory copies or realignment would be required at load
time, reducing performance.

**Why align PAK data to GPU requirements?**

Storing asset data in the PAK file aligned to GPU requirements (e.g., 256 bytes
for constant buffers, 16 bytes for vertex buffers) enables direct memory mapping
and zero-copy streaming, which are critical for high-performance asset loading
and rendering. If PAK data were stored unaligned, every resource would require
extra memory copies and realignment at load time, increasing CPU overhead and
latency—especially for large or streaming assets.

Modern engines (Unreal, Frostbite, etc.) align asset data in their containers to
match GPU requirements, specifically to enable direct mapping and efficient
upload. D3D12MemAlloc enforces alignment for GPU resources, so matching
alignment in the PAK file allows the engine to avoid costly intermediate buffers
and double-copy operations.

While unaligned storage saves a small amount of disk space, the performance cost
at load time outweighs the benefit. Alignment is essential for future-proofing,
streaming, and efficient resource management.

**Summary:** PAK file alignment is designed to match GPU upload requirements
(especially for D3D12 and Vulkan), enabling direct mapping and zero-copy
streaming. This distinction is critical for high-performance asset loading and
future-proofing the format.

**Module boundary note:** This document explains the *format* constraints.
Actual upload planning, staging, command recording, and fence tracking are
Renderer responsibilities (see `src/Oxygen/Renderer/Upload/README.md`).

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
[ Browse Index (Optional) ]
    [ PakBrowseIndexHeader ]
    [ PakBrowseIndexEntry[] ]
    [ UTF-8 String Table (not null-terminated) ]
[ AssetDirectory[] ]
    [ AssetDirectoryEntry 0 ]
    [ AssetDirectoryEntry 1 ]
    ...
    [ AssetDirectoryEntry N-1 ]
[ PakFooter ]
```

## 🔎 5. Embedded Browse Index (Virtual Paths)

The `.pak` directory is keyed by `AssetKey` and is sufficient for runtime
loading, but it is not sufficient for editor browsing because it does not
contain canonical virtual paths.

To support editor and tooling workflows (scene explorer, content browser,
diagnostics), a pak file may embed a **Browse Index** that maps virtual paths
to `AssetKey`.

### Location

The browse index is stored as a contiguous blob anywhere in the file. Its
location is referenced from the pak footer fields:

- `PakFooter.browse_index_offset` (uint64, absolute)
- `PakFooter.browse_index_size`   (uint64, size in bytes)

If either value is `0`, the browse index is considered absent.

> Recommended placement: immediately before `PakFooter` to keep patching simple
> (write the blob, then write the footer with the final offset/size).

### Payload Format (v1)

All values are little-endian.

#### PakBrowseIndexHeader (24 bytes)

| Field | Type | Notes |
| --- | --- | --- |
| `magic` | 8 bytes | ASCII `OXPAKBIX` |
| `version` | uint32 | `1` |
| `entry_count` | uint32 | number of entries |
| `string_table_size` | uint32 | bytes |
| `reserved` | uint32 | must be 0 |

#### PakBrowseIndexEntry (24 bytes)

| Field | Type | Notes |
| --- | --- | --- |
| `asset_key` | 16 bytes | serialized `AssetKey` |
| `virtual_path_offset` | uint32 | offset into the string table |
| `virtual_path_length` | uint32 | length in bytes |

#### String Table

- Raw UTF-8 bytes.
- Strings are **not** null-terminated.
- Each entry references a slice via `(offset,length)`.

### Virtual Path Rules

- Must be canonical and start with `/`.
- Must be of the form `/{Mount}/{Path}` (e.g. `/Content/Textures/Wood.png`).
- Must not contain `\\`, `//`, `.` or `..` path segments.

### Requirements

- Runtime loading does not require the browse index.
- Editor browsing and any feature that needs **virtual-path enumeration**
  requires the browse index to be present.

## Loading Procedure

1. **Bootstrap Phase**: Load PakFooter and AssetDirectory to build asset lookup
   table
2. **Resource Resolution**: Load required Global Resource Tables based on asset
   types needed
3. **Asset Loading**: For each requested asset: a. Load asset descriptor using
   AssetKey -> directory entry -> descriptor mapping b. Parse embedded asset
   (AssetKey) and resource (ResourceIndexT) references within the descriptor c.
   Recursively resolve embedded AssetKey dependencies d. Resolve resource IDs to
   actual data offsets using resource tables e. Memory map or load required data
   blob regions

**Loading Strategy Benefits:**

- **Partial Loading**: Only load resource tables and data regions actually
  needed
- **Memory Mapping**: Entire resource regions can be memory mapped efficiently
- **Stable References**: Resource IDs remain valid regardless of loading order
- **Deduplication**: Multiple assets can reference the same resource ID
- **Platform Optimization**: Skip resource types not supported on target
  platform
