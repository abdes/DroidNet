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
#include <limits>
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
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/Integrity.h>
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
    //! The digest its producer computed as it wrote the file; without one,
    //! `Finish` hashes the file.
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

} // namespace

struct LooseCookedWriter::Impl final {
  Impl(std::filesystem::path cooked_root, cooked::CookerStorage& storage)
    : storage_(make_observer(&storage))
    , cooked_root_(std::move(cooked_root))
  {
    const auto lock = content::internal::LooseCookedIndexImpl::LockFileShared(
      IndexPath(cooked_root_));
    LoadExistingIndexIfPresent_();
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
      .descriptor_relpath = std::string(descriptor_relpath),
      .descriptor_size = bytes.size(),
      .descriptor_sha256 = digest,
      .references = references,
      .source_path = ResolveSource_(producer),
    };

    RequireOwnIdentity_(
      record.virtual_path, record.source_path, "WriteAssetDescriptor");
    const auto path_on_disk
      = cooked_root_ / std::filesystem::path(descriptor_relpath);
    WriteBinaryFile(*storage_, path_on_disk, bytes);

    InsertAsset_(record, "WriteAssetDescriptor");
    written_virtual_paths_.emplace(virtual_path);
  }

  auto WriteFile(const FileKind kind, std::string_view relpath,
    std::span<const std::byte> bytes) -> void
  {
    data::loose_cooked::RequireIndexRelPath(relpath);

    const auto path_on_disk = cooked_root_ / std::filesystem::path(relpath);

    StoredFile record {
      .kind = kind,
      .relpath = std::string(relpath),
      .size = bytes.size(),
      .sha256 = base::ComputeSha256(bytes),
      .updated = true,
      .externally_written = false,
    };

    StoreFile_(std::move(record), "WriteFile");
    WriteBinaryFile(*storage_, path_on_disk, bytes);
  }

  auto RegisterExternalFile(const FileKind kind, std::string_view relpath,
    const std::optional<LooseCookedFileDigest>& produced,
    std::string_view virtual_path = {}, std::string source_path = {}) -> void
  {
    data::loose_cooked::RequireIndexRelPath(relpath);
    if (kind == FileKind::kTextureDescriptor) {
      RequireCanonicalVirtualPath(virtual_path);
      RequireCanonicalVirtualPath(source_path);
    } else if (!virtual_path.empty()) {
      throw std::runtime_error(
        "RegisterExternalFile: only texture descriptors have virtual paths");
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
    const std::optional<LooseCookedFileDigest>& produced) -> void
  {
    RegisterExternalFile(FileKind::kTextureDescriptor, relpath, produced,
      virtual_path, ResolveSource_(producer));
  }

  auto RegisterExternalAuxiliaryFile(std::string_view relpath,
    const LooseCookedProducer& producer,
    const std::optional<LooseCookedFileDigest>& produced) -> void
  {
    RegisterExternalFile(
      FileKind::kAuxiliary, relpath, produced, {}, ResolveSource_(producer));
  }

  auto RegisterProducedAssetDescriptor(const data::AssetKey& key,
    const data::AssetType asset_type, std::string_view virtual_path,
    std::string_view descriptor_relpath, const LooseCookedFileDigest& produced,
    const data::AssetReferences& references,
    const LooseCookedProducer& producer) -> void
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
    const auto bytes = cooked::ReadWholeFile(*storage_, path_on_disk);
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
    const auto index_path = IndexPath(cooked_root_);
    // Merge with the latest published index and replace it as one step for
    // every in-process writer and reader of this cooked root.
    const auto lock
      = content::internal::LooseCookedIndexImpl::LockFileExclusive(index_path);

    auto current_assets = assets_;
    auto current_files = files_;

    LoadExistingIndexIfPresent_();

    for (const auto& [key, asset] : current_assets) {
      static_cast<void>(key);
      if (written_virtual_paths_.contains(asset.virtual_path)) {
        InsertAsset_(asset, "Finish.merge_assets");
      }
    }

    for (auto& [path, file] : current_files) {
      static_cast<void>(path);
      if (!file.updated) {
        continue; // Preserve other writers' published metadata.
      }
      if (file.externally_written && file.produced.has_value()) {
        // Its producer hashed the bytes as it wrote them.
        const auto physical_path = cooked_root_ / file.relpath;
        file.size = SizeOfExistingFile(*storage_, physical_path, "Finish");
        RequireProducedSize(file.produced, file.size, physical_path);
        file.sha256 = file.produced->sha256;
      } else if (file.externally_written) {
        // A resource data file: every import appends to it, so none of them
        // knows the digest of the whole file.
        const auto physical_path = cooked_root_ / file.relpath;
        const auto device = storage_->Open(physical_path);
        if (!device) {
          throw std::system_error(
            device.error(), "cannot open '" + physical_path.string() + "'");
        }
        const auto size = device->Size();
        if (!size) {
          throw std::system_error(
            size.error(), "cannot measure '" + physical_path.string() + "'");
        }
        file.size = size->get();
        const auto digest = serio::ComputeSha256(*device);
        if (!digest) {
          throw std::runtime_error("cannot hash '" + physical_path.string()
            + "': " + digest.error().message());
        }
        file.sha256 = *digest;
      }
      StoreFile_(std::move(file), "Finish.merge_files");
    }

    auto removed_files = ApplyRemovals_(current_files);

    const auto cooked_root_str = cooked_root_.string();
    LOG_SCOPE_F(INFO, "Finish {}", cooked_root_str);

    ValidateRequiredFilePairs_();

    const auto source_key = ResolveSourceKey_();
    const auto content_version = ResolveContentVersion_();

    LooseCookedWriteResult out {
      .cooked_root = cooked_root_,
      .source_key = source_key,
      .content_version = content_version,
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
        .assets = out.assets,
        .files = out.files,
      });
    DeleteRemovedFiles_(out.removed_files);

    return out;
  }

private:
  //! Requires the index file lock and the merged index.
  [[nodiscard]] auto ApplyRemovals_(
    const std::unordered_map<std::string, StoredFile>& session_files)
    -> std::vector<std::string>
  {
    std::vector<std::string> removed;
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
      auto descriptor = asset->second.descriptor_relpath;
      ForgetAsset_(asset->first);
      LOG_F(INFO, "Removed cooked asset '{}' ({})", virtual_path, descriptor);
      removed.push_back(std::move(descriptor));
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
      removed.push_back(relpath);
    }

    RetireUnwrittenOutputs_(session_files, removed);

    // A descriptor still listed by a remaining asset stays on disk.
    std::erase_if(removed, [&](const std::string& relpath) -> bool {
      return std::ranges::any_of(assets_, [&](const auto& item) -> bool {
        return item.second.descriptor_relpath == relpath;
      });
    });
    return removed;
  }

  //! Retires the outputs the index records for a re-cooked source that this
  //! writer does not write. Requires the index file lock and the merged index.
  auto RetireUnwrittenOutputs_(
    const std::unordered_map<std::string, StoredFile>& session_files,
    std::vector<std::string>& removed) -> void
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
      removed.push_back(asset.descriptor_relpath);
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
      removed.push_back(relpath);
    }
  }

  //! Deletes files the published index no longer lists, and the folders they
  //! leave empty, so a later write names its folders anew: on a file system
  //! that ignores case, a kept folder would keep a name renamed only in case.
  auto DeleteRemovedFiles_(const std::vector<std::string>& relpaths) const
    -> void
  {
    for (const auto& relpath : relpaths) {
      if (const auto removed
        = storage_->Remove(cooked_root_ / std::filesystem::path(relpath));
        !removed) {
        throw std::system_error(removed.error(),
          "Failed to delete removed cooked file '" + relpath + "'");
      }
      for (auto folder = std::filesystem::path(relpath).parent_path();
        !folder.empty(); folder = folder.parent_path()) {
        const auto removed = storage_->RemoveEmptyFolder(cooked_root_ / folder);
        if (!removed) {
          throw std::system_error(removed.error(),
            "Failed to delete emptied cooked folder '" + folder.generic_string()
              + "'");
        }
        if (!*removed) {
          break;
        }
      }
    }
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
      // A root holds one table or data file of each kind, at one path.
      const auto existing
        = std::ranges::find_if(files_, [&](const auto& item) -> auto {
            return item.second.kind == file.kind;
          });
      if (existing != files_.end() && existing->first != file.relpath) {
        throw std::runtime_error(std::string(context) + ": file kind "
          + std::to_string(static_cast<uint16_t>(file.kind)) + " is at '"
          + existing->first + "', not at '" + file.relpath + "'");
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

  //! Requires the index file lock.
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
      auto bytes = cooked::ReadWholeFile(*storage_, index_path);
      if (!bytes) {
        throw std::runtime_error(
          "Failed to read existing index: " + bytes.error().message());
      }
      const auto index
        = oxygen::content::internal::LooseCookedIndexImpl::LoadFromBytes(
          std::move(*bytes));
      existing_guid_ = index.Guid();
      existing_content_version_ = index.ContentVersion();

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

      DLOG_F(INFO, "Loaded existing loose cooked index: assets={}, files={}",
        assets_.size(), files_.size());
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

    LOG_F(INFO, "Wrote loose cooked index: assets={}, files={}",
      content.assets.size(), content.files.size());
  }

  //! Where the cooked root is stored: every read, write and removal.
  observer_ptr<cooked::CookerStorage> storage_;
  std::filesystem::path cooked_root_;

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
  std::string_view relpath,
  const std::optional<LooseCookedFileDigest>& produced) -> void
{
  impl_->RegisterExternalFile(kind, relpath, produced);
}

auto LooseCookedWriter::RegisterExternalTextureDescriptor(
  std::string_view relpath, std::string_view virtual_path,
  const LooseCookedProducer& producer,
  const std::optional<LooseCookedFileDigest>& produced) -> void
{
  impl_->RegisterExternalTextureDescriptor(
    relpath, virtual_path, producer, produced);
}

auto LooseCookedWriter::RegisterProducedAssetDescriptor(
  const data::AssetKey& key, const data::AssetType asset_type,
  std::string_view virtual_path, std::string_view descriptor_relpath,
  const LooseCookedFileDigest& produced,
  const data::AssetReferences& references, const LooseCookedProducer& producer)
  -> void
{
  impl_->RegisterProducedAssetDescriptor(key, asset_type, virtual_path,
    descriptor_relpath, produced, references, producer);
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
  const LooseCookedProducer& producer,
  const std::optional<LooseCookedFileDigest>& produced) -> void
{
  impl_->RegisterExternalAuxiliaryFile(relpath, producer, produced);
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
