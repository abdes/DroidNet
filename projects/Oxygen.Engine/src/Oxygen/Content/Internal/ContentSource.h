//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/Detail/LooseCookedIndex.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/ResourceTable.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::internal {

using oxygen::base::ComputeFileSha256;
using oxygen::base::IsAllZero;
using oxygen::base::Sha256Digest;

//! Asset location within a PAK file.
struct PakAssetLocator {
  data::pak::AssetDirectoryEntry entry {};
};

//! Asset location within a loose cooked root.
struct LooseCookedAssetLocator {
  std::filesystem::path descriptor_path;
};

//! Type-erased locator for an asset descriptor.
using AssetLocator = std::variant<PakAssetLocator, LooseCookedAssetLocator>;

//! Minimal runtime-facing abstraction over a source of cooked bytes.
/*!
 A ContentSource provides cooked descriptor bytes and cooked resource bytes.

 This is an internal runtime abstraction used by the loader pipeline to treat
 different storage forms uniformly (e.g. `.pak` vs loose cooked directories).

 It is not an editor mount-point abstraction.
*/
class IContentSource : public oxygen::Object {
public:
  ~IContentSource() override = default;

  IContentSource() = default;

  IContentSource(const IContentSource&) = delete;
  IContentSource(IContentSource&&) = delete;
  auto operator=(const IContentSource&) -> IContentSource& = delete;
  auto operator=(IContentSource&&) -> IContentSource& = delete;

  [[nodiscard]] virtual auto DebugName() const noexcept -> std::string_view = 0;

  [[nodiscard]] virtual auto GetSourceKey() const noexcept -> data::SourceKey
    = 0;

  [[nodiscard]] virtual auto FindAsset(const data::AssetKey& key) const noexcept
    -> std::optional<AssetLocator>
    = 0;

  [[nodiscard]] virtual auto CreateAssetDescriptorReader(
    const AssetLocator& locator) const -> std::unique_ptr<serio::AnyReader>
    = 0;

  [[nodiscard]] virtual auto CreateBufferTableReader() const
    -> std::unique_ptr<serio::AnyReader>
    = 0;

  [[nodiscard]] virtual auto CreateTextureTableReader() const
    -> std::unique_ptr<serio::AnyReader>
    = 0;

  [[nodiscard]] virtual auto CreateScriptTableReader() const
    -> std::unique_ptr<serio::AnyReader>
    = 0;

  [[nodiscard]] virtual auto GetBufferTable() const noexcept
    -> const ResourceTable<data::BufferResource>* = 0;

  [[nodiscard]] virtual auto GetTextureTable() const noexcept
    -> const ResourceTable<data::TextureResource>* = 0;

  [[nodiscard]] virtual auto GetScriptTable() const noexcept
    -> const ResourceTable<data::ScriptResource>* = 0;

  [[nodiscard]] virtual auto CreateBufferDataReader() const
    -> std::unique_ptr<serio::AnyReader>
    = 0;

  [[nodiscard]] virtual auto CreateTextureDataReader() const
    -> std::unique_ptr<serio::AnyReader>
    = 0;

  [[nodiscard]] virtual auto CreateScriptDataReader() const
    -> std::unique_ptr<serio::AnyReader>
    = 0;
};

//! ContentSource backed by an existing `PakFile`.
class PakFileSource final : public IContentSource {
public:
  OXYGEN_TYPED(PakFileSource)

public:
  explicit PakFileSource(
    const std::filesystem::path& pak_path, const bool verify_content_hashes)
    : pak_(pak_path)
    , debug_name_(pak_.FilePath().string())
    , footer_(ReadFooter(pak_path))
  {
    if (verify_content_hashes) {
      pak_.ValidateCrc32Integrity();
    }
  }

  ~PakFileSource() override = default;

  [[nodiscard]] auto DebugName() const noexcept -> std::string_view override
  {
    return debug_name_;
  }

  [[nodiscard]] auto GetSourceKey() const noexcept -> data::SourceKey override
  {
    return pak_.Guid();
  }

  [[nodiscard]] auto FindAsset(const data::AssetKey& key) const noexcept
    -> std::optional<AssetLocator> override
  {
    const auto entry = pak_.FindEntry(key);
    if (!entry) {
      return std::nullopt;
    }

    return AssetLocator { PakAssetLocator { .entry = *entry } };
  }

  [[nodiscard]] auto CreateAssetDescriptorReader(
    const AssetLocator& locator) const
    -> std::unique_ptr<serio::AnyReader> override
  {
    const auto* pak_loc = std::get_if<PakAssetLocator>(&locator);
    if (pak_loc == nullptr) {
      return nullptr;
    }

    return std::make_unique<OwningPakSectionReader>(
      pak_.FilePath(), static_cast<size_t>(pak_loc->entry.desc_offset));
  }

  [[nodiscard]] auto CreateBufferTableReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    return std::make_unique<OwningPakSectionReader>(pak_.FilePath(), 0);
  }

  [[nodiscard]] auto CreateTextureTableReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    return std::make_unique<OwningPakSectionReader>(pak_.FilePath(), 0);
  }

  [[nodiscard]] auto CreateScriptTableReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    return std::make_unique<OwningPakSectionReader>(pak_.FilePath(), 0);
  }

  [[nodiscard]] auto GetBufferTable() const noexcept
    -> const ResourceTable<data::BufferResource>* override
  {
    return pak_.GetResourceTable<data::BufferResource>();
  }

  [[nodiscard]] auto GetTextureTable() const noexcept
    -> const ResourceTable<data::TextureResource>* override
  {
    return pak_.GetResourceTable<data::TextureResource>();
  }

  [[nodiscard]] auto GetScriptTable() const noexcept
    -> const ResourceTable<data::ScriptResource>* override
  {
    return pak_.GetResourceTable<data::ScriptResource>();
  }

  [[nodiscard]] auto CreateBufferDataReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (!footer_) {
      return nullptr;
    }
    return std::make_unique<OwningPakSectionReader>(
      pak_.FilePath(), static_cast<size_t>(footer_->buffer_region.offset));
  }

  [[nodiscard]] auto CreateTextureDataReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (!footer_) {
      return nullptr;
    }
    return std::make_unique<OwningPakSectionReader>(
      pak_.FilePath(), static_cast<size_t>(footer_->texture_region.offset));
  }

  [[nodiscard]] auto CreateScriptDataReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (!footer_) {
      return nullptr;
    }
    return std::make_unique<OwningPakSectionReader>(
      pak_.FilePath(), static_cast<size_t>(footer_->script_region.offset));
  }

  [[nodiscard]] auto Pak() const noexcept -> const PakFile& { return pak_; }

private:
  class OwningPakSectionReader final : public serio::AnyReader {
  public:
    OwningPakSectionReader(const std::filesystem::path& path, size_t offset)
      : stream_(path, std::ios::in)
      , reader_(stream_)
    {
      (void)reader_.Seek(offset);
    }

    ~OwningPakSectionReader() override = default;

    OXYGEN_MAKE_NON_COPYABLE(OwningPakSectionReader)
    OXYGEN_MAKE_NON_MOVABLE(OwningPakSectionReader)

    [[nodiscard]] auto ReadBlob(size_t size) noexcept
      -> oxygen::Result<std::vector<std::byte>> override
    {
      return reader_.ReadBlob(size);
    }

    [[nodiscard]] auto ReadBlobInto(std::span<std::byte> buffer) noexcept
      -> oxygen::Result<void> override
    {
      return reader_.ReadBlobInto(buffer);
    }

    [[nodiscard]] auto Position() noexcept -> oxygen::Result<size_t> override
    {
      return reader_.Position();
    }

    [[nodiscard]] auto AlignTo(size_t alignment) noexcept
      -> oxygen::Result<void> override
    {
      return reader_.AlignTo(alignment);
    }

    [[nodiscard]] auto ScopedAlignment(uint16_t alignment) noexcept(false)
      -> serio::AlignmentGuard override
    {
      return reader_.ScopedAlignment(alignment);
    }

    [[nodiscard]] auto Forward(size_t num_bytes) noexcept
      -> oxygen::Result<void> override
    {
      return reader_.Forward(num_bytes);
    }

    [[nodiscard]] auto Seek(size_t pos) noexcept
      -> oxygen::Result<void> override
    {
      return reader_.Seek(pos);
    }

  private:
    serio::FileStream<> stream_;
    PakFile::Reader reader_;
  };

  [[nodiscard]] static auto ReadFooter(const std::filesystem::path& pak_path)
    -> std::optional<data::pak::PakFooter>
  {
    std::error_code ec;
    const auto file_size = std::filesystem::file_size(pak_path, ec);
    if (ec || file_size < sizeof(data::pak::PakFooter)) {
      return std::nullopt;
    }

    serio::FileStream<> stream(pak_path, std::ios::in);
    if (!stream.Seek(
          static_cast<size_t>(file_size - sizeof(data::pak::PakFooter)))) {
      return std::nullopt;
    }

    data::pak::PakFooter footer {};
    auto read_result
      = stream.Read(reinterpret_cast<std::byte*>(&footer), sizeof(footer));
    if (!read_result) {
      return std::nullopt;
    }

    // Basic magic check.
    if (std::string_view(footer.footer_magic, sizeof(footer.footer_magic))
      != std::string_view("OXPAKEND", 8)) {
      return std::nullopt;
    }

    return footer;
  }

  PakFile pak_;
  std::string debug_name_;
  std::optional<data::pak::PakFooter> footer_;
};

//! ContentSource backed by a loose cooked root directory.
class LooseCookedSource final : public IContentSource {
public:
  OXYGEN_TYPED(LooseCookedSource)

public:
  explicit LooseCookedSource(
    const std::filesystem::path& cooked_root, const bool verify_content_hashes)
    : cooked_root_(cooked_root)
    , debug_name_(cooked_root_.string())
    , index_(detail::LooseCookedIndex::LoadFromFile(
        cooked_root_ / "container.index.bin"))
    , verify_content_hashes_(verify_content_hashes)
  {
    using oxygen::data::loose_cooked::FileKind;

    ValidateDescriptorFilesExistAndMatchIndex();

    if (const auto rel = index_.FindFileRelPath(FileKind::kBuffersTable); rel) {
      buffers_table_path_ = cooked_root_ / std::filesystem::path(*rel);
      ValidateFileRecordExistsAndMatchesSize(
        FileKind::kBuffersTable, *buffers_table_path_);
      InitializeBufferTable();
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kTexturesTable);
      rel) {
      textures_table_path_ = cooked_root_ / std::filesystem::path(*rel);
      ValidateFileRecordExistsAndMatchesSize(
        FileKind::kTexturesTable, *textures_table_path_);
      InitializeTextureTable();
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kBuffersData); rel) {
      buffers_data_path_ = cooked_root_ / std::filesystem::path(*rel);
      ValidateFileRecordExistsAndMatchesSize(
        FileKind::kBuffersData, *buffers_data_path_);
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kTexturesData); rel) {
      textures_data_path_ = cooked_root_ / std::filesystem::path(*rel);
      ValidateFileRecordExistsAndMatchesSize(
        FileKind::kTexturesData, *textures_data_path_);
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kScriptsTable); rel) {
      scripts_table_path_ = cooked_root_ / std::filesystem::path(*rel);
      ValidateFileRecordExistsAndMatchesSize(
        FileKind::kScriptsTable, *scripts_table_path_);
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kScriptsData); rel) {
      scripts_data_path_ = cooked_root_ / std::filesystem::path(*rel);
      ValidateFileRecordExistsAndMatchesSize(
        FileKind::kScriptsData, *scripts_data_path_);
    }

    ValidateTableDataPairs();
  }

  ~LooseCookedSource() override = default;

  [[nodiscard]] auto DebugName() const noexcept -> std::string_view override
  {
    return debug_name_;
  }

  [[nodiscard]] auto GetSourceKey() const noexcept -> data::SourceKey override
  {
    return index_.Guid();
  }

  [[nodiscard]] auto FindAsset(const data::AssetKey& key) const noexcept
    -> std::optional<AssetLocator> override
  {
    const auto rel = index_.FindDescriptorRelPath(key);
    if (!rel) {
      return std::nullopt;
    }

    return AssetLocator { LooseCookedAssetLocator {
      .descriptor_path = cooked_root_ / std::filesystem::path(*rel),
    } };
  }

  [[nodiscard]] auto CreateAssetDescriptorReader(
    const AssetLocator& locator) const
    -> std::unique_ptr<serio::AnyReader> override
  {
    const auto* loose_loc = std::get_if<LooseCookedAssetLocator>(&locator);
    if (loose_loc == nullptr) {
      return nullptr;
    }

    return std::make_unique<OwningFileReader>(loose_loc->descriptor_path);
  }

  [[nodiscard]] auto CreateBufferTableReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (!buffers_table_path_) {
      return nullptr;
    }
    return std::make_unique<OwningFileReader>(*buffers_table_path_);
  }

  [[nodiscard]] auto CreateTextureTableReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (!textures_table_path_) {
      return nullptr;
    }
    return std::make_unique<OwningFileReader>(*textures_table_path_);
  }

  [[nodiscard]] auto CreateScriptTableReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    return nullptr;
  }

  [[nodiscard]] auto GetBufferTable() const noexcept
    -> const ResourceTable<data::BufferResource>* override
  {
    return buffers_table_ ? &(*buffers_table_) : nullptr;
  }

  [[nodiscard]] auto GetTextureTable() const noexcept
    -> const ResourceTable<data::TextureResource>* override
  {
    return textures_table_ ? &(*textures_table_) : nullptr;
  }

  [[nodiscard]] auto GetScriptTable() const noexcept
    -> const ResourceTable<data::ScriptResource>* override
  {
    return nullptr;
  }

  [[nodiscard]] auto CreateBufferDataReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (!buffers_data_path_) {
      return nullptr;
    }
    return std::make_unique<OwningFileReader>(*buffers_data_path_);
  }

  [[nodiscard]] auto CreateTextureDataReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (!textures_data_path_) {
      return nullptr;
    }
    return std::make_unique<OwningFileReader>(*textures_data_path_);
  }

  [[nodiscard]] auto CreateScriptDataReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    return nullptr;
  }

  [[nodiscard]] auto ReadScriptSlotRecords(const uint32_t start_index,
    const uint32_t count) const -> std::vector<data::pak::ScriptSlotRecord>
  {
    std::vector<data::pak::ScriptSlotRecord> records;
    if (count == 0) {
      return records;
    }
    CHECK_F(scripts_table_path_.has_value(),
      "scripts.table is required to read script slot records");

    constexpr size_t kRecordSize = sizeof(data::pak::ScriptSlotRecord);
    const size_t start_offset = static_cast<size_t>(start_index) * kRecordSize;
    const size_t bytes_to_read = static_cast<size_t>(count) * kRecordSize;

    serio::FileStream<> stream(*scripts_table_path_, std::ios::in);
    serio::Reader<serio::FileStream<>> reader(stream);
    auto align_guard = reader.ScopedAlignment(1);
    (void)align_guard;

    auto seek_res = reader.Seek(start_offset);
    if (!seek_res) {
      throw std::runtime_error("Failed to seek scripts.table slot range");
    }
    auto blob = reader.ReadBlob(bytes_to_read);
    if (!blob) {
      throw std::runtime_error("Failed to read scripts.table slot range");
    }

    records.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
      const size_t offset = static_cast<size_t>(i) * kRecordSize;
      const auto src
        = std::span<const std::byte>(*blob).subspan(offset, kRecordSize);
      std::memcpy(std::addressof(records[static_cast<size_t>(i)]), src.data(),
        kRecordSize);
    }
    return records;
  }

  [[nodiscard]] auto ReadScriptParamRecords(
    const data::pak::OffsetT absolute_offset, const uint32_t count) const
    -> std::vector<data::pak::ScriptParamRecord>
  {
    std::vector<data::pak::ScriptParamRecord> records;
    if (count == 0) {
      return records;
    }
    CHECK_F(scripts_data_path_.has_value(),
      "scripts.data is required to read script parameter records");

    constexpr size_t kRecordSize = sizeof(data::pak::ScriptParamRecord);
    const size_t bytes_to_read = static_cast<size_t>(count) * kRecordSize;

    serio::FileStream<> stream(*scripts_data_path_, std::ios::in);
    serio::Reader<serio::FileStream<>> reader(stream);
    auto align_guard = reader.ScopedAlignment(1);
    (void)align_guard;

    auto seek_res = reader.Seek(static_cast<size_t>(absolute_offset));
    if (!seek_res) {
      throw std::runtime_error("Failed to seek scripts.data parameter range");
    }
    auto blob = reader.ReadBlob(bytes_to_read);
    if (!blob) {
      throw std::runtime_error("Failed to read scripts.data parameter range");
    }

    records.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
      const size_t offset = static_cast<size_t>(i) * kRecordSize;
      const auto src
        = std::span<const std::byte>(*blob).subspan(offset, kRecordSize);
      std::memcpy(std::addressof(records[static_cast<size_t>(i)]), src.data(),
        kRecordSize);
    }
    return records;
  }

private:
  auto ValidateTableDataPairs() const -> void
  {
    const auto buffers_table = buffers_table_path_.has_value();
    const auto buffers_data = buffers_data_path_.has_value();
    if (buffers_table != buffers_data) {
      throw std::runtime_error(
        "Loose cooked root must provide both buffers.table and buffers.data");
    }

    const auto textures_table = textures_table_path_.has_value();
    const auto textures_data = textures_data_path_.has_value();
    if (textures_table != textures_data) {
      throw std::runtime_error(
        "Loose cooked root must provide both textures.table and textures.data");
    }

    const auto scripts_table = scripts_table_path_.has_value();
    const auto scripts_data = scripts_data_path_.has_value();
    if (scripts_table != scripts_data) {
      throw std::runtime_error(
        "Loose cooked root must provide both scripts.table and scripts.data");
    }
  }

  auto ValidateFileRecordExistsAndMatchesSize(
    oxygen::data::loose_cooked::FileKind kind,
    const std::filesystem::path& absolute_path) const -> void
  {
    const auto t0 = std::chrono::steady_clock::now();

    std::error_code ec;
    if (!std::filesystem::exists(absolute_path, ec) || ec) {
      throw std::runtime_error(
        "Loose cooked root missing file: " + absolute_path.string());
    }

    const auto expected_size_opt = index_.FindFileSize(kind);
    if (!expected_size_opt) {
      return;
    }

    const auto actual_size = std::filesystem::file_size(absolute_path, ec);
    if (ec) {
      throw std::runtime_error(
        "Failed to stat file: " + absolute_path.string());
    }

    if (actual_size != *expected_size_opt) {
      throw std::runtime_error(
        "Loose cooked file size mismatch: " + absolute_path.string()
        + " expected=" + std::to_string(*expected_size_opt)
        + " actual=" + std::to_string(actual_size));
    }

    const auto t1 = std::chrono::steady_clock::now();
    LOG_F(INFO,
      "LooseCookedSource: validated file record kind={} path={} time_ms={}",
      static_cast<int>(kind), absolute_path.string(),
      std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
  }

  auto ValidateDescriptorFilesExistAndMatchIndex() const -> void
  {
    const auto keys = index_.GetAllAssetKeys();
    LOG_F(INFO,
      "LooseCookedSource: validating descriptors begin root={} assets={} "
      "verify_hashes={}",
      cooked_root_.string(), keys.size(),
      verify_content_hashes_ ? "yes" : "no");

    const auto t0 = std::chrono::steady_clock::now();
    std::chrono::nanoseconds stat_time { 0 };
    std::chrono::nanoseconds sha_time { 0 };
    size_t sha_assets = 0;
    size_t sha_disabled = 0;
    size_t sha_skipped_all_zero = 0;
    size_t sha_missing = 0;

    std::error_code ec;
    for (const auto& key : keys) {
      const auto t_stat0 = std::chrono::steady_clock::now();
      const auto rel_opt = index_.FindDescriptorRelPath(key);
      const auto size_opt = index_.FindDescriptorSize(key);
      const auto sha_opt = index_.FindDescriptorSha256(key);
      if (!rel_opt || !size_opt) {
        throw std::runtime_error(
          "Loose cooked index missing descriptor metadata for an asset");
      }

      const auto abs = cooked_root_ / std::filesystem::path(*rel_opt);
      if (!std::filesystem::exists(abs, ec) || ec) {
        throw std::runtime_error(
          "Loose cooked root missing descriptor: " + abs.string());
      }

      const auto actual_size = std::filesystem::file_size(abs, ec);
      if (ec) {
        throw std::runtime_error("Failed to stat descriptor: " + abs.string());
      }

      if (actual_size != *size_opt) {
        throw std::runtime_error("Loose cooked descriptor size mismatch: "
          + abs.string() + " expected=" + std::to_string(*size_opt)
          + " actual=" + std::to_string(actual_size));
      }

      const auto t_stat1 = std::chrono::steady_clock::now();
      stat_time += std::chrono::duration_cast<std::chrono::nanoseconds>(
        t_stat1 - t_stat0);

      if (sha_opt) {
        Sha256Digest expected = {};
        std::copy_n(sha_opt->data(), expected.size(), expected.begin());
        if (!IsAllZero(expected)) {
          if (verify_content_hashes_) {
            const auto t_sha0 = std::chrono::steady_clock::now();
            const auto actual_sha = ComputeFileSha256(abs);
            const auto t_sha1 = std::chrono::steady_clock::now();
            sha_time += std::chrono::duration_cast<std::chrono::nanoseconds>(
              t_sha1 - t_sha0);
            ++sha_assets;
            if (actual_sha != expected) {
              throw std::runtime_error(
                "Loose cooked descriptor SHA-256 mismatch: " + abs.string());
            }
          } else {
            ++sha_disabled;
          }
        } else {
          ++sha_skipped_all_zero;
        }
      } else {
        ++sha_missing;
      }
    }

    const auto t1 = std::chrono::steady_clock::now();
    LOG_F(INFO,
      "LooseCookedSource: validating descriptors end root={} assets={} "
      "time_ms={} stat_ms={} sha_ms={} sha_assets={} sha_disabled={} "
      "sha_skipped={} sha_missing={}",
      cooked_root_.string(), keys.size(),
      std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count(),
      std::chrono::duration_cast<std::chrono::milliseconds>(stat_time).count(),
      std::chrono::duration_cast<std::chrono::milliseconds>(sha_time).count(),
      sha_assets, sha_disabled, sha_skipped_all_zero, sha_missing);
  }

  auto InitializeBufferTable() -> void
  {
    if (!buffers_table_path_) {
      return;
    }
    std::error_code ec;
    const auto size = std::filesystem::file_size(*buffers_table_path_, ec);
    if (ec) {
      throw std::runtime_error(
        "Failed to stat buffers.table: " + buffers_table_path_->string());
    }

    constexpr uint64_t kEntrySize = sizeof(data::pak::BufferResourceDesc);
    if (kEntrySize == 0 || (size % kEntrySize) != 0) {
      throw std::runtime_error(
        "Invalid buffers.table size: " + buffers_table_path_->string());
    }

    const auto count = size / kEntrySize;
    if (count > (std::numeric_limits<uint32_t>::max)()) {
      throw std::runtime_error(
        "buffers.table too large: " + buffers_table_path_->string());
    }

    data::pak::ResourceTable meta {
      .offset = 0,
      .count = static_cast<uint32_t>(count),
      .entry_size = static_cast<uint32_t>(kEntrySize),
    };
    buffers_table_.emplace(meta);
  }

  auto InitializeTextureTable() -> void
  {
    if (!textures_table_path_) {
      return;
    }
    std::error_code ec;
    const auto size = std::filesystem::file_size(*textures_table_path_, ec);
    if (ec) {
      throw std::runtime_error(
        "Failed to stat textures.table: " + textures_table_path_->string());
    }

    constexpr uint64_t kEntrySize = sizeof(data::pak::TextureResourceDesc);
    if (kEntrySize == 0 || (size % kEntrySize) != 0) {
      throw std::runtime_error(
        "Invalid textures.table size: " + textures_table_path_->string());
    }

    const auto count = size / kEntrySize;
    if (count > (std::numeric_limits<uint32_t>::max)()) {
      throw std::runtime_error(
        "textures.table too large: " + textures_table_path_->string());
    }

    data::pak::ResourceTable meta {
      .offset = 0,
      .count = static_cast<uint32_t>(count),
      .entry_size = static_cast<uint32_t>(kEntrySize),
    };
    textures_table_.emplace(meta);
  }

  class OwningFileReader final : public serio::AnyReader {
  public:
    explicit OwningFileReader(const std::filesystem::path& path)
      : stream_(path, std::ios::in)
      , reader_(stream_)
    {
    }

    ~OwningFileReader() override = default;

    OXYGEN_MAKE_NON_COPYABLE(OwningFileReader)
    OXYGEN_MAKE_NON_MOVABLE(OwningFileReader)

    [[nodiscard]] auto ReadBlob(size_t size) noexcept
      -> oxygen::Result<std::vector<std::byte>> override
    {
      return reader_.ReadBlob(size);
    }

    [[nodiscard]] auto ReadBlobInto(std::span<std::byte> buffer) noexcept
      -> oxygen::Result<void> override
    {
      return reader_.ReadBlobInto(buffer);
    }

    [[nodiscard]] auto Position() noexcept -> oxygen::Result<size_t> override
    {
      return reader_.Position();
    }

    [[nodiscard]] auto AlignTo(size_t alignment) noexcept
      -> oxygen::Result<void> override
    {
      return reader_.AlignTo(alignment);
    }

    [[nodiscard]] auto ScopedAlignment(uint16_t alignment) noexcept(false)
      -> serio::AlignmentGuard override
    {
      return reader_.ScopedAlignment(alignment);
    }

    [[nodiscard]] auto Forward(size_t num_bytes) noexcept
      -> oxygen::Result<void> override
    {
      return reader_.Forward(num_bytes);
    }

    [[nodiscard]] auto Seek(size_t pos) noexcept
      -> oxygen::Result<void> override
    {
      return reader_.Seek(pos);
    }

  private:
    serio::FileStream<> stream_;
    serio::Reader<serio::FileStream<>> reader_;
  };

  std::filesystem::path cooked_root_;
  std::string debug_name_;
  detail::LooseCookedIndex index_;

  bool verify_content_hashes_ { false };

  std::optional<std::filesystem::path> buffers_table_path_;
  std::optional<std::filesystem::path> textures_table_path_;
  std::optional<std::filesystem::path> buffers_data_path_;
  std::optional<std::filesystem::path> textures_data_path_;
  std::optional<std::filesystem::path> scripts_table_path_;
  std::optional<std::filesystem::path> scripts_data_path_;

  std::optional<ResourceTable<data::BufferResource>> buffers_table_;
  std::optional<ResourceTable<data::TextureResource>> textures_table_;
};

} // namespace oxygen::content::internal
