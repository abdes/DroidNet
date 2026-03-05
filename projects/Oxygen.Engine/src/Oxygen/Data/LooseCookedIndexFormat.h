//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/api_export.h>

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

namespace oxygen::data::loose_cooked {

//=== Type Aliases ===--------------------------------------------------------//

using OffsetT = uint64_t;
using SizeT = uint64_t;
using CountT = uint32_t;

//=== Constants ===-----------------------------------------------------------//

//! 8-byte header magic: {'O','X','L','C','I','D','X',0}
constexpr std::array<char, 8> kHeaderMagic
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
enum IndexFlags : uint32_t { // NOLINT(*-enum-size)
  //! Declares that asset entries contain valid virtual paths.
  kHasVirtualPaths = OXYGEN_FLAG(0),
  //! Declares that the file-records section is present and must be validated.
  kHasFileRecords = OXYGEN_FLAG(1),
};

OXYGEN_DEFINE_FLAGS_OPERATORS(IndexFlags)

//! String representation of IndexFlags bitmask.
OXGN_DATA_NDAPI auto to_string(IndexFlags value) -> std::string;

//! Mask of all known v1 index flags.
[[maybe_unused]] constexpr IndexFlags kKnownIndexFlags
  = kHasVirtualPaths | kHasFileRecords;

//=== Index File Format Structures ===---------------------------------------//

//! Fixed-size header at the start of the loose cooked index.
#pragma pack(push, 1)
struct IndexHeader {
  std::array<char, 8> magic = kHeaderMagic; // NOLINT
  uint16_t version = 1; // Schema version
  uint16_t content_version = 0; // Content version (cook-defined)
  uint32_t flags = 0; // IndexFlags bitset; 0 = legacy/unspecified
  std::array<uint8_t, 16> source_identity = {};

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

  uint8_t _reserved[176] = {}; // Tail reserve to keep fixed 256-byte header
};
#pragma pack(pop)
static_assert(sizeof(IndexHeader) == 256);

//! Asset directory entry.
/*! Stores the runtime identity plus the descriptor-relative path.
 */
#pragma pack(push, 1)
struct AssetEntry {
  AssetKey asset_key;
  uint8_t asset_type = 0; // AssetType enum (loader dispatch)

  // Offsets are into the string table.
  uint32_t descriptor_relpath_offset = 0; // e.g. "assets/Materials/Dark.mat"
  uint32_t virtual_path_offset = 0; // e.g. "/Content/Materials/Dark.mat"

  // Descriptor integrity (metadata only; validation policy is runtime-defined)
  SizeT descriptor_size = 0;
  uint8_t descriptor_sha256[kSha256Size] = {};
};
#pragma pack(pop)
static_assert(sizeof(AssetEntry) == 65);

//! Kind of a file record.
enum class FileKind : uint16_t { // NOLINT(*-enum-size)
  kUnknown = 0,
  kBuffersTable = 1,
  kBuffersData = 2,
  kTexturesTable = 3,
  kTexturesData = 4,
  kScriptsTable = 5,
  kScriptsData = 6,
  kPhysicsTable = 7,
  kPhysicsData = 8,
  kScriptBindingsTable = 9,
  kScriptBindingsData = 10,
};

//! File record for resources and other cooked artifacts.
#pragma pack(push, 1)
struct FileRecord {
  FileKind kind = FileKind::kUnknown;
  SizeT size = 0;
  //! Offset into string table for the relative path.
  uint32_t relpath_offset = 0; // e.g. "resources/buffers.table"
};
#pragma pack(pop)
static_assert(sizeof(FileRecord) == 14);

} // namespace oxygen::data::loose_cooked
