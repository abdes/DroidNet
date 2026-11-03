//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Content/AssetValidation.h>
#include <Oxygen/Content/Internal/LooseCookedIndexImpl.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedLayout.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexEncoder.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/LooseCookedPaths.h>
#include <Oxygen/Data/LooseCookedResources.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/FileLock.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import {

namespace {

  using data::loose_cooked::FileKind;

  auto RequireCanonicalVirtualPath(const std::string_view virtual_path) -> void
  {
    if (const auto error = oxygen::content::ValidateVirtualPath(virtual_path);
      error.has_value()) {
      throw std::runtime_error(std::string("Virtual path is not canonical: ")
        + nostd::to_string(*error));
    }
  }

  //! An asset the index lists, as the writer holds it.
  using StoredAsset = LooseCookedAssetRecord;

  struct StoredFile final {
    FileKind kind = FileKind::kUnknown;
    std::string relpath;
    uint64_t size = 0;
    base::Sha256Digest sha256 {};
    bool updated = false;
    bool externally_written = false;
    std::string virtual_path;
    std::string source_path;
    //! The digest its producer computed as it wrote the file; none for a
    //! resource data file, whose resources are verified one by one.
    std::optional<LooseCookedFileDigest> produced;
  };

  //! Records keyed by path rather than by kind: a root holds many of them.
  [[nodiscard]] auto IsPathKeyed(const FileKind kind) noexcept -> bool
  {
    return kind == FileKind::kAuxiliary || kind == FileKind::kTextureDescriptor;
  }

  //! Identities that differ only in case are one file on a case-insensitive
  //! file system, so the writer compares them folded.
  [[nodiscard]] auto FoldIdentity(const std::string_view virtual_path)
    -> std::string
  {
    return content::FoldCase(virtual_path).value_or(std::string(virtual_path));
  }

  //! Publishes `bytes` as the file `path`; its folders are created.
  auto WriteBinaryFile(cooked::CookerStorage& storage,
    const std::filesystem::path& path, const std::span<const std::byte> bytes)
    -> void
  {
    const auto published = cooked::PublishWholeFile(storage, path, bytes);
    if (!published) {
      throw std::system_error(published.error(),
        "Failed to publish cooked file '" + path.string() + "'");
    }
  }

  //! The size of an externally written file, which must exist.
  auto SizeOfExistingFile(const cooked::CookerStorage& storage,
    const std::filesystem::path& path, const std::string_view context)
    -> uint64_t
  {
    const auto size = storage.SizeOf(path);
    if (!size.has_value()) {
      throw std::runtime_error(
        std::string(context) + ": file does not exist: " + path.string());
    }
    return size->get();
  }

  //! Refuses a file whose size is not the size its producer hashed.
  auto RequireProducedSize(const std::optional<LooseCookedFileDigest>& produced,
    const uint64_t size, const std::filesystem::path& path) -> void
  {
    if (produced.has_value() && produced->size != size) {
      throw std::runtime_error("'" + path.string() + "' holds "
        + std::to_string(size) + " bytes, not the "
        + std::to_string(produced->size) + " bytes its producer hashed");
    }
  }

  auto IndexPath(const std::filesystem::path& cooked_root)
    -> std::filesystem::path
  {
    return cooked_root / internal::loose_layout::kIndexFileName;
  }

  //! A file a commit lists, as its successor finds what it stopped listing.
  struct PublishedFile final {
    std::string relpath;
    FileKind kind = FileKind::kUnknown;
  };

} // namespace

struct LooseCookedWriter::Impl final {
  Impl(std::filesystem::path cooked_root, cooked::CookerStorage& storage)
    : storage_(make_observer(&storage))
    , cooked_root_(std::move(cooked_root))
  {
    AcquireWriterLease_();
    LoadExistingIndexIfPresent_();
    // No other writer can run: what no commit lists is a crashed one's.
    DeleteUncommitted_();
    Reclaim_();
  }

  auto SetSourceKey(std::optional<data::SourceKey> key) -> void
  {
    source_key_override_ = key;
  }

  auto SetContentVersion(uint16_t version) -> void
  {
    content_version_override_ = version;
  }

  //! The producing source of a new entry: its own, or for a sidecar the
  //! producer of the asset it completes.
  [[nodiscard]] auto ResolveSource_(const LooseCookedProducer& producer) const
    -> std::string
  {
    if (!producer.completes.empty()) {
      if (const auto key = key_by_virtual_path_.find(producer.completes);
        key != key_by_virtual_path_.end()) {
        return assets_.at(key->second).source_path;
      }
      if (producer.source.empty()) {
        throw std::runtime_error("Cannot complete an asset of unknown "
                                 "producer: "
          + producer.completes);
      }
    }
    // A mount path names a real file; identity rules do not apply to it.
    if (!producer.source.starts_with('/')) {
      throw std::runtime_error(
        "The recorded source is not a mount path: '" + producer.source + "'");
    }
    return producer.source;
  }

  //! The source that produced an identity, if the index lists it.
  [[nodiscard]] auto FindProducer_(const std::string& virtual_path) const
    -> std::optional<std::string>
  {
    if (const auto key = key_by_virtual_path_.find(virtual_path);
      key != key_by_virtual_path_.end()) {
      return assets_.at(key->second).source_path;
    }
    if (const auto texture = texture_by_virtual_path_.find(virtual_path);
      texture != texture_by_virtual_path_.end()) {
      return files_.at(texture->second).source_path;
    }
    return std::nullopt;
  }

  //! Refuses an identity another source produced, or one differing from an
  //! identity of the index only in case.
  auto RequireOwnIdentity_(const std::string& virtual_path,
    const std::string& source, const std::string_view context) const -> void
  {
    if (!path_by_folded_.empty()) {
      const auto& held = path_by_folded_.begin()->second;
      if (content::MountRootOf(held) != content::MountRootOf(virtual_path)) {
        throw LooseCookedOutputMountMixed(std::string(context) + ": '"
          + virtual_path + "' is not in the mount of the cooked root, which "
          + "holds '" + held + "'");
      }
    }
    if (const auto spelled = path_by_folded_.find(FoldIdentity(virtual_path));
      spelled != path_by_folded_.end() && spelled->second != virtual_path) {
      throw LooseCookedIdentityCollision(std::string(context) + ": '"
        + virtual_path + "' differs only in case from '" + spelled->second
        + "'");
    }
    if (const auto producer = FindProducer_(virtual_path);
      producer.has_value() && FoldIdentity(*producer) != FoldIdentity(source)) {
      throw LooseCookedIdentityCollision(std::string(context) + ": '"
        + virtual_path + "' is produced by '" + *producer + "', not by '"
        + source + "'");
    }
  }

  //! Lists an asset, replacing the entry its source produced before.
  auto InsertAsset_(const StoredAsset& asset, const std::string_view context)
    -> void
  {
    RequireOwnIdentity_(asset.virtual_path, asset.source_path, context);
    if (const auto existing = assets_.find(asset.key); existing != assets_.end()
      && existing->second.virtual_path != asset.virtual_path) {
      throw std::runtime_error(std::string(context) + ": asset key "
        + data::to_string(asset.key) + " names both '"
        + existing->second.virtual_path + "' and '" + asset.virtual_path + "'");
    }
    if (const auto previous = key_by_virtual_path_.find(asset.virtual_path);
      previous != key_by_virtual_path_.end() && previous->second != asset.key) {
      assets_.erase(previous->second);
    }
    assets_.insert_or_assign(asset.key, asset);
    key_by_virtual_path_.insert_or_assign(asset.virtual_path, asset.key);
    path_by_folded_.insert_or_assign(
      FoldIdentity(asset.virtual_path), asset.virtual_path);
  }

  auto ForgetAsset_(const data::AssetKey& key) -> void
  {
    const auto asset = assets_.find(key);
    if (asset == assets_.end()) {
      return;
    }
    key_by_virtual_path_.erase(asset->second.virtual_path);
    path_by_folded_.erase(FoldIdentity(asset->second.virtual_path));
    assets_.erase(asset);
  }

  auto ForgetFile_(const std::string& relpath) -> void
  {
    const auto file = files_.find(relpath);
    if (file == files_.end()) {
      return;
    }
    if (!file->second.virtual_path.empty()) {
      texture_by_virtual_path_.erase(file->second.virtual_path);
      path_by_folded_.erase(FoldIdentity(file->second.virtual_path));
    }
    files_.erase(file);
  }

  auto WriteAssetDescriptor(const data::AssetKey& key,
    data::AssetType asset_type, std::string_view virtual_path,
    std::string_view descriptor_relpath, std::span<const std::byte> bytes,
    const data::AssetReferences& references,
    const LooseCookedProducer& producer) -> void
  {
    RequireCanonicalVirtualPath(virtual_path);
    data::loose_cooked::RequireIndexRelPath(descriptor_relpath);
    ValidateAssetDescriptor(asset_type, key, bytes, references);

    const auto digest = base::ComputeSha256(bytes);

    StoredAsset record {
      .key = key,
      .asset_type = asset_type,
      .virtual_path = std::string(virtual_path),
      .descriptor_relpath
      = data::loose_cooked::ContentRelPath(descriptor_relpath, digest),
      .descriptor_size = bytes.size(),
      .descriptor_sha256 = digest,
      .references = references,
      .source_path = ResolveSource_(producer),
    };

    RequireOwnIdentity_(
      record.virtual_path, record.source_path, "WriteAssetDescriptor");
    WriteContentFile_(record.descriptor_relpath, bytes);

    InsertAsset_(record, "WriteAssetDescriptor");
    written_virtual_paths_.emplace(virtual_path);
  }

  auto WriteFile(const FileKind kind, std::string_view relpath,
    std::span<const std::byte> bytes) -> void
  {
    data::loose_cooked::RequireIndexRelPath(relpath);

    // A resource data file keeps its name and only grows; its table rows
    // hold the digests of its resources. Any other file is content-named.
    const auto is_data_file = data::loose_cooked::IsResourceDataFile(kind);
    const auto digest
      = is_data_file ? base::Sha256Digest {} : base::ComputeSha256(bytes);
    StoredFile record {
      .kind = kind,
      .relpath = is_data_file
        ? std::string(relpath)
        : data::loose_cooked::ContentRelPath(relpath, digest),
      .size = bytes.size(),
      .sha256 = digest,
      .updated = true,
      .externally_written = false,
    };

    if (is_data_file) {
      RequireAppended_(record.relpath, bytes);
    }
    const auto stored_relpath = record.relpath;
    StoreFile_(std::move(record), "WriteFile");
    if (is_data_file) {
      WriteBinaryFile(
        *storage_, cooked_root_ / std::filesystem::path(stored_relpath), bytes);
    } else {
      WriteContentFile_(stored_relpath, bytes);
    }
  }

  auto RegisterExternalFile(const FileKind kind, std::string_view relpath,
    const std::optional<LooseCookedFileDigest>& produced,
    std::string_view virtual_path = {}, std::string source_path = {}) -> void
  {
    data::loose_cooked::RequireIndexRelPath(relpath);
    if (data::loose_cooked::IsResourceDataFile(kind) == produced.has_value()) {
      throw std::runtime_error(produced.has_value()
          ? "RegisterExternalFile: a resource data file has no digest"
          : "RegisterExternalFile: the file has no digest");
    }
    if (kind == FileKind::kTextureDescriptor
      || (kind == FileKind::kAuxiliary && !virtual_path.empty())) {
      RequireCanonicalVirtualPath(virtual_path);
      RequireCanonicalVirtualPath(source_path);
    } else if (!virtual_path.empty()) {
      throw std::runtime_error("RegisterExternalFile: only texture descriptors "
                               "and auxiliary files have virtual paths");
    } else if (!source_path.empty()) {
      if (kind != FileKind::kAuxiliary) {
        throw std::runtime_error("RegisterExternalFile: only texture "
                                 "descriptors and auxiliary files have "
                                 "sources");
      }
      RequireCanonicalVirtualPath(source_path);
    }

    const auto path_on_disk = cooked_root_ / std::filesystem::path(relpath);
    const auto size
      = SizeOfExistingFile(*storage_, path_on_disk, "RegisterExternalFile");
    RequireProducedSize(produced, size, path_on_disk);

    StoredFile record {
      .kind = kind,
      .relpath = std::string(relpath),
      .size = size,
      .updated = true,
      .externally_written = true,
      .virtual_path = std::string(virtual_path),
      .source_path = std::move(source_path),
      .produced = produced,
    };

    StoreFile_(std::move(record), "RegisterExternalFile");
  }

  auto RegisterExternalTextureDescriptor(std::string_view relpath,
    std::string_view virtual_path, const LooseCookedProducer& producer,
    const LooseCookedFileDigest& produced) -> void
  {
    RegisterExternalFile(FileKind::kTextureDescriptor, relpath, produced,
      virtual_path, ResolveSource_(producer));
  }

  auto RegisterExternalAuxiliaryFile(std::string_view relpath,
    std::string_view virtual_path, const LooseCookedProducer& producer,
    const LooseCookedFileDigest& produced) -> void
  {
    RegisterExternalFile(FileKind::kAuxiliary, relpath, produced, virtual_path,
      ResolveSource_(producer));
  }

  auto RegisterProducedAssetDescriptor(const data::AssetKey& key,
    const data::AssetType asset_type, std::string_view virtual_path,
    std::string_view descriptor_relpath, const LooseCookedFileDigest& produced,
    const data::AssetReferences& references,
    const LooseCookedProducer& producer,
    const std::span<const data::loose_cooked::IndexedRequirement> requirements)
    -> void
  {
    RequireCanonicalVirtualPath(virtual_path);
    data::loose_cooked::RequireIndexRelPath(descriptor_relpath);

    // Its producer validated and hashed the bytes as it wrote them; the file
    // on disk is only measured.
    const auto path_on_disk
      = cooked_root_ / std::filesystem::path(descriptor_relpath);
    RequireProducedSize(produced,
      SizeOfExistingFile(
        *storage_, path_on_disk, "RegisterProducedAssetDescriptor"),
      path_on_disk);
    StoreAsset_(key, asset_type, virtual_path, descriptor_relpath, produced,
      references, producer, "RegisterProducedAssetDescriptor");
    assets_.at(key).requirements.assign(
      requirements.begin(), requirements.end());
  }

  auto RegisterExternalAssetDescriptor(const data::AssetKey& key,
    const data::AssetType asset_type, std::string_view virtual_path,
    std::string_view descriptor_relpath,
    const data::AssetReferences& references,
    const LooseCookedProducer& producer) -> void
  {
    RequireCanonicalVirtualPath(virtual_path);
    data::loose_cooked::RequireIndexRelPath(descriptor_relpath);

    // A descriptor nothing here produced is read once, to validate and hash.
    const auto path_on_disk
      = cooked_root_ / std::filesystem::path(descriptor_relpath);
    const auto bytes = cooked::ReadWholeFile(
      *storage_, path_on_disk, cooked::kMaxMetadataBytes);
    if (!bytes) {
      throw std::system_error(bytes.error(),
        "Failed to read registered descriptor '" + path_on_disk.string() + "'");
    }
    ValidateAssetDescriptor(asset_type, key, *bytes, references);
    StoreAsset_(key, asset_type, virtual_path, descriptor_relpath,
      LooseCookedFileDigest {
        .size = bytes->size(),
        .sha256 = base::ComputeSha256(*bytes),
      },
      references, producer, "RegisterExternalAssetDescriptor");
  }

  auto StoreAsset_(const data::AssetKey& key, const data::AssetType asset_type,
    const std::string_view virtual_path,
    const std::string_view descriptor_relpath,
    const LooseCookedFileDigest& digest,
    const data::AssetReferences& references,
    const LooseCookedProducer& producer, const char* context) -> void
  {
    InsertAsset_(
      StoredAsset {
        .key = key,
        .asset_type = asset_type,
        .virtual_path = std::string(virtual_path),
        .descriptor_relpath = std::string(descriptor_relpath),
        .descriptor_size = digest.size,
        .descriptor_sha256 = digest.sha256,
        .references = references,
        .source_path = ResolveSource_(producer),
      },
      context);
    written_virtual_paths_.emplace(virtual_path);
  }

  auto RemoveAssetDescriptor(const std::string_view virtual_path) -> void
  {
    RequireCanonicalVirtualPath(virtual_path);
    removed_virtual_paths_.emplace(virtual_path);
  }

  auto RemoveAuxiliaryFile(const std::string_view relpath) -> void
  {
    data::loose_cooked::RequireIndexRelPath(relpath);
    removed_auxiliary_files_.emplace(relpath);
  }

  auto RetireUnwrittenOutputs(const LooseCookedRecook& recook) -> void
  {
    if (!recook.source.starts_with('/')) {
      throw std::runtime_error(
        "The re-cooked source is not a mount path: '" + recook.source + "'");
    }
    RequireCanonicalVirtualPath(recook.identity);
    data::loose_cooked::RequireIndexRelPath(recook.identity_relpath);
    recooks_.push_back({
      .source = FoldIdentity(recook.source),
      .identity = FoldIdentity(recook.identity),
      .identity_relpath = FoldIdentity(recook.identity_relpath),
    });
  }

  [[nodiscard]] auto Finish() -> LooseCookedWriteResult
  {
    // The writer lease, held since the writer opened the root, keeps every
    // other writer out: the commit it read is still the latest.
    const auto index_path = IndexPath(cooked_root_);

    for (auto& [path, file] : files_) {
      static_cast<void>(path);
      if (file.updated && file.externally_written) {
        // Its producer hashed the bytes as it wrote them; a resource data
        // file has no digest of its own, only its resources do.
        const auto physical_path = cooked_root_ / file.relpath;
        file.size = SizeOfExistingFile(*storage_, physical_path, "Finish");
        RequireProducedSize(file.produced, file.size, physical_path);
        file.sha256 = file.produced.has_value() ? file.produced->sha256
                                                : base::Sha256Digest {};
      }
    }

    const auto session_files = files_;
    ApplyRemovals_(session_files);

    const auto cooked_root_str = cooked_root_.string();
    LOG_SCOPE_F(INFO, "Finish {}", cooked_root_str);

    ValidateRequiredFilePairs_();
    // Retired from what the two commits list: a path the new one lists, as
    // any kind of file, stays.
    const auto kept_paths = PhysicalPaths_();
    const auto commit = commit_ + 1;
    auto removed_files = std::vector<std::string> {};
    for (const auto& [folded, file] : published_paths_) {
      if (!kept_paths.contains(folded)) {
        removed_files.push_back(file.relpath);
        retired_.push_back({
          .kind = file.kind,
          .relpath = file.relpath,
          .retired_at = commit,
        });
      }
    }
    // A retired file the new commit lists again is current, not retired.
    std::erase_if(retired_, [&](const auto& retired) -> bool {
      return kept_paths.contains(FoldIdentity(retired.relpath));
    });
    std::ranges::sort(removed_files);

    const auto source_key = ResolveSourceKey_();
    const auto content_version = ResolveContentVersion_();

    LooseCookedWriteResult out {
      .cooked_root = cooked_root_,
      .source_key = source_key,
      .content_version = content_version,
      .commit = commit,
      .removed_files = std::move(removed_files),
    };

    out.assets.reserve(assets_.size());
    for (const auto& [key, asset] : assets_) {
      static_cast<void>(key);
      out.assets.push_back(asset);
    }
    std::ranges::sort(out.assets, {}, &LooseCookedAssetRecord::key);

    out.files.reserve(files_.size());
    for (const auto& [relpath, file] : files_) {
      static_cast<void>(relpath);
      out.files.push_back({
        .kind = file.kind,
        .relpath = file.relpath,
        .size = file.size,
        .sha256 = file.sha256,
        .virtual_path = file.virtual_path,
        .source_path = file.source_path,
      });
    }
    std::ranges::sort(out.files, {}, &LooseCookedFileRecord::relpath);

    WriteIndex_(index_path,
      {
        .source_key = source_key,
        .content_version = content_version,
        .commit = commit,
        .assets = out.assets,
        .files = out.files,
        .retired = retired_,
      });
    commit_ = commit;
    published_paths_ = ListedPaths_();
    for (auto& [path, file] : files_) {
      static_cast<void>(path);
      file.updated = false;
    }

    // What this session wrote and the commit does not list, then what no
    // reader of an earlier commit needs any more.
    DeleteUncommitted_();
    Reclaim_();
    return out;
  }

private:
  //! The physical path of every file the index lists, folded as a
  //! case-insensitive file system compares them, to its spelling.
  /*!
   @throw LooseCookedIdentityCollision When two paths differ only in case.
  */
  [[nodiscard]] auto PhysicalPaths_() const
    -> std::unordered_map<std::string, std::string>
  {
    auto paths = std::unordered_map<std::string, std::string> {};
    const auto add = [&](const std::string& relpath) -> void {
      const auto [held, added]
        = paths.try_emplace(FoldIdentity(relpath), relpath);
      if (!added && held->second != relpath) {
        throw LooseCookedIdentityCollision("the cooked file '" + relpath
          + "' differs only in case from '" + held->second + "'");
      }
    };
    for (const auto& [key, asset] : assets_) {
      static_cast<void>(key);
      add(asset.descriptor_relpath);
    }
    for (const auto& [relpath, file] : files_) {
      static_cast<void>(file);
      add(relpath);
    }
    return paths;
  }

  //! Requires the writer lease.
  auto ApplyRemovals_(
    const std::unordered_map<std::string, StoredFile>& session_files) -> void
  {
    for (const auto& virtual_path : removed_virtual_paths_) {
      if (written_virtual_paths_.contains(virtual_path)) {
        throw std::runtime_error(
          "Cannot remove an asset this writer also writes: " + virtual_path);
      }
      const auto key = key_by_virtual_path_.find(virtual_path);
      if (key == key_by_virtual_path_.end()) {
        throw std::runtime_error(
          "Cannot remove an asset the index does not list: " + virtual_path);
      }
      const auto asset = assets_.find(key->second);
      if (asset == assets_.end()) {
        throw std::runtime_error(
          "Loose cooked index maps a virtual path to no asset: "
          + virtual_path);
      }
      LOG_F(INFO, "Removed cooked asset '{}' ({})", virtual_path,
        asset->second.descriptor_relpath);
      ForgetAsset_(asset->first);
    }

    for (const auto& relpath : removed_auxiliary_files_) {
      if (const auto written = session_files.find(relpath);
        written != session_files.end() && written->second.updated) {
        throw std::runtime_error(
          "Cannot remove an auxiliary file this writer also writes: "
          + relpath);
      }
      const auto file = files_.find(relpath);
      if (file == files_.end() || !IsPathKeyed(file->second.kind)) {
        throw std::runtime_error(
          "Cannot remove an auxiliary file the index does not list: "
          + relpath);
      }
      ForgetFile_(relpath);
      LOG_F(INFO, "Removed cooked auxiliary file '{}'", relpath);
    }

    RetireUnwrittenOutputs_(session_files);
  }

  //! Retires the outputs the index records for a re-cooked source that this
  //! writer does not write. Requires the writer lease.
  auto RetireUnwrittenOutputs_(
    const std::unordered_map<std::string, StoredFile>& session_files) -> void
  {
    if (recooks_.empty()) {
      return;
    }
    // One source may back several identities, so a re-cook retires only what
    // lies at or under its own identity.
    const auto at_or_under
      = [](const std::string& path, const std::string& root) -> bool {
      return path == root
        || (path.size() > root.size() && path.starts_with(root)
          && path.at(root.size()) == '/');
    };
    const auto recooked
      = [&](const std::string& source, const std::string& virtual_path,
          const std::string& relpath) -> bool {
      if (source.empty()) {
        return false;
      }
      const auto folded_source = FoldIdentity(source);
      return std::ranges::any_of(recooks_, [&](const auto& recook) -> bool {
        return recook.source == folded_source
          && (virtual_path.empty()
              ? at_or_under(FoldIdentity(relpath), recook.identity_relpath)
              : at_or_under(FoldIdentity(virtual_path), recook.identity));
      });
    };

    auto retired_assets = std::vector<data::AssetKey> {};
    for (const auto& [key, asset] : assets_) {
      // A physics sidecar keeps its scene's producer but comes from a job of
      // its own, which this re-cook did not run.
      if (asset.asset_type == data::AssetType::kPhysicsScene) {
        continue;
      }
      if (recooked(asset.source_path, asset.virtual_path, {})
        && !written_virtual_paths_.contains(asset.virtual_path)) {
        retired_assets.push_back(key);
      }
    }
    for (const auto& key : retired_assets) {
      const auto& asset = assets_.at(key);
      LOG_F(INFO, "Retired cooked asset '{}' ({}): '{}' no longer produces it",
        asset.virtual_path, asset.descriptor_relpath, asset.source_path);
      ForgetAsset_(key);
    }

    auto retired_files = std::vector<std::string> {};
    for (const auto& [relpath, file] : files_) {
      const auto written = session_files.find(relpath);
      if (IsPathKeyed(file.kind)
        && recooked(file.source_path, file.virtual_path, relpath)
        && (written == session_files.end() || !written->second.updated)) {
        retired_files.push_back(relpath);
      }
    }
    for (const auto& relpath : retired_files) {
      LOG_F(INFO, "Retired cooked file '{}': '{}' no longer produces it",
        relpath, files_.at(relpath).source_path);
      ForgetFile_(relpath);
    }
  }

  //! Takes the root's writer lease, held until the writer is destroyed.
  auto AcquireWriterLease_() -> void
  {
    auto lease = storage_->TryLock(
      cooked_root_ / std::string(data::loose_cooked::kWriterLockFileName),
      serio::FileLockMode::kExclusive);
    if (!lease) {
      if (lease.error() == std::errc::device_or_resource_busy) {
        throw LooseCookedRootBusy(
          "another writer is cooking '" + cooked_root_.generic_string() + "'");
      }
      throw std::system_error(lease.error(),
        "cannot take the writer lease of '" + cooked_root_.string() + "'");
    }
    writer_lease_ = std::move(lease).value();
  }

  //! Publishes `bytes` as the content-named file `relpath`, unless a file the
  //! root holds already has that name, and so those bytes.
  auto WriteContentFile_(const std::string& relpath,
    const std::span<const std::byte> bytes) const -> void
  {
    const auto path = cooked_root_ / std::filesystem::path(relpath);
    if (const auto size = storage_->SizeOf(path);
      size.has_value() && size->get() == bytes.size()) {
      return;
    }
    WriteBinaryFile(*storage_, path, bytes);
  }

  //! Refuses data file bytes that do not begin with what the commit records
  //! of the file: a data file only grows.
  auto RequireAppended_(const std::string& relpath,
    const std::span<const std::byte> bytes) const -> void
  {
    const auto existing = files_.find(relpath);
    if (existing == files_.end() || existing->second.size == 0) {
      return;
    }
    const auto committed = cooked::ReadWholeFile(*storage_,
      cooked_root_ / std::filesystem::path(relpath), cooked::kMaxDataFileBytes);
    const auto appended = committed.has_value()
      && committed->size() >= existing->second.size
      && bytes.size() >= existing->second.size
      && std::ranges::equal(
        bytes.first(static_cast<size_t>(existing->second.size)),
        std::span(*committed)
          .first(static_cast<size_t>(existing->second.size)));
    if (!appended) {
      throw std::runtime_error("WriteFile: the resource data file '" + relpath
        + "' would not begin with its committed bytes");
    }
  }

  //! Deletes the file `relpath` and the folders it leaves empty, so a later
  //! write names its folders anew: on a file system that ignores case, a
  //! kept folder would keep a name renamed only in case.
  auto DeleteFile_(const std::string& relpath) const -> Result<void>
  {
    CHECK_RESULT(
      storage_->Remove(cooked_root_ / std::filesystem::path(relpath)));
    for (auto folder = std::filesystem::path(relpath).parent_path();
      !folder.empty(); folder = folder.parent_path()) {
      const auto removed = storage_->RemoveEmptyFolder(cooked_root_ / folder);
      if (!removed) {
        return ::oxygen::Err(removed.error());
      }
      if (!*removed) {
        break;
      }
    }
    return {};
  }

  //! Drops what no commit records: each data file's bytes past its committed
  //! length, and every file of a writer's naming that no commit lists. Only
  //! the writer lease holder runs it, so these are its own or a crashed
  //! writer's; any other unlisted file is left for checks to refuse.
  auto DeleteUncommitted_() -> void
  {
    for (const auto& [relpath, file] : files_) {
      if (!data::loose_cooked::IsResourceDataFile(file.kind)) {
        continue;
      }
      const auto path = cooked_root_ / std::filesystem::path(relpath);
      if (const auto size = storage_->SizeOf(path);
        size.has_value() && size->get() > file.size) {
        if (const auto cut
          = storage_->Truncate(path, serio::ByteCount { file.size });
          !cut) {
          throw std::system_error(cut.error(),
            "Failed to drop the uncommitted tail of '" + relpath + "'");
        }
        LOG_F(INFO, "Dropped {} uncommitted bytes of '{}'",
          size->get() - file.size, relpath);
      }
    }
    const auto entries = storage_->List(cooked_root_);
    if (!entries) {
      return; // A root not written yet holds nothing.
    }
    auto listed = std::unordered_set<std::string> {};
    for (const auto& [folded, file] : ListedPaths_()) {
      static_cast<void>(folded);
      listed.insert(file.relpath);
    }
    for (const auto& retired : retired_) {
      listed.insert(retired.relpath);
    }
    for (const auto& entry : *entries) {
      const auto& relpath = entry.relative_path;
      if (entry.kind != content::lc::RootEntryKind::kFile
        || data::loose_cooked::IsReservedRelPath(relpath)
        || listed.contains(relpath)
        || !data::loose_cooked::IsWriterOutputRelPath(relpath)) {
        continue;
      }
      if (const auto deleted = DeleteFile_(relpath); !deleted) {
        throw std::system_error(deleted.error(),
          "Failed to delete the uncommitted file '" + relpath + "'");
      }
      LOG_F(INFO, "Deleted the uncommitted file '{}'", relpath);
    }
  }

  //! Deletes each retired file no reader of an earlier commit can open: one
  //! retired at commit `R` waits while a reader leases a commit before `R`.
  auto Reclaim_() -> void
  {
    if (retired_.empty()) {
      return;
    }
    const auto leases_folder
      = cooked_root_ / std::string(data::loose_cooked::kLeasesFolderName);
    auto oldest_read = std::optional<uint64_t> {};
    if (const auto leases = storage_->List(leases_folder); leases.has_value()) {
      for (const auto& entry : *leases) {
        const auto commit
          = data::loose_cooked::LeasedCommitOf(entry.relative_path);
        if (entry.kind != content::lc::RootEntryKind::kFile
          || !commit.has_value()) {
          continue;
        }
        // Held only for the check: a lease nobody holds is removed with it.
        const auto probe = storage_->TryLock(
          leases_folder / entry.relative_path, serio::FileLockMode::kExclusive);
        if (!probe && probe.error() == std::errc::device_or_resource_busy) {
          oldest_read = std::min(oldest_read.value_or(*commit), *commit);
        }
      }
    }
    std::erase_if(retired_, [&](const auto& retired) -> bool {
      if (oldest_read.has_value() && retired.retired_at > *oldest_read) {
        return false;
      }
      if (const auto deleted = DeleteFile_(retired.relpath); !deleted) {
        // A reader that does not share deletion holds it; the next session
        // tries again.
        LOG_F(WARNING, "Kept the retired file '{}': {}", retired.relpath,
          deleted.error().message());
        return false;
      }
      return true;
    });
  }

  //! Every path the index lists, folded as a case-insensitive file system
  //! compares them, with its spelling and kind.
  [[nodiscard]] auto ListedPaths_() const
    -> std::unordered_map<std::string, PublishedFile>
  {
    auto paths = std::unordered_map<std::string, PublishedFile> {};
    for (const auto& [key, asset] : assets_) {
      static_cast<void>(key);
      paths.insert_or_assign(FoldIdentity(asset.descriptor_relpath),
        PublishedFile {
          .relpath = asset.descriptor_relpath,
          .kind = FileKind::kAssetDescriptor,
        });
    }
    for (const auto& [relpath, file] : files_) {
      paths.insert_or_assign(FoldIdentity(relpath),
        PublishedFile { .relpath = relpath, .kind = file.kind });
    }
    return paths;
  }

  [[nodiscard]] auto HasFileKind_(const FileKind kind) const -> bool
  {
    return std::ranges::any_of(files_,
      [kind](const auto& item) -> auto { return item.second.kind == kind; });
  }

  auto StoreFile_(StoredFile file, const std::string_view context) -> void
  {
    if (file.kind == FileKind::kTextureDescriptor
      && file.virtual_path.empty()) {
      throw std::runtime_error(
        "Texture descriptor records require a virtual path: " + file.relpath);
    }
    if (!IsPathKeyed(file.kind)) {
      // A root holds one table or data file of each kind. A data file keeps
      // its one path; a table of new rows has a new content name, and its
      // record replaces the old one, which the commit then retires.
      const auto existing
        = std::ranges::find_if(files_, [&](const auto& item) -> auto {
            return item.second.kind == file.kind;
          });
      if (existing != files_.end() && existing->first != file.relpath) {
        if (data::loose_cooked::IsResourceDataFile(file.kind)) {
          throw std::runtime_error(std::string(context) + ": file kind "
            + std::to_string(static_cast<uint16_t>(file.kind)) + " is at '"
            + existing->first + "', not at '" + file.relpath + "'");
        }
        files_.erase(existing);
      }
    }
    if (!file.virtual_path.empty()) {
      RequireOwnIdentity_(file.virtual_path, file.source_path, context);
      if (const auto previous
        = texture_by_virtual_path_.find(file.virtual_path);
        previous != texture_by_virtual_path_.end()
        && previous->second != file.relpath) {
        ForgetFile_(std::string(previous->second));
      }
    }
    if (const auto existing = files_.find(file.relpath);
      existing != files_.end()
      && existing->second.virtual_path != file.virtual_path) {
      ForgetFile_(file.relpath);
    }
    if (!file.virtual_path.empty()) {
      texture_by_virtual_path_.insert_or_assign(
        file.virtual_path, file.relpath);
      path_by_folded_.insert_or_assign(
        FoldIdentity(file.virtual_path), file.virtual_path);
    }
    const auto path = file.relpath;
    files_.insert_or_assign(path, std::move(file));
  }

  //! Requires the writer lease.
  auto LoadExistingIndexIfPresent_() -> void
  {
    assets_.clear();
    files_.clear();
    key_by_virtual_path_.clear();
    texture_by_virtual_path_.clear();
    path_by_folded_.clear();

    const auto index_path = IndexPath(cooked_root_);
    if (storage_->KindOf(index_path) != cooked::PathKind::kFile) {
      return;
    }

    try {
      auto bytes = cooked::ReadWholeFile(
        *storage_, index_path, cooked::kMaxMetadataBytes);
      if (!bytes) {
        throw std::runtime_error(
          "Failed to read existing index: " + bytes.error().message());
      }
      const auto index
        = oxygen::content::internal::LooseCookedIndexImpl::LoadFromBytes(
          std::move(*bytes));
      existing_guid_ = index.Guid();
      existing_content_version_ = index.ContentVersion();
      commit_ = index.Commit();
      retired_.assign(index.RetiredFiles().begin(), index.RetiredFiles().end());

      for (const auto key : index.GetAllAssetKeys()) {
        const auto rel = index.FindDescriptorRelPath(key);
        const auto vpath = index.FindVirtualPath(key);
        const auto type_u8 = index.FindAssetType(key);
        const auto size = index.FindDescriptorSize(key);
        const auto sha = index.FindDescriptorSha256(key);
        const auto source = index.FindSourcePath(key);
        auto references = index.FindAssetReferences(key);

        if (!rel || !vpath || !type_u8 || !size || !source || !references) {
          continue;
        }

        StoredAsset record {
          .key = key,
          .asset_type = static_cast<data::AssetType>(*type_u8),
          .virtual_path = std::string(*vpath),
          .descriptor_relpath = std::string(*rel),
          .descriptor_size = *size,
          .descriptor_sha256 = {},
          .references = std::move(*references),
          .source_path = std::string(*source),
        };

        if (sha.has_value()) {
          std::copy_n(sha->begin(), record.descriptor_sha256.size(),
            record.descriptor_sha256.begin());
        }
        for (const auto& [asset, requirement] : index.Requirements()) {
          if (asset == key) {
            record.requirements.push_back(requirement);
          }
        }

        InsertAsset_(record, "Load");
      }

      for (const auto& file : index.GetFileInventory()) {
        if (!file.kind.has_value()) {
          continue; // Asset records are restored above.
        }
        StoreFile_(
          StoredFile {
            .kind = *file.kind,
            .relpath = file.relative_path,
            .size = file.size,
            .sha256 = file.sha256,
            .updated = false,
            .externally_written = false,
            .virtual_path = file.virtual_path,
            .source_path = file.source_path,
          },
          "Load");
      }

      published_paths_ = ListedPaths_();
      DLOG_F(INFO,
        "Loaded loose cooked commit {}: assets={}, files={}, retired={}",
        commit_, assets_.size(), files_.size(), retired_.size());
    } catch (const std::exception& ex) {
      throw std::runtime_error(
        std::string("Failed to load existing loose cooked index: ")
        + ex.what());
    }
  }

  auto ValidateRequiredFilePairs_() const -> void
  {
    const auto has_buffers_table = HasFileKind_(FileKind::kBuffersTable);
    const auto has_buffers_data = HasFileKind_(FileKind::kBuffersData);
    if (has_buffers_table != has_buffers_data) {
      throw std::runtime_error(
        "Loose cooked index must provide both buffers.table and buffers.data");
    }

    const auto has_textures_table = HasFileKind_(FileKind::kTexturesTable);
    const auto has_textures_data = HasFileKind_(FileKind::kTexturesData);
    if (has_textures_table != has_textures_data) {
      throw std::runtime_error("Loose cooked index must provide both "
                               "textures.table and textures.data");
    }

    const auto has_scripts_table = HasFileKind_(FileKind::kScriptsTable);
    const auto has_scripts_data = HasFileKind_(FileKind::kScriptsData);
    if (has_scripts_table != has_scripts_data) {
      throw std::runtime_error(
        "Loose cooked index must provide both scripts.table and scripts.data");
    }

    const auto has_physics_table = HasFileKind_(FileKind::kPhysicsTable);
    const auto has_physics_data = HasFileKind_(FileKind::kPhysicsData);
    if (has_physics_table != has_physics_data) {
      throw std::runtime_error(
        "Loose cooked index must provide both physics.table and physics.data");
    }
  }

  [[nodiscard]] auto ResolveSourceKey_() const -> data::SourceKey
  {
    if (source_key_override_.has_value()) {
      return *source_key_override_;
    }
    if (existing_guid_.has_value()) {
      return *existing_guid_;
    }

    return data::SourceKey { Uuid::Generate() };
  }

  [[nodiscard]] auto ResolveContentVersion_() const -> uint16_t
  {
    if (content_version_override_.has_value()) {
      return *content_version_override_;
    }
    if (existing_content_version_.has_value()) {
      return *existing_content_version_;
    }
    return 0;
  }

  //! Streams the index into a publication that replaces the published one
  //! when it commits.
  auto WriteIndex_(const std::filesystem::path& index_path,
    const data::loose_cooked::IndexContent& content) const -> void
  {
    auto publication = storage_->Publish(index_path);
    if (!publication) {
      throw std::system_error(
        publication.error(), "Failed to publish loose cooked index");
    }
    auto writer = serio::Writer(**publication);
    if (const auto encoded
      = data::loose_cooked::EncodeLooseCookedIndex(content, writer);
      !encoded) {
      throw std::system_error(
        encoded.error(), "Failed to write loose cooked index");
    }
    if (const auto committed = (*publication)->Commit(); !committed) {
      throw std::system_error(
        committed.error(), "Failed to publish loose cooked index");
    }

    LOG_F(INFO, "Wrote loose cooked commit {}: assets={}, files={}, retired={}",
      content.commit, content.assets.size(), content.files.size(),
      content.retired.size());
  }

  //! Where the cooked root is stored: every read, write and removal.
  observer_ptr<cooked::CookerStorage> storage_;
  std::filesystem::path cooked_root_;
  //! The root's writer lease, held for the writer's life.
  std::unique_ptr<cooked::StorageLock> writer_lease_;
  //! The commit the root holds: 0 before its first.
  uint64_t commit_ = 0;
  //! What that commit lists, folded, as its successor finds what it stopped
  //! listing.
  std::unordered_map<std::string, PublishedFile> published_paths_;
  //! Files earlier commits listed that may still be on disk.
  std::vector<data::loose_cooked::RetiredFile> retired_;

  std::optional<data::SourceKey> source_key_override_;
  std::optional<uint16_t> content_version_override_;

  std::optional<data::SourceKey> existing_guid_;
  std::optional<uint16_t> existing_content_version_;

  std::unordered_map<data::AssetKey, StoredAsset> assets_;
  std::unordered_map<std::string, StoredFile> files_;
  std::unordered_map<std::string, data::AssetKey> key_by_virtual_path_;
  //! Texture descriptor identities and the paths of their records.
  std::unordered_map<std::string, std::string> texture_by_virtual_path_;
  //! Every identity of the index, folded for case, and its spelling.
  std::unordered_map<std::string, std::string> path_by_folded_;
  std::unordered_set<std::string> written_virtual_paths_;
  std::set<std::string> removed_virtual_paths_;
  std::set<std::string> removed_auxiliary_files_;
  //! The complete re-cooks this writer publishes, folded for case.
  std::vector<LooseCookedRecook> recooks_;
};

LooseCookedWriter::LooseCookedWriter(
  std::filesystem::path cooked_root, cooked::CookerStorage& storage)
  : impl_(std::make_unique<Impl>(std::move(cooked_root), storage))
{
}

LooseCookedWriter::~LooseCookedWriter() = default;

auto LooseCookedWriter::SetSourceKey(std::optional<data::SourceKey> key) -> void
{
  impl_->SetSourceKey(key);
}

auto LooseCookedWriter::SetContentVersion(const uint16_t version) -> void
{
  impl_->SetContentVersion(version);
}

auto LooseCookedWriter::WriteAssetDescriptor(const data::AssetKey& key,
  const data::AssetType asset_type, std::string_view virtual_path,
  std::string_view descriptor_relpath, const std::span<const std::byte> bytes,
  const data::AssetReferences& references, const LooseCookedProducer& producer)
  -> void
{
  impl_->WriteAssetDescriptor(key, asset_type, virtual_path, descriptor_relpath,
    bytes, references, producer);
}

auto LooseCookedWriter::WriteFile(const FileKind kind, std::string_view relpath,
  const std::span<const std::byte> bytes) -> void
{
  impl_->WriteFile(kind, relpath, bytes);
}

auto LooseCookedWriter::RegisterExternalFile(const FileKind kind,
  std::string_view relpath, const LooseCookedFileDigest& produced) -> void
{
  impl_->RegisterExternalFile(kind, relpath, produced);
}

auto LooseCookedWriter::RegisterExternalDataFile(
  const FileKind kind, std::string_view relpath) -> void
{
  impl_->RegisterExternalFile(kind, relpath, std::nullopt);
}

auto LooseCookedWriter::RegisterExternalTextureDescriptor(
  std::string_view relpath, std::string_view virtual_path,
  const LooseCookedProducer& producer, const LooseCookedFileDigest& produced)
  -> void
{
  impl_->RegisterExternalTextureDescriptor(
    relpath, virtual_path, producer, produced);
}

auto LooseCookedWriter::RegisterProducedAssetDescriptor(
  const data::AssetKey& key, const data::AssetType asset_type,
  std::string_view virtual_path, std::string_view descriptor_relpath,
  const LooseCookedFileDigest& produced,
  const data::AssetReferences& references, const LooseCookedProducer& producer,
  const std::span<const data::loose_cooked::IndexedRequirement> requirements)
  -> void
{
  impl_->RegisterProducedAssetDescriptor(key, asset_type, virtual_path,
    descriptor_relpath, produced, references, producer, requirements);
}

auto LooseCookedWriter::RegisterExternalAssetDescriptor(
  const data::AssetKey& key, const data::AssetType asset_type,
  std::string_view virtual_path, std::string_view descriptor_relpath,
  const data::AssetReferences& references, const LooseCookedProducer& producer)
  -> void
{
  impl_->RegisterExternalAssetDescriptor(
    key, asset_type, virtual_path, descriptor_relpath, references, producer);
}

auto LooseCookedWriter::RegisterExternalAuxiliaryFile(std::string_view relpath,
  std::string_view virtual_path, const LooseCookedProducer& producer,
  const LooseCookedFileDigest& produced) -> void
{
  impl_->RegisterExternalAuxiliaryFile(
    relpath, virtual_path, producer, produced);
}

auto LooseCookedWriter::RemoveAssetDescriptor(std::string_view virtual_path)
  -> void
{
  impl_->RemoveAssetDescriptor(virtual_path);
}

auto LooseCookedWriter::RemoveAuxiliaryFile(std::string_view relpath) -> void
{
  impl_->RemoveAuxiliaryFile(relpath);
}

auto LooseCookedWriter::RetireUnwrittenOutputs(const LooseCookedRecook& recook)
  -> void
{
  impl_->RetireUnwrittenOutputs(recook);
}

auto LooseCookedWriter::Finish() -> LooseCookedWriteResult
{
  return impl_->Finish();
}

} // namespace oxygen::content::import
