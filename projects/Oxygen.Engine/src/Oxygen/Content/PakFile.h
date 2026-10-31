//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/ResourceTable.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/AnyDevice.h>

namespace oxygen::serio {
class IoDriver;
} // namespace oxygen::serio

namespace oxygen::content {

//! Reader and accessor for Oxygen .pak asset containers
/*!
 PakFile reads a pak from one contiguous device, a mapped file or bytes in
 memory, and parses its metadata in place: the header, the footer, the asset
 directory, the embedded catalog and the browse index.
 Everything it holds is immutable once it is constructed, so concurrent reads
 need no lock. Descriptors and reference inventories are views or decodes of
 the mapped bytes.

 Payloads are not read through the mapping: `Payloads()` is the device a
 loader awaits them from. A pak opened with an IO driver opens its file a
 second time through the driver, so payload reads complete on the driver's
 loop without blocking it; any other pak serves its payloads from its one
 device, with awaited reads that complete at once (memory is ready at once; a
 mapped pak without a driver blocks the awaiting thread for each read).

 ```cpp
 PakFile pak{"assets.pak"};
 const auto entry = pak.FindEntry(key);
 const auto descriptor = pak.DescriptorBytes(*entry);
 ```

  @see oxygen::content::ResourceTable, data::pak::core::AssetDirectoryEntry,
      data::pak::core::PakHeader, data::pak::core::PakFooter
*/
class PakFile {
public:
  //! Type alias for the buffer resource table.
  using BuffersTableT = ResourceTable<data::BufferResource>;

  //! Type alias for the texture resource table.
  using TexturesTableT = ResourceTable<data::TextureResource>;
  //! Type alias for the script resource table.
  using ScriptsTableT = ResourceTable<data::ScriptResource>;
  //! Type alias for the physics resource table.
  using PhysicsTableT = ResourceTable<data::PhysicsResource>;

  //! Whether opening a pak checks its integrity.
  enum class Verification : uint8_t {
    //! Trust the bytes; `ValidateCrc32Integrity()` checks them later.
    kNone,
    //! Check the footer's CRC32 of the whole pak, in one sequential pass,
    //! before parsing anything past the header and footer, so the parse
    //! reads pages the pass already brought in. A pak with no CRC32 is
    //! refused (`CookedOpenFailure::kUnverifiable`).
    kCrc32,
  };

  //! Opens the pak at `path`, mapping it for its metadata; with `driver`, it
  //! also opens it through `driver` for its payloads.
  /*!
   The pak holds `driver`, which outlives the payload file it opened.

   @throw data::CookedOpenError When the file is not a pak, has a format
     version this build cannot read, is malformed, or fails `verification`.
   @throw std::system_error When the file cannot be opened.
  */
  OXGN_CNTT_API explicit PakFile(const std::filesystem::path& path,
    Verification verification = Verification::kNone,
    std::shared_ptr<serio::IoDriver> driver = {});

  //! Reads the pak `device` holds; `name` stands for its path in logs.
  /*!
   @throw std::invalid_argument When `device` is not contiguous.
   @throw data::CookedOpenError When the bytes are not a pak, have a format
     version this build cannot read, or are malformed.
   @throw data::CookedOpenError `kCorrupt` too when `verification` checks the
     CRC32 and it does not match.
  */
  OXGN_CNTT_API PakFile(serio::AnyDevice device, std::filesystem::path name,
    Verification verification = Verification::kNone);

  //! Checks a pak header: its magic, format version and source identity.
  /*!
   @throw data::CookedOpenError `kWrongFormat` for another magic,
     `kUnsupportedVersion` for another format version, `kCorrupt` for an
     invalid source identity.
  */
  OXGN_CNTT_API static auto CheckHeader(
    const data::pak::core::PakHeader& header) -> void;

  //! Checks a pak footer of a file of `file_size` bytes: its magic, and that
  //! the embedded catalog lies between the header and the footer.
  /*!
   @throw data::CookedOpenError `kCorrupt` when either check fails.
  */
  OXGN_CNTT_API static auto CheckFooter(
    const data::pak::core::PakFooter& footer, uint64_t file_size) -> void;

  ~PakFile() = default;

  OXYGEN_MAKE_NON_COPYABLE(PakFile)
  OXYGEN_MAKE_NON_MOVABLE(PakFile)

  //=== File Information ===--------------------------------------------------//

  //! Get the path to the PAK file.
  auto FilePath() const noexcept -> const std::filesystem::path&
  {
    return path_;
  }

  //! The contiguous device that holds the pak's metadata: its mapping, or
  //! the bytes it was opened from.
  auto Device() const noexcept -> const serio::AnyDevice& { return device_; }

  //! The device payloads are awaited from, with the offsets of `Device()`.
  auto Payloads() const noexcept -> const serio::AnyAsyncDevice&
  {
    return payloads_;
  }

  //! The validated footer.
  auto Footer() const noexcept -> const data::pak::core::PakFooter&
  {
    return footer_;
  }

  //! Validated physical layer metadata, including patch deletions and bases.
  auto Catalog() const noexcept -> const data::PakCatalog& { return catalog_; }

  //=== Asset Directory Access ===--------------------------------------------//

  //! Find a directory entry by asset key.
  OXGN_CNTT_NDAPI auto FindEntry(const data::AssetKey& key) const noexcept
    -> std::optional<data::pak::core::AssetDirectoryEntry>;

  //! Get the full asset directory.
  OXGN_CNTT_NDAPI auto Directory() const noexcept
    -> std::span<const data::pak::core::AssetDirectoryEntry>;

  //! The descriptor bytes of `entry`, a view of the mapped pak that is valid
  //! while this PakFile lives.
  /*!
   @throw std::out_of_range When `entry` lies outside the file.
  */
  OXGN_CNTT_NDAPI auto DescriptorBytes(
    const data::pak::core::AssetDirectoryEntry& entry) const
    -> std::span<const std::byte>;

  //! Decodes the reference inventory of the asset `key`.
  OXGN_CNTT_NDAPI auto ReadAssetReferences(const data::AssetKey& key) const
    -> data::AssetReferences;

  //=== Browse Index (Virtual Paths) ===-------------------------------------//

  //! Browse index entry mapping an AssetKey to a canonical virtual path.
  struct BrowseEntry {
    std::string virtual_path;
    data::AssetKey asset_key;
  };

  //! Browse index entry mapping a texture descriptor's virtual path to its
  //! texture table index.
  struct BrowseTextureEntry {
    std::string virtual_path;
    data::pak::core::ResourceIndexT texture_index;
  };

  //! The asset and texture entries of a browse index.
  struct BrowseContents {
    std::vector<BrowseEntry> entries;
    std::vector<BrowseTextureEntry> textures;
  };

  //! Decodes a browse index whose textures index a texture table of
  //! `texture_count` rows; an unreadable one throws `data::CookedOpenError`.
  OXGN_CNTT_NDAPI static auto DecodeBrowseIndex(
    std::span<const std::byte> bytes, uint32_t texture_count) -> BrowseContents;

  //! Get the embedded browse index entries.
  OXGN_CNTT_NDAPI auto BrowseIndex() const noexcept
    -> std::span<const BrowseEntry>;

  //! Resolve a virtual path to an AssetKey using the embedded browse index.
  OXGN_CNTT_NDAPI auto ResolveAssetKeyByVirtualPath(
    std::string_view virtual_path) const noexcept
    -> std::optional<data::AssetKey>;

  //! Get the embedded browse index texture entries.
  OXGN_CNTT_NDAPI auto BrowseTextures() const noexcept
    -> std::span<const BrowseTextureEntry>;

  //! Resolve a texture descriptor's virtual path to its texture table index.
  OXGN_CNTT_NDAPI auto ResolveTextureIndexByVirtualPath(
    std::string_view virtual_path) const noexcept
    -> std::optional<data::pak::core::ResourceIndexT>;

  //=== Header Information ===-----------------------------------------------//

  //! Get the PAK format version from the header.
  OXGN_CNTT_NDAPI auto FormatVersion() const noexcept -> uint16_t;

  //! Get the PAK content version from the header.
  OXGN_CNTT_NDAPI auto ContentVersion() const noexcept -> uint16_t;

  //! Get the PAK GUID from the header.
  OXGN_CNTT_NDAPI auto Guid() const noexcept -> data::SourceKey;

  //! Validate the PAK CRC32 integrity hash, if present.
  /*!
   If the footer's `pak_crc32` is zero, validation is skipped.

   @throw data::CookedOpenError `kCorrupt` when the computed CRC32 does not
     match the footer.
   @throw std::system_error When the file cannot be read.
  */
  OXGN_CNTT_API auto ValidateCrc32Integrity() const -> void;

  //=== Resource Table Access ===---------------------------------------------//

  //! Get the resource table for buffers.
  OXGN_CNTT_NDAPI auto BuffersTable() const -> BuffersTableT&;

  //! Get the resource table for textures.
  OXGN_CNTT_NDAPI auto TexturesTable() const -> TexturesTableT&;
  //! Get the resource table for script resources.
  OXGN_CNTT_NDAPI auto ScriptsTable() const -> ScriptsTableT&;
  //! Get the resource table for physics resources.
  OXGN_CNTT_NDAPI auto PhysicsTable() const -> PhysicsTableT&;

  //! Check if a resource table of the given type exists in the PAK file.
  //! The pak's bytes: the view of the contiguous device it was opened from.
  [[nodiscard]] auto Bytes() const noexcept -> std::span<const std::byte>
  {
    return bytes_;
  }

  template <PakResource T> auto HasTableOf() const -> bool
  {
    if constexpr (std::is_same_v<T, data::BufferResource>) {
      return static_cast<bool>(buffers_table_);
    } else if constexpr (std::is_same_v<T, data::TextureResource>) {
      return static_cast<bool>(textures_table_);
    } else if constexpr (std::is_same_v<T, data::ScriptResource>) {
      return static_cast<bool>(scripts_table_);
    } else if constexpr (std::is_same_v<T, data::PhysicsResource>) {
      return static_cast<bool>(physics_table_);
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
    } else if constexpr (std::is_same_v<T, data::ScriptResource>) {
      return scripts_table_ ? &(*scripts_table_) : nullptr;
    } else if constexpr (std::is_same_v<T, data::PhysicsResource>) {
      return physics_table_ ? &(*physics_table_) : nullptr;
    } else {
      return nullptr;
    }
  }

private:
  PakFile(serio::AnyDevice device, std::filesystem::path name,
    Verification verification, std::shared_ptr<serio::IoDriver> driver,
    std::optional<serio::AnyAsyncDevice> payloads);

  auto InitializeResourceTablesFromFooter() const -> void;

  std::filesystem::path path_;

  //! Declared before the devices: the driver outlives the file it opened.
  std::shared_ptr<serio::IoDriver> driver_;

  //! The device and its bytes, which the descriptor views handed out alias.
  //! Keep `bytes_` and `payloads_` after `device_`: they may view it.
  serio::AnyDevice device_;
  std::span<const std::byte> bytes_;
  serio::AnyAsyncDevice payloads_;

  data::pak::core::PakHeader header_ {};
  data::SourceKey source_key_ {};
  data::pak::core::PakFooter footer_ {};
  data::PakCatalog catalog_ {};

  std::vector<data::pak::core::AssetDirectoryEntry> directory_;
  std::unordered_map<data::AssetKey, size_t> key_to_index_;

  std::vector<BrowseEntry> browse_index_;
  std::unordered_map<std::string_view, data::AssetKey> browse_vpath_to_key_;
  std::vector<BrowseTextureEntry> browse_textures_;

  // Resource table members (optional, only if present in the PAK file)
  mutable std::optional<BuffersTableT> buffers_table_;
  mutable std::optional<TexturesTableT> textures_table_;
  mutable std::optional<ScriptsTableT> scripts_table_;
  mutable std::optional<PhysicsTableT> physics_table_;
};

} // namespace oxygen::content
