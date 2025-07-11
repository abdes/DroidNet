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

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/ResourceTable.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content {

//! Reader for Oxygen .pak asset containers.
/*! Provides read-only access to asset directory and data in a .pak file.

  @see AssetDirectoryEntry, AssetKey
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

  //=== Header Information ===-----------------------------------------------//

  //! Get the PAK format version from the header.
  /*!
    @return Format version number from the PAK header.
  */
  OXGN_CNTT_NDAPI auto FormatVersion() const noexcept -> uint16_t;

  //! Get the PAK content version from the header.
  /*!
    @return Content version number from the PAK header.
  */
  OXGN_CNTT_NDAPI auto ContentVersion() const noexcept -> uint16_t;

  //=== Resource Table Access ===---------------------------------------------//

  //! Get the resource table for buffers.
  /*!
    Returns a ResourceTable for buffer resources using the built-in loader.
    @return ResourceTable for buffers, or std::nullopt if not present.
    @see ResourceTable
  */
  OXGN_CNTT_NDAPI auto BuffersTable() const -> BuffersTableT&;

  //! Get the resource table for textures.
  /*!
    Returns a ResourceTable for texture resources using the built-in loader.
    @return ResourceTable for textures, or std::nullopt if not present.
    @see ResourceTable
  */
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

   @tparam T The resource type (must satisfy PakResource concept)
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

  auto CreateBufferDataReader() const -> Reader;
  auto CreateTextureDataReader() const -> Reader;

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

  // Resource table members (optional, only if present in the PAK file)
  mutable std::optional<BuffersTableT> buffers_table_;
  mutable std::optional<TexturesTableT> textures_table_;
};

} // namespace oxygen::content
