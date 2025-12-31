//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/ResourceTable.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/Internal/LooseCookedIndex.h>

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

  [[nodiscard]] virtual auto GetBufferTable() const noexcept
    -> const ResourceTable<data::BufferResource>* = 0;

  [[nodiscard]] virtual auto GetTextureTable() const noexcept
    -> const ResourceTable<data::TextureResource>* = 0;

  [[nodiscard]] virtual auto CreateBufferDataReader() const
    -> std::unique_ptr<serio::AnyReader>
    = 0;

  [[nodiscard]] virtual auto CreateTextureDataReader() const
    -> std::unique_ptr<serio::AnyReader>
    = 0;
};

//! ContentSource backed by an existing `PakFile`.
class PakFileSource final : public IContentSource {
public:
  OXYGEN_TYPED(PakFileSource)

public:
  explicit PakFileSource(const std::filesystem::path& pak_path)
    : pak_(pak_path)
    , debug_name_(pak_.FilePath().string())
    , footer_(ReadFooter(pak_path))
  {
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
  explicit LooseCookedSource(const std::filesystem::path& cooked_root)
    : cooked_root_(cooked_root)
    , debug_name_(cooked_root_.string())
    , index_(
        LooseCookedIndex::LoadFromFile(cooked_root_ / "container.index.bin"))
  {
    using oxygen::data::loose_cooked::v1::FileKind;

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
  }

  auto ValidateFileRecordExistsAndMatchesSize(
    oxygen::data::loose_cooked::v1::FileKind kind,
    const std::filesystem::path& absolute_path) const -> void
  {
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

    const auto expected_sha_opt = index_.FindFileSha256(kind);
    if (!expected_sha_opt) {
      return;
    }

    Sha256Digest expected = {};
    std::copy_n(expected_sha_opt->data(), expected.size(), expected.begin());
    if (IsAllZero(expected)) {
      return;
    }

    const auto actual_sha = ComputeFileSha256(absolute_path);
    if (actual_sha != expected) {
      throw std::runtime_error(
        "Loose cooked file SHA-256 mismatch: " + absolute_path.string());
    }
  }

  auto ValidateDescriptorFilesExistAndMatchIndex() const -> void
  {
    std::error_code ec;
    for (const auto& key : index_.GetAllAssetKeys()) {
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

      if (sha_opt) {
        Sha256Digest expected = {};
        std::copy_n(sha_opt->data(), expected.size(), expected.begin());
        if (!IsAllZero(expected)) {
          const auto actual_sha = ComputeFileSha256(abs);
          if (actual_sha != expected) {
            throw std::runtime_error(
              "Loose cooked descriptor SHA-256 mismatch: " + abs.string());
          }
        }
      }
    }
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
  LooseCookedIndex index_;

  std::optional<std::filesystem::path> buffers_table_path_;
  std::optional<std::filesystem::path> textures_table_path_;
  std::optional<std::filesystem::path> buffers_data_path_;
  std::optional<std::filesystem::path> textures_data_path_;

  std::optional<ResourceTable<data::BufferResource>> buffers_table_;
  std::optional<ResourceTable<data::TextureResource>> textures_table_;
};

} // namespace oxygen::content::internal
