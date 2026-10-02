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
#include <ios>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Endian.h>
#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Content/AssetValidation.h>
#include <Oxygen/Content/Internal/LooseCookedIndexCodec.h> // IWYU pragma: keep
#include <Oxygen/Content/Internal/LooseCookedIndexImpl.h>
#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import {

namespace {

  using data::loose_cooked::AssetEntry;
  using data::loose_cooked::FileKind;
  using data::loose_cooked::FileRecord;
  using data::loose_cooked::IndexHeader;

  constexpr std::string_view kIndexFileName = "container.index.bin";

  auto ThrowOnError(const Result<void>& result, std::string_view what) -> void
  {
    if (!result) {
      throw std::runtime_error(
        std::string(what) + ": " + result.error().message());
    }
  }

  auto IsAllZeros(std::span<const uint8_t> bytes) noexcept -> bool
  {
    return std::ranges::all_of(
      bytes, [](const auto b) -> auto { return b == 0; });
  }

  auto ValidateNoDotSegments(
    const std::string_view path, const std::string_view what) -> void
  {
    size_t pos = 0;
    while (pos <= path.size()) {
      const auto next = path.find('/', pos);
      const auto len
        = (next == std::string_view::npos) ? (path.size() - pos) : (next - pos);
      const auto segment = path.substr(pos, len);
      if (segment == ".") {
        throw std::runtime_error(std::string(what) + " must not contain '.'");
      }
      if (segment == "..") {
        throw std::runtime_error(std::string(what) + " must not contain '..'");
      }

      if (next == std::string_view::npos) {
        break;
      }
      pos = next + 1;
    }
  }

  auto ValidateRelativePath(const std::string_view relpath) -> void
  {
    if (relpath.empty()) {
      throw std::runtime_error("Index path must not be empty");
    }

    if (relpath.contains('\\')) {
      throw std::runtime_error("Index path must use '/' as the separator");
    }
    if (relpath.contains(':')) {
      throw std::runtime_error("Index path must not contain ':'");
    }
    if (relpath.front() == '/') {
      throw std::runtime_error("Index path must be container-relative");
    }
    if (relpath.back() == '/') {
      throw std::runtime_error("Index path must not end with '/'");
    }
    if (relpath.contains("//")) {
      throw std::runtime_error("Index path must not contain '//'");
    }

    ValidateNoDotSegments(relpath, "Index path");

    std::filesystem::path p(relpath);
    if (p.is_absolute() || p.has_root_path() || p.has_root_name()) {
      throw std::runtime_error("Index path must be container-relative");
    }

    for (const auto& part : p) {
      if (part == "..") {
        throw std::runtime_error("Index path must not contain '..'");
      }
    }
  }

  auto ValidateVirtualPath(const std::string_view virtual_path) -> void
  {
    if (const auto error
      = oxygen::content::ValidateCanonicalVirtualPath(virtual_path,
        oxygen::content::VirtualPathRuleSet::kSyntaxAndStandardMountRoot);
      error.has_value()) {
      throw std::runtime_error(
        "Virtual path is not canonical: " + std::string(*error));
    }
  }

  class StringTableBuilder final {
  public:
    StringTableBuilder() { table_.push_back('\0'); }

    [[nodiscard]] auto Add(std::string_view s) -> uint32_t
    {
      const auto it = offset_by_string_.find(std::string(s));
      if (it != offset_by_string_.end()) {
        return it->second;
      }

      if (table_.size() > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("String table too large");
      }

      const auto offset = static_cast<uint32_t>(table_.size());
      table_.append(s);
      table_.push_back('\0');

      offset_by_string_.insert_or_assign(std::string(s), offset);
      return offset;
    }

    [[nodiscard]] auto Bytes() const noexcept -> std::span<const std::byte>
    {
      return std::as_bytes(std::span(table_.data(), table_.size()));
    }

    [[nodiscard]] auto SizeBytes() const noexcept -> uint64_t
    {
      return table_.size();
    }

  private:
    std::string table_;
    std::unordered_map<std::string, uint32_t> offset_by_string_;
  };

  struct StoredAsset final {
    data::AssetKey key {};
    data::AssetType asset_type = data::AssetType::kUnknown;
    std::string virtual_path;
    std::string descriptor_relpath;
    uint64_t descriptor_size = 0;
    std::array<uint8_t, data::loose_cooked::kSha256Size> descriptor_sha256 = {};
    data::AssetReferences references;
  };

  struct StoredFile final {
    FileKind kind = FileKind::kUnknown;
    std::string relpath;
    uint64_t size = 0;
    base::Sha256Digest sha256 {};
    bool updated = false;
    bool externally_written = false;
  };

  [[nodiscard]] auto IsEquivalent(
    const StoredAsset& lhs, const StoredAsset& rhs) -> bool
  {
    return lhs.key == rhs.key && lhs.asset_type == rhs.asset_type
      && lhs.virtual_path == rhs.virtual_path
      && lhs.descriptor_relpath == rhs.descriptor_relpath
      && lhs.descriptor_size == rhs.descriptor_size
      && lhs.descriptor_sha256 == rhs.descriptor_sha256
      && lhs.references == rhs.references;
  }

  [[nodiscard]] auto IsEquivalent(const StoredFile& lhs, const StoredFile& rhs)
    -> bool
  {
    return lhs.kind == rhs.kind && lhs.relpath == rhs.relpath
      && lhs.size == rhs.size;
  }

  [[nodiscard]] auto BuildVirtualPathCollisionMessage(std::string_view context,
    const StoredAsset& incoming, const data::AssetKey& existing_key,
    const StoredAsset* existing) -> std::string
  {
    auto message = std::string(
      "Conflicting virtual path mapping in loose cooked container");
    message += ": context=";
    message += context;
    message += " virtual_path='";
    message += incoming.virtual_path;
    message += "' existing_key=";
    message += data::to_string(existing_key);
    message += " incoming_key=";
    message += data::to_string(incoming.key);
    message += " incoming_descriptor='";
    message += incoming.descriptor_relpath;
    message += "' incoming_type=";
    message += std::to_string(static_cast<uint16_t>(incoming.asset_type));

    if (existing != nullptr) {
      message += " existing_descriptor='";
      message += existing->descriptor_relpath;
      message += "' existing_type=";
      message += std::to_string(static_cast<uint16_t>(existing->asset_type));
    } else {
      message += " existing_descriptor=<missing-or-current-session>";
    }

    return message;
  }

  auto WriteBinaryFile(const std::filesystem::path& path,
    const std::span<const std::byte> bytes) -> void
  {
    std::filesystem::create_directories(base::ToNativePath(path.parent_path()));

    serio::FileStream stream(path, std::ios::out | std::ios::trunc);
    ThrowOnError(stream.Write(bytes), "Failed to write cooked file");
    ThrowOnError(stream.Flush(), "Failed to flush cooked file");
  }

  auto ReadIndexHeaderOrThrow(const std::filesystem::path& index_path)
    -> IndexHeader
  {
    serio::FileStream stream(index_path, std::ios::in);
    serio::Reader reader(stream);

    auto header_result = reader.Read<IndexHeader>();
    if (!header_result) {
      throw std::runtime_error("Failed to read existing index header: "
        + header_result.error().message());
    }

    return header_result.value();
  }

  auto CopyDigestOrZero(const std::optional<base::Sha256Digest>& digest)
    -> std::array<uint8_t, data::loose_cooked::kSha256Size>
  {
    std::array<uint8_t, data::loose_cooked::kSha256Size> out = {};
    if (!digest.has_value()) {
      return out;
    }
    std::copy_n(digest->begin(), out.size(), out.begin());
    return out;
  }

  auto GetCookedRootLock(const std::filesystem::path& cooked_root)
    -> std::shared_ptr<std::mutex>
  {
    static std::mutex map_mutex;
    static std::unordered_map<std::string, std::shared_ptr<std::mutex>> locks;

    const auto key = base::PathIdentityKey(cooked_root);
    std::scoped_lock lock(map_mutex);
    auto it = locks.find(key);
    if (it != locks.end()) {
      return it->second;
    }
    auto created = std::make_shared<std::mutex>();
    locks.emplace(key, created);
    return created;
  }

} // namespace

struct LooseCookedWriter::Impl final {
  explicit Impl(std::filesystem::path cooked_root)
    : cooked_root_(std::move(cooked_root))
  {
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

  auto SetCollisionPolicy(const LooseCookedWriter::CollisionPolicy policy)
    -> void
  {
    collision_policy_ = policy;
  }

  auto HandleAssetCollision_(const data::AssetKey& key,
    const StoredAsset& existing, const StoredAsset& incoming,
    std::string_view context) -> bool
  {
    if (IsEquivalent(existing, incoming)) {
      return true;
    }
    ++collision_summary_.asset_collisions;
    if (collision_policy_ == LooseCookedWriter::CollisionPolicy::kError) {
      ++collision_summary_.rejected;
      throw std::runtime_error(std::string(context)
        + ": asset collision for key " + data::to_string(key));
    }
    if (collision_policy_
      == LooseCookedWriter::CollisionPolicy::kWarnKeepExisting) {
      ++collision_summary_.kept_existing;
      LOG_F(WARNING,
        "{}: asset collision for key {} policy=warn_keep_existing action=keep",
        context, data::to_string(key));
      return false;
    }
    ++collision_summary_.replaced_existing;
    LOG_F(WARNING,
      "{}: asset collision for key {} policy=warn_replace action=replace",
      context, data::to_string(key));
    return true;
  }

  auto HandleFileCollision_(const FileKind kind, const StoredFile& existing,
    const StoredFile& incoming, std::string_view context) -> bool
  {
    if (IsEquivalent(existing, incoming)
      || (incoming.externally_written
        && existing.relpath == incoming.relpath)) {
      // Registering the new extent of the same externally written file is a
      // metadata refresh. A collision changes which file owns this kind.
      return true;
    }
    ++collision_summary_.file_collisions;
    if (collision_policy_ == LooseCookedWriter::CollisionPolicy::kError) {
      ++collision_summary_.rejected;
      throw std::runtime_error(std::string(context)
        + ": file collision for kind "
        + std::to_string(static_cast<uint16_t>(kind)));
    }
    if (collision_policy_
      == LooseCookedWriter::CollisionPolicy::kWarnKeepExisting) {
      ++collision_summary_.kept_existing;
      LOG_F(WARNING,
        "{}: file collision for kind={} policy=warn_keep_existing action=keep",
        context, static_cast<uint16_t>(kind));
      return false;
    }
    ++collision_summary_.replaced_existing;
    LOG_F(WARNING,
      "{}: file collision for kind={} policy=warn_replace action=replace",
      context, static_cast<uint16_t>(kind));
    return true;
  }

  auto HandleVirtualPathCollision(
    const StoredAsset& incoming, std::string_view context) -> bool
  {
    const auto existing_key_it
      = key_by_virtual_path_.find(incoming.virtual_path);
    if (existing_key_it == key_by_virtual_path_.end()) {
      return true;
    }

    const auto existing_key = existing_key_it->second;
    if (existing_key == incoming.key) {
      return true;
    }

    const auto existing_asset_it = assets_.find(existing_key);
    if (existing_asset_it == assets_.end()
      || !loaded_asset_keys_.contains(existing_key)) {
      throw std::runtime_error(BuildVirtualPathCollisionMessage(
        context, incoming, existing_key, nullptr));
    }

    const auto& existing = existing_asset_it->second;
    if (existing.asset_type != incoming.asset_type
      || existing.descriptor_relpath != incoming.descriptor_relpath) {
      throw std::runtime_error(BuildVirtualPathCollisionMessage(
        context, incoming, existing_key, &existing));
    }

    const auto already_replaced
      = replaced_loaded_virtual_paths_.contains(incoming.virtual_path);

    if (!already_replaced) {
      ++collision_summary_.asset_collisions;
    }
    if (collision_policy_ == LooseCookedWriter::CollisionPolicy::kError) {
      ++collision_summary_.rejected;
      throw std::runtime_error(std::string(context)
        + ": virtual path collision for " + incoming.virtual_path);
    }
    if (collision_policy_
      == LooseCookedWriter::CollisionPolicy::kWarnKeepExisting) {
      if (!already_replaced) {
        ++collision_summary_.kept_existing;
        LOG_F(WARNING,
          "{}: virtual path collision for '{}' policy=warn_keep_existing "
          "action=keep existing_key={} incoming_key={} descriptor='{}'",
          context, incoming.virtual_path, data::to_string(existing_key),
          data::to_string(incoming.key), incoming.descriptor_relpath);
      }
      return false;
    }

    if (!already_replaced) {
      ++collision_summary_.replaced_existing;
      LOG_F(WARNING,
        "{}: virtual path collision for '{}' policy=warn_replace "
        "action=replace existing_key={} incoming_key={} descriptor='{}'",
        context, incoming.virtual_path, data::to_string(existing_key),
        data::to_string(incoming.key), incoming.descriptor_relpath);
      replaced_loaded_virtual_paths_.insert(incoming.virtual_path);
    }
    assets_.erase(existing_asset_it);
    loaded_asset_keys_.erase(existing_key);
    return true;
  }

  auto WriteAssetDescriptor(const data::AssetKey& key,
    data::AssetType asset_type, std::string_view virtual_path,
    std::string_view descriptor_relpath, std::span<const std::byte> bytes,
    const data::AssetReferences& references) -> void
  {
    ValidateVirtualPath(virtual_path);
    ValidateRelativePath(descriptor_relpath);
    ValidateAssetDescriptor(asset_type, key, bytes, references);

    const auto digest = base::ComputeSha256(bytes);

    StoredAsset record {
      .key = key,
      .asset_type = asset_type,
      .virtual_path = std::string(virtual_path),
      .descriptor_relpath = std::string(descriptor_relpath),
      .descriptor_size = bytes.size(),
      .descriptor_sha256 = CopyDigestOrZero(digest),
      .references = references,
    };

    if (!HandleVirtualPathCollision(record, "WriteAssetDescriptor")) {
      return;
    }

    if (const auto existing_it = assets_.find(key); existing_it != assets_.end()
      && !HandleAssetCollision_(
        key, existing_it->second, record, "WriteAssetDescriptor")) {
      return;
    }

    const auto path_on_disk
      = cooked_root_ / std::filesystem::path(descriptor_relpath);
    WriteBinaryFile(path_on_disk, bytes);

    assets_.insert_or_assign(key, record);
    key_by_virtual_path_.insert_or_assign(std::string(virtual_path), key);
  }

  auto WriteFile(const FileKind kind, std::string_view relpath,
    std::span<const std::byte> bytes) -> void
  {
    ValidateRelativePath(relpath);

    const auto path_on_disk = cooked_root_ / std::filesystem::path(relpath);

    StoredFile record {
      .kind = kind,
      .relpath = std::string(relpath),
      .size = bytes.size(),
      .sha256 = base::ComputeSha256(bytes),
      .updated = true,
      .externally_written = false,
    };

    if (StoreFile_(std::move(record), "WriteFile")) {
      WriteBinaryFile(path_on_disk, bytes);
    }
  }

  auto RegisterExternalFile(const FileKind kind, std::string_view relpath)
    -> void
  {
    ValidateRelativePath(relpath);

    const auto path_on_disk = cooked_root_ / std::filesystem::path(relpath);

    std::error_code ec;
    if (!std::filesystem::exists(base::ToNativePath(path_on_disk), ec)) {
      throw std::runtime_error(
        "RegisterExternalFile: file does not exist: " + path_on_disk.string());
    }

    const auto size
      = std::filesystem::file_size(base::ToNativePath(path_on_disk), ec);
    if (ec) {
      throw std::runtime_error("RegisterExternalFile: failed to get file size: "
        + path_on_disk.string());
    }

    StoredFile record {
      .kind = kind,
      .relpath = std::string(relpath),
      .size = size,
      .updated = true,
      .externally_written = true,
    };

    static_cast<void>(StoreFile_(std::move(record), "RegisterExternalFile"));
  }

  auto RegisterExternalAssetDescriptor(const data::AssetKey& key,
    const data::AssetType asset_type, std::string_view virtual_path,
    std::string_view descriptor_relpath, uint64_t descriptor_size,
    const data::AssetReferences& references,
    std::optional<base::Sha256Digest> descriptor_sha256) -> void
  {
    ValidateVirtualPath(virtual_path);
    ValidateRelativePath(descriptor_relpath);

    const auto path_on_disk
      = cooked_root_ / std::filesystem::path(descriptor_relpath);

    std::error_code ec;
    if (!std::filesystem::exists(base::ToNativePath(path_on_disk), ec)) {
      throw std::runtime_error("RegisterExternalAssetDescriptor: file does not "
                               "exist: "
        + path_on_disk.string());
    }

    const auto size_on_disk
      = std::filesystem::file_size(base::ToNativePath(path_on_disk), ec);
    if (ec) {
      throw std::runtime_error(
        "RegisterExternalAssetDescriptor: failed to get file size: "
        + path_on_disk.string());
    }

    if (descriptor_size == 0) {
      descriptor_size = size_on_disk;
    } else if (descriptor_size != size_on_disk) {
      throw std::runtime_error(
        "RegisterExternalAssetDescriptor: size mismatch for: "
        + path_on_disk.string());
    }

    if (descriptor_size > std::numeric_limits<size_t>::max()) {
      throw std::runtime_error("Asset descriptor exceeds addressable size");
    }
    serio::FileStream<> stream(path_on_disk, std::ios::in);
    serio::Reader reader(stream);
    const auto bytes = reader.ReadBlob(static_cast<size_t>(descriptor_size));
    if (!bytes) {
      throw std::runtime_error(
        "Could not read registered descriptor: " + path_on_disk.string());
    }
    ValidateAssetDescriptor(asset_type, key, *bytes, references);
    if (!descriptor_sha256.has_value()) {
      descriptor_sha256 = base::ComputeSha256(*bytes);
    }

    StoredAsset record {
      .key = key,
      .asset_type = asset_type,
      .virtual_path = std::string(virtual_path),
      .descriptor_relpath = std::string(descriptor_relpath),
      .descriptor_size = descriptor_size,
      .descriptor_sha256 = CopyDigestOrZero(descriptor_sha256),
      .references = references,
    };

    if (!HandleVirtualPathCollision(
          record, "RegisterExternalAssetDescriptor")) {
      return;
    }

    if (const auto existing_it = assets_.find(key); existing_it != assets_.end()
      && !HandleAssetCollision_(
        key, existing_it->second, record, "RegisterExternalAssetDescriptor")) {
      return;
    }
    assets_.insert_or_assign(key, record);
    key_by_virtual_path_.insert_or_assign(std::string(virtual_path), key);
  }

  [[nodiscard]] auto Finish() -> LooseCookedWriteResult
  {
    auto cooked_root_lock = GetCookedRootLock(cooked_root_);
    std::scoped_lock lock(*cooked_root_lock);

    auto current_assets = assets_;
    auto current_files = files_;

    assets_.clear();
    files_.clear();
    key_by_virtual_path_.clear();
    loaded_asset_keys_.clear();

    LoadExistingIndexIfPresent_();

    for (const auto& [key, asset] : current_assets) {
      if (!HandleVirtualPathCollision(asset, "Finish.merge_assets")) {
        continue;
      }
      if (const auto existing = assets_.find(key); existing != assets_.end()
        && !HandleAssetCollision_(
          key, existing->second, asset, "Finish.merge_assets")) {
        continue;
      }
      assets_.insert_or_assign(key, asset);
      key_by_virtual_path_.insert_or_assign(asset.virtual_path, key);
    }

    for (auto& [path, file] : current_files) {
      static_cast<void>(path);
      if (!file.updated) {
        continue; // Preserve other writers' published metadata.
      }
      if (file.externally_written) {
        const auto physical_path = cooked_root_ / file.relpath;
        file.size
          = std::filesystem::file_size(base::ToNativePath(physical_path));
        file.sha256 = base::ComputeFileSha256(physical_path);
      }
      static_cast<void>(StoreFile_(std::move(file), "Finish.merge_files"));
    }

    const auto cooked_root_str = cooked_root_.string();
    LOG_SCOPE_F(INFO, fmt::format("Finish {}", cooked_root_str).c_str());

    ValidateRequiredFilePairs_();

    const auto source_key = ResolveSourceKey_();
    const auto content_version = ResolveContentVersion_();

    const auto index_path
      = cooked_root_ / std::filesystem::path(kIndexFileName);
    std::filesystem::create_directories(
      base::ToNativePath(index_path.parent_path()));

    WriteIndex_(index_path, source_key, content_version);

    LooseCookedWriteResult out {
      .cooked_root = cooked_root_,
      .source_key = source_key,
      .content_version = content_version,
      .collision_summary = collision_summary_,
    };

    out.assets.reserve(assets_.size());
    for (const auto& [k, a] : assets_) {
      (void)k;
      LooseCookedAssetRecord rec {
        .key = a.key,
        .asset_type = a.asset_type,
        .virtual_path = a.virtual_path,
        .descriptor_relpath = a.descriptor_relpath,
        .descriptor_size = a.descriptor_size,
        .descriptor_sha256 = {},
        .references = a.references,
      };

      if (!IsAllZeros(a.descriptor_sha256)) {
        base::Sha256Digest digest {};
        std::copy_n(a.descriptor_sha256.begin(), digest.size(), digest.begin());
        rec.descriptor_sha256 = digest;
      }

      out.assets.push_back(std::move(rec));
    }

    out.files.reserve(files_.size());
    for (const auto& [kind, f] : files_) {
      (void)kind;
      LooseCookedFileRecord rec {
        .kind = f.kind,
        .relpath = f.relpath,
        .size = f.size,
        .sha256 = f.sha256,
      };

      out.files.push_back(std::move(rec));
    }

    return out;
  }

private:
  [[nodiscard]] auto HasFileKind_(const FileKind kind) const -> bool
  {
    return std::ranges::any_of(
      files_, [kind](const auto& item) { return item.second.kind == kind; });
  }

  auto StoreFile_(StoredFile file, const std::string_view context) -> bool
  {
    const auto existing = file.kind == FileKind::kAuxiliary
      ? files_.find(file.relpath)
      : std::ranges::find_if(files_,
          [&](const auto& item) { return item.second.kind == file.kind; });
    if (existing != files_.end()) {
      if (!HandleFileCollision_(file.kind, existing->second, file, context)) {
        return false;
      }
      if (existing->first != file.relpath) {
        files_.erase(existing);
      }
    }
    const auto path = file.relpath;
    files_.insert_or_assign(path, std::move(file));
    return true;
  }

  auto LoadExistingIndexIfPresent_() -> void
  {
    const auto index_path
      = cooked_root_ / std::filesystem::path(kIndexFileName);
    if (!std::filesystem::exists(base::ToNativePath(index_path))) {
      return;
    }

    try {
      const auto header = ReadIndexHeaderOrThrow(index_path);
      const auto existing_guid
        = data::SourceKey::FromBytes(header.source_identity);
      if (!existing_guid.has_value()) {
        throw std::runtime_error(
          "Existing loose cooked index has invalid non-v7 source identity");
      }
      existing_guid_ = existing_guid.value();
      existing_content_version_ = header.content_version;

      const auto index
        = oxygen::content::internal::LooseCookedIndexImpl::LoadFromFile(
          index_path);

      for (const auto key : index.GetAllAssetKeys()) {
        const auto rel = index.FindDescriptorRelPath(key);
        const auto vpath = index.FindVirtualPath(key);
        const auto type_u8 = index.FindAssetType(key);
        const auto size = index.FindDescriptorSize(key);
        const auto sha = index.FindDescriptorSha256(key);
        auto references = index.FindAssetReferences(key);

        if (!rel || !vpath || !type_u8 || !size || !references) {
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
        };

        if (sha.has_value()) {
          std::copy_n(sha->begin(), record.descriptor_sha256.size(),
            record.descriptor_sha256.begin());
        }

        assets_.insert_or_assign(key, record);
        key_by_virtual_path_.insert_or_assign(record.virtual_path, key);
        loaded_asset_keys_.insert(key);
      }

      for (const auto& file : index.GetFileInventory()) {
        if (!file.kind.has_value()) {
          continue; // Asset records are restored above.
        }
        files_.emplace(file.relative_path,
          StoredFile {
            .kind = *file.kind,
            .relpath = file.relative_path,
            .size = file.size,
            .sha256 = file.sha256,
            .updated = false,
            .externally_written = false,
          });
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

  auto WriteIndex_(const std::filesystem::path& index_path,
    const data::SourceKey& source_key, const uint16_t content_version) -> void
  {
    if (!IsLittleEndian()) {
      throw std::runtime_error(
        "LooseCookedWriter currently requires little-endian host");
    }

    std::unordered_set<std::string> unique_virtual_paths;
    unique_virtual_paths.reserve(assets_.size());
    for (const auto& [key, asset] : assets_) {
      (void)key;
      if (!unique_virtual_paths.insert(asset.virtual_path).second) {
        throw std::runtime_error(
          "Duplicate virtual path string in loose cooked index");
      }
    }

    StringTableBuilder strings;

    std::vector<AssetEntry> asset_entries;
    asset_entries.reserve(assets_.size());

    std::vector<data::AssetKey> keys;
    keys.reserve(assets_.size());
    for (const auto& [key, asset] : assets_) {
      (void)asset;
      keys.push_back(key);
    }
    std::ranges::sort(keys);

    for (const auto& key : keys) {
      const auto& a = assets_.at(key);

      AssetEntry entry {};
      entry.asset_key = a.key;
      entry.descriptor_relpath_offset = strings.Add(a.descriptor_relpath);
      entry.virtual_path_offset = strings.Add(a.virtual_path);
      entry.asset_type = static_cast<uint8_t>(a.asset_type);
      entry.descriptor_size = a.descriptor_size;
      static_assert(
        sizeof(entry.descriptor_sha256) == sizeof(a.descriptor_sha256));
      std::copy_n(std::begin(a.descriptor_sha256),
        std::size(entry.descriptor_sha256),
        std::begin(entry.descriptor_sha256));

      asset_entries.push_back(entry);
    }

    std::vector<FileRecord> file_records;
    file_records.reserve(files_.size());

    std::vector<std::string> paths;
    paths.reserve(files_.size());
    for (const auto& [path, file] : files_) {
      static_cast<void>(file);
      paths.push_back(path);
    }
    std::ranges::sort(paths);
    for (const auto& path : paths) {
      const auto& file = files_.at(path);
      file_records.push_back(FileRecord {
        .kind = file.kind,
        .size = file.size,
        .relpath_offset = strings.Add(file.relpath),
        .sha256 = file.sha256,
      });
    }

    IndexHeader header {};
    header.version = data::loose_cooked::kIndexVersion;
    header.content_version = content_version;

    header.flags = data::loose_cooked::kHasVirtualPaths;
    if (!file_records.empty()) {
      header.flags |= data::loose_cooked::kHasFileRecords;
    }

    header.string_table_offset = sizeof(IndexHeader);
    header.string_table_size = strings.SizeBytes();

    header.asset_entries_offset
      = header.string_table_offset + header.string_table_size;
    header.asset_count = static_cast<uint32_t>(asset_entries.size());
    header.asset_entry_size = sizeof(AssetEntry);

    header.file_records_offset = header.asset_entries_offset
      + (asset_entries.size() * sizeof(AssetEntry));
    header.file_record_count = static_cast<uint32_t>(file_records.size());
    header.file_record_size = sizeof(FileRecord);

    auto reference_cursor = header.file_records_offset
      + (uint64_t { header.file_record_count } * sizeof(FileRecord));
    for (size_t i = 0; i < keys.size(); ++i) {
      const auto& references = assets_.at(keys.at(i)).references;
      auto& table = asset_entries.at(i).references;
      table.resource_count
        = static_cast<uint32_t>(references.Resources().size());
      table.key_count = static_cast<uint32_t>(references.Keys().size());
      const auto size = data::AssetReferences::EncodedSize(
        table.resource_count, table.key_count);
      if (size != 0U) {
        if (size > std::numeric_limits<uint64_t>::max() - reference_cursor) {
          throw std::overflow_error(
            "Loose cooked reference block offset overflow");
        }
        table.offset = reference_cursor;
        reference_cursor += size;
      }
    }

    const auto guid_bytes = data::as_bytes(source_key);
    std::ranges::transform(guid_bytes, std::begin(header.source_identity),
      [](const auto byte) -> auto { return std::to_integer<uint8_t>(byte); });

    serio::MemoryStream stream;
    serio::Writer writer(stream);

    ThrowOnError(writer.WriteBlob(std::as_bytes(std::span(&header, 1))),
      "Failed to write index header");

    ThrowOnError(
      writer.WriteBlob(strings.Bytes()), "Failed to write string table");

    for (const auto& e : asset_entries) {
      ThrowOnError(writer.WriteBlob(std::as_bytes(std::span(&e, 1))),
        "Failed to write asset entry");
    }

    for (const auto& r : file_records) {
      ThrowOnError(writer.WriteBlob(std::as_bytes(std::span(&r, 1))),
        "Failed to write file record");
    }

    for (const auto& key : keys) {
      const auto encoded = assets_.at(key).references.Encode();
      if (!encoded) {
        throw std::runtime_error(encoded.error());
      }
      ThrowOnError(
        writer.WriteBlob(*encoded), "Failed to write asset references");
    }

    ThrowOnError(writer.Flush(), "Failed to encode index file");
    const auto commit = serio::WriteFileAtomically(index_path, stream.Data());
    if (!commit) {
      throw std::system_error(
        commit.error(), "Failed to publish loose cooked index");
    }
    if (commit->durability_error) {
      throw std::system_error(commit->durability_error,
        "Loose cooked index published but directory synchronization failed");
    }

    LOG_F(INFO,
      "Wrote loose cooked index: assets={}, files={}, strings={} bytes",
      asset_entries.size(), file_records.size(), strings.SizeBytes());
  }

  std::filesystem::path cooked_root_;

  LooseCookedWriter::CollisionPolicy collision_policy_
    = LooseCookedWriter::CollisionPolicy::kWarnReplace;

  std::optional<data::SourceKey> source_key_override_;
  std::optional<uint16_t> content_version_override_;

  std::optional<data::SourceKey> existing_guid_;
  std::optional<uint16_t> existing_content_version_;

  std::unordered_map<data::AssetKey, StoredAsset> assets_;
  std::unordered_map<std::string, StoredFile> files_;
  std::unordered_map<std::string, data::AssetKey> key_by_virtual_path_;
  std::unordered_set<data::AssetKey> loaded_asset_keys_;
  std::unordered_set<std::string> replaced_loaded_virtual_paths_;
  LooseCookedCollisionSummary collision_summary_ {};
};

LooseCookedWriter::LooseCookedWriter(std::filesystem::path cooked_root)
  : impl_(std::make_unique<Impl>(std::move(cooked_root)))
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

auto LooseCookedWriter::SetCollisionPolicy(const CollisionPolicy policy) -> void
{
  impl_->SetCollisionPolicy(policy);
}

auto LooseCookedWriter::WriteAssetDescriptor(const data::AssetKey& key,
  const data::AssetType asset_type, std::string_view virtual_path,
  std::string_view descriptor_relpath, const std::span<const std::byte> bytes,
  const data::AssetReferences& references) -> void
{
  impl_->WriteAssetDescriptor(
    key, asset_type, virtual_path, descriptor_relpath, bytes, references);
}

auto LooseCookedWriter::WriteFile(const FileKind kind, std::string_view relpath,
  const std::span<const std::byte> bytes) -> void
{
  impl_->WriteFile(kind, relpath, bytes);
}

auto LooseCookedWriter::RegisterExternalFile(
  const FileKind kind, std::string_view relpath) -> void
{
  impl_->RegisterExternalFile(kind, relpath);
}

auto LooseCookedWriter::RegisterExternalAssetDescriptor(
  const data::AssetKey& key, const data::AssetType asset_type,
  std::string_view virtual_path, std::string_view descriptor_relpath,
  const uint64_t descriptor_size, const data::AssetReferences& references,
  std::optional<base::Sha256Digest> descriptor_sha256) -> void
{
  impl_->RegisterExternalAssetDescriptor(key, asset_type, virtual_path,
    descriptor_relpath, descriptor_size, references, descriptor_sha256);
}

auto LooseCookedWriter::Finish() -> LooseCookedWriteResult
{
  return impl_->Finish();
}

} // namespace oxygen::content::import
