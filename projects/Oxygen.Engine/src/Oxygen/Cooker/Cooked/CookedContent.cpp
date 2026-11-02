//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/LooseCookedResources.h>
#include <Oxygen/Data/PakFormat_audio.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ResourceDescriptor.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Integrity.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::cooked {

namespace {

  namespace fs = std::filesystem;
  namespace core = data::pak::core;
  using data::CookedDefect;
  using data::CookedOpenError;
  using data::loose_cooked::FileKind;
  using lc::FileIntegrity;
  using lc::LooseCookedIndex;
  using oxygen::data::loose_cooked::kIndexFileName;

  //! Every table kind, in table order.
  constexpr auto kTableKinds = std::array {
    TableKind::kTexture,
    TableKind::kBuffer,
    TableKind::kScript,
    TableKind::kPhysics,
    TableKind::kAudio,
  };

  auto TableSlot(const TableKind kind) -> size_t
  {
    return static_cast<size_t>(kind);
  }

  auto Corrupt(const CookedDefect defect, const fs::path& path,
    const std::string_view what) -> CookedOpenError
  {
    return { defect, "'" + path.generic_string() + "': " + std::string(what) };
  }

  auto Unreadable(const fs::path& path) -> CookedOpenError
  {
    return Corrupt(CookedDefect::kTruncated, path, "cannot be read");
  }

  auto Size(const serio::AnyDevice& device, const fs::path& path) -> uint64_t
  {
    const auto size = device.Size();
    if (!size) {
      throw Unreadable(path);
    }
    return size->get();
  }

  auto ReadAt(const serio::AnyDevice& device, const fs::path& path,
    const uint64_t offset, const std::span<std::byte> out) -> void
  {
    if (!serio::ReadExactlyAt(device, serio::ByteOffset { offset }, out)) {
      throw Unreadable(path);
    }
  }

  //! Opens `path` for one read; the device closes when it goes out of scope.
  auto OpenFile(const CookerStorageReader& storage, const fs::path& path)
    -> serio::AnyDevice
  {
    auto device = storage.Open(path);
    if (!device) {
      throw Unreadable(path);
    }
    return std::move(device).value();
  }

  //! Verifies each resource of a loose root's data files against the SHA-256
  //! of its table row; the tables themselves were verified as files.
  auto VerifyResources(const CookerStorageReader& storage, const fs::path& root,
    const std::span<const lc::FileIntegrity> inventory) -> void
  {
    for (const auto& table : inventory) {
      const auto data_kind = table.kind.has_value()
        ? data::loose_cooked::DataFileKindOf(*table.kind)
        : std::nullopt;
      if (!data_kind.has_value()) {
        continue;
      }
      const auto data_file = std::ranges::find(
        inventory, std::optional { *data_kind }, &lc::FileIntegrity::kind);
      if (data_file == inventory.end()) {
        continue;
      }
      const auto table_path = root / table.relative_path;
      const auto rows = serio::ReadAll(
        OpenFile(storage, table_path), serio::ByteCount { table.size });
      if (!rows) {
        throw Unreadable(table_path);
      }
      const auto payloads
        = [&] -> std::vector<data::loose_cooked::ResourcePayload> {
        try {
          return data::loose_cooked::ReadResourcePayloads(
            *table.kind, rows->Bytes());
        } catch (const CookedOpenError& error) {
          throw Corrupt(error.Defect(), table_path, error.what());
        }
      }();
      const auto data_path = root / data_file->relative_path;
      const auto failures = data::loose_cooked::VerifyResourcePayloads(
        payloads, OpenFile(storage, data_path));
      if (!failures.empty()) {
        const auto& first = failures.front();
        const auto out_of_bounds = first.reason
          == data::loose_cooked::ResourcePayloadFailure::kOutOfBounds;
        throw Corrupt(out_of_bounds ? CookedDefect::kOutOfBounds
                                    : CookedDefect::kDigestMismatch,
          data_path,
          "the resource of row " + std::to_string(first.row)
            + (out_of_bounds ? " lies outside the file"
                             : " does not have the SHA-256 of its table row"));
      }
    }
  }

  //! Fills `out` with the bytes at `offset` of `path`: from `device` when it
  //! is set (a pak), else from the file opened for this read.
  auto ReadStored(const CookerStorageReader& storage,
    const observer_ptr<const serio::AnyDevice> device, const fs::path& path,
    const uint64_t offset, const std::span<std::byte> out) -> void
  {
    if (device) {
      ReadAt(*device, path, offset, out);
      return;
    }
    ReadAt(OpenFile(storage, path), path, offset, out);
  }

  auto FileSize(const CookerStorageReader& storage, const fs::path& path)
    -> uint64_t
  {
    return Size(OpenFile(storage, path), path);
  }

  auto FileSha256(const CookerStorageReader& storage, const fs::path& path)
    -> std::optional<base::Sha256Digest>
  {
    auto device = storage.Open(path);
    if (!device) {
      return std::nullopt;
    }
    const auto digest = serio::ComputeSha256(*device);
    return digest ? std::optional(*digest) : std::nullopt;
  }

  //! The row of `kind` whose payload `row` holds: where it lies in its data.
  auto PayloadRange(const TableKind kind, const std::span<const std::byte> row)
    -> std::pair<uint64_t, uint64_t>
  {
    const auto read = [&]<class Desc> -> std::pair<uint64_t, uint64_t> {
      const auto desc = serio::CopyAt<Desc>(row);
      if (!desc) {
        throw std::invalid_argument("the row is shorter than its descriptor");
      }
      return { desc->data_offset, desc->size_bytes };
    };
    switch (kind) {
    case TableKind::kTexture:
      return read.operator()<core::TextureResourceDesc>();
    case TableKind::kBuffer:
      return read.operator()<core::BufferResourceDesc>();
    case TableKind::kScript:
      return read.operator()<data::pak::scripting::ScriptResourceDesc>();
    case TableKind::kPhysics:
      return read.operator()<data::pak::physics::PhysicsResourceDesc>();
    case TableKind::kAudio:
      return read.operator()<data::pak::audio::AudioResourceDesc>();
    }
    throw std::invalid_argument("unknown table kind");
  }

  auto AddMount(std::set<std::string>& mounts, const std::string_view path)
    -> void
  {
    if (const auto root = content::MountRootOf(path); !root.empty()) {
      mounts.emplace(root.substr(1));
    }
  }

  auto LoadIndex(const CookerStorageReader& storage, const fs::path& root)
    -> LooseCookedIndex
  {
    const auto index_path = root / kIndexFileName;
    if (storage.KindOf(index_path) != PathKind::kFile) {
      throw CookedOpenError(CookedDefect::kAbsent,
        "'" + root.generic_string() + "' holds no "
          + std::string(kIndexFileName));
    }
    auto bytes = ReadWholeFile(storage, index_path, kMaxMetadataBytes);
    if (!bytes) {
      throw Unreadable(index_path);
    }
    // The index takes the bytes it was read into; nothing is copied.
    return LooseCookedIndex::LoadFromBytes(std::move(*bytes));
  }

  auto OpenPak(const CookerStorageReader& storage, const fs::path& path)
    -> std::unique_ptr<PakFile>
  {
    auto device = storage.Open(path);
    if (!device) {
      throw Corrupt(CookedDefect::kAbsent, path, "cannot be opened as a pak");
    }
    return std::make_unique<PakFile>(std::move(device).value(), path);
  }

  //! Whether the script descriptor at `descriptor` names an external source.
  auto HasExternalSource(
    const CookerStorageReader& storage, const fs::path& descriptor) -> bool
  {
    auto device = storage.Open(descriptor);
    if (!device) {
      throw Unreadable(descriptor);
    }
    auto desc = data::pak::scripting::ScriptAssetDesc {};
    ReadAt(*device, descriptor, 0, std::as_writable_bytes(std::span(&desc, 1)));
    return desc.external_source_path_offset != 0;
  }

  auto LooseSummary(const CookerStorageReader& storage, const fs::path& root,
    const LooseCookedIndex& index) -> CookedSummary
  {
    auto summary = CookedSummary {
      .kind = data::CookedSourceKind::kLooseCooked,
      .source_key = index.Guid(),
      .mounts = {},
      .assets = {},
      .textures = {},
      .deleted = {},
      .requirements = {},
      .external_scripts = {},
    };
    auto mounts = std::set<std::string> {};
    auto scripts = std::set<std::string> {};
    for (const auto& key : index.GetAllAssetKeys()) {
      const auto virtual_path = index.FindVirtualPath(key).value_or("");
      AddMount(mounts, virtual_path);
      const auto type
        = static_cast<data::AssetType>(index.FindAssetType(key).value_or(0U));
      summary.assets.emplace(key, type);
      const auto relpath = index.FindDescriptorRelPath(key);
      if (type == data::AssetType::kScript && relpath
        && HasExternalSource(storage, root / *relpath)) {
        scripts.emplace(virtual_path);
      }
    }
    for (const auto& file : index.GetFileInventory()) {
      AddMount(mounts, file.virtual_path);
      if (file.kind == FileKind::kTextureDescriptor) {
        summary.textures.insert(
          data::TextureKeyFromVirtualPath(file.virtual_path));
      }
    }
    summary.mounts.assign(mounts.begin(), mounts.end());
    summary.external_scripts.assign(scripts.begin(), scripts.end());
    return summary;
  }

  auto PakSummary(const PakFile& pak) -> CookedSummary
  {
    const auto& catalog = pak.Catalog();
    auto summary = CookedSummary {
      .kind = data::CookedSourceKind::kPak,
      .source_key = catalog.source_key,
      .mounts = { catalog.mount },
      .assets = {},
      .textures = {},
      .deleted = { catalog.deleted.begin(), catalog.deleted.end() },
      .requirements = catalog.dependencies,
      .external_scripts = {},
    };
    for (const auto& entry : catalog.entries) {
      summary.assets.emplace(entry.asset_key, entry.asset_type);
    }
    for (const auto& texture : pak.BrowseTextures()) {
      summary.textures.insert(
        data::TextureKeyFromVirtualPath(texture.virtual_path));
    }
    return summary;
  }

  auto IsFolder(const CookerStorageReader& storage, const fs::path& path)
    -> bool
  {
    return storage.KindOf(path) == PathKind::kFolder;
  }

} // namespace

auto TableOf(const data::ResourceKind kind) -> TableKind
{
  switch (kind) {
  case data::ResourceKind::kTexture:
    return TableKind::kTexture;
  case data::ResourceKind::kBuffer:
    return TableKind::kBuffer;
  case data::ResourceKind::kScript:
    return TableKind::kScript;
  case data::ResourceKind::kPhysics:
    return TableKind::kPhysics;
  case data::ResourceKind::kExternalTexture:
    break;
  }
  throw std::invalid_argument("the resource kind has no table");
}

//=== CookedResourceTable ===-------------------------------------------------//

CookedResourceTable::CookedResourceTable(const TableKind kind,
  const CookerStorageReader& storage,
  const observer_ptr<const serio::AnyDevice> device, StoredBytes rows,
  StoredBytes data)
  : kind_(kind)
  , storage_(make_observer(&storage))
  , device_(device)
  , rows_(std::move(rows))
  , data_(std::move(data))
{
  const auto row_size = RowSize(kind);
  if (rows_.range.count.get() % row_size != 0U) {
    throw Corrupt(CookedDefect::kWrongRowSize, rows_.file,
      "the resource table holds a partial row");
  }
  row_count_ = static_cast<uint32_t>(rows_.range.count.get() / row_size);
}

auto CookedResourceTable::RowSize(const TableKind kind) -> size_t
{
  switch (kind) {
  case TableKind::kTexture:
    return sizeof(core::TextureResourceDesc);
  case TableKind::kBuffer:
    return sizeof(core::BufferResourceDesc);
  case TableKind::kScript:
    return sizeof(data::pak::scripting::ScriptResourceDesc);
  case TableKind::kPhysics:
    return sizeof(data::pak::physics::PhysicsResourceDesc);
  case TableKind::kAudio:
    return sizeof(data::pak::audio::AudioResourceDesc);
  }
  throw std::invalid_argument("unknown table kind");
}

auto CookedResourceTable::RowBytes(const ResourceIndexT row) const
  -> std::vector<std::byte>
{
  if (row.get() >= row_count_) {
    throw std::out_of_range("the resource table has no such row");
  }
  const auto row_size = RowSize(kind_);
  auto bytes = std::vector<std::byte>(row_size);
  ReadStored(*storage_, device_, rows_.file,
    rows_.range.offset.get() + (static_cast<uint64_t>(row.get()) * row_size),
    bytes);
  return bytes;
}

auto CookedResourceTable::PayloadStored(const ResourceIndexT row) const
  -> StoredBytes
{
  const auto [offset, size] = PayloadRange(kind_, RowBytes(row));
  const auto begin = data_.range.offset.get();
  const auto end = begin + data_.range.count.get();
  // A pak's rows count from the start of the pak, a loose root's from the
  // start of the data file, where its data range starts.
  if (offset < begin || offset > end || size > end - offset) {
    throw std::out_of_range("the payload lies outside the table's data");
  }
  return {
    .file = data_.file,
    .range = { .offset = serio::ByteOffset { offset },
      .count = serio::ByteCount { size }, },
  };
}

auto CookedResourceTable::Payload(const ResourceIndexT row) const
  -> std::vector<std::byte>
{
  const auto stored = PayloadStored(row);
  auto bytes = std::vector<std::byte>(stored.range.count.get());
  if (!bytes.empty()) {
    ReadStored(
      *storage_, device_, stored.file, stored.range.offset.get(), bytes);
  }
  return bytes;
}

//=== CookedContent ===-------------------------------------------------------//

struct CookedContent::Impl {
  explicit Impl(
    const CookerStorageReader& storage_in, data::CookedSource source_in)
    : storage(make_observer(&storage_in))
    , source(std::move(source_in))
  {
  }

  observer_ptr<const CookerStorageReader> storage;
  data::CookedSource source;
  CookedSummary summary;
  //! Sorted by key.
  std::vector<CookedAsset> assets;
  //! Parallel to `assets`.
  std::vector<StoredBytes> descriptors;
  std::map<std::string, size_t, std::less<>> by_virtual_path;
  //! Sorted by virtual path.
  std::vector<NamedTexture> named_textures;
  //! Sorted by name.
  std::vector<CookedSidecar> sidecars;
  std::vector<CookedResourceTable> tables;
  //! The open pak, which owns the pak's device.
  std::unique_ptr<PakFile> pak;

  //! A loose root's member files and what the index records of them.
  std::vector<FileIntegrity> inventory;

  //! The pak's device; null for a loose root.
  [[nodiscard]] auto PakDevice() const -> observer_ptr<const serio::AnyDevice>
  {
    return pak ? make_observer(&pak->Device())
               : observer_ptr<const serio::AnyDevice> {};
  }

  auto LoadLoose() -> void;
  auto LoadLooseAssets(const LooseCookedIndex& index) -> void;
  auto LoadLooseTables(const LooseCookedIndex& index) -> void;
  auto LoadLooseNamedFiles(const CookedResourceTable& textures) -> void;
  auto LoadPak() -> void;
  auto LoadPakAssets() -> void;
  auto LoadPakTables() -> void;
  auto LoadPakNamedTextures() -> void;
  auto IndexAssets() -> void;
  auto VerifyLooseFiles(lc::IntegrityCheck check) const -> void;
  auto VerifyLooseDescriptors(lc::IntegrityCheck check) const -> void;
};

auto CookedContent::Impl::LoadLoose() -> void
{
  const auto& root = source.path;
  const auto index = LoadIndex(*storage, root);
  summary = LooseSummary(*storage, root, index);
  inventory = index.GetFileInventory();
  LoadLooseAssets(index);
  LoadLooseTables(index);
  LoadLooseNamedFiles(tables.at(TableSlot(TableKind::kTexture)));
}

auto CookedContent::Impl::LoadLooseAssets(const LooseCookedIndex& index) -> void
{
  const auto& root = source.path;
  for (const auto& key : index.GetAllAssetKeys()) {
    const auto relpath = index.FindDescriptorRelPath(key);
    if (!relpath) {
      throw Corrupt(
        CookedDefect::kMissingPart, root, "an asset has no descriptor");
    }
    const auto file = root / fs::path(std::string(*relpath));
    auto size = index.FindDescriptorSize(key).value_or(0U);
    if (size == 0U) {
      size = FileSize(*storage, file);
    }
    auto digest = base::Sha256Digest {};
    if (const auto sha = index.FindDescriptorSha256(key)) {
      std::ranges::copy(*sha, digest.begin());
    }
    if (digest == base::Sha256Digest {}) {
      const auto computed = FileSha256(*storage, file);
      if (!computed) {
        throw Unreadable(file);
      }
      digest = *computed;
    }
    assets.push_back({
      .key = key,
      .type
      = static_cast<data::AssetType>(index.FindAssetType(key).value_or(0U)),
      .virtual_path = std::string(index.FindVirtualPath(key).value_or("")),
      .references
      = index.FindAssetReferences(key).value_or(data::AssetReferences {}),
      .descriptor_size = serio::ByteCount { size },
      .descriptor_sha256 = digest,
      .source_path = std::string(index.FindSourcePath(key).value_or("")),
    });
    descriptors.push_back({
      .file = file,
      .range = { .offset = serio::ByteOffset { 0 },
        .count = serio::ByteCount { size }, },
    });
  }
}

auto CookedContent::Impl::LoadLooseTables(const LooseCookedIndex& index) -> void
{
  const auto& root = source.path;
  const auto table_files = std::array {
    std::pair { FileKind::kTexturesTable, FileKind::kTexturesData },
    std::pair { FileKind::kBuffersTable, FileKind::kBuffersData },
    std::pair { FileKind::kScriptsTable, FileKind::kScriptsData },
    std::pair { FileKind::kPhysicsTable, FileKind::kPhysicsData },
  };
  for (size_t slot = 0; slot < kTableKinds.size(); ++slot) {
    const auto kind = kTableKinds.at(slot);
    if (slot >= table_files.size()) {
      // A loose root holds no audio rows.
      tables.emplace_back(kind);
      continue;
    }
    const auto table_relpath
      = index.FindFileRelPath(table_files.at(slot).first);
    if (!table_relpath) {
      tables.emplace_back(kind);
      continue;
    }
    const auto table_file = root / fs::path(std::string(*table_relpath));
    auto rows = StoredBytes {
      .file = table_file,
      .range = { .offset = serio::ByteOffset { 0 },
        .count = serio::ByteCount { FileSize(*storage, table_file) }, },
    };
    auto data = StoredBytes {};
    if (const auto data_relpath
      = index.FindFileRelPath(table_files.at(slot).second)) {
      data.file = root / fs::path(std::string(*data_relpath));
      data.range = {
        .offset = serio::ByteOffset { 0 },
        .count = serio::ByteCount { FileSize(*storage, data.file) },
      };
    }
    tables.emplace_back(kind, *storage, observer_ptr<const serio::AnyDevice> {},
      std::move(rows), std::move(data));
  }
}

auto CookedContent::Impl::LoadLooseNamedFiles(
  const CookedResourceTable& textures) -> void
{
  const auto& root = source.path;
  for (const auto& file : inventory) {
    if (file.kind == FileKind::kTextureDescriptor) {
      const auto path = root / file.relative_path;
      const auto device = OpenFile(*storage, path);
      if (Size(device, path) != data::kTextureResourceDescriptorSize) {
        throw Corrupt(
          CookedDefect::kSizeMismatch, path, "is not a texture descriptor");
      }
      auto bytes = std::vector<std::byte>(data::kTextureResourceDescriptorSize);
      ReadAt(device, path, 0, bytes);
      const auto decoded = data::DecodeTextureResourceDescriptor(bytes);
      if (!decoded) {
        throw Corrupt(decoded.error().Defect(), path, decoded.error().what());
      }
      const auto row_in_table = decoded->index != core::kNoResourceIndex
        && decoded->index.get() < textures.RowCount();
      if (!row_in_table) {
        throw Corrupt(CookedDefect::kOutOfBounds, path,
          "names a row outside the texture table");
      }
      const auto row = textures.RowBytes(decoded->index);
      if (std::memcmp(row.data(), &decoded->descriptor, row.size()) != 0) {
        throw Corrupt(CookedDefect::kInconsistent, path,
          "disagrees with its texture table row");
      }
      named_textures.push_back({
        .virtual_path = file.virtual_path,
        .row = decoded->index,
        .source_path = file.source_path,
      });
    } else if (file.kind == FileKind::kAuxiliary) {
      const auto path = root / file.relative_path;
      sidecars.push_back({
        .name = file.relative_path,
        .source_path = file.source_path,
        .stored = { .file = path,
          .range = { .offset = serio::ByteOffset { 0 },
            .count = serio::ByteCount { file.size }, }, },
      });
    }
  }
}

auto CookedContent::Impl::LoadPak() -> void
{
  pak = OpenPak(*storage, source.path);
  summary = PakSummary(*pak);
  LoadPakAssets();
  LoadPakTables();
  LoadPakNamedTextures();
}

auto CookedContent::Impl::LoadPakAssets() -> void
{
  const auto& catalog = pak->Catalog();

  auto paths = std::map<data::AssetKey, std::string> {};
  for (const auto& entry : pak->BrowseIndex()) {
    paths.insert_or_assign(entry.asset_key, entry.virtual_path);
  }
  for (const auto& entry : pak->Directory()) {
    const auto listed = std::ranges::find(
      catalog.entries, entry.asset_key, &data::PakCatalogEntry::asset_key);
    if (listed == catalog.entries.end()) {
      throw Corrupt(CookedDefect::kInconsistent, source.path,
        "the catalog does not list a pak asset");
    }
    const auto path = paths.find(entry.asset_key);
    assets.push_back({
      .key = entry.asset_key,
      .type = entry.asset_type,
      .virtual_path = path == paths.end() ? std::string {} : path->second,
      .references = pak->ReadAssetReferences(entry.asset_key),
      .descriptor_size = serio::ByteCount { entry.desc_size },
      .descriptor_sha256 = listed->descriptor_digest,
      .source_path = {},
    });
    descriptors.push_back({
      .file = source.path,
      .range = { .offset = serio::ByteOffset { entry.desc_offset },
        .count = serio::ByteCount { entry.desc_size }, },
    });
  }
}

auto CookedContent::Impl::LoadPakTables() -> void
{
  const auto& footer = pak->Footer();
  const auto layout = std::array {
    std::pair { &footer.texture_table, &footer.texture_region },
    std::pair { &footer.buffer_table, &footer.buffer_region },
    std::pair { &footer.script_resource_table, &footer.script_region },
    std::pair { &footer.physics_resource_table, &footer.physics_region },
    std::pair { &footer.audio_table, &footer.audio_region },
  };
  for (size_t slot = 0; slot < kTableKinds.size(); ++slot) {
    const auto kind = kTableKinds.at(slot);
    const auto& [table, region] = layout.at(slot);
    if (table->count == 0U) {
      tables.emplace_back(kind);
      continue;
    }
    if (table->entry_size != CookedResourceTable::RowSize(kind)) {
      throw Corrupt(CookedDefect::kWrongRowSize, source.path,
        "a resource table has an unknown row size");
    }
    tables.emplace_back(kind, *storage, make_observer(&pak->Device()),
      StoredBytes {
        .file = source.path,
        .range = { .offset = serio::ByteOffset { table->offset },
          .count = serio::ByteCount { static_cast<uint64_t>(table->count)
            * table->entry_size }, },
      },
      StoredBytes {
        .file = source.path,
        .range = { .offset = serio::ByteOffset { region->offset },
          .count = serio::ByteCount { region->size }, },
      });
  }
}

auto CookedContent::Impl::LoadPakNamedTextures() -> void
{
  for (const auto& texture : pak->BrowseTextures()) {
    named_textures.push_back({
      .virtual_path = texture.virtual_path,
      .row = texture.texture_index,
      .source_path = {},
    });
  }
}

auto CookedContent::Impl::IndexAssets() -> void
{
  auto order = std::vector<size_t>(assets.size());
  for (size_t i = 0; i < order.size(); ++i) {
    order.at(i) = i;
  }
  std::ranges::sort(order, [this](const size_t lhs, const size_t rhs) -> bool {
    return assets.at(lhs).key < assets.at(rhs).key;
  });
  auto sorted_assets = std::vector<CookedAsset> {};
  auto sorted_descriptors = std::vector<StoredBytes> {};
  for (const auto i : order) {
    sorted_assets.push_back(std::move(assets.at(i)));
    sorted_descriptors.push_back(std::move(descriptors.at(i)));
  }
  assets = std::move(sorted_assets);
  descriptors = std::move(sorted_descriptors);
  for (size_t i = 0; i < assets.size(); ++i) {
    if (!assets.at(i).virtual_path.empty()) {
      by_virtual_path.insert_or_assign(assets.at(i).virtual_path, i);
    }
  }
  std::ranges::sort(named_textures, {}, &NamedTexture::virtual_path);
  std::ranges::sort(sidecars, {}, &CookedSidecar::name);
}

auto CookedContent::Open(const CookerStorageReader& storage,
  const data::CookedSource& source) -> CookedContent
{
  auto impl = std::make_unique<Impl>(storage, source);
  if (source.kind == data::CookedSourceKind::kLooseCooked) {
    impl->LoadLoose();
  } else {
    impl->LoadPak();
  }
  impl->IndexAssets();
  return CookedContent(std::move(impl));
}

auto CookedContent::Open(
  const CookerStorageReader& storage, const fs::path& path) -> CookedContent
{
  return Open(storage,
    data::CookedSource {
      .kind = IsFolder(storage, path) ? data::CookedSourceKind::kLooseCooked
                                      : data::CookedSourceKind::kPak,
      .path = path,
    });
}

auto CookedContent::IsCookedSource(
  const CookerStorageReader& storage, const fs::path& path) noexcept -> bool
{
  // Composing the index path can throw on a path the platform cannot convert;
  // such a path names no cooked source.
  try {
    const auto kind = storage.KindOf(path);
    return kind == PathKind::kFile
      || (kind == PathKind::kFolder
        && storage.KindOf(path / kIndexFileName) == PathKind::kFile);
  } catch (const std::exception&) {
    return false;
  }
}

CookedContent::CookedContent(std::unique_ptr<Impl> impl) noexcept
  : impl_(std::move(impl))
{
}

CookedContent::~CookedContent() = default;

CookedContent::CookedContent(CookedContent&&) noexcept = default;

auto CookedContent::operator=(CookedContent&&) noexcept
  -> CookedContent& = default;

auto CookedContent::Source() const noexcept -> const data::CookedSource&
{
  return impl_->source;
}

auto CookedContent::Summary() const noexcept -> const CookedSummary&
{
  return impl_->summary;
}

auto CookedContent::Assets() const noexcept -> std::span<const CookedAsset>
{
  return impl_->assets;
}

auto CookedContent::FindAsset(const data::AssetKey& key) const noexcept
  -> const CookedAsset*
{
  const auto it = std::ranges::lower_bound(
    impl_->assets, key, std::less {}, &CookedAsset::key);
  return it != impl_->assets.end() && it->key == key ? std::addressof(*it)
                                                     : nullptr;
}

auto CookedContent::FindAsset(
  const std::string_view virtual_path) const noexcept -> const CookedAsset*
{
  const auto it = impl_->by_virtual_path.find(virtual_path);
  return it == impl_->by_virtual_path.end()
    ? nullptr
    : std::addressof(impl_->assets.at(it->second));
}

auto CookedContent::DescriptorStored(const data::AssetKey& key) const
  -> StoredBytes
{
  const auto* asset = FindAsset(key);
  if (asset == nullptr) {
    throw std::out_of_range("the cooked source holds no such asset");
  }
  return impl_->descriptors.at(
    static_cast<size_t>(asset - impl_->assets.data()));
}

auto CookedContent::Descriptor(const data::AssetKey& key) const
  -> std::vector<std::byte>
{
  const auto stored = DescriptorStored(key);
  if (!impl_->pak) {
    // A loose descriptor is a whole file; one of another size was changed
    // after the index recorded it.
    const auto size = FileSize(*impl_->storage, stored.file);
    if (size != stored.range.count.get()) {
      throw Corrupt(CookedDefect::kSizeMismatch, stored.file,
        "has " + std::to_string(size)
          + " bytes, but the index records its size as "
          + std::to_string(stored.range.count.get()));
    }
  }
  auto bytes = std::vector<std::byte>(stored.range.count.get());
  ReadStored(*impl_->storage, impl_->PakDevice(), stored.file,
    stored.range.offset.get(), bytes);
  return bytes;
}

auto CookedContent::ResourceTable(const TableKind kind) const
  -> const CookedResourceTable&
{
  return impl_->tables.at(TableSlot(kind));
}

auto CookedContent::NamedTextures() const noexcept
  -> std::span<const NamedTexture>
{
  return impl_->named_textures;
}

auto CookedContent::FindNamedTexture(
  const std::string_view virtual_path) const noexcept -> const NamedTexture*
{
  const auto it = std::ranges::lower_bound(impl_->named_textures, virtual_path,
    std::less {}, &NamedTexture::virtual_path);
  return it != impl_->named_textures.end() && it->virtual_path == virtual_path
    ? std::addressof(*it)
    : nullptr;
}

auto CookedContent::Sidecars() const noexcept -> std::span<const CookedSidecar>
{
  return impl_->sidecars;
}

auto CookedContent::Catalog() const noexcept -> const data::PakCatalog*
{
  return impl_->pak ? &impl_->pak->Catalog() : nullptr;
}

auto CookedContent::Impl::VerifyLooseFiles(const lc::IntegrityCheck check) const
  -> void
{
  const auto& root = source.path;
  for (const auto& file : inventory) {
    const auto path = root / file.relative_path;
    if (storage->KindOf(path) != PathKind::kFile) {
      throw Corrupt(CookedDefect::kMissingPart, path, "is missing");
    }
    const auto device = OpenFile(*storage, path);
    if (Size(device, path) != file.size) {
      throw Corrupt(
        CookedDefect::kSizeMismatch, path, "does not have its indexed size");
    }
    // A resource data file has no digest of its own: its rows do.
    const auto has_own_digest = !(file.kind.has_value()
      && data::loose_cooked::IsResourceDataFile(*file.kind));
    if (check == lc::IntegrityCheck::kFull && has_own_digest) {
      const auto digest = serio::ComputeSha256(device);
      if (!digest) {
        throw Unreadable(path);
      }
      if (*digest != file.sha256) {
        throw Corrupt(CookedDefect::kDigestMismatch, path,
          "does not have its indexed digest");
      }
    }
  }
}

auto CookedContent::Impl::VerifyLooseDescriptors(
  const lc::IntegrityCheck check) const -> void
{
  for (size_t i = 0; i < assets.size(); ++i) {
    const auto& stored = descriptors.at(i);
    const auto device = OpenFile(*storage, stored.file);
    if (Size(device, stored.file) != stored.range.count.get()) {
      throw Corrupt(CookedDefect::kSizeMismatch, stored.file,
        "does not have its indexed size");
    }
    if (check == lc::IntegrityCheck::kFull) {
      const auto digest = serio::ComputeSha256(device);
      if (!digest) {
        throw Unreadable(stored.file);
      }
      if (*digest != assets.at(i).descriptor_sha256) {
        throw Corrupt(CookedDefect::kDigestMismatch, stored.file,
          "does not have its indexed digest");
      }
    }
  }
}

auto CookedContent::VerifyStorage(const lc::IntegrityCheck check) const -> void
{
  if (impl_->pak) {
    if (check == lc::IntegrityCheck::kFull) {
      impl_->pak->ValidateCrc32Integrity();
    }
    return;
  }
  impl_->VerifyLooseFiles(check);
  if (check == lc::IntegrityCheck::kFull) {
    VerifyResources(*impl_->storage, impl_->source.path, impl_->inventory);
  }
  impl_->VerifyLooseDescriptors(check);
}

auto ReadCookedSummary(const CookerStorageReader& storage, const fs::path& path)
  -> CookedSummary
{
  if (IsFolder(storage, path)) {
    return LooseSummary(storage, path, LoadIndex(storage, path));
  }
  return PakSummary(*OpenPak(storage, path));
}

} // namespace oxygen::content::cooked
