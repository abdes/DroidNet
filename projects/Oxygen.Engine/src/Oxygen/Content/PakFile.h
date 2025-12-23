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
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/ResourceTable.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content {

//! Reader and accessor for Oxygen .pak asset containers
/*!
 PakFile provides read-only, thread-safe access to the asset directory, resource
 tables, and data regions of an Oxygen Engine .pak file. It supports type-safe
 resource table queries, asset directory lookups, and region-based data
 streaming for buffer and texture resources.

 ### Key Features

 - **Thread-Safe Reads**: Uses internal mutex for safe concurrent access.
 - **Type-Safe Resource Table Access**: Template-based queries for buffer and
   texture resource tables.
 - **Asset Directory Lookup**: Fast key-to-index mapping for asset queries.
 - **Region-Based Data Streaming**: Provides readers for buffer and texture data
   regions, aligned for efficient I/O.
 - **Format Versioning**: Exposes header and content version info.

 ### Usage Patterns

 ```cpp
 PakFile pak{"assets.pak"};
 auto entry = pak.FindEntry(key);
 auto& buffers = pak.BuffersTable();
 auto reader = pak.CreateReader(entry.value());
 ```

 ### Architecture Notes

 - Designed for bindless resource management and modular asset loading.
 - Integrates with ResourceTable and asset registry systems.
 - Only supports reading; writing is not implemented.

 @see oxygen::content::ResourceTable, data::pak::AssetDirectoryEntry,
      data::pak::PakHeader, data::pak::PakFooter
*/
class PakFile {
  // Internal: initialize resource tables if present in the footer
  auto InitBuffersTable() const -> void;
  auto InitTexturesTable() const -> void;

public:
  using Reader = serio::Reader<serio::FileStream<>>;

  //! Type alias for the buffer resource table.
  using BuffersTableT = ResourceTable<data::BufferResource>;

  //! Type alias for the texture resource table.
  using TexturesTableT = ResourceTable<data::TextureResource>;

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

  //=== File Information ===--------------------------------------------------//

  //! Get the path to the PAK file.
  auto FilePath() const noexcept -> const auto& { return file_path_; }

  //=== Asset Directory Access ===--------------------------------------------//

  //! Find a directory entry by asset key.
  OXGN_CNTT_NDAPI auto FindEntry(const data::AssetKey& key) const noexcept
    -> std::optional<data::pak::AssetDirectoryEntry>;

  //! Get the full asset directory.
  OXGN_CNTT_NDAPI auto Directory() const noexcept
    -> std::span<const data::pak::AssetDirectoryEntry>;

  //! Create a Reader positioned at the asset's data.
  OXGN_CNTT_NDAPI auto CreateReader(
    const data::pak::AssetDirectoryEntry& entry) const -> Reader;

  //=== Browse Index (Virtual Paths) ===-------------------------------------//

  //! Browse index entry mapping an AssetKey to a canonical virtual path.
  struct BrowseEntry {
    std::string virtual_path;
    data::AssetKey asset_key;
  };

  //! Check whether this pak contains an embedded browse index.
  OXGN_CNTT_NDAPI auto HasBrowseIndex() const noexcept -> bool;

  //! Get the embedded browse index entries.
  OXGN_CNTT_NDAPI auto BrowseIndex() const noexcept
    -> std::span<const BrowseEntry>;

  //! Resolve a virtual path to an AssetKey using the embedded browse index.
  OXGN_CNTT_NDAPI auto ResolveAssetKeyByVirtualPath(
    std::string_view virtual_path) const noexcept
    -> std::optional<data::AssetKey>;

  //=== Header Information ===-----------------------------------------------//

  //! Get the PAK format version from the header.
  OXGN_CNTT_NDAPI auto FormatVersion() const noexcept -> uint16_t;

  //! Get the PAK content version from the header.
  OXGN_CNTT_NDAPI auto ContentVersion() const noexcept -> uint16_t;

  //=== Resource Table Access ===---------------------------------------------//

  //! Get the resource table for buffers.
  OXGN_CNTT_NDAPI auto BuffersTable() const -> BuffersTableT&;

  //! Get the resource table for textures.
  OXGN_CNTT_NDAPI auto TexturesTable() const -> TexturesTableT&;

  //! Check if a resource table of the given type exists in the PAK file.
  template <PakResource T> auto HasTableOf() const -> bool
  {
    if constexpr (std::is_same_v<T, data::BufferResource>) {
      return static_cast<bool>(buffers_table_);
    } else if constexpr (std::is_same_v<T, data::TextureResource>) {
      return static_cast<bool>(textures_table_);
    } else {
      return false;
    }
  }

  //! Get the resource table for the specified resource type.
  /*!
    Returns a pointer to the ResourceTable for the specified resource type.
    Uses template specialization to return the appropriate table.

    @tparam T Resource type (must satisfy PakResource concept)
    @return Pointer to ResourceTable for type T, or nullptr if not present

    @see HasTableOf, BuffersTable, TexturesTable
  */
  template <PakResource T> auto GetResourceTable() const -> ResourceTable<T>*
  {
    if constexpr (std::is_same_v<T, data::BufferResource>) {
      return buffers_table_ ? &(*buffers_table_) : nullptr;
    } else if constexpr (std::is_same_v<T, data::TextureResource>) {
      return textures_table_ ? &(*textures_table_) : nullptr;
    } else {
      return nullptr;
    }
  }

  //=== Data Regions Access ===-----------------------------------------------//

  //! Create a Reader for the buffer data region.
  auto CreateBufferDataReader() const -> Reader;

  //! Create a Reader for the texture data region.
  auto CreateTextureDataReader() const -> Reader;

  //! Create a Reader for the data region of the specified resource type.
  /*!
    Returns a Reader positioned at the start of the data region for the
    specified resource type.

    @tparam T Resource type (must satisfy PakResource concept)
    @return Reader for the resource data region.
    @throw std::invalid_argument if resource type is unsupported.

    @see CreateBufferDataReader, CreateTextureDataReader
  */
  template <PakResource T> auto CreateDataReader() const -> Reader
  {
    if constexpr (std::is_same_v<T, data::BufferResource>) {
      return CreateBufferDataReader();
    } else if constexpr (std::is_same_v<T, data::TextureResource>) {
      return CreateTextureDataReader();
    } else {
      throw std::invalid_argument(
        "Unsupported resource type for CreateBufferDataReader");
    }
  }

private:
  auto ReadHeader(serio::FileStream<>* stream) -> void;
  auto ReadFooter(serio::FileStream<>* stream) -> void;
  auto ReadDirectory(serio::FileStream<>* stream, uint32_t asset_count) -> void;
  auto ReadDirectoryEntry(Reader& reader) -> void;
  auto ReadBrowseIndex(serio::FileStream<>* stream, size_t file_size) -> void;

  std::filesystem::path file_path_; // Path to the PAK file

  data::pak::PakHeader header_ {};
  data::pak::PakFooter footer_ {};

  //! Stream for reading the PAK file metadata (header, footer, directory,
  //! descriptor tables)
  std::unique_ptr<serio::FileStream<>> meta_stream_;

  //! Streams for reading the data from data regions (aligned)
  std::unique_ptr<serio::FileStream<>> buffer_data_stream_;
  std::unique_ptr<serio::FileStream<>> texture_data_stream_;

  std::vector<data::pak::AssetDirectoryEntry> directory_;
  mutable std::mutex mutex_;
  mutable std::unordered_map<data::AssetKey, size_t> key_to_index_;

  std::vector<BrowseEntry> browse_index_;
  std::unordered_map<std::string_view, data::AssetKey> browse_vpath_to_key_;

  // Resource table members (optional, only if present in the PAK file)
  mutable std::optional<BuffersTableT> buffers_table_;
  mutable std::optional<TexturesTableT> textures_table_;
};

} // namespace oxygen::content
