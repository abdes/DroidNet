//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormatVersions.h>
#include <Oxygen/Data/PakFormat_references.h>
#include <Oxygen/Data/Unorm16.h>
#include <Oxygen/Data/api_export.h>

// packed structs intentionally embed unaligned NamedType ResourceIndexT fields
OXYGEN_DIAGNOSTIC_PUSH
OXYGEN_DIAGNOSTIC_DISABLE_MSVC(4315)
// NOLINTBEGIN(*-avoid-c-arrays)

//! Oxygen PAK format core domain schema.
/*!
 Owns foundational container/resource/asset structures and shared scalar types.

 Every multi-byte value of a pak, and of the loose cooked files that share its
 records, is little-endian; floating-point values are IEEE-754. Each record's
 field offsets are part of the contract and pinned below its definition.

 A `_reserved` field only rounds its record to a fixed size (see
 `Docs/binary_packing_discipline.md`); it is never space for later fields.
 Its bytes are zero: writers zero them, and readers refuse a file whose
 reserved bytes are not.
*/
namespace oxygen::data::pak::core {

static_assert(std::endian::native == std::endian::little,
  "pak records are read and written in place on little-endian hosts");

//=== Type Aliases ===--------------------------------------------------------//

using oxygen::ResourceIndexT;

//! Offset type for file positions
using OffsetT = uint64_t;

// Resource index type/constants are sourced from Core metadata catalog.
#include <Oxygen/Core/Meta/Data/ResourceIndex.inc>
#include <Oxygen/Serio/Record.h>

//! Data blob size type
using DataBlobSizeT = uint32_t;

//! Offset type for slices into embedded string tables
using StringTableOffsetT = uint32_t;

//! Size type for slices into embedded string tables
using StringTableSizeT = uint32_t;

//! Canonical per-artifact integrity digest type (SHA-256).
using ContentHashDigest = oxygen::base::Sha256Digest;

//=== Constants ===-----------------------------------------------------------//

//! Resource index constants (`kFallbackResourceIndex`, `kNoResourceIndex`) are
//! sourced from `Oxygen/Core/Meta/Data/ResourceIndex.inc`.

//! Maximum size for data blobs in bytes.
[[maybe_unused]] constexpr DataBlobSizeT kDataBlobMaxSize
  = std::numeric_limits<uint32_t>::max();

//! Canonical 8-byte PAK header magic bytes for the PAK container.
inline constexpr std::array<char, 8> kPakHeaderMagic
  = { 'O', 'X', 'P', 'A', 'K', 0, 0, 0 };

//! Canonical 8-byte PAK footer magic bytes for the PAK container.
inline constexpr std::array<char, 8> kPakFooterMagic
  = { 'O', 'X', 'P', 'A', 'K', 'E', 'N', 'D' };

//! Whether the bytes of a reserve are all zero, as the format requires of
//! every `_reserved` field; pass `std::as_bytes(std::span { r._reserved })`.
//! A union arm's reserve is checked only when the arm is the active one.
[[nodiscard]] inline auto IsZeroReserve(
  const std::span<const std::byte> reserve) noexcept -> bool
{
  return std::ranges::all_of(reserve,
    [](const std::byte value) -> bool { return value == std::byte { 0U }; });
}

//! Current PAK binary format version; a build reads only this version.
constexpr uint16_t kPakVersion = version::kPakVersion;
inline constexpr uint32_t kBrowseIndexVersion = version::kBrowseIndexVersion;
[[maybe_unused]] constexpr uint16_t kCurrentPakFormatVersion = kPakVersion;

//! The boundary every resource payload of a pak starts on, and every resource
//! region is padded to: 4 KiB, so a payload can be read unbuffered or handed
//! to DirectStorage as it lies in the file.
inline constexpr uint32_t kPakPayloadAlignment = 4096U;

//! The boundary every resource table, asset descriptor, the asset directory,
//! the browse index and the footer of a pak start on.
inline constexpr uint32_t kPakMetadataAlignment = 16U;

//! `TextureResourceDesc::compression_type` of a payload in a format that is
//! not block compressed.
inline constexpr uint8_t kTextureCompressionNone = 0U;
//! `TextureResourceDesc::compression_type` of a BC7 payload (`kBC7UNorm`,
//! `kBC7UNormSRGB`); the value is the BCn family number.
inline constexpr uint8_t kTextureCompressionBc7 = 7U;

//! The `compression_type` a texture row of `format` records: the one place a
//! compression is tied to the formats that use it. A new compression adds
//! its constant above and its formats here; writers and readers follow.
[[nodiscard]] constexpr auto TextureCompressionOf(const Format format) noexcept
  -> uint8_t
{
  switch (format) {
  case Format::kBC7UNorm:
  case Format::kBC7UNormSRGB:
    return kTextureCompressionBc7;
  default:
    return kTextureCompressionNone;
  }
}

//=== PAK File Format Structures ===------------------------------------------//

//! Fixed-size header at the start of the PAK file
/*!
 The header contains metadata about the PAK file format version, content
 version, and reserved space for future extensions. It is always located at the
 very beginning of the PAK file.
*/
#pragma pack(push, 1)
struct PakHeader {
  char magic[8] = { 'O', 'X', 'P', 'A', 'K', 0, 0, 0 };
  uint16_t version = kPakVersion; // Current container format version
  uint16_t content_version = 0;
  //! Unique identifier for this PAK
  std::array<uint8_t, 16> source_identity = {};

  // Tail reserve to keep fixed 256-byte header contract.
  uint8_t _reserved[228] = {};
};
#pragma pack(pop)
static_assert(sizeof(PakHeader) == 256);
static_assert(offsetof(PakHeader, magic) == 0);
static_assert(offsetof(PakHeader, version) == 8);
static_assert(offsetof(PakHeader, content_version) == 10);
static_assert(offsetof(PakHeader, source_identity) == 12);

//! Resource region offset/size descriptor.
/*!
 A region is a contiguous block of data within the PAK file, containing blobs
 of a specific type (e.g., textures, buffers, audio). A region that holds
 payloads starts on a `kPakPayloadAlignment` boundary and is padded with null
 bytes to one; every payload in it starts on such a boundary too, which also
 satisfies the smaller alignment its descriptor records.

 @see ResourceTable, PakFooter
*/
#pragma pack(push, 1)
struct ResourceRegion {
  uint64_t offset = 0; // Absolute offset from start of PAK file
  uint64_t size = 0; // Size of the region in bytes
};
#pragma pack(pop)
static_assert(sizeof(ResourceRegion) == 16);
static_assert(offsetof(ResourceRegion, offset) == 0);
static_assert(offsetof(ResourceRegion, size) == 8);

//! Resource table.
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
static_assert(offsetof(ResourceTable, offset) == 0);
static_assert(offsetof(ResourceTable, count) == 8);
static_assert(offsetof(ResourceTable, entry_size) == 12);

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
  ResourceTable physics_resource_table = {}; // PhysicsResourceDesc entries

  // -- Embedded Browse Index (Optional) --
  //
  // When non-zero, these fields describe the location of an embedded browse
  // index (OXPAKBIX) used by editor/tooling for virtual-path enumeration.
  // Runtime loading does not require this index.
  OffsetT browse_index_offset = 0;
  uint64_t browse_index_size = 0;

  //! Mandatory canonical catalog: physical entries, deletions and base layers.
  OffsetT catalog_offset = 0;
  uint64_t catalog_size = 0;

  // Rounds the footer to 256 bytes. The one reserve not at the end of its
  // record: the CRC32 and the footer magic follow it, because the magic must
  // be the last 8 bytes of the file and the CRC32 sits at a fixed distance
  // from the end (see `Docs/binary_packing_discipline.md`, Rule 5).
  uint8_t _reserved[28] = {};

  // -- CRC32 Integrity --
  // CRC32 covers the *entire* file, including the footer and footer magic
  // bytes, EXCEPT these 4 bytes (`pak_crc32`, at footer offset 244, file
  // offset `file_size - 12`) which are excluded (skipped) from the CRC stream
  // during calculation.
  // Standard IEEE CRC32 parameters: polynomial 0x04C11DB7, initial value
  // 0xFFFFFFFF, reflect in/out, final XOR 0xFFFFFFFF.
  // A value of 0 indicates that integrity validation SHOULD be skipped.
  uint32_t pak_crc32 = 0; // CRC32 integrity hash (excluded from calculation)

  // The last thing in the PAK file is the footer magic bytes.
  char footer_magic[8] = { 'O', 'X', 'P', 'A', 'K', 'E', 'N', 'D' };
};
#pragma pack(pop)
static_assert(sizeof(PakFooter) == 256);
static_assert(offsetof(PakFooter, directory_offset) == 0);
static_assert(offsetof(PakFooter, directory_size) == 8);
static_assert(offsetof(PakFooter, asset_count) == 16);
static_assert(offsetof(PakFooter, texture_region) == 24);
static_assert(offsetof(PakFooter, buffer_region) == 40);
static_assert(offsetof(PakFooter, audio_region) == 56);
static_assert(offsetof(PakFooter, script_region) == 72);
static_assert(offsetof(PakFooter, physics_region) == 88);
static_assert(offsetof(PakFooter, texture_table) == 104);
static_assert(offsetof(PakFooter, buffer_table) == 120);
static_assert(offsetof(PakFooter, audio_table) == 136);
static_assert(offsetof(PakFooter, script_resource_table) == 152);
static_assert(offsetof(PakFooter, physics_resource_table) == 168);
static_assert(offsetof(PakFooter, browse_index_offset) == 184);
static_assert(offsetof(PakFooter, browse_index_size) == 192);
static_assert(offsetof(PakFooter, catalog_offset) == 200);
static_assert(offsetof(PakFooter, catalog_size) == 208);
static_assert(offsetof(PakFooter, pak_crc32) == 244);
static_assert(offsetof(PakFooter, footer_magic) == 248);

//! Asset directory entry.
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
  AssetType asset_type { AssetType::kUnknown };
  OffsetT entry_offset = 0; // Absolute offset of *this* directory entry
  OffsetT desc_offset = 0; // Absolute offset of the asset descriptor
  uint32_t desc_size = 0; // Size of asset descriptor (for sanity check)
  AssetReferenceTable references {};

  // Tail reserve to keep fixed 64-byte directory record size.
  uint8_t _reserved[11] = {}; // Tail reserve to keep fixed 64-byte entry size
};
#pragma pack(pop)
static_assert(sizeof(AssetDirectoryEntry) == 64);
static_assert(offsetof(AssetDirectoryEntry, asset_key) == 0);
static_assert(offsetof(AssetDirectoryEntry, asset_type) == 16);
static_assert(offsetof(AssetDirectoryEntry, entry_offset) == 17);
static_assert(offsetof(AssetDirectoryEntry, desc_offset) == 25);
static_assert(offsetof(AssetDirectoryEntry, desc_size) == 33);
static_assert(offsetof(AssetDirectoryEntry, references) == 37);

//! Embedded browse index header.
/*!
 Provides a mapping from canonical virtual paths to AssetKeys and texture
 resources for editor and tooling use. Runtime loading by key does not need it;
 resolving a texture by its virtual path does.

 The payload is the header, `entry_count` asset entries, `texture_entry_count`
 texture entries and the string table.

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
  uint32_t version = kBrowseIndexVersion;
  uint32_t entry_count = 0;
  uint32_t texture_entry_count = 0;
  StringTableSizeT string_table_size = 0;
};
#pragma pack(pop)
static_assert(sizeof(PakBrowseIndexHeader) == 24);
static_assert(offsetof(PakBrowseIndexHeader, magic) == 0);
static_assert(offsetof(PakBrowseIndexHeader, version) == 8);
static_assert(offsetof(PakBrowseIndexHeader, entry_count) == 12);
static_assert(offsetof(PakBrowseIndexHeader, texture_entry_count) == 16);
static_assert(offsetof(PakBrowseIndexHeader, string_table_size) == 20);

//! Embedded browse index entry.
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
static_assert(offsetof(PakBrowseIndexEntry, asset_key) == 0);
static_assert(offsetof(PakBrowseIndexEntry, virtual_path_offset) == 16);
static_assert(offsetof(PakBrowseIndexEntry, virtual_path_length) == 20);

//! Embedded browse index texture entry.
/*!
 Maps the virtual path of a cooked texture descriptor (`.otex`) to its index
 in the pak's texture table. Several paths may share one texture.
*/
#pragma pack(push, 1)
struct PakBrowseIndexTextureEntry {
  ResourceIndexT texture_index = kNoResourceIndex;
  StringTableOffsetT virtual_path_offset = 0;
  StringTableSizeT virtual_path_length = 0;
};
#pragma pack(pop)
static_assert(sizeof(PakBrowseIndexTextureEntry) == 12);
static_assert(offsetof(PakBrowseIndexTextureEntry, texture_index) == 0);
static_assert(offsetof(PakBrowseIndexTextureEntry, virtual_path_offset) == 4);
static_assert(offsetof(PakBrowseIndexTextureEntry, virtual_path_length) == 8);

//! Buffer resource table entry
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
    // At most one is set; readers refuse more. None means Static.
    0x80 : Dynamic           (frequently updated)
    0x100: Static            (rarely updated)
    0x200: Immutable         (never updated after creation)
    ```

  @note `content_hash` is the full SHA-256 of the buffer's `size_bytes` bytes
  at `data_offset`. Its producer computes it from the bytes it writes; readers
  verify the data against it and importers deduplicate buffers by it.
*/
#pragma pack(push, 1)
struct BufferResourceDesc {
  OffsetT data_offset = 0; //!< Absolute offset to buffer data in the pak
  DataBlobSizeT size_bytes = 0; //!< Size of buffer data in bytes
  uint32_t usage_flags = 0; //!< Usage hints (see above)
  uint32_t element_stride = 0; //!< 1 for raw buffers, 0 when unused
  uint8_t element_format = 0; //!< Format enum value (0 = raw or structured)
  ContentHashDigest content_hash = {}; //!< SHA-256 of the buffer data
};
#pragma pack(pop)
static_assert(sizeof(BufferResourceDesc) == 53);
static_assert(offsetof(BufferResourceDesc, data_offset) == 0);
static_assert(offsetof(BufferResourceDesc, size_bytes) == 8);
static_assert(offsetof(BufferResourceDesc, usage_flags) == 12);
static_assert(offsetof(BufferResourceDesc, element_stride) == 16);
static_assert(offsetof(BufferResourceDesc, element_format) == 20);
static_assert(offsetof(BufferResourceDesc, content_hash) == 21);

//! Texture resource table entry
/*!
  @note Texture `format` must be one of the core type `Format` enum values.
  @note `compression_type` is `TextureCompressionOf(format)`; readers refuse
  any other value (`HasSupportedCompression`). `format` stays authoritative.
  @note Textures are always aligned to 256 bytes.
  @note `content_hash` is the full SHA-256 of the texture's `size_bytes`
  payload bytes at `data_offset`. Its producer computes it from the bytes it
  writes; readers verify the data against it and importers deduplicate
  textures by it without reading the data file again.
*/
#pragma pack(push, 1)
// NOLINTNEXTLINE(*-type-member-init) - MUST be initialized by users
struct TextureResourceDesc {
  core::OffsetT data_offset; // Absolute offset to texture data
  core::DataBlobSizeT size_bytes; // Size of texture data
  uint8_t texture_type; // 2D, 3D, Cube, etc. (enum) (defined externally)
  uint8_t compression_type; // kTextureCompression* of `format`
  uint32_t width; // Texture width
  uint32_t height; // Texture height
  uint16_t depth; // For 3D textures (volume), otherwise 1
  uint16_t array_layers; // For array textures/cubemap arrays, otherwise 1
  uint16_t mip_levels; // Number of mip levels
  uint8_t format; // Texture format enum
  uint16_t alignment; // 256 for textures
  ContentHashDigest content_hash = {}; // SHA-256 of the texture payload
};
#pragma pack(pop)
static_assert(sizeof(TextureResourceDesc) == 63);

//! Whether `row` records the compression its format implies; any other
//! value is one this build does not implement.
[[nodiscard]] constexpr auto HasSupportedCompression(
  const TextureResourceDesc& row) noexcept -> bool
{
  return row.compression_type
    == TextureCompressionOf(static_cast<Format>(row.format));
}
static_assert(offsetof(TextureResourceDesc, data_offset) == 0);
static_assert(offsetof(TextureResourceDesc, size_bytes) == 8);
static_assert(offsetof(TextureResourceDesc, texture_type) == 12);
static_assert(offsetof(TextureResourceDesc, compression_type) == 13);
static_assert(offsetof(TextureResourceDesc, width) == 14);
static_assert(offsetof(TextureResourceDesc, height) == 18);
static_assert(offsetof(TextureResourceDesc, depth) == 22);
static_assert(offsetof(TextureResourceDesc, array_layers) == 24);
static_assert(offsetof(TextureResourceDesc, mip_levels) == 26);
static_assert(offsetof(TextureResourceDesc, format) == 28);
static_assert(offsetof(TextureResourceDesc, alignment) == 29);
static_assert(offsetof(TextureResourceDesc, content_hash) == 31);

//! A descriptor's string table.
/*!
 Every asset descriptor holds one string table: packed, NUL-terminated UTF-8
 strings stored back to back. The table starts with a single `\0` byte, so
 offset `0` is the empty string. `offset` is relative to the start of the
 descriptor (its `AssetHeader`) and lies after the descriptor's fixed struct.

 Every string a descriptor holds (the asset name, node and mesh names, script
 parameter keys and values, shader paths) is a `StringTableOffsetT` into this
 table. Strings have no size limit beyond the 32-bit table size.
*/
#pragma pack(push, 1)
struct DescriptorStringTable {
  StringTableOffsetT offset = 0;
  StringTableSizeT size = 0;
};
#pragma pack(pop)
static_assert(sizeof(DescriptorStringTable) == 8);
static_assert(offsetof(DescriptorStringTable, offset) == 0);
static_assert(offsetof(DescriptorStringTable, size) == 4);

//! Asset header.
/*!
 Always the first field in every asset descriptor. Contains metadata about the
 asset, such as its type, name, version, streaming priority, content hash, and
 variant flags.

 ### Notes

 - `strings` locates the descriptor's string table; `name_offset` is the asset
   name's offset in it. The name is a label for tools and logs, not an
   identity.
 - The `streaming_priority` field is used to determine the order in which
   assets should be loaded. Lower values indicate higher priority.
 - The `asset_type` field is redundant with the directory entry, but is
   necessary for debugging and sanity checks.
 - The `content_hash` field is the SHA-256 of the whole descriptor, taken with
   this field zeroed (`data::ComputeDescriptorHash`). A cook records it and
   every reader of cooked content refuses a descriptor that does not.
 - The `variant_flags` field is a project-defined bitfield that can be used to
   store additional metadata about the asset, such as its intended use or
   compatibility with specific features.
*/
#pragma pack(push, 1)
struct AssetHeader {
  uint8_t asset_type = 0; // Redundant with directory for debugging
  DescriptorStringTable strings = {}; // The descriptor's string table
  StringTableOffsetT name_offset = 0; // Asset name, for debugging/tools
  uint8_t version = 0; // Asset format version (up to 256 versions)
  uint8_t streaming_priority = 0; // Loading priority: 0=highest, 255=lowest
  ContentHashDigest content_hash = {}; // Full SHA-256 content integrity hash
  uint32_t variant_flags = 0; // Project-defined (not interpreted by engine)
};
#pragma pack(pop)
static_assert(sizeof(AssetHeader) == 51);
static_assert(offsetof(AssetHeader, asset_type) == 0);
static_assert(offsetof(AssetHeader, strings) == 1);
static_assert(offsetof(AssetHeader, name_offset) == 9);
static_assert(offsetof(AssetHeader, version) == 13);
static_assert(offsetof(AssetHeader, streaming_priority) == 14);
static_assert(offsetof(AssetHeader, content_hash) == 15);
static_assert(offsetof(AssetHeader, variant_flags) == 47);

} // namespace oxygen::data::pak::core

namespace oxygen::serio {
// clang-format off
template <> inline constexpr bool kIsRecord<data::AssetKey> = true;
template <> inline constexpr bool kIsRecord<data::pak::core::ResourceIndexT> = true;
template <> inline constexpr bool kIsRecord<data::pak::core::ResourceRegion> = true;
template <> inline constexpr bool kIsRecord<data::pak::core::ResourceTable> = true;
template <> inline constexpr bool kIsRecord<data::pak::core::PakHeader> = true;
template <> inline constexpr bool kIsRecord<data::pak::core::PakFooter> = true;
template <> inline constexpr bool kIsRecord<data::pak::core::AssetDirectoryEntry> = true;
template <> inline constexpr bool kIsRecord<data::pak::core::PakBrowseIndexHeader> = true;
template <> inline constexpr bool kIsRecord<data::pak::core::PakBrowseIndexEntry> = true;
template <> inline constexpr bool kIsRecord<data::pak::core::PakBrowseIndexTextureEntry> = true;
template <> inline constexpr bool kIsRecord<data::pak::core::BufferResourceDesc> = true;
template <> inline constexpr bool kIsRecord<data::pak::core::TextureResourceDesc> = true;
template <> inline constexpr bool kIsRecord<data::pak::core::AssetHeader> = true;
// clang-format on
} // namespace oxygen::serio

// NOLINTEND(*-avoid-c-arrays)
OXYGEN_DIAGNOSTIC_POP
