# Chunking / Loading

This document defines a binary `.pak` container format optimized for GPU asset
streaming, alignment, and fast loading. Inspired by Unreal Engine, Frostbite,
and modern GPU requirements.

---

## 🧱 1. PakHeader (Fixed Size, 64 bytes)

```cpp
struct PakHeader { // pack(1)
    char     magic[8];         // {'O','X','P','A','K',0,0,0}
    uint16_t version;          // Format version
    uint16_t content_version;  // Asset schema version
    uint8_t  reserved[52];     // Reserved for future use
};
```

---

## 🔚 2. PakFooter (Fixed Size, 64 bytes)

```cpp
struct PakFooter { // pack(1)
    uint64_t directory_offset;   // Offset to the asset directory
    uint64_t directory_size;     // Size of the asset directory
    uint64_t asset_count;        // Number of assets in the directory
    uint8_t  reserved[24];       // Reserved for future use
    uint64_t pak_hash;           // Hash of entire file for integrity
    char     footer_magic[8];    // { 'O', 'X', 'P', 'A', 'K', 'E', 'N', 'D' }
};
static_assert(sizeof(PakFooter) == 64);
```

The fields in PakFooter enable fast, robust, and redundant access to the asset
directory and file integrity validation:

- `directory_offset` and `directory_size`: Allow the loader to seek directly to
  the directory for fast asset lookup.
- `asset_count`: Number of assets in the directory, for direct iteration and
  validation.
- `pak_hash`: Integrity hash of the entire file (excluding the hash field
  itself).
- `footer_magic`: 8-byte sentinel string ("OXPAKEND") for validation and fast
  footer detection.
- `reserved`: Reserved for future use, must be zero.

All offsets and sizes are in bytes. The footer is always the last 64 bytes of
the file.

---

## 📂 3. AssetDirectory Entry (Aligned to 64 bytes)

Each entry describes one asset in the blob.

```cpp
struct AssetKey { // pack(1)
    uint8_t  guid[16];  // 128-bit GUID (raw bytes)
    uint32_t variant;   // Project-defined mask/flag (not interpreted by engine)
    uint8_t  version;   // Project-defined version (up to 256 versions)
    uint8_t  type;      // AssetType enum value (up to 256 types)
    uint16_t reserved = 0;  // Reserved for future use or alignment
};
static_assert(sizeof(AssetKey) == 24);

struct AssetDirectoryEntry { // pack(1)
    AssetKey key;                // 24 bytes: guid, variant, version, type, reserved
    uint64_t entry_offset = 0;   // Absolute offset of the directory entry
    uint64_t data_offset = 0;    // Absolute offset of the asset data blob
    uint32_t data_size = 0;      // Size of asset data
    uint32_t alignment = 256;    // Required alignment
    uint16_t dependency_count = 0; // Number of dependencies
    uint8_t  compression = 0;      // Compression method (0 = none)
    uint8_t  reserved0 = 0;        // Reserved for future use
    uint8_t  reserved[12] = {};    // Padding to 64 bytes
    // Followed by: AssetKey dependencies[dependency_count];
};
static_assert(sizeof(AssetDirectoryEntry) == 64);
```

- All asset lookups and dependencies use AssetKey.
- The 'variant' field is a 32-bit project-defined mask/flag, not interpreted by
  the engine, and not used for LODs.
- LODs are always built-in to geometry assets.
- Geometry = one or more LODs (indexed 0..N-1), each LOD is a Mesh, each Mesh is
  one or more MeshViews (sub-meshes).

### AssetDirectoryEntry Fields: `size` and `alignment`

The `AssetDirectoryEntry` structure includes two important fields: `size` and `alignment`. These fields are critical for correctly loading and using the asset data, especially in relation to GPU and platform requirements.

- `size`: The actual, unpadded size of the asset data in bytes. Used at runtime
  for reading, decompressing, or uploading the asset to the GPU. This is the
  number of bytes that are meaningful for the asset.
- `alignment`: The required alignment for the asset data, as dictated by the GPU
  or graphics API (e.g., D3D12/Vulkan). The asset builder sets this field at
  build time based on asset type and platform requirements. The loader can use
  this field to validate that the asset data is correctly aligned in memory
  before uploading to the GPU.

> **Note:** Both fields are determined at asset build time and are essential for correct and efficient loading and GPU resource creation. The
> loader uses `size` for data operations and `alignment` for validation and resource creation. Offsets are always absolute file offsets, so padding is only for alignment, not for sequential traversal.

---

## 📦 3. AssetDataBlob

- Raw binary data for all assets.
- Each asset is aligned to its required boundary (see GPU Alignment Guidelines below):
  - Textures: 256 bytes
  - Constant buffers: 256 bytes
  - Vertex buffers: 16 bytes
  - Index buffers: 4 bytes
- Assets are stored in GPU-native formats:
  - Textures: BCn, ASTC, etc.
  - Meshes: tightly packed vertex/index buffers
  - Shaders: precompiled bytecode (DXIL, SPIR-V)

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

## 🧠 GPU Alignment Guidelines

| Asset Type      | Alignment | Notes                          |
|------------------|-----------|--------------------------------|
| Constant Buffer  | 256 bytes | Required by D3D12/Vulkan       |
| Vertex Buffer    | 16 bytes  | For vec4 alignment             |
| Index Buffer     | 4 bytes   | Typically uint32_t             |
| Texture Data     | 256 bytes | For optimal copy granularity   |
| Shader Bytecode  | 16 bytes  | Optional, for cache line align |

---

## 📜 High-Level File Layout

The binary layout of a `.pak` file is as follows:

```text
[ PakHeader ]
[ AssetDataBlob ]
    [ Asset 0 Data (aligned, may include header, buffers, etc.) ]
    [ Asset 1 Data (aligned) ]
    ...
    [ Asset N-1 Data (aligned) ]
[ AssetDirectory[] ]
    [ AssetDirectoryEntry 0 ]
    [ AssetKey dependencies[0] (if any) ]
    [ AssetDirectoryEntry 1 ]
    [ AssetKey dependencies[1] (if any) ]
    ...
    [ AssetDirectoryEntry N-1 ]
    [ AssetKey dependencies[N-1] (if any) ]
[ PakFooter ]
```

- **PakHeader**: Fixed-size header at the start of the file. Contains magic,
  version, asset count, and directory location/size.
- **AssetDataBlob**: Contiguous region containing all asset data, each asset
  aligned to its required boundary. Each asset's data may include a
  type-specific header (e.g., MeshAssetHeader, GeometryAssetHeader, etc.),
  followed by buffers or sub-structures as defined by the asset type.
- **AssetDirectory[]**: Array of `AssetDirectoryEntry` structs, one per asset,
  followed by zero or more `AssetKey` dependencies for each entry (as specified
  by `dependencyCount`).
- **PakFooter**: Fixed-size footer at the end of the file, providing redundant
  directory location/size and integrity hash for fast random access and
  validation.

> **Note:**
>
> - All offsets in directory entries are relative to the start of the
>   AssetDataBlob region.
> - The AssetDataBlob and AssetDirectory[] regions are located using offsets in
>   the PakHeader (and optionally PakFooter).
> - Asset data is always aligned and padded as required by the `alignment` field
>   in each directory entry.
> - The loader must use the directory to locate and interpret each asset's data.

```cpp
#pragma once
#include <cstdint>
#include <array>
#include <vector>

// -----------------------------
// 📦 PakHeader (64 bytes)
// -----------------------------
#pragma pack(push, 1)
struct PakHeader {
    char     magic[8] = { 'O', 'X', 'P', 'A', 'K', 0, 0, 0 }; // File identifier
    uint16_t version = 1;                      // Format version
    uint16_t content_version = 0;              // Content version
    uint32_t reserved0 = 0;                    // Reserved for future use
    uint8_t  reserved[48] = {};                // Reserved for future use
};
#pragma pack(pop)
static_assert(sizeof(PakHeader) == 64);

// -----------------------------
// 📂 AssetDirectoryEntry (64 bytes + optional dependencies)
// -----------------------------
#pragma pack(push, 1)
struct AssetDirectoryEntry {
    AssetKey key;                // 24 bytes: guid, variant, version, type, reserved
    uint64_t entry_offset = 0;   // Absolute offset of the directory entry
    uint64_t data_offset = 0;    // Absolute offset of the asset data blob
    uint32_t data_size = 0;      // Size of asset data
    uint32_t alignment = 256;    // Required alignment
    uint16_t dependency_count = 0; // Number of dependencies
    uint8_t  compression = 0;      // Compression method (0 = none)
    uint8_t  reserved0 = 0;        // Reserved for future use
    uint8_t  reserved[12] = {};    // Padding to 64 bytes

    // Followed by: AssetKey dependencies[dependency_count];
};
#pragma pack(pop)
static_assert(sizeof(AssetDirectoryEntry) == 64);

// -----------------------------
// 📦 PakFooter (64 bytes)
// -----------------------------
#pragma pack(push, 1)
struct PakFooter {
    uint64_t directory_offset = 0;
    uint64_t directory_size = 0;
    uint64_t asset_count = 0;             // Number of assets in the directory
    uint8_t  reserved[24] = {};          // Reserved for future use
    uint64_t pak_hash = 0;               // Optional integrity hash
    char     footer_magic[8] = { 'O', 'X', 'P', 'A', 'K', 'E', 'N', 'D' };
    uint8_t  reserved[32] = {};          // Reserved for future use
};
#pragma pack(pop)
static_assert(sizeof(PakFooter) == 64);

// -----------------------------
// 🧱 MeshAssetHeader (example asset layout)
// -----------------------------
struct alignas(64) MeshAssetHeader {
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t vertexStride = 0;
    uint32_t indexStride = 0;
    uint32_t vertexBufferSize = 0;
    uint32_t indexBufferSize = 0;
    float    boundingBoxMin[3] = {};
    float    boundingBoxMax[3] = {};
    uint8_t  reserved[16] = {}; // Padding to 64 bytes
    // Followed by: vertex buffer, index buffer, optional metadata
};

// -----------------------------
// 🧩 GeometryAssetHeader (64 bytes + LOD table)
// -----------------------------
struct alignas(64) GeometryAssetHeader {
    uint32_t lodCount = 0;                // Number of LODs (must be >= 1)
    uint32_t reserved0 = 0;
    float    boundingBoxMin[3] = {};
    float    boundingBoxMax[3] = {};
    uint8_t  reserved[40] = {};           // Padding to 64 bytes
    // Followed by: AssetKey lodKeys[lodCount];
    // Optionally: geometry-level metadata
};

// -----------------------------
// 🧱 MeshAssetHeader (64 bytes + MeshView table)
// -----------------------------
struct alignas(64) MeshAssetHeader {
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t vertexStride = 0;
    uint32_t indexStride = 0;
    uint32_t vertexBufferSize = 0;
    uint32_t indexBufferSize = 0;
    uint32_t meshViewCount = 0;           // Number of MeshViews (sub-meshes)
    uint32_t reserved0 = 0;
    float    boundingBoxMin[3] = {};
    float    boundingBoxMax[3] = {};
    uint8_t  reserved[8] = {};            // Padding to 64 bytes
    // Followed by: MeshViewDesc meshViews[meshViewCount];
    //              vertex buffer, index buffer, optional metadata
};

// -----------------------------
// 🧩 MeshViewDesc (32 bytes)
// -----------------------------
struct alignas(16) MeshViewDesc {
    uint32_t firstIndex = 0;              // Start index in index buffer
    uint32_t indexCount = 0;              // Number of indices
    uint32_t firstVertex = 0;             // Start vertex in vertex buffer
    uint32_t vertexCount = 0;             // Number of vertices
    uint32_t materialIndex = 0;           // Material slot or index
    uint32_t reserved[3] = {};
    // Optionally: name hash or string offset
};

// -----------------------------
// 🖼️ TextureAssetHeader (64 bytes)
// -----------------------------
struct alignas(64) TextureAssetHeader {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipCount = 1;
    uint32_t arrayLayers = 1;
    uint32_t format = 0;             // Enum or DXGI/VK format code
    uint32_t imageSize = 0;          // Total size in bytes
    uint32_t alignment = 256;        // Required alignment
    uint8_t  isCubemap = 0;
    uint8_t  reserved[35] = {};      // Padding to 64 bytes
    // Followed by: image data (GPU-native format, e.g., BCn, ASTC)
};

// -----------------------------
// 📝 ShaderAssetHeader (64 bytes)
// -----------------------------
struct alignas(64) ShaderAssetHeader {
    uint32_t stage = 0;              // Enum: Vertex, Fragment, etc.
    uint32_t codeSize = 0;           // Size of bytecode in bytes
    uint32_t entryPointLength = 0;   // Length of entry point string
    uint32_t reserved0 = 0;
    uint8_t  reserved[48] = {};
    // Followed by: entry point string, then bytecode
};

// -----------------------------
// 🎨 MaterialAssetHeader (64 bytes)
// -----------------------------
struct alignas(64) MaterialAssetHeader {
    uint64_t shaderAssetID = 0;      // Reference to Shader asset
    uint32_t textureCount = 0;       // Number of bound textures
    uint32_t paramCount = 0;         // Number of float/vector params
    uint8_t  reserved[48] = {};
    // Followed by: array of texture asset IDs (uint64_t[textureCount])
    //              array of parameter values (float[paramCount] or struct)
};

// -----------------------------
// 🔊 AudioAssetHeader (64 bytes)
// -----------------------------
struct alignas(64) AudioAssetHeader {
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    uint32_t bitsPerSample = 0;
    uint32_t dataSize = 0;           // Size of audio data in bytes
    uint32_t format = 0;             // Enum: PCM, Vorbis, etc.
    uint8_t  reserved[44] = {};
    // Followed by: audio data (compressed or raw)
};
