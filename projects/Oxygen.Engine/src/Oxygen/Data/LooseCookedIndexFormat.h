//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <Oxygen/Data/AssetKey.h>

//! Oxygen loose cooked index binary format specification
/**!
 @file LooseCookedIndexFormat.h

 ### Invariants
 - All structures are packed with `#pragma pack(push,1)` eliminating implicit
   padding in the serialized on-disk representation.
 - Endianness is little-endian (Intel / x86-64). Cross-platform loaders on
   big-endian architectures MUST byte-swap scalar fields.
 - All offsets are absolute from the start of the index file.
 - All strings are UTF-8 and stored in a single null-terminated string table.
   Offsets into the string table are 0-based.
 - All paths stored in the index are container-relative (no leading slash and
   no physical filesystem roots).
*/

namespace oxygen::data::loose_cooked::v1 {

//=== Type Aliases ===--------------------------------------------------------//

using OffsetT = uint64_t;
using SizeT = uint64_t;
using CountT = uint32_t;

//=== Constants ===-----------------------------------------------------------//

//! 8-byte header magic: {'O','X','L','C','I','D','X',0}
static constexpr std::array<char, 8> kHeaderMagic
  = { 'O', 'X', 'L', 'C', 'I', 'D', 'X', 0 };

//! SHA-256 size in bytes.
constexpr size_t kSha256Size = 32;

//! Index header flags (v1).
/**!
 The `IndexHeader::flags` field is used to declare which logical sections are
 present/required in the index.

 Backward compatibility note:
 - `flags == 0` is treated as a legacy value. Loaders may accept older indexes
   that do not populate flags.
 - When `flags != 0`, loaders should enforce these bits strictly.
*/
enum IndexFlags : uint32_t {
  //! Declares that asset entries contain valid virtual paths.
  kHasVirtualPaths = 1u << 0,
  //! Declares that the file-records section is present and must be validated.
  kHasFileRecords = 1u << 1,
};

//! Mask of all known v1 index flags.
static constexpr uint32_t kKnownIndexFlags
  = static_cast<uint32_t>(kHasVirtualPaths)
  | static_cast<uint32_t>(kHasFileRecords);

//=== Index File Format Structures ===---------------------------------------//

//! Fixed-size header at the start of the loose cooked index (256 bytes).
#pragma pack(push, 1)
struct IndexHeader {
  char magic[8] = { 'O', 'X', 'L', 'C', 'I', 'D', 'X', 0 };
  uint16_t version = 1; // Schema version
  uint16_t content_version = 0; // Content version (cook-defined)
  uint32_t flags = 0; // IndexFlags bitset; 0 = legacy/unspecified

  // -- String table (null-terminated UTF-8 strings) --
  OffsetT string_table_offset = 0;
  SizeT string_table_size = 0;

  // -- Asset entries section --
  OffsetT asset_entries_offset = 0;
  CountT asset_count = 0;
  uint32_t asset_entry_size = 0; // sizeof(AssetEntry) for this schema

  // -- File metadata records (tables/data) --
  OffsetT file_records_offset = 0;
  CountT file_record_count = 0;
  uint32_t file_record_size = 0; // sizeof(FileRecord) for this schema

  uint8_t guid[16] = {}; // Unique identifier for this loose cooked source
  uint8_t reserved[176] = {};
};
#pragma pack(pop)
static_assert(sizeof(IndexHeader) == 256);

//! Asset directory entry.
/*! Stores the runtime identity plus the descriptor-relative path.
 */
#pragma pack(push, 1)
struct AssetEntry {
  AssetKey asset_key;

  // Offsets are into the string table.
  uint32_t descriptor_relpath_offset = 0; // e.g. "assets/Materials/Dark.mat"
  uint32_t virtual_path_offset = 0; // e.g. "/Content/Materials/Dark.mat"

  uint8_t asset_type = 0; // AssetType enum (loader dispatch)
  uint8_t reserved0[3] = {};

  // Descriptor integrity (metadata only; validation policy is runtime-defined)
  SizeT descriptor_size = 0;
  uint8_t descriptor_sha256[kSha256Size] = {};

  uint8_t reserved1[8] = {};
};
#pragma pack(pop)
static_assert(sizeof(AssetEntry) == 76);

//! Kind of a file record.
enum class FileKind : uint16_t {
  kUnknown = 0,
  kBuffersTable = 1,
  kBuffersData = 2,
  kTexturesTable = 3,
  kTexturesData = 4,
};

//! File record for resources and other cooked artifacts.
#pragma pack(push, 1)
struct FileRecord {
  FileKind kind = FileKind::kUnknown;
  uint16_t reserved0 = 0;

  // Offset into string table for the relative path.
  uint32_t relpath_offset = 0; // e.g. "resources/buffers.table"

  SizeT size = 0;

  // NOTE: File-level SHA256 was removed in favor of per-resource content_hash
  // stored in TextureResourceDesc/BufferResourceDesc. Append-only data files
  // invalidate whole-file hashes on each append.
  uint8_t reserved1[48] = {};
};
#pragma pack(pop)
static_assert(sizeof(FileRecord) == 64);

} // namespace oxygen::data::loose_cooked::v1

namespace oxygen::data::loose_cooked {
//! Default namespace alias for latest version of the PAK format
using namespace v1;
} // namespace oxygen::data::loose_cooked
