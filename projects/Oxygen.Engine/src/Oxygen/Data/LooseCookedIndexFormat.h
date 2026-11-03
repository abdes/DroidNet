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
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormatVersions.h>
#include <Oxygen/Data/PakFormat_references.h>
#include <Oxygen/Data/api_export.h>
#include <Oxygen/Serio/Record.h>

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

//! The index file, at the cooked root.
inline constexpr std::string_view kIndexFileName = "container.index.bin";

//! Cooperative reader/publisher lease marker for an immutable cooked
//! generation.
inline constexpr std::string_view kGenerationLeaseFileName = ".generation.lock";

//! The writer lease, at the cooked root: a writer holds it exclusively for
//! its whole session, so a root has one writer at a time.
inline constexpr std::string_view kWriterLockFileName = "container.writer.lock";

//! The folder of the reader leases, at the cooked root: a reader of commit
//! `c` holds `.leases/<c>.lock` shared, so a writer never reclaims a file a
//! commit it reads lists.
inline constexpr std::string_view kLeasesFolderName = ".leases";

//! 8-byte header magic: {'O','X','L','C','I','D','X',0}
constexpr std::array<char, 8> kHeaderMagic
  = { 'O', 'X', 'L', 'C', 'I', 'D', 'X', 0 };

//! SHA-256 size in bytes.
constexpr size_t kSha256Size = 32;

//! Current loose index version. Older layouts must be recooked.
inline constexpr uint16_t kIndexVersion
  = pak::version::kLooseCookedIndexVersion;

//! Required-section declarations in the current index.
// The flag values are implicitly convertible to the on-disk uint32_t mask.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum IndexFlags : uint32_t {
  //! Declares that asset entries contain valid virtual paths.
  kHasVirtualPaths = OXYGEN_FLAG(0),
  //! Declares that the file-records section is present and must be validated.
  kHasFileRecords = OXYGEN_FLAG(1),
};

OXYGEN_DEFINE_FLAGS_OPERATORS(IndexFlags)

//! String representation of IndexFlags bitmask.
OXGN_DATA_NDAPI auto to_string(IndexFlags value) -> std::string;

//! Mask of all known index flags.
[[maybe_unused]] constexpr IndexFlags kKnownIndexFlags
  = kHasVirtualPaths | kHasFileRecords;

//=== Index File Format Structures ===---------------------------------------//

//! Fixed-size header at the start of the loose cooked index.
#pragma pack(push, 1)
struct IndexHeader {
  std::array<char, 8> magic = kHeaderMagic; // NOLINT
  uint16_t version = kIndexVersion; // Schema version
  uint16_t content_version = 0; // Content version (cook-defined)
  uint32_t flags = kKnownIndexFlags; // IndexFlags bitset
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

  //! The commit this index is: 1 for a root's first, one more for each
  //! publication after it.
  uint64_t commit = 0;

  // -- Retired files: listed by an earlier commit, not by this one --
  OffsetT retired_records_offset = 0;
  CountT retired_record_count = 0;
  uint32_t retired_record_size = 0; // sizeof(RetiredRecord) for this schema

  // -- What the assets require of other mounts --
  OffsetT requirement_records_offset = 0;
  CountT requirement_record_count = 0;
  uint32_t requirement_record_size = 0; // sizeof(RequirementRecord)

  // The packed record uses fixed C arrays to specify its binary layout.
  // NOLINTNEXTLINE(modernize-avoid-c-arrays)
  uint8_t _reserved[136] = {}; // Tail reserve to keep fixed 256-byte header
};
#pragma pack(pop)
static_assert(sizeof(IndexHeader) == 256);

//! Asset directory entry.
/*! Stores the runtime identity plus the descriptor-relative path, and the
 identity of the source that produced the asset. A source re-cooking its own
 asset replaces it; another source producing the same identity is refused.
 */
#pragma pack(push, 1)
struct AssetEntry {
  AssetKey asset_key;
  uint8_t asset_type = 0; // AssetType enum (loader dispatch)

  // Offsets are into the string table.
  uint32_t descriptor_relpath_offset = 0; // e.g. "assets/Materials/Dark.mat"
  uint32_t virtual_path_offset = 0; // e.g. "/Content/Materials/Dark.mat"
  //! Mount path of the producing source, `/<Mount>/<path in mount>`, e.g.
  //! "/Content/Materials/Dark.omat.json"; never derived from the asset's
  //! virtual path.
  uint32_t source_path_offset = 0;

  // Mandatory descriptor integrity; runtime hash verification is opt-in.
  SizeT descriptor_size = 0;
  // NOLINTNEXTLINE(modernize-avoid-c-arrays) - packed wire representation
  uint8_t descriptor_sha256[kSha256Size] = {};
  pak::core::AssetReferenceTable references {};
};
#pragma pack(pop)
static_assert(sizeof(AssetEntry) == 85);

//! Kind of a file record.
enum class FileKind : uint16_t {
  kUnknown = 0,
  kBuffersTable = 1,
  kBuffersData = 2,
  kTexturesTable = 3,
  kTexturesData = 4,
  kScriptsTable = 5,
  kScriptsData = 6,
  kPhysicsTable = 7,
  kPhysicsData = 8,
  //! Explicitly emitted resource descriptors or other non-role content files.
  kAuxiliary = 11,
  //! A texture resource descriptor (`.otex`), addressed by its virtual path.
  kTextureDescriptor = 12,
  //! An asset descriptor; only a retired record names one, since an asset
  //! entry lists a current descriptor.
  kAssetDescriptor = 13,
};

//! File record for resources and other cooked artifacts.
#pragma pack(push, 1)
struct FileRecord {
  FileKind kind = FileKind::kUnknown;
  SizeT size = 0;
  //! Offset into string table for the relative path.
  uint32_t relpath_offset = 0; // e.g. "resources/buffers.table"
  //! Offset into string table for the virtual path a reference names the
  //! file by: a texture descriptor has one, an auxiliary file may (a buffer
  //! sidecar does), and every other record uses the empty string at offset 0.
  //! The file itself has a content name.
  uint32_t virtual_path_offset = 0; // e.g. "/Content/Textures/Sky.otex"
  //! Offset into string table for the mount path of the source that produced
  //! a texture descriptor or an auxiliary file; every other record, and an
  //! auxiliary file of no recorded producer, uses the empty string.
  uint32_t source_path_offset = 0; // e.g. "/Content/Ship/Ship.glb"
  std::array<uint8_t, kSha256Size> sha256 {};
};
#pragma pack(pop)
static_assert(sizeof(FileRecord) == 54);

//! A file an earlier commit listed and this one does not.
/*!
 Commits before `retired_at` list it, so readers of those commits may still
 open it; a writer deletes it once no reader holds one of them. The file may
 already be gone.
*/
#pragma pack(push, 1)
struct RetiredRecord {
  FileKind kind = FileKind::kUnknown;
  //! Offset into string table for the relative path.
  uint32_t relpath_offset = 0;
  //! The first commit that does not list the file.
  uint64_t retired_at = 0;
};
#pragma pack(pop)
static_assert(sizeof(RetiredRecord) == 14);

//! An asset or a texture of another mount that an asset references.
/*!
 Records follow the asset entries' order. A reference in the root's own mount
 is no requirement.
*/
#pragma pack(push, 1)
struct RequirementRecord {
  //! Index of the requiring asset in the asset entries.
  uint32_t asset_index = 0;
  //! Offset into string table for the required mount's name, such as
  //! `Engine`.
  uint32_t mount_offset = 0;
  //! `KeyReferenceKind`: `kAsset` or `kTexture`.
  uint8_t kind = 0;
  AssetKey key;
  //! `AssetType` of the required asset; `kUnknown` for a texture.
  uint8_t asset_type = 0;
};
#pragma pack(pop)
static_assert(sizeof(RequirementRecord) == 26);

} // namespace oxygen::data::loose_cooked

// clang-format off
namespace oxygen::serio {
template <> inline constexpr bool kIsRecord<data::loose_cooked::IndexHeader> = true;
template <> inline constexpr bool kIsRecord<data::loose_cooked::AssetEntry> = true;
template <> inline constexpr bool kIsRecord<data::loose_cooked::FileRecord> = true;
template <> inline constexpr bool kIsRecord<data::loose_cooked::RetiredRecord> = true;
template <> inline constexpr bool kIsRecord<data::loose_cooked::RequirementRecord> = true;
} // namespace oxygen::serio
// clang-format on
