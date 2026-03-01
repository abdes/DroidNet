//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/Internal/IContentSource.h>

namespace oxygen::content::internal {

class LooseCookedSource final : public IContentSource {
public:
  OXYGEN_TYPED(LooseCookedSource)

public:
  explicit LooseCookedSource(
    std::filesystem::path cooked_root, const bool verify_content_hashes)
    : cooked_root_(std::move(cooked_root))
    , debug_name_(cooked_root_.string())
    , index_(LooseCookedIndexImpl::LoadFromFile(
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
      InitializeScriptTable();
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kScriptsData); rel) {
      scripts_data_path_ = cooked_root_ / std::filesystem::path(*rel);
      ValidateFileRecordExistsAndMatchesSize(
        FileKind::kScriptsData, *scripts_data_path_);
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kScriptBindingsTable);
      rel) {
      script_bindings_table_path_ = cooked_root_ / std::filesystem::path(*rel);
      ValidateFileRecordExistsAndMatchesSize(
        FileKind::kScriptBindingsTable, *script_bindings_table_path_);
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kScriptBindingsData);
      rel) {
      script_bindings_data_path_ = cooked_root_ / std::filesystem::path(*rel);
      ValidateFileRecordExistsAndMatchesSize(
        FileKind::kScriptBindingsData, *script_bindings_data_path_);
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kPhysicsTable); rel) {
      physics_table_path_ = cooked_root_ / std::filesystem::path(*rel);
      ValidateFileRecordExistsAndMatchesSize(
        FileKind::kPhysicsTable, *physics_table_path_);
      InitializePhysicsTable();
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kPhysicsData); rel) {
      physics_data_path_ = cooked_root_ / std::filesystem::path(*rel);
      ValidateFileRecordExistsAndMatchesSize(
        FileKind::kPhysicsData, *physics_data_path_);
    }

    ValidateTableDataPairs();
  }

  ~LooseCookedSource() override = default;

  OXYGEN_MAKE_NON_COPYABLE(LooseCookedSource)
  OXYGEN_DEFAULT_MOVABLE(LooseCookedSource)

  [[nodiscard]] auto DebugName() const noexcept -> std::string_view override
  {
    return debug_name_;
  }
  [[nodiscard]] auto SourcePath() const noexcept
    -> std::filesystem::path override
  {
    return cooked_root_;
  }

  [[nodiscard]] auto GetSourceKey() const noexcept -> data::SourceKey override
  {
    return index_.Guid();
  }

  [[nodiscard]] auto CookedRoot() const noexcept -> const std::filesystem::path&
  {
    return cooked_root_;
  }

  [[nodiscard]] auto FindVirtualPath(const data::AssetKey& key) const noexcept
    -> std::optional<std::string_view>
  {
    return index_.FindVirtualPath(key);
  }

  [[nodiscard]] auto HasAsset(const data::AssetKey& key) const noexcept
    -> bool override
  {
    return index_.FindDescriptorRelPath(key).has_value();
  }

  [[nodiscard]] auto GetAssetCount() const noexcept -> size_t override
  {
    return index_.GetAllAssetKeys().size();
  }

  [[nodiscard]] auto GetAssetKeyByIndex(const uint32_t index) const noexcept
    -> std::optional<data::AssetKey> override
  {
    const auto keys = index_.GetAllAssetKeys();
    if (index >= keys.size()) {
      return std::nullopt;
    }
    return keys[index];
  }

  [[nodiscard]] auto CreateAssetDescriptorReader(
    const data::AssetKey& key) const
    -> std::unique_ptr<serio::AnyReader> override
  {
    const auto rel = index_.FindDescriptorRelPath(key);
    if (!rel.has_value()) {
      return nullptr;
    }

    return std::make_unique<OwningFileReader>(
      cooked_root_ / std::filesystem::path(*rel));
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
    if (!scripts_table_path_) {
      return nullptr;
    }
    return std::make_unique<OwningFileReader>(*scripts_table_path_);
  }

  [[nodiscard]] auto CreatePhysicsTableReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (!physics_table_path_) {
      return nullptr;
    }
    return std::make_unique<OwningFileReader>(*physics_table_path_);
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
    return scripts_table_ ? &(*scripts_table_) : nullptr;
  }

  [[nodiscard]] auto GetPhysicsTable() const noexcept
    -> const ResourceTable<data::PhysicsResource>* override
  {
    return physics_table_ ? &(*physics_table_) : nullptr;
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
    if (!scripts_data_path_) {
      return nullptr;
    }
    return std::make_unique<OwningFileReader>(*scripts_data_path_);
  }

  [[nodiscard]] auto CreatePhysicsDataReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (!physics_data_path_) {
      return nullptr;
    }
    return std::make_unique<OwningFileReader>(*physics_data_path_);
  }

  [[nodiscard]] auto ReadScriptSlotRecords(
    const uint32_t start_index, const uint32_t count) const
    -> std::vector<data::pak::scripting::ScriptSlotRecord> override
  {
    std::vector<data::pak::scripting::ScriptSlotRecord> records;
    if (count == 0) {
      return records;
    }
    if (!script_bindings_table_path_.has_value()) {
      throw std::runtime_error(
        "script-bindings.table is required to read script slot records");
    }

    constexpr size_t kRecordSize
      = sizeof(data::pak::scripting::ScriptSlotRecord);
    const size_t start_offset = static_cast<size_t>(start_index) * kRecordSize;
    const size_t bytes_to_read = static_cast<size_t>(count) * kRecordSize;

    serio::FileStream<> stream(*script_bindings_table_path_, std::ios::in);
    serio::Reader<serio::FileStream<>> reader(stream);
    auto align_guard = reader.ScopedAlignment(1);
    (void)align_guard;

    auto seek_res = reader.Seek(start_offset);
    if (!seek_res) {
      throw std::runtime_error("Failed to seek script-bindings.table slot "
                               "range");
    }
    auto blob = reader.ReadBlob(bytes_to_read);
    if (!blob) {
      throw std::runtime_error("Failed to read script-bindings.table slot "
                               "range");
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
    const data::pak::core::OffsetT absolute_offset, const uint32_t count) const
    -> std::vector<data::pak::scripting::ScriptParamRecord> override
  {
    std::vector<data::pak::scripting::ScriptParamRecord> records;
    if (count == 0) {
      return records;
    }
    if (!script_bindings_data_path_.has_value()) {
      throw std::runtime_error(
        "script-bindings.data is required to read script parameter records");
    }

    constexpr size_t kRecordSize
      = sizeof(data::pak::scripting::ScriptParamRecord);
    const size_t bytes_to_read = static_cast<size_t>(count) * kRecordSize;

    serio::FileStream<> stream(*script_bindings_data_path_, std::ios::in);
    serio::Reader<serio::FileStream<>> reader(stream);
    auto align_guard = reader.ScopedAlignment(1);
    (void)align_guard;

    auto seek_res = reader.Seek(static_cast<size_t>(absolute_offset));
    if (!seek_res) {
      throw std::runtime_error("Failed to seek script-bindings.data parameter "
                               "range");
    }
    auto blob = reader.ReadBlob(bytes_to_read);
    if (!blob) {
      throw std::runtime_error("Failed to read script-bindings.data parameter "
                               "range");
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

  [[nodiscard]] auto ResolveVirtualPath(
    const data::AssetKey& key) const noexcept
    -> std::optional<std::string> override
  {
    if (const auto vpath = index_.FindVirtualPath(key); vpath.has_value()) {
      return std::string(*vpath);
    }
    return std::nullopt;
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

    const auto script_bindings_table = script_bindings_table_path_.has_value();
    const auto script_bindings_data = script_bindings_data_path_.has_value();
    if (script_bindings_table != script_bindings_data) {
      throw std::runtime_error("Loose cooked root must provide both "
                               "script-bindings.table and "
                               "script-bindings.data");
    }

    const auto physics_table = physics_table_path_.has_value();
    const auto physics_data = physics_data_path_.has_value();
    if (physics_table != physics_data) {
      throw std::runtime_error(
        "Loose cooked root must provide both physics.table and physics.data");
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

    constexpr uint64_t kEntrySize = sizeof(data::pak::core::BufferResourceDesc);
    if (kEntrySize == 0 || (size % kEntrySize) != 0) {
      throw std::runtime_error(
        "Invalid buffers.table size: " + buffers_table_path_->string());
    }

    const auto count = size / kEntrySize;
    if (count > (std::numeric_limits<uint32_t>::max)()) {
      throw std::runtime_error(
        "buffers.table too large: " + buffers_table_path_->string());
    }

    data::pak::core::ResourceTable meta {
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

    constexpr uint64_t kEntrySize
      = sizeof(data::pak::render::TextureResourceDesc);
    if (kEntrySize == 0 || (size % kEntrySize) != 0) {
      throw std::runtime_error(
        "Invalid textures.table size: " + textures_table_path_->string());
    }

    const auto count = size / kEntrySize;
    if (count > (std::numeric_limits<uint32_t>::max)()) {
      throw std::runtime_error(
        "textures.table too large: " + textures_table_path_->string());
    }

    data::pak::core::ResourceTable meta {
      .offset = 0,
      .count = static_cast<uint32_t>(count),
      .entry_size = static_cast<uint32_t>(kEntrySize),
    };
    textures_table_.emplace(meta);
  }

  auto InitializePhysicsTable() -> void
  {
    if (!physics_table_path_) {
      return;
    }
    std::error_code ec;
    const auto size = std::filesystem::file_size(*physics_table_path_, ec);
    if (ec) {
      throw std::runtime_error(
        "Failed to stat physics.table: " + physics_table_path_->string());
    }

    constexpr uint64_t kEntrySize
      = sizeof(data::pak::physics::PhysicsResourceDesc);
    if (kEntrySize == 0 || (size % kEntrySize) != 0) {
      throw std::runtime_error(
        "Invalid physics.table size: " + physics_table_path_->string());
    }

    const auto count = size / kEntrySize;
    if (count > (std::numeric_limits<uint32_t>::max)()) {
      throw std::runtime_error(
        "physics.table too large: " + physics_table_path_->string());
    }

    data::pak::core::ResourceTable meta {
      .offset = 0,
      .count = static_cast<uint32_t>(count),
      .entry_size = static_cast<uint32_t>(kEntrySize),
    };
    physics_table_.emplace(meta);
  }

  [[nodiscard]] auto ScriptSlotCount() const noexcept -> uint32_t override
  {
    if (!script_bindings_table_path_) {
      return 0;
    }
    constexpr uint64_t kSlotRecordSize
      = sizeof(data::pak::scripting::ScriptSlotRecord);
    if (kSlotRecordSize == 0) {
      return 0;
    }
    std::error_code ec;
    const auto size
      = std::filesystem::file_size(*script_bindings_table_path_, ec);
    if (ec) {
      return 0;
    }
    return static_cast<uint32_t>(size / kSlotRecordSize);
  }

  auto InitializeScriptTable() -> void
  {
    if (!scripts_table_path_) {
      return;
    }
    std::error_code ec;
    const auto size = std::filesystem::file_size(*scripts_table_path_, ec);
    if (ec) {
      throw std::runtime_error(
        "Failed to stat scripts.table: " + scripts_table_path_->string());
    }

    constexpr uint64_t kEntrySize
      = sizeof(data::pak::scripting::ScriptResourceDesc);
    if (kEntrySize == 0 || (size % kEntrySize) != 0) {
      throw std::runtime_error(
        "Invalid scripts.table size: " + scripts_table_path_->string());
    }

    const auto count = size / kEntrySize;
    if (count > (std::numeric_limits<uint32_t>::max)()) {
      throw std::runtime_error(
        "scripts.table too large: " + scripts_table_path_->string());
    }

    data::pak::core::ResourceTable meta {
      .offset = 0,
      .count = static_cast<uint32_t>(count),
      .entry_size = static_cast<uint32_t>(kEntrySize),
    };
    scripts_table_.emplace(meta);
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
  LooseCookedIndexImpl index_;

  bool verify_content_hashes_ { false };

  std::optional<std::filesystem::path> buffers_table_path_;
  std::optional<std::filesystem::path> textures_table_path_;
  std::optional<std::filesystem::path> buffers_data_path_;
  std::optional<std::filesystem::path> textures_data_path_;
  std::optional<std::filesystem::path> scripts_table_path_;
  std::optional<std::filesystem::path> scripts_data_path_;
  std::optional<std::filesystem::path> script_bindings_table_path_;
  std::optional<std::filesystem::path> script_bindings_data_path_;
  std::optional<std::filesystem::path> physics_table_path_;
  std::optional<std::filesystem::path> physics_data_path_;

  std::optional<ResourceTable<data::BufferResource>> buffers_table_;
  std::optional<ResourceTable<data::TextureResource>> textures_table_;
  std::optional<ResourceTable<data::ScriptResource>> scripts_table_;
  std::optional<ResourceTable<data::PhysicsResource>> physics_table_;
};

} // namespace oxygen::content::internal
