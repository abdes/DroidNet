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

#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/Unorm16.h>
#include <Oxygen/Data/api_export.h>

// packed structs intentionally embed unaligned NamedType ResourceIndexT fields
OXYGEN_DIAGNOSTIC_PUSH
OXYGEN_DIAGNOSTIC_DISABLE_MSVC(4315)
// NOLINTBEGIN(*-avoid-c-arrays,*-magic-numbers)

//! Oxygen PAK format core domain schema.
/*!
 Owns foundational container/resource/asset structures and shared scalar types.
*/
namespace oxygen::data::pak::core {

//=== Type Aliases ===--------------------------------------------------------//

using ResourceIndexT = oxygen::ResourceIndexT;

//! Offset type for file positions (8 bytes)
using OffsetT = uint64_t;

// Resource index type/constants are sourced from Core metadata catalog.
#include <Oxygen/Core/Meta/Data/ResourceIndex.inc>

//! Data blob size type (4 bytes)
using DataBlobSizeT = uint32_t;

//! Offset type for slices into embedded string tables (4 bytes)
using StringTableOffsetT = uint32_t;

//! Size type for slices into embedded string tables (4 bytes)
using StringTableSizeT = uint32_t;

//=== Constants ===-----------------------------------------------------------//

//! Maximum asset name length including null terminator
constexpr size_t kMaxNameSize = 64;

//! Resource index constants (`kFallbackResourceIndex`, `kNoResourceIndex`) are
//! sourced from `Oxygen/Core/Meta/Data/ResourceIndex.inc`.

//! Maximum size for data blobs in bytes.
[[maybe_unused]] constexpr DataBlobSizeT kDataBlobMaxSize
  = (std::numeric_limits<uint32_t>::max)();

//! Canonical 8-byte PAK header magic bytes for the PAK container.
inline constexpr std::array<char, 8> kPakHeaderMagic
  = { 'O', 'X', 'P', 'A', 'K', 0, 0, 0 };

//! Canonical 8-byte PAK footer magic bytes for the PAK container.
inline constexpr std::array<char, 8> kPakFooterMagic
  = { 'O', 'X', 'P', 'A', 'K', 'E', 'N', 'D' };

//! Current PAK binary format version (v7 only).
constexpr uint16_t kPakVersion = 7;
[[maybe_unused]] constexpr uint16_t kCurrentPakFormatVersion = kPakVersion;

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
  uint16_t version = kPakVersion; // Format version (must be v7)
  uint16_t content_version = 0; // Content version
  std::array<uint8_t, 16> guid = {}; // Unique identifier for this PAK
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
  ResourceRegion script_region = {}; // Script payload region
  ResourceRegion physics_region = {}; // Physics payload region

  // -- Resource tables --
  ResourceTable texture_table = {};
  ResourceTable buffer_table = {};
  ResourceTable audio_table = {};
  ResourceTable script_resource_table = {}; // ScriptResourceDesc entries
  ResourceTable script_slot_table = {}; // ScriptSlotRecord entries
  ResourceTable physics_resource_table = {}; // PhysicsResourceDesc entries

  // -- Embedded Browse Index (Optional) --
  //
  // When non-zero, these fields describe the location of an embedded browse
  // index (OXPAKBIX) used by editor/tooling for virtual-path enumeration.
  // Runtime loading does not require this index.
  OffsetT browse_index_offset = 0;
  uint64_t browse_index_size = 0;

  // Reserved for future use
  uint8_t reserved[28] = {};

  // -- CRC32 Integrity --
  // CRC32 covers the *entire* file, including the footer and footer magic
  // bytes, EXCEPT these 4 bytes (`pak_crc32`) which are excluded (skipped)
  // from the CRC stream during calculation.
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
  AssetKey asset_key {};
  AssetType asset_type { AssetType::kUnknown };
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

  @note `content_hash` stores the first 8 bytes of the SHA256 of the buffer
  data. Used for fast deduplication during incremental imports.

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
  uint64_t content_hash = 0; //!< First 8 bytes of SHA256 of buffer data
  uint8_t reserved[3] = {}; //!< Reserved for future use (must be zero)
};
#pragma pack(pop)
static_assert(sizeof(BufferResourceDesc) == 32);

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

} // namespace oxygen::data::pak::core

// NOLINTEND(*-avoid-c-arrays,*-magic-numbers)
OXYGEN_DIAGNOSTIC_POP
