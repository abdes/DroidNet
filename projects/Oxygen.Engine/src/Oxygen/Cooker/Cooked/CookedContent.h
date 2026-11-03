//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::cooked {

//! Where some bytes of a cooked source are stored: a range of one of its
//! files. A pak build copies them verbatim from there.
struct StoredBytes final {
  std::filesystem::path file;
  serio::ByteRange range {};

  auto operator==(const StoredBytes&) const -> bool = default;
};

//! An asset of a cooked source: a typed descriptor at a virtual path.
struct CookedAsset final {
  data::AssetKey key;
  data::AssetType type = data::AssetType::kUnknown;
  //! Its virtual path, `/<Mount>/...`; empty when a pak's browse index does
  //! not list it.
  std::string virtual_path;
  //! The keys and resources its descriptor binds.
  data::AssetReferences references;
  serio::ByteCount descriptor_size {};
  //! The SHA-256 of its descriptor, as the loose index or the pak catalog
  //! records it.
  base::Sha256Digest descriptor_sha256 {};
  //! The virtual path of the source file that produced it; empty in a pak.
  std::string source_path;
};

//! A virtual path bound to a texture row: a texture descriptor.
struct NamedTexture final {
  std::string virtual_path;
  ResourceIndexT row { 0U };
  //! The virtual path of the source file that produced it; empty in a pak.
  std::string source_path;
};

//! A record that names a resource row from outside the tables, such as a
//! buffer's `.obuf`. Only loose roots hold sidecars; a pak build reads them.
struct CookedSidecar final {
  //! Its name, the path below the loose root, such as `Buffers/Mesh.obuf`.
  std::string name;
  //! The virtual path of the source file that produced it.
  std::string source_path;
  StoredBytes stored;
};

//! The resource tables of a cooked source.
enum class TableKind : uint8_t {
  kTexture,
  kBuffer,
  kScript,
  kPhysics,
  //! Audio rows; only paks hold them, and no binding names them.
  kAudio,
};

//! The table of the resources a binding of `kind` names; `kExternalTexture`
//! has none.
OXGN_COOK_NDAPI auto TableOf(data::ResourceKind kind) -> TableKind;

//! The rows of one resource table of a cooked source, and their payloads.
/*!
 Row zero is the reserved empty row. A source with no table of a kind has an
 empty table of it: no rows at all.
*/
class CookedResourceTable final {
public:
  //! An empty table of `kind`.
  explicit CookedResourceTable(TableKind kind) noexcept
    : kind_(kind)
  {
  }

  //! A table whose rows and payloads are stored as `rows` and `data`, read
  //! from `device` when it is set (a pak), else through `files`.
  OXGN_COOK_API CookedResourceTable(TableKind kind,
    const CookerStorageReader& storage,
    observer_ptr<const serio::AnyDevice> device, StoredBytes rows,
    StoredBytes data);

  [[nodiscard]] auto Kind() const noexcept -> TableKind { return kind_; }

  //! The number of rows, row zero included; zero for an absent table.
  [[nodiscard]] auto RowCount() const noexcept -> uint32_t
  {
    return row_count_;
  }

  //! The size of one row: the size of the kind's resource descriptor.
  OXGN_COOK_NDAPI static auto RowSize(TableKind kind) -> size_t;

  //! The bytes of row `row`; throws `std::out_of_range` past the last row.
  OXGN_COOK_NDAPI auto RowBytes(ResourceIndexT row) const
    -> std::vector<std::byte>;

  //! Row `row` as the kind's resource descriptor `Desc`.
  template <class Desc>
  [[nodiscard]] auto Row(const ResourceIndexT row) const -> Desc
  {
    const auto bytes = RowBytes(row);
    if (bytes.size() != sizeof(Desc)) {
      throw std::invalid_argument("the descriptor is not of this table's kind");
    }
    const auto desc = serio::CopyAt<Desc>(bytes);
    if (!desc) {
      throw std::invalid_argument("the descriptor is not of this table's kind");
    }
    return *desc;
  }

  //! Where the payload of row `row` is stored; throws `std::out_of_range`
  //! when it lies outside the table's data.
  OXGN_COOK_NDAPI auto PayloadStored(ResourceIndexT row) const -> StoredBytes;

  //! The payload of row `row`.
  OXGN_COOK_NDAPI auto Payload(ResourceIndexT row) const
    -> std::vector<std::byte>;

  //! Where every row is stored, row zero included.
  [[nodiscard]] auto RowsStored() const noexcept -> const StoredBytes&
  {
    return rows_;
  }

  //! Where the payloads are stored, as one range.
  [[nodiscard]] auto DataStored() const noexcept -> const StoredBytes&
  {
    return data_;
  }

private:
  TableKind kind_;
  uint32_t row_count_ { 0U };
  observer_ptr<const CookerStorageReader> storage_;
  //! A pak's device; null for a loose root, whose files are opened per read.
  observer_ptr<const serio::AnyDevice> device_;
  StoredBytes rows_;
  StoredBytes data_;
};

//! What a cooked source provides, deletes and requires, read without loading
//! its assets.
struct CookedSummary final {
  data::CookedSourceKind kind = data::CookedSourceKind::kLooseCooked;
  data::SourceKey source_key {};
  //! The mounts its virtual paths start with, such as `Props`, sorted. A
  //! valid source has one; a source that holds nothing has none.
  std::vector<std::string> mounts;
  //! The assets it provides, with their types; a pak's after its deletions.
  std::map<data::AssetKey, data::AssetType> assets;
  //! The keys of the textures it provides (`TextureKeyFromVirtualPath`).
  std::set<data::AssetKey> textures;
  //! The lower assets a pak deletes, which nothing below it then provides.
  std::set<data::AssetKey> deleted;
  //! What it requires of other mounts. A loose root states none.
  std::vector<data::PakCatalogDependency> requirements;
  //! The virtual paths of its scripts whose source is external, sorted. A pak
  //! has none: PakTool seals scripts.
  std::vector<std::string> external_scripts;
};

//! The read view of one cooked source: a loose cooked root or a pak.
/*!
 It is opened once and never changes. A loose root and a pak answer every
 query the same way; where the bytes are stored is the source's own business.
 Opening checks that the source agrees with itself: a loose root's named
 textures against their table rows, a pak's header, footer, catalog and
 browse index.

 A pak stays open while the content lives. A loose root's storage are opened
 for each read and closed after it, so the import service rewrites them
 while a job holds the content; a read then sees the bytes of that moment.
*/
class CookedContent final {
public:
  //! Opens `source` through `files`, which outlive the content.
  /*!
   @throw data::CookedFormatError `kWrongFormat` for a folder without an index
     or a file that is not a pak; `kCorrupt` for one that cannot be read or
     disagrees with itself.
  */
  OXGN_COOK_NDAPI static auto Open(const CookerStorageReader& storage,
    const data::CookedSource& source) -> CookedContent;

  //! Opens `path`: a folder as a loose root, a file as a pak.
  OXGN_COOK_NDAPI static auto Open(const CookerStorageReader& storage,
    const std::filesystem::path& path) -> CookedContent;

  //! Whether `path` names something `Open` reads: a pak file or a folder
  //! holding a loose cooked index.
  OXGN_COOK_NDAPI static auto IsCookedSource(const CookerStorageReader& storage,
    const std::filesystem::path& path) noexcept -> bool;

  OXGN_COOK_API ~CookedContent();
  OXYGEN_MAKE_NON_COPYABLE(CookedContent)
  OXGN_COOK_API CookedContent(CookedContent&&) noexcept;
  OXGN_COOK_API auto operator=(CookedContent&&) noexcept -> CookedContent&;

  OXGN_COOK_NDAPI auto Source() const noexcept -> const data::CookedSource&;
  OXGN_COOK_NDAPI auto Summary() const noexcept -> const CookedSummary&;

  //! The assets it holds, by key; a pak's directory, before its deletions.
  OXGN_COOK_NDAPI auto Assets() const noexcept -> std::span<const CookedAsset>;

  //! The asset `key`, or null.
  OXGN_COOK_NDAPI auto FindAsset(const data::AssetKey& key) const noexcept
    -> const CookedAsset*;

  //! The asset at `virtual_path`, or null.
  OXGN_COOK_NDAPI auto FindAsset(std::string_view virtual_path) const noexcept
    -> const CookedAsset*;

  //! The descriptor bytes of the asset `key`; throws `std::out_of_range` when
  //! the source holds no such asset.
  OXGN_COOK_NDAPI auto Descriptor(const data::AssetKey& key) const
    -> std::vector<std::byte>;

  //! Where the descriptor of the asset `key` is stored.
  OXGN_COOK_NDAPI auto DescriptorStored(const data::AssetKey& key) const
    -> StoredBytes;

  //! The table of `kind`; empty when the source has none.
  OXGN_COOK_NDAPI auto ResourceTable(TableKind kind) const
    -> const CookedResourceTable&;

  //! The table the bindings of `kind` name; `kExternalTexture` has none.
  [[nodiscard]] auto ResourceTable(const data::ResourceKind kind) const
    -> const CookedResourceTable&
  {
    return ResourceTable(TableOf(kind));
  }

  //! Its named textures, by virtual path.
  OXGN_COOK_NDAPI auto NamedTextures() const noexcept
    -> std::span<const NamedTexture>;

  //! The texture named `virtual_path`, or null.
  OXGN_COOK_NDAPI auto FindNamedTexture(
    std::string_view virtual_path) const noexcept -> const NamedTexture*;

  //! Its sidecars, by name; a pak has none.
  OXGN_COOK_NDAPI auto Sidecars() const noexcept
    -> std::span<const CookedSidecar>;

  //! The pak catalog; null for a loose root.
  OXGN_COOK_NDAPI auto Catalog() const noexcept -> const data::PakCatalog*;

  //! Checks the stored bytes against what the source records of them: a
  //! loose root's member files against their indexed sizes and, with
  //! `kFull`, their digests, and each resource of its data files against the
  //! SHA-256 of its table row; a pak's CRC with `kFull`.
  /*!
   @throw std::runtime_error Naming the first file that disagrees.
  */
  OXGN_COOK_API auto VerifyStorage(lc::IntegrityCheck check) const -> void;

private:
  struct Impl;
  explicit CookedContent(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

//! Reads the summary of the cooked source `path` names without loading its
//! assets: a loose root's index, a pak's header, footer, browse index and
//! catalog. A loose root's script descriptors are read only for their
//! external source.
/*!
 @throw data::CookedFormatError As `CookedContent::Open`.
*/
OXGN_COOK_NDAPI auto ReadCookedSummary(const CookerStorageReader& storage,
  const std::filesystem::path& path) -> CookedSummary;

} // namespace oxygen::content::cooked
