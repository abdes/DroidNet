//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Base/Endian.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/Detail/LooseCookedIndex.h>
#include <Oxygen/Content/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Content/Internal/LooseCookedIndexLoad.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import {

namespace {

  using data::loose_cooked::v1::AssetEntry;
  using data::loose_cooked::v1::FileKind;
  using data::loose_cooked::v1::FileRecord;
  using data::loose_cooked::v1::IndexHeader;

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
    return std::ranges::all_of(bytes, [](const auto b) { return b == 0; });
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
    if (virtual_path.empty()) {
      throw std::runtime_error("Virtual path must not be empty");
    }
    if (virtual_path.contains('\\')) {
      throw std::runtime_error("Virtual path must use '/' as the separator");
    }
    if (virtual_path.front() != '/') {
      throw std::runtime_error("Virtual path must start with '/'");
    }
    if (virtual_path.size() > 1 && virtual_path.back() == '/') {
      throw std::runtime_error(
        "Virtual path must not end with '/' (except the root)");
    }
    if (virtual_path.contains("//")) {
      throw std::runtime_error("Virtual path must not contain '//'");
    }

    ValidateNoDotSegments(virtual_path, "Virtual path");
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

      if (table_.size() > (std::numeric_limits<uint32_t>::max)()) {
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
    std::array<uint8_t, data::loose_cooked::v1::kSha256Size> descriptor_sha256
      = {};
  };

  struct StoredFile final {
    FileKind kind = FileKind::kUnknown;
    std::string relpath;
    uint64_t size = 0;
  };

  auto WriteBinaryFile(const std::filesystem::path& path,
    const std::span<const std::byte> bytes) -> void
  {
    std::filesystem::create_directories(path.parent_path());

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
    -> std::array<uint8_t, data::loose_cooked::v1::kSha256Size>
  {
    std::array<uint8_t, data::loose_cooked::v1::kSha256Size> out = {};
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

    const auto key = cooked_root.lexically_normal().string();
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

  auto SetComputeSha256(bool enabled) -> void { compute_sha256_ = enabled; }

  auto WriteAssetDescriptor(const data::AssetKey& key,
    data::AssetType asset_type, std::string_view virtual_path,
    std::string_view descriptor_relpath, std::span<const std::byte> bytes)
    -> void
  {
    ValidateVirtualPath(virtual_path);
    ValidateRelativePath(descriptor_relpath);

    if (const auto existing_it
      = key_by_virtual_path_.find(std::string(virtual_path));
      existing_it != key_by_virtual_path_.end() && existing_it->second != key) {
      throw std::runtime_error(
        "Conflicting virtual path mapping in loose cooked container");
    }

    const auto path_on_disk
      = cooked_root_ / std::filesystem::path(descriptor_relpath);
    WriteBinaryFile(path_on_disk, bytes);

    std::optional<base::Sha256Digest> digest;
    if (compute_sha256_) {
      digest = base::ComputeSha256(bytes);
    }

    StoredAsset record {
      .key = key,
      .asset_type = asset_type,
      .virtual_path = std::string(virtual_path),
      .descriptor_relpath = std::string(descriptor_relpath),
      .descriptor_size = (bytes.size()),
      .descriptor_sha256 = CopyDigestOrZero(digest),
    };

    assets_.insert_or_assign(key, record);
    key_by_virtual_path_.insert_or_assign(std::string(virtual_path), key);
  }

  auto WriteFile(const FileKind kind, std::string_view relpath,
    std::span<const std::byte> bytes) -> void
  {
    ValidateRelativePath(relpath);

    const auto path_on_disk = cooked_root_ / std::filesystem::path(relpath);
    WriteBinaryFile(path_on_disk, bytes);

    StoredFile record {
      .kind = kind,
      .relpath = std::string(relpath),
      .size = (bytes.size()),
    };

    files_.insert_or_assign(kind, record);
  }

  auto RegisterExternalFile(const FileKind kind, std::string_view relpath)
    -> void
  {
    ValidateRelativePath(relpath);

    const auto path_on_disk = cooked_root_ / std::filesystem::path(relpath);

    std::error_code ec;
    if (!std::filesystem::exists(path_on_disk, ec)) {
      throw std::runtime_error(
        "RegisterExternalFile: file does not exist: " + path_on_disk.string());
    }

    const auto size = std::filesystem::file_size(path_on_disk, ec);
    if (ec) {
      throw std::runtime_error("RegisterExternalFile: failed to get file size: "
        + path_on_disk.string());
    }

    StoredFile record {
      .kind = kind,
      .relpath = std::string(relpath),
      .size = size,
    };

    files_.insert_or_assign(kind, record);
  }

  auto RegisterExternalAssetDescriptor(const data::AssetKey& key,
    const data::AssetType asset_type, std::string_view virtual_path,
    std::string_view descriptor_relpath, uint64_t descriptor_size,
    std::optional<base::Sha256Digest> descriptor_sha256) -> void
  {
    ValidateVirtualPath(virtual_path);
    ValidateRelativePath(descriptor_relpath);

    if (const auto existing_it
      = key_by_virtual_path_.find(std::string(virtual_path));
      existing_it != key_by_virtual_path_.end() && existing_it->second != key) {
      throw std::runtime_error(
        "Conflicting virtual path mapping in loose cooked container");
    }

    const auto path_on_disk
      = cooked_root_ / std::filesystem::path(descriptor_relpath);

    std::error_code ec;
    if (!std::filesystem::exists(path_on_disk, ec)) {
      throw std::runtime_error("RegisterExternalAssetDescriptor: file does not "
                               "exist: "
        + path_on_disk.string());
    }

    const auto size_on_disk = std::filesystem::file_size(path_on_disk, ec);
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

    if (!compute_sha256_) {
      descriptor_sha256 = std::nullopt;
    }

    StoredAsset record {
      .key = key,
      .asset_type = asset_type,
      .virtual_path = std::string(virtual_path),
      .descriptor_relpath = std::string(descriptor_relpath),
      .descriptor_size = descriptor_size,
      .descriptor_sha256 = CopyDigestOrZero(descriptor_sha256),
    };

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

    LoadExistingIndexIfPresent_();

    for (const auto& [key, asset] : current_assets) {
      if (const auto existing_it
        = key_by_virtual_path_.find(asset.virtual_path);
        existing_it != key_by_virtual_path_.end()
        && existing_it->second != key) {
        throw std::runtime_error(
          "Conflicting virtual path mapping in loose cooked container");
      }
      assets_.insert_or_assign(key, asset);
      key_by_virtual_path_.insert_or_assign(asset.virtual_path, key);
    }

    for (const auto& [kind, file] : current_files) {
      files_.insert_or_assign(kind, file);
    }

    const auto cooked_root_str = cooked_root_.string();
    LOG_SCOPE_F(INFO, fmt::format("Finish {}", cooked_root_str).c_str());

    ValidateRequiredFilePairs_();

    const auto source_key = ResolveSourceKey_();
    const auto content_version = ResolveContentVersion_();

    const auto index_path
      = cooked_root_ / std::filesystem::path(kIndexFileName);
    std::filesystem::create_directories(index_path.parent_path());

    WriteIndex_(index_path, source_key, content_version);

    LooseCookedWriteResult out {
      .cooked_root = cooked_root_,
      .source_key = source_key,
      .content_version = content_version,
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
      };

      out.files.push_back(std::move(rec));
    }

    return out;
  }

private:
  auto LoadExistingIndexIfPresent_() -> void
  {
    const auto index_path
      = cooked_root_ / std::filesystem::path(kIndexFileName);
    if (!std::filesystem::exists(index_path)) {
      return;
    }

    try {
      const auto header = ReadIndexHeaderOrThrow(index_path);
      existing_guid_ = data::SourceKey::FromBytes(header.guid);
      existing_content_version_ = header.content_version;

      const auto index = detail::LooseCookedIndex::LoadFromFile(index_path);

      for (const auto key : index.GetAllAssetKeys()) {
        const auto rel = index.FindDescriptorRelPath(key);
        const auto vpath = index.FindVirtualPath(key);
        const auto type_u8 = index.FindAssetType(key);
        const auto size = index.FindDescriptorSize(key);
        const auto sha = index.FindDescriptorSha256(key);

        if (!rel || !vpath || !type_u8 || !size) {
          continue;
        }

        StoredAsset record {
          .key = key,
          .asset_type = static_cast<data::AssetType>(*type_u8),
          .virtual_path = std::string(*vpath),
          .descriptor_relpath = std::string(*rel),
          .descriptor_size = *size,
        };

        if (sha.has_value()) {
          std::copy_n(sha->begin(), record.descriptor_sha256.size(),
            record.descriptor_sha256.begin());
        }

        assets_.insert_or_assign(key, record);
        key_by_virtual_path_.insert_or_assign(record.virtual_path, key);
      }

      for (const auto kind : index.GetAllFileKinds()) {
        const auto rel = index.FindFileRelPath(kind);
        const auto size = index.FindFileSize(kind);
        if (!rel || !size) {
          continue;
        }

        StoredFile record {
          .kind = kind,
          .relpath = std::string(*rel),
          .size = *size,
        };

        files_.insert_or_assign(kind, record);
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
    const auto has_buffers_table = files_.contains(FileKind::kBuffersTable);
    const auto has_buffers_data = files_.contains(FileKind::kBuffersData);
    if (has_buffers_table != has_buffers_data) {
      throw std::runtime_error(
        "Loose cooked index must provide both buffers.table and buffers.data");
    }

    const auto has_textures_table = files_.contains(FileKind::kTexturesTable);
    const auto has_textures_data = files_.contains(FileKind::kTexturesData);
    if (has_textures_table != has_textures_data) {
      throw std::runtime_error("Loose cooked index must provide both "
                               "textures.table and textures.data");
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

    auto bytes = data::GenerateAssetGuid();
    if (IsAllZeros(bytes)) {
      bytes[0] = 1;
    }
    return data::SourceKey { bytes };
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

    std::vector<FileKind> kinds;
    kinds.reserve(files_.size());
    for (const auto& [kind, file] : files_) {
      (void)file;
      kinds.push_back(kind);
    }
    std::ranges::sort(kinds, [](const FileKind a, const FileKind b) {
      return static_cast<uint16_t>(a) < static_cast<uint16_t>(b);
    });

    for (const auto kind : kinds) {
      const auto& f = files_.at(kind);

      FileRecord record {};
      record.kind = f.kind;
      record.relpath_offset = strings.Add(f.relpath);
      record.size = f.size;

      file_records.push_back(record);
    }

    IndexHeader header {};
    header.version = 1;
    header.content_version = content_version;

    header.flags = data::loose_cooked::v1::kHasVirtualPaths;
    if (!file_records.empty()) {
      header.flags |= data::loose_cooked::v1::kHasFileRecords;
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

    const auto& guid_bytes = source_key.get();
    std::ranges::copy(guid_bytes, std::begin(header.guid));

    serio::FileStream stream(index_path, std::ios::out | std::ios::trunc);
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

    ThrowOnError(writer.Flush(), "Failed to flush index file");

    LOG_F(INFO,
      "Wrote loose cooked index: assets={}, files={}, strings={} bytes",
      asset_entries.size(), file_records.size(), strings.SizeBytes());
  }

  std::filesystem::path cooked_root_;

  bool compute_sha256_ = true;

  std::optional<data::SourceKey> source_key_override_;
  std::optional<uint16_t> content_version_override_;

  std::optional<data::SourceKey> existing_guid_;
  std::optional<uint16_t> existing_content_version_;

  std::unordered_map<data::AssetKey, StoredAsset> assets_;
  std::unordered_map<FileKind, StoredFile> files_;
  std::unordered_map<std::string, data::AssetKey> key_by_virtual_path_;
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

auto LooseCookedWriter::SetComputeSha256(const bool enabled) -> void
{
  impl_->SetComputeSha256(enabled);
}

auto LooseCookedWriter::WriteAssetDescriptor(const data::AssetKey& key,
  const data::AssetType asset_type, std::string_view virtual_path,
  std::string_view descriptor_relpath, const std::span<const std::byte> bytes)
  -> void
{
  impl_->WriteAssetDescriptor(
    key, asset_type, virtual_path, descriptor_relpath, bytes);
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
  const uint64_t descriptor_size,
  std::optional<base::Sha256Digest> descriptor_sha256) -> void
{
  impl_->RegisterExternalAssetDescriptor(key, asset_type, virtual_path,
    descriptor_relpath, descriptor_size, descriptor_sha256);
}

auto LooseCookedWriter::Finish() -> LooseCookedWriteResult
{
  return impl_->Finish();
}

} // namespace oxygen::content::import
