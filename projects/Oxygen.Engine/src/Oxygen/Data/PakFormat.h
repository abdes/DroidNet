//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/Unorm16.h>

//! Oxygen PAK file binary format specification
/**!
 @file PakFormat.h

 ### Invariants
 - All structures are packed with `#pragma pack(push,1)` eliminating implicit
   padding in the serialized on-disk representation. The engine may copy
   these packed structs into runtime-aligned representations before use.
 - All *file* offsets are absolute from the start of the PAK file (no relative
   offsets) and are of type `OffsetT` (uint64_t).
 - Offsets into embedded tables/blobs (e.g., string tables) are relative to the
   start of the containing blob and use the smallest appropriate integer type.
 - Endianness is little-endian (Intel / x86-64). Cross-platform loaders on
   big-endian architectures MUST byte-swap scalar fields.
 - All sizes are in bytes.
 - All strings are null-terminated.
 - All names are null-terminated strings, with a fixed-size of `kMaxNameSize`
   (including the null terminator) and padded with null bytes.
 - All indices are 0-based. Except when explicitly stated otherwise, `0` is a
   valid index.
 - Resource index value `0` is reserved: if a resource category defines a
   fallback, index `0` refers to that fallback resource. In that case, packers
   MUST populate index `0` with the fallback asset (not a hole). For textures,
   this means the texture resource table entry `0` always exists and points to
   the fallback texture.
   If a category has no fallback concept, `0` denotes an absent / not-assigned
   reference (see `kFallbackResourceIndex` and `kNoResourceIndex`).
 - All hashes for content integrity are 32-bit CRC32 values for corruption
   detection and performance (standard IEEE polynomial 0x04C11DB7, initial
   value 0xFFFFFFFF, reflected input/output, final XOR 0xFFFFFFFF).
*/

// TODO: Define constants for hash algorithm enumeration

namespace oxygen::data::pak::v2 {

//=== Type Aliases ===--------------------------------------------------------//

//! Offset type for file positions (8 bytes)
using OffsetT = uint64_t;

//! Resource index type (4 bytes)
using ResourceIndexT = uint32_t;

//! Data blob size type (4 bytes)
using DataBlobSizeT = uint32_t;

//! Offset type for slices into embedded string tables (4 bytes)
using StringTableOffsetT = uint32_t;

//! Size type for slices into embedded string tables (4 bytes)
using StringTableSizeT = uint32_t;

//! 16-bit unsigned normalized scalar.

//=== Constants ===-----------------------------------------------------------//

//! Maximum asset name length including null terminator
constexpr size_t kMaxNameSize = 64;

//! Resource index indicating explicit fallback to default resource.
//! When a resource *type* defines an engine/tool-provided fallback asset,
//! references using this value (0) resolve to that fallback.
//!
//! Contract: When a fallback exists, packers MUST store the fallback asset at
//! index 0 of the corresponding resource table. For textures, index 0 must be
//! populated with the fallback texture.
constexpr ResourceIndexT kFallbackResourceIndex = 0;

//! Resource index indicating "no resource assigned" for types that have no
//! concept of fallback. For such types *both* constants compare equal; the
//! semantic difference depends on the resource category's rules. Tooling
//! should still emit 0 but may label it appropriately in diagnostics.
constexpr ResourceIndexT kNoResourceIndex = 0;

//! Material flag indicating that textures must not be sampled.
//!
//! When set, the renderer/shaders must ignore all texture references for the
//! material and use scalar fallbacks only.
//!
//! This flag exists because texture resource index `0` is reserved for the
//! fallback texture when a fallback exists (textures do). Therefore, a texture
//! index of `0` cannot unambiguously mean "no texture" for materials.
constexpr uint32_t kMaterialFlag_NoTextureSampling = (1u << 0);

//! Material flag indicating that the material should be treated as
//! double-sided.
//!
//! When set, the renderer should disable backface culling for this material.
constexpr uint32_t kMaterialFlag_DoubleSided = (1u << 1);

//! Material flag indicating that the material uses alpha testing (cutout).
//!
//! When set, the renderer/shaders should apply alpha cutoff testing using the
//! material's `alpha_cutoff` parameter.
constexpr uint32_t kMaterialFlag_AlphaTest = (1u << 2);

//! Material flag indicating that the material is unlit.
//!
//! When set, shading should not apply lighting and should render using
//! base color + emissive only.
constexpr uint32_t kMaterialFlag_Unlit = (1u << 3);

//! Material flag indicating glTF ORM channel packing semantics.
//!
//! When set, the metallic/roughness texture(s) follow glTF conventions:
//! - Roughness is sampled from the G channel
//! - Metalness is sampled from the B channel
//! Ambient occlusion is typically sampled from the R channel of the AO/ORM
//! texture.
constexpr uint32_t kMaterialFlag_GltfOrmPacked = (1u << 4);

//! Maximum size for data blobs in bytes
constexpr DataBlobSizeT kDataBlobMaxSize
  = (std::numeric_limits<uint32_t>::max)();

//=== PAK File Format Structures ===------------------------------------------//

//! Fixed-size header at the start of the PAK file (256 bytes)
/*!
 The header contains metadata about the PAK file format version, content
 version, and reserved space for future extensions. It is always located at the
 very beginning of the PAK file.
*/
#pragma pack(push, 1)
struct PakHeader {
  char magic[8] = { 'O', 'X', 'P', 'A', 'K', 0, 0, 0 };
  uint16_t version = 2; // Format version
  uint16_t content_version = 0; // Content version
  uint8_t guid[16] = {}; // Unique identifier for this PAK
  // Reserved for future use
  uint8_t reserved[228] = {};
};
#pragma pack(pop)
static_assert(sizeof(PakHeader) == 256);

//! Resource region offset/size descriptor (16 bytes).
/*!
 A regions is a contiguous block of data within the PAK file, containing blobs
 of a specific type (e.g., textures, buffers, audio). All resource data blobs
 within a region are aligned to their required boundaries for direct memory
 mapping, and padded with null bytes as needed.

 @see ResourceTable, PakFooter
*/
#pragma pack(push, 1)
struct ResourceRegion {
  uint64_t offset = 0; // Absolute offset from start of PAK file
  uint64_t size = 0; // Size of the region in bytes
};
#pragma pack(pop)
static_assert(sizeof(ResourceRegion) == 16);

//! Resource table (16 bytes).
/*!
 Resource tables connect resource IDs (ResourceIndexT) to **absolute** offsets
 (that should be within the corresponding typed region's [offset, offset+size).
 This indirection enables stable references while preserving memory mapping
 efficiency for the regions.

 @note All resource tables are indexed with a `ResourceIndexT` index, with `0`
 reserved for the fallback resource when a fallback exists. In that case,
 packers MUST populate index `0` with the fallback entry. When no fallback
 resource is logically possible, `0` means absent/invalid.

 @see ResourceRegion, PakFooter
*/
#pragma pack(push, 1)
struct ResourceTable {
  uint64_t offset = 0; // Absolute offset from start of PAK file
  uint32_t count = 0; // Number of entries in table
  uint32_t entry_size = 0; // Size of each entry in bytes
};
#pragma pack(pop)
static_assert(sizeof(ResourceTable) == 16);

//! Fixed-size footer at the end of the PAK file.
/*!
 Provides fast access to the asset directory, resource regions, and tables.
 May contain a non-zero integrity hash for the PAK file that can be used to
 check for corruption or tampering.

 The footer is always located at the very end of the PAK file, and has a
 fixed size. This allows immediate access to the asset directory and resource
 tables without parsing the entire file.

 @see PakHeader, ResourceRegion, ResourceTable
*/
#pragma pack(push, 1)
struct PakFooter {
  uint64_t directory_offset = 0; // Absolute offset to asset directory
  uint64_t directory_size = 0; // Size of asset directory in bytes
  uint64_t asset_count = 0; // Number of entries in the directory

  // -- Resource data regions --
  ResourceRegion texture_region = {};
  ResourceRegion buffer_region = {};
  ResourceRegion audio_region = {};

  // -- Resource tables --
  ResourceTable texture_table = {};
  ResourceTable buffer_table = {};
  ResourceTable audio_table = {};

  // -- Embedded Browse Index (Optional) --
  //
  // When non-zero, these fields describe the location of an embedded browse
  // index (OXPAKBIX) used by editor/tooling for virtual-path enumeration.
  // Runtime loading does not require this index.
  OffsetT browse_index_offset = 0;
  uint64_t browse_index_size = 0;

  // Reserved for future use
  uint8_t reserved[108] = {};

  // -- CRC32 Integrity --
  // CRC32 covers the *entire* file, including the footer and footer magic
  // bytes, EXCEPT these 4 bytes (`pak_crc32`) which are treated as zero /
  // skipped during calculation.
  // Standard IEEE CRC32 parameters: polynomial 0x04C11DB7, initial value
  // 0xFFFFFFFF, reflect in/out, final XOR 0xFFFFFFFF.
  // A value of 0 indicates that integrity validation SHOULD be skipped.
  uint32_t pak_crc32 = 0; // CRC32 integrity hash (excluded from calculation)

  // The last thing in the PAK file is the footer magic bytes.
  char footer_magic[8] = { 'O', 'X', 'P', 'A', 'K', 'E', 'N', 'D' };
};
#pragma pack(pop)
static_assert(sizeof(PakFooter) == 256);

//! Asset directory entry (64 bytes).
/*!
 The directory is an array of `AssetDirectoryEntry` structs, one for each asset
 in the PAK file. It is located at the offset specified in the PakFooter.

 Each entry in the asset directory corresponds to a single asset descriptor. It
 contains the asset key, type, and absolute offsets to the entry and descriptor
 data. This allows for fast loading of assets by their key.
*/
#pragma pack(push, 1)
struct AssetDirectoryEntry {
  AssetKey asset_key;
  uint8_t asset_type; // AssetType enum - for loader dispatch
  OffsetT entry_offset = 0; // Absolute offset of *this* directory entry
  OffsetT desc_offset = 0; // Absolute offset of the asset descriptor
  uint32_t desc_size = 0; // Size of asset descriptor (for sanity check)

  // Reserved for future use
  uint8_t reserved[27] = {}; // Padding to 64 bytes
};
#pragma pack(pop)
static_assert(sizeof(AssetDirectoryEntry) == 64);

//! Embedded browse index header (24 bytes).
/*!
 Provides a mapping from canonical virtual paths to AssetKeys for editor and
 tooling use. The browse index is not required for runtime loading.

 The browse index payload is stored as a contiguous blob at
 `browse_index_offset` with length `browse_index_size` and is referenced from
 the PakFooter browse index fields.

 @note Virtual paths are UTF-8 bytes and are not null-terminated in the string
 table.
 @see PakFooter
*/
#pragma pack(push, 1)
struct PakBrowseIndexHeader {
  char magic[8] = { 'O', 'X', 'P', 'A', 'K', 'B', 'I', 'X' };
  uint32_t version = 1;
  uint32_t entry_count = 0;
  StringTableSizeT string_table_size = 0;
  uint32_t reserved = 0;
};
#pragma pack(pop)
static_assert(sizeof(PakBrowseIndexHeader) == 24);

//! Embedded browse index entry (24 bytes).
/*!
 Each entry maps one AssetKey to a virtual path, stored as a slice in the
 string table.
*/
#pragma pack(push, 1)
struct PakBrowseIndexEntry {
  AssetKey asset_key;
  StringTableOffsetT virtual_path_offset = 0;
  StringTableSizeT virtual_path_length = 0;
};
#pragma pack(pop)
static_assert(sizeof(PakBrowseIndexEntry) == 24);

// -----------------------------------------------------------------------------
// Resource Descriptors
// -----------------------------------------------------------------------------

//! Texture resource table entry (32 bytes)
/*!
  @note Texture `format` must be one of the core type `Format` enum values.
  @note Textures are always aligned to 256 bytes.
*/
#pragma pack(push, 1)
struct TextureResourceDesc {
  OffsetT data_offset; // Absolute offset to texture data
  DataBlobSizeT size_bytes; // Size of texture data
  uint8_t texture_type; // 2D, 3D, Cube, etc. (enum) (defined externally)
  uint8_t compression_type; // Compression (BC1, BC3, ASTC, etc.) (external)
  uint32_t width; // Texture width
  uint32_t height; // Texture height
  uint16_t depth; // For 3D textures (volume), otherwise 1
  uint16_t array_layers; // For array textures/cubemap arrays, otherwise 1
  uint16_t mip_levels; // Number of mip levels
  uint8_t format; // Texture format enum
  uint16_t alignment; // 256 for textures

  // Reserved for future use
  uint8_t reserved[9] = {};
};
#pragma pack(pop)
static_assert(sizeof(TextureResourceDesc) == 40);

//! Buffer resource table entry (32 bytes)
/*!
  Describes a buffer resource in the asset pak. Buffer data can be raw bytes,
  typed with a specific format, or structured with a specific element stride.

  - Raw buffers correspond to `element_format` of `0` and a stride of `1`.
  - Typed buffers have a non-zero `element_format`; in which case, the format
    specifies the size of each element in the buffer and `element_stride` is
    ignored (should be `0` for safety).
  - Structured buffers have their `element_format` set to `0` and
    `element_stride` set to the size of each element in bytes (>1).

  @note Buffer `element_format` must be one of the core type `Format` enum
  values.

  @note Buffers are always aligned to their `element_stride`, with `1`
  indicating a raw buffer of bytes. `0` is invalid/unset.

  @note Buffer `usage_flags` is a bitfield providing hints to the engine or
  graphics API. The constants below define the possible flags:

    ```cpp
    // --- Buffer Role Flags (can be combined) ---
    0x01 : VertexBuffer      (vertex input source)
    0x02 : IndexBuffer       (index input source)
    0x04 : ConstantBuffer    (shader constants/uniforms)
    0x08 : StorageBuffer     (read/write in shaders)
    0x10 : IndirectBuffer    (indirect draw/dispatch arguments)

    // --- CPU Access Flags (can be combined) ---
    0x20 : CPUWritable       (CPU can write to buffer)
    0x40 : CPUReadable       (CPU can read from buffer)

    // --- Update Frequency Flags (mutually exclusive) ---
    // Only one of these should be set; if none, default to Static.
    0x80 : Dynamic           (frequently updated)
    0x100: Static            (rarely updated)
    0x200: Immutable         (never updated after creation)
    ```

  @note The `reserved` field is for future expansion and must be
  zero-initialized.
*/
#pragma pack(push, 1)
struct BufferResourceDesc {
  OffsetT data_offset = 0; //!< Absolute offset to buffer data in the pak
  DataBlobSizeT size_bytes = 0; //!< Size of buffer data in bytes
  uint32_t usage_flags = 0; //!< Usage hints (see above)
  uint32_t element_stride = 0; //!< 1 for raw buffers, 0 when unused
  uint8_t element_format = 0; //!< Format enum value (0 = raw or structured)
  uint8_t reserved[11] = {}; //!< Reserved for future use (must be zero)
};
#pragma pack(pop)
static_assert(sizeof(BufferResourceDesc) == 32);

//! Audio resource table entry (32 bytes).
#pragma pack(push, 1)
struct AudioResourceDesc {
  OffsetT data_offset; // Absolute offset to audio data
  DataBlobSizeT size_bytes; // Size of audio data
  uint32_t sample_rate; // Audio sample rate
  uint32_t channels; // Number of channels
  uint32_t audio_format; // PCM, Vorbis, etc.
  uint16_t bits_per_sample; // Bits per sample
  uint16_t alignment; // Required alignment

  // Reserved for future use
  uint8_t reserved[4];
};
#pragma pack(pop)
static_assert(sizeof(AudioResourceDesc) == 32);

//! Shader descriptor (424 bytes)
/*!
  Describes a shader stage for material or pipeline binding. Does not contain
  bytecode; only metadata and lookup information.

  - `shader_type`: Shader stage (ShaderType enum value).
  - `source_path`: Canonical repo-relative shader source path (forward slashes,
    normalized, no absolute paths).
  - `entry_point`: Explicit entry point name.
  - `defines`: Canonical defines string for compilation (sorted, unique names).
  - `shader_hash`: 64-bit hash of shader source for validation.
*/
#pragma pack(push, 1)
struct ShaderReferenceDesc {
  uint8_t shader_type = 0; // ShaderType enum value
  uint8_t reserved0[7] = {};

  char source_path[120] = {}; // Null-terminated, null-padded
  char entry_point[32] = {}; // Null-terminated, null-padded
  char defines[256] = {}; // Null-terminated, null-padded (may be empty)

  uint64_t shader_hash = 0; // Hash of source for validation
};
#pragma pack(pop)
static_assert(sizeof(ShaderReferenceDesc) == 424);

// -----------------------------------------------------------------------------
// Assets
// -----------------------------------------------------------------------------

//! Asset header - Per-Asset Metadata (95 bytes on disk, packed).
/*!
 Always the first field in every asset descriptor. Contains metadata about the
 asset, such as its type, name, version, streaming priority, content hash, and
 variant flags.

 ### Notes

 - The `streaming_priority` field is used to determine the order in which
   assets should be loaded. Lower values indicate higher priority.
 - The `asset_type` field is redundant with the directory entry, but is
   necessary for debugging and sanity checks.
 - The `content_hash` field is used to verify the integrity of the asset data.
 - The `variant_flags` field is a project-defined bitfield that can be used to
   store additional metadata about the asset, such as its intended use or
   compatibility with specific features.
*/
#pragma pack(push, 1)
struct AssetHeader {
  uint8_t asset_type = 0; // Redundant with directory for debugging
  char name[kMaxNameSize] = {}; // Asset name for debugging/tools (64 bytes)
  uint8_t version = 0; // Asset format version (up to 256 versions)
  uint8_t streaming_priority = 0; // Loading priority: 0=highest, 255=lowest
  uint64_t content_hash = 0; // Content integrity hash
  uint32_t variant_flags = 0; // Project-defined (not interpreted by engine)

  // Reserved for future use
  uint8_t reserved[16] = {};
};
#pragma pack(pop)
static_assert(sizeof(AssetHeader) == 95);

//=== Material Asset ===------------------------------------------------------//

//! Material asset descriptor (256 bytes)
/*!
  Describes a material asset for physically-based rendering (PBR) and other
  shading models. This structure encodes all core material properties, texture
  references, and metadata required for rendering and asset management.

  ### Attached Textures

  Each material texture is referenced by an index into the texture resource
  table. The fields `base_color_texture`, `normal_texture`, `metallic_texture`,
  `roughness_texture`, and `ambient_occlusion_texture` map to the main PBR
  slots. `reserved_textures` supports future or custom slots.

  Texture index semantics depend on category rules:
  - For textures (which define a fallback), index `0` refers to the fallback
    texture (`kFallbackResourceIndex`). Packers MUST populate texture table
    entry `0` with the fallback texture.
  - To explicitly disable texture sampling for a material (use scalar fallbacks
    such as `base_color`), set the material flag
  `kMaterialFlag_NoTextureSampling`.

  For non-texture categories that do not define a fallback concept, `0`
  (`kNoResourceIndex`) denotes an absent / not-assigned reference.

  ### Field Details

  - `material_domain`: Specifies the intended rendering domain or pipeline for
    the material. Common values include:
      - Opaque: Standard surface, fully opaque, rendered in the main pass.
      - AlphaBlended: Transparent or semi-transparent, rendered in a separate
        pass with blending.
      - Masked: Uses alpha test/cutout for hard-edged transparency (e.g.,
        foliage).
      - Decal: Used for projected or mesh decals.
      - UserInterface: For user interface elements.
      - PostProcess: For post-processing effects. This field enables the engine
        to select the correct rendering path, culling, and sorting behavior.

  - `flags`: Bitfield encoding material options and features. Typical bits
    include:
      - Double-sided: Disables backface culling for thin geometry (e.g., leaves,
        cloth).
      - AlphaTest: Enables alpha cutout (masking) for hard transparency.
      - ReceivesShadows: Controls whether the material receives dynamic shadows.
      - CastsShadows: Controls whether the material casts shadows.
      - Unlit: Disables lighting, used for UI, effects, or emissive-only
        materials.
      - Wireframe: Renders geometry as wireframe (debug/visualization).
      - Custom: Reserved bits for project- or engine-specific features. The
        exact bit layout should be defined in a shared header or documentation
        for consistency.

  - `shader_stages`: Bitfield indicating which shader stages are used. Each set
    bit corresponds to a stage in the graphics or compute pipeline (see
    `ShaderType` core type). For each set bit, an index into the
    ShaderResourceTable follows this struct, in stage order. This enables
    flexible shader binding and future extension without breaking binary
    compatibility.

  @see AssetHeader, TextureResourceDesc, ShaderResourceDesc, ShaderType
*/
#pragma pack(push, 1)
struct MaterialAssetDesc {
  AssetHeader header;
  uint8_t material_domain; // e.g. Opaque, AlphaBlended
  uint32_t flags; // Bitfield for double-sided, alpha test, etc.
  uint32_t shader_stages; // Bitfield for shaders used; entries that follow
                          // are in ascending bit index order (LSB->MSB)

  // --- Scalar factors (PBR) ---
  float base_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f }; // RGBA fallback
  float normal_scale = 1.0f;
  Unorm16 metalness = Unorm16 { 0.0f };
  Unorm16 roughness = Unorm16 { 1.0f };
  Unorm16 ambient_occlusion = Unorm16 { 1.0f };

  // --- Core texture references (Index into TextureResourceTable,
  // kNoResourceIndex = invalid/none) ---
  ResourceIndexT base_color_texture = kNoResourceIndex;
  ResourceIndexT normal_texture = kNoResourceIndex;
  ResourceIndexT metallic_texture = kNoResourceIndex;
  ResourceIndexT roughness_texture = kNoResourceIndex;
  ResourceIndexT ambient_occlusion_texture = kNoResourceIndex;

  static_assert(kNoResourceIndex == 0);

  // --- Additional texture references (optional, Tier 1/2) ---
  ResourceIndexT emissive_texture = kNoResourceIndex;
  ResourceIndexT specular_texture = kNoResourceIndex;
  ResourceIndexT sheen_color_texture = kNoResourceIndex;
  ResourceIndexT clearcoat_texture = kNoResourceIndex;
  ResourceIndexT clearcoat_normal_texture = kNoResourceIndex;
  ResourceIndexT transmission_texture = kNoResourceIndex;
  ResourceIndexT thickness_texture = kNoResourceIndex;

  // --- Additional scalar parameters (Tier 1/2) ---
  // Emissive
  HalfFloat emissive_factor[3]
    = { HalfFloat { 0.0f }, HalfFloat { 0.0f }, HalfFloat { 0.0f } };
  // Alpha
  Unorm16 alpha_cutoff = Unorm16 { 0.5f };
  // Dielectric response
  float ior = 1.5f;
  Unorm16 specular_factor = Unorm16 { 1.0f };
  // Sheen (KHR_materials_sheen)
  HalfFloat sheen_color_factor[3]
    = { HalfFloat { 0.0f }, HalfFloat { 0.0f }, HalfFloat { 0.0f } };
  // Clearcoat (KHR_materials_clearcoat)
  Unorm16 clearcoat_factor = Unorm16 { 0.0f };
  Unorm16 clearcoat_roughness = Unorm16 { 0.0f };
  // Transmission / Volume (KHR_materials_transmission + KHR_materials_volume)
  Unorm16 transmission_factor = Unorm16 { 0.0f };
  Unorm16 thickness_factor = Unorm16 { 0.0f };
  HalfFloat attenuation_color[3]
    = { HalfFloat { 1.0f }, HalfFloat { 1.0f }, HalfFloat { 1.0f } };
  float attenuation_distance = 0.0f;

  uint8_t reserved[40] = {};
};
// Followed by:
// - Array of ShaderReferenceDesc entries in ascending set-bit order of
//   `shader_stages` (least-significant set bit first). Count is population
//   count of `shader_stages`.
#pragma pack(pop)
static_assert(sizeof(MaterialAssetDesc) == 256);

//=== Geometry Asset ===------------------------------------------------------//

//! Geometry asset descriptor (256 bytes)
/*!
  Describes a geometry asset, with one or more levels of detail (LODs) for
  efficient rendering. This structure provides the metadata and bounding
  information for the geometry, and is followed by an array of MeshDesc
  structures (one per LOD).

  ### Relationships

  - 1 GeometryAssetDesc : N MeshDesc (LODs)
  - 1 MeshDesc : N SubMeshDesc (submeshes)
  - 1 SubMeshDesc : N MeshViewDesc (mesh views)
  - 1 SubMeshDesc : 1 MaterialAsset (by AssetKey)

  ### Notes

  - `lod_count`: Number of LODs (must be >= 1). Each LOD is described by a
    MeshDesc.
  - `bounding_box_min`, `bounding_box_max`: Axis-aligned bounding box (AABB) for
    the entire geometry, used for culling and spatial queries.

  @see MeshDesc, SubMeshDesc, MeshViewDesc, AssetHeader
*/
#pragma pack(push, 1)
struct GeometryAssetDesc {
  AssetHeader header;
  uint32_t lod_count = 0; // Number of LODs (must be >= 1)
  float bounding_box_min[3] = {}; // AABB min coordinates
  float bounding_box_max[3] = {}; // AABB max coordinates

  // Reserved for future use
  uint8_t reserved[133] = {};
};
// Followed by: MeshDesc meshes[lod_count];
#pragma pack(pop)
static_assert(sizeof(GeometryAssetDesc) == 256);

//! Fields for a standard (static) mesh
#pragma pack(push, 1)
struct StandardMeshInfo {
  ResourceIndexT vertex_buffer = 0; //!< Reference to vertex buffer
  ResourceIndexT index_buffer = 0; //!< Reference to index buffer
  float bounding_box_min[3] = {}; //!< AABB min coordinates
  float bounding_box_max[3] = {}; //!< AABB max coordinates
};
#pragma pack(pop)
static_assert(sizeof(StandardMeshInfo) == 32);

//! Fields for a procedural mesh
#pragma pack(push, 1)
struct ProceduralMeshInfo {
  uint32_t params_size = 0; //!< Size of procedural parameter blob (bytes)
};
#pragma pack(pop)
static_assert(sizeof(ProceduralMeshInfo) == 4);

//! Mesh descriptor (104 bytes + SubMesh table)
/*!
  Describes a single mesh LOD within a geometry asset. Each MeshDesc contains
  references to vertex and index buffers, a list of submeshes, and bounding
  information for the mesh.

  ### Relationships

  - 1 MeshDesc : N SubMeshDesc (submeshes)
  - 1 MeshDesc : 1 vertex buffer, 1 index buffer (by ResourceIndexT)
  - MeshDesc are grouped under GeometryAssetDesc

  ### Notes

  - `submesh_count`: Number of SubMeshDesc structures following this mesh.
  - `mesh_view_count`: Total number of MeshViewDesc structures in all submeshes.
  - `bounding_box_min`, `bounding_box_max`: AABB for the mesh.

  @see SubMeshDesc, MeshViewDesc, GeometryAssetDesc
*/
#pragma pack(push, 1)
struct MeshDesc {
  char name[kMaxNameSize] = {};
  uint8_t mesh_type = 0; // MeshType enum value
  uint32_t submesh_count = 0; // Number of SubMeshes
  uint32_t mesh_view_count = 0; // Total number of MeshViews (all SubMeshes)
  union {
    //! Static Mesh. All info is self-contained in this structure.
    StandardMeshInfo standard;
    //! Procedural Mesh. Parameters blob follow the MeshDesc immediately. Mesh
    //! name is used to identify the procedural mesh type, and should be in the
    //! format: `Generator/MeshName`, where `Generator` is a known procedural
    //! mesh generator type (e.g., `Terrain`, `Plane`, `Sphere`, etc.)
    //! understandable or resolvable by the geometry loader.
    ProceduralMeshInfo procedural;
  } info {};

#define OXYGEN_MESH_IS(NAME, ENUM)                                             \
  [[nodiscard]] constexpr bool Is##NAME() const                                \
  {                                                                            \
    return static_cast<std::underlying_type_t<MeshType>>(MeshType::ENUM)       \
      == mesh_type;                                                            \
  }

  OXYGEN_MESH_IS(Standard, kStandard)
  OXYGEN_MESH_IS(Procedural, kProcedural)
  OXYGEN_MESH_IS(Skinned, kSkinned)
  OXYGEN_MESH_IS(MorphTarget, kMorphTarget)
  OXYGEN_MESH_IS(Instanced, kInstanced)
  OXYGEN_MESH_IS(Collision, kCollision)
  OXYGEN_MESH_IS(Navigation, kNavigation)
  OXYGEN_MESH_IS(Billboard, kBillboard)
  OXYGEN_MESH_IS(Voxel, kVoxel)

#undef OXYGEN_MESH_IS
};
// Followed by:
// - Optional blob of data depending on `mesh_type`. Blob size is specified by
//   the MeshInfo structure.
// - SubMeshDesc submeshes[submesh_count];
#pragma pack(pop)
static_assert(sizeof(MeshDesc) == 105);

//! Sub-mesh descriptor (108 bytes + MeshView table)
/*!
  Describes a logical partition of a mesh, typically corresponding to a region
  rendered with a single material. Each SubMeshDesc references a material asset
  and contains a list of mesh views (geometry ranges).

  ### Relationships
  - 1 SubMeshDesc : N MeshViewDesc (mesh views)
  - 1 SubMeshDesc : 1 MaterialAsset (by AssetKey)
  - SubMeshDesc are grouped under MeshDesc

  ### Notes
  - `mesh_view_count`: Number of MeshViewDesc structures following this submesh.
  - `bounding_box_min`, `bounding_box_max`: AABB for the submesh.

  @see MeshDesc, MeshViewDesc, MaterialAssetDesc
*/
#pragma pack(push, 1)
struct SubMeshDesc {
  char name[kMaxNameSize] = {};
  AssetKey material_asset_key; // AssetKey reference to MaterialAsset
  uint32_t mesh_view_count = 0; // Number of MeshViews in this SubMesh
  float bounding_box_min[3] = {}; // AABB min coordinates
  float bounding_box_max[3] = {}; // AABB max coordinates
};
// Followed by: MeshViewDesc mesh_views[mesh_view_count]
#pragma pack(pop)
static_assert(sizeof(SubMeshDesc) == 108);

//! Mesh view descriptor (16 bytes)
/*!
  Describes a contiguous range of indices and vertices within a mesh, used for
  rendering a portion of geometry (e.g., a primitive group or section).

  ### Relationships

  - 1 MeshViewDesc : 1 range in index buffer, 1 range in vertex buffer
  - MeshViewDesc are grouped under SubMeshDesc

  @see MeshDesc, SubMeshDesc
*/
#pragma pack(push, 1)
struct MeshViewDesc {
  //! Buffer index type for mesh views (4 bytes)
  using BufferIndexT = DataBlobSizeT;

  BufferIndexT first_index = 0; // Start index in index buffer
  BufferIndexT index_count = 0; // Number of indices
  BufferIndexT first_vertex = 0; // Start vertex in vertex buffer
  BufferIndexT vertex_count = 0; // Number of vertices
};
#pragma pack(pop)
static_assert(sizeof(MeshViewDesc) == 16);

//=== Scene Asset ===---------------------------------------------------------//

//! Scene data table descriptor (16 bytes).
/*!
 Describes a packed array of records inside a scene descriptor.

 Offsets are bytes relative to the start of the scene descriptor payload.
*/
#pragma pack(push, 1)
struct SceneDataTable {
  OffsetT offset = 0;
  uint32_t count = 0;
  uint32_t entry_size = 0;
};
#pragma pack(pop)
static_assert(sizeof(SceneDataTable) == 16);

//! Scene node table descriptor (alias).
using SceneNodeTable = SceneDataTable;

//! Index type for scene node tables.
using SceneNodeIndexT = uint32_t;

//! Scene component table descriptor (alias).
using SceneComponentTable = SceneDataTable;

//! Index type for scene component tables.
using SceneComponentIndexT = uint32_t;

//! Scene string table descriptor (8 bytes).
/*!
 Describes the packed scene string table blob.
 Offsets are relative to the start of the scene descriptor payload.
*/
#pragma pack(push, 1)
struct SceneStringTable {
  StringTableOffsetT offset = 0;
  StringTableSizeT size = 0;
};
#pragma pack(pop)
static_assert(sizeof(SceneStringTable) == 8);

//! Scene asset descriptor (256 bytes)
/*!
 Describes a scene (level) asset. As with all asset descriptors in this file,
 `AssetHeader` is the first field.

 The descriptor payload is a packed byte blob (no implicit padding) and is
 followed by:

 - `NodeRecord nodes[nodes.count];` at `nodes.offset`
 - a packed, NUL-terminated UTF-8 scene string table blob described by
   `scene_strings`
 - optional component tables (e.g. `RenderableRecord[]`) described by the
   component table directory at `component_table_directory_offset`

 `nodes.entry_size` MUST match the corresponding struct size for the scene
 format version. Component tables declare their own `entry_size` via
 `SceneComponentTableDesc::table.entry_size`.

 Strings are stored back-to-back and sized to their actual length.
 `NodeRecord::scene_name_offset` is a byte offset into the scene string table.
 The scene string table MUST start with a single `\0` byte so that offset `0`
 refers to the empty string.

 @note Scene graph indices have no sentinel values by contract. Indices are
   always valid for their type; out-of-range indices are treated as errors by
   loaders/tooling.
*/
#pragma pack(push, 1)
struct SceneAssetDesc {
  AssetHeader header;

  SceneNodeTable nodes = {};
  SceneStringTable scene_strings = {};

  // Directory of component tables.
  // Points to an array of `SceneComponentTableDesc` entries.
  OffsetT component_table_directory_offset = 0;
  uint32_t component_table_count = 0;

  uint8_t reserved[125] = {};
};
#pragma pack(pop)
static_assert(sizeof(SceneAssetDesc) == 256);

//! Scene component table directory entry.
/*!
 Describes an optional component table attached to scene nodes.

 All offsets are bytes relative to the start of the descriptor payload.

 @note This is a forward-compatible extension point. Loaders may ignore unknown
   component types. Known component tables are typically sorted by `node_index`
   for efficient loading.
*/
#pragma pack(push, 1)
struct SceneComponentTableDesc {
  uint32_t component_type = 0; // Format-defined component kind
  SceneComponentTable table = {};
};
#pragma pack(pop)
static_assert(sizeof(SceneComponentTableDesc) == 20);

#pragma pack(push, 1)

//! Scene node flags for `NodeRecord::node_flags`.
constexpr uint32_t kSceneNodeFlag_Visible = (1u << 0);
constexpr uint32_t kSceneNodeFlag_Static = (1u << 1);
constexpr uint32_t kSceneNodeFlag_CastsShadows = (1u << 2);
constexpr uint32_t kSceneNodeFlag_ReceivesShadows = (1u << 3);
constexpr uint32_t kSceneNodeFlag_RayCastingSelectable = (1u << 4);
constexpr uint32_t kSceneNodeFlag_IgnoreParentTransform = (1u << 5);

//! Node record used by the cooked scene descriptor (68 bytes).
/*!
  Describes a single node in the scene hierarchy. Nodes are stored in a flat
  array in the `SceneAssetDesc`.

  ### Hierarchy
  - The node at index 0 is always the root node.
  - `parent_index` refers to the index of the parent node in the same table.
  - If `parent_index` equals the node's own index, the node has no parent (is a
    root).

  ### Transform
  - Transforms are local to the parent.
  - Rotation is stored as a quaternion (x, y, z, w).

  @see SceneAssetDesc
*/
struct NodeRecord {
  AssetKey node_id; // Stable GUID for the node
  StringTableOffsetT scene_name_offset = 0; // Offset into scene string table

  SceneNodeIndexT parent_index = 0; // Index of parent node (or self if root)

  //! Bitfield of `kSceneNodeFlag_*` constants
  uint32_t node_flags = 0;

  // Local Transform (TRS)
  float translation[3] = { 0.0F, 0.0F, 0.0F };
  float rotation[4] = { 0.0F, 0.0F, 0.0F, 1.0F }; // Quaternion (XYZW)
  float scale[3] = { 1.0F, 1.0F, 1.0F };
};
#pragma pack(pop)
static_assert(sizeof(NodeRecord) == 68);

#pragma pack(push, 1)

//! Renderable component record (36 bytes).
/*!
  Attaches a geometry asset to a scene node.

  ### Relationships
  - Links to a `NodeRecord` via `node_index`.
  - References a `GeometryAsset` via `geometry_key`.

  @note Component tables are typically sorted by `node_index` for efficient
  loading.
*/
struct RenderableRecord {
  SceneNodeIndexT node_index = 0; // Index of the owner node
  AssetKey geometry_key; // Geometry asset to render
  uint32_t visible = 1; // Visibility flag (boolean)
  uint8_t reserved[12] = {}; // Reserved for future use (e.g. LOD bias)
};
#pragma pack(pop)
static_assert(sizeof(RenderableRecord) == 36);

#pragma pack(push, 1)

//! Perspective camera component record (32 bytes).
/*!
  Attaches a perspective camera to a scene node.

  ### Coordinate System
  - The camera looks down the -Z axis in its local space.
  - FOV is vertical, in radians.
*/
struct PerspectiveCameraRecord {
  SceneNodeIndexT node_index = 0; // Index of the owner node
  float fov_y = 0.785398f; // Vertical FOV in radians (~45 deg)
  float aspect_ratio = 1.777778f; // Width / Height (default 16:9)
  float near_plane = 0.1f; // Distance to near clipping plane
  float far_plane = 1000.0f; // Distance to far clipping plane
  uint8_t reserved[12] = {};
};
#pragma pack(pop)
static_assert(sizeof(PerspectiveCameraRecord) == 32);

#pragma pack(push, 1)

//! Orthographic camera component record (40 bytes).
/*!
  Attaches an orthographic camera to a scene node.

  ### Volume
  - Defined by a box (left, right, bottom, top, near, far) in local space.
*/
struct OrthographicCameraRecord {
  SceneNodeIndexT node_index = 0; // Index of the owner node
  float left = -10.0f;
  float right = 10.0f;
  float bottom = -10.0f;
  float top = 10.0f;
  float near_plane = -100.0f;
  float far_plane = 100.0f;
  uint8_t reserved[12] = {};
};
#pragma pack(pop)
static_assert(sizeof(OrthographicCameraRecord) == 40);

} // namespace oxygen::data::pak::v2

namespace oxygen::data::pak {
//! Default namespace alias for latest version of the PAK format
using namespace v2;
} // namespace oxygen::data::pak
