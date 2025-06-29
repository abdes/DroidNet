//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/FileStream.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Reader.h>
#include <Oxygen/Content/AssetKey.h>
#include <Oxygen/Content/api_export.h>

namespace oxygen::content {

//! Directory entry for a single asset in a .pak file.
/*!
 AssetDirectoryEntry describes a single asset's metadata in the .pak file
 directory. This struct is written/read as a 64-byte binary block.

 @see AssetKey, PakFile
*/
#pragma pack(push, 1)
struct AssetDirectoryEntry {
  AssetKey key; //!< Asset identifier (24 bytes)
  uint64_t entry_offset; //!< Absolute offset of the directory entry
  uint64_t data_offset; //!< Absolute offset of the asset data blob
  uint32_t data_size; //!< Size of asset data
  uint32_t alignment; //!< Required alignment
  uint16_t dependency_count; //!< Number of dependencies
  uint8_t compression; //!< Compression method
  uint8_t reserved0; //!< Reserved for future use

  uint8_t reserved[12]; //!< Padding to 64 bytes
  // Followed by: AssetKey dependencies[dependency_count];
};
#pragma pack(pop)
static_assert(sizeof(AssetDirectoryEntry) == 64);

//! Header for Oxygen .pak asset containers.
/*!
 PakHeader describes the .pak file header. This struct is written/read as a
 64-byte binary block.

 @see PakFile
*/
#pragma pack(push, 1)
struct PakHeader {
  char magic[8]; //!< 8-byte magic identifier: {'O','X','P','A','K',0,0,0}
  uint16_t version; //!< File format version
  uint16_t content_version; //!< Content version
  uint8_t reserved[52]; //!< Reserved for future use, must be zero
};
#pragma pack(pop)
static_assert(sizeof(PakHeader) == 64, "PAK Header must be 64 bytes");

//! Footer for Oxygen .pak asset containers.
/*!
 PakFooter describes the .pak file footer. This struct is written/read as a
 64-byte binary block.

 @see PakFile
*/
#pragma pack(push, 1)
struct PakFooter {
  uint64_t directory_offset; //!< Offset to the asset directory
  uint64_t directory_size; //!< Size of the asset directory
  uint64_t asset_count; //!< Number of assets in the directory
  uint8_t reserved[24]; //!< Reserved for future use, must be zero
  uint64_t pak_hash; //!< Hash of entire file for integrity
  char footer_magic[8]; //!< Footer magic: {'O','X','P','A','K','E','N','D'}
};
#pragma pack(pop)
static_assert(sizeof(PakFooter) == 64, "PAK Footer must be 64 bytes");

//! Reader for Oxygen .pak asset containers.
/*! Provides read-only access to asset directory and data in a .pak file.

  @see AssetDirectoryEntry, AssetKey
*/
class PakFile {
public:
  using Reader = oxygen::serio::Reader<oxygen::serio::FileStream<>>;

  //! 8-byte header magic: {'O','X','P','A','K',0,0,0}
  static constexpr std::array<uint8_t, 8> kHeaderMagic
    = { 'O', 'X', 'P', 'A', 'K', 0, 0, 0 };

  //! 8-byte footer magic: {'O','X','P','A','K','E','N','D'}
  static constexpr std::array<uint8_t, 8> kFooterMagic
    = { 'O', 'X', 'P', 'A', 'K', 'E', 'N', 'D' };

  //! Open a .pak file for reading.
  OXGN_CNTT_API explicit PakFile(const std::filesystem::path& path);

  ~PakFile() = default;

  OXYGEN_MAKE_NON_COPYABLE(PakFile)
  OXYGEN_DEFAULT_MOVABLE(PakFile)

  //! Find a directory entry by asset key.
  OXGN_CNTT_NDAPI auto FindEntry(const AssetKey& key) const noexcept
    -> std::optional<AssetDirectoryEntry>;

  //! Get the full asset directory.
  OXGN_CNTT_NDAPI auto Directory() const noexcept
    -> std::span<const AssetDirectoryEntry>;

  //! Create a Reader positioned at the asset's data.
  OXGN_CNTT_NDAPI auto CreateReader(const AssetDirectoryEntry& entry) const
    -> Reader;

  //! Get the dependencies for a given asset directory entry.
  OXGN_CNTT_NDAPI auto Dependencies(const AssetDirectoryEntry& entry) const
    -> std::vector<AssetKey>;

private:
  void ReadHeader(oxygen::serio::FileStream<>* stream);
  void ReadFooter(oxygen::serio::FileStream<>* stream);
  void ReadDirectory(oxygen::serio::FileStream<>* stream, uint32_t asset_count);
  auto ReadDirectoryEntry(Reader& reader) -> void;

  PakHeader header_ {};
  PakFooter footer_ {};
  std::unique_ptr<oxygen::serio::FileStream<>> stream_;
  std::vector<AssetDirectoryEntry> directory_;
  mutable std::mutex mutex_;
  mutable std::unordered_map<AssetKey, size_t> key_to_index_;
};

} // namespace oxygen::content
