//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <utility>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/Internal/ContentFileReader.h>
#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Serio/FileLock.h>

namespace oxygen::content::internal {

class LooseCookedSource final : public IContentSource {
public:
  OXYGEN_TYPED(LooseCookedSource)

public:
  //! Index-only opening supports lookups while a cook has unpublished files.
  enum class OpenMode : uint8_t {
    kIndexOnly,
    kValidateMetadata,
    kVerifyContent
  };

  explicit LooseCookedSource(std::filesystem::path cooked_root,
    const OpenMode mode, serio::FileLock generation_lock = {})
    : cooked_root_(std::move(cooked_root))
    , debug_name_(cooked_root_.string())
    , generation_lock_(std::move(generation_lock))
    , index_(LooseCookedIndexImpl::LoadFromFile(
        cooked_root_ / "container.index.bin"))
  {
    using oxygen::data::loose_cooked::FileKind;

    if (mode != OpenMode::kIndexOnly) {
      index_.ValidateContent(cooked_root_,
        mode == OpenMode::kVerifyContent ? lc::IntegrityCheck::kFull
                                         : lc::IntegrityCheck::kMetadata);
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kBuffersTable); rel) {
      buffers_table_path_ = cooked_root_ / std::filesystem::path(*rel);
      InitializeBufferTable();
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kTexturesTable);
      rel) {
      textures_table_path_ = cooked_root_ / std::filesystem::path(*rel);
      InitializeTextureTable();
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kBuffersData); rel) {
      buffers_data_path_ = cooked_root_ / std::filesystem::path(*rel);
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kTexturesData); rel) {
      textures_data_path_ = cooked_root_ / std::filesystem::path(*rel);
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kScriptsTable); rel) {
      scripts_table_path_ = cooked_root_ / std::filesystem::path(*rel);
      InitializeScriptTable();
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kScriptsData); rel) {
      scripts_data_path_ = cooked_root_ / std::filesystem::path(*rel);
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kPhysicsTable); rel) {
      physics_table_path_ = cooked_root_ / std::filesystem::path(*rel);
      InitializePhysicsTable();
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kPhysicsData); rel) {
      physics_data_path_ = cooked_root_ / std::filesystem::path(*rel);
    }

    ValidateTableDataPairs();
  }

  ~LooseCookedSource() override = default;

  OXYGEN_MAKE_NON_COPYABLE(LooseCookedSource)
  OXYGEN_MAKE_NON_MOVABLE(LooseCookedSource)

  [[nodiscard]] auto DebugName() const noexcept -> std::string_view override
  {
    return debug_name_;
  }
  [[nodiscard]] auto SourcePath() const noexcept
    -> const std::filesystem::path& override
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

  [[nodiscard]] auto GetAssetType(const data::AssetKey& key) const noexcept
    -> std::optional<data::AssetType> override
  {
    const auto type = index_.FindAssetType(key);
    return type ? std::optional { static_cast<data::AssetType>(*type) }
                : std::nullopt;
  }

  [[nodiscard]] auto GetAssetCount() const noexcept -> size_t override
  {
    return index_.GetAllAssetKeys().size();
  }

  [[nodiscard]] auto HasKeyReferences(const data::AssetKey& key) const noexcept
    -> bool override
  {
    return index_.HasKeyReferences(key);
  }

  [[nodiscard]] auto FindAssetKeyByVirtualPath(
    const std::string_view path) const -> std::optional<data::AssetKey> override
  {
    return index_.FindAssetKeyByVirtualPath(path);
  }

  [[nodiscard]] auto GetAssetKeyByIndex(const uint32_t index) const noexcept
    -> std::optional<data::AssetKey> override
  {
    const auto keys = index_.GetAllAssetKeys();
    if (index >= keys.size()) {
      return std::nullopt;
    }
    return oxygen::base::CheckedAt(keys, index);
  }

  [[nodiscard]] auto CreateAssetDescriptorReader(
    const data::AssetKey& key) const
    -> std::unique_ptr<serio::AnyReader> override
  {
    const auto rel = index_.FindDescriptorRelPath(key);
    if (!rel.has_value()) {
      return nullptr;
    }

    return std::make_unique<ContentFileReader>(
      cooked_root_ / std::filesystem::path(*rel));
  }

  [[nodiscard]] auto ReadAssetReferenceMetadata(const data::AssetKey& key) const
    -> data::AssetReferences override
  {
    auto references = index_.FindAssetReferences(key);
    if (!references) {
      throw std::out_of_range(
        "Asset reference inventory is not in this source");
    }
    return std::move(*references);
  }

  [[nodiscard]] auto CreateBufferTableReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (!buffers_table_path_) {
      return nullptr;
    }
    return std::make_unique<ContentFileReader>(*buffers_table_path_);
  }

  [[nodiscard]] auto CreateTextureTableReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (!textures_table_path_) {
      return nullptr;
    }
    return std::make_unique<ContentFileReader>(*textures_table_path_);
  }

  [[nodiscard]] auto CreateScriptTableReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (!scripts_table_path_) {
      return nullptr;
    }
    return std::make_unique<ContentFileReader>(*scripts_table_path_);
  }

  [[nodiscard]] auto CreatePhysicsTableReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (!physics_table_path_) {
      return nullptr;
    }
    return std::make_unique<ContentFileReader>(*physics_table_path_);
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
    return std::make_unique<ContentFileReader>(*buffers_data_path_);
  }

  [[nodiscard]] auto CreateTextureDataReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (!textures_data_path_) {
      return nullptr;
    }
    return std::make_unique<ContentFileReader>(*textures_data_path_);
  }

  [[nodiscard]] auto CreateScriptDataReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (!scripts_data_path_) {
      return nullptr;
    }
    return std::make_unique<ContentFileReader>(*scripts_data_path_);
  }

  [[nodiscard]] auto CreatePhysicsDataReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (!physics_data_path_) {
      return nullptr;
    }
    return std::make_unique<ContentFileReader>(*physics_data_path_);
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

    const auto physics_table = physics_table_path_.has_value();
    const auto physics_data = physics_data_path_.has_value();
    if (physics_table != physics_data) {
      throw std::runtime_error(
        "Loose cooked root must provide both physics.table and physics.data");
    }
  }

  auto InitializeBufferTable() -> void
  {
    if (!buffers_table_path_) {
      return;
    }
    const auto indexed_size
      = index_.FindFileSize(data::loose_cooked::FileKind::kBuffersTable);
    if (!indexed_size) {
      throw std::runtime_error("Index is missing buffers.table size");
    }
    const auto size = *indexed_size;

    constexpr uint64_t kEntrySize = sizeof(data::pak::core::BufferResourceDesc);
    if (kEntrySize == 0 || (size % kEntrySize) != 0) {
      throw std::runtime_error(
        "Invalid buffers.table size: " + buffers_table_path_->string());
    }

    const auto count = size / kEntrySize;
    if (count > std::numeric_limits<uint32_t>::max()) {
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
    const auto indexed_size
      = index_.FindFileSize(data::loose_cooked::FileKind::kTexturesTable);
    if (!indexed_size) {
      throw std::runtime_error("Index is missing textures.table size");
    }
    const auto size = *indexed_size;

    constexpr uint64_t kEntrySize
      = sizeof(data::pak::core::TextureResourceDesc);
    if (kEntrySize == 0 || (size % kEntrySize) != 0) {
      throw std::runtime_error(
        "Invalid textures.table size: " + textures_table_path_->string());
    }

    const auto count = size / kEntrySize;
    if (count > std::numeric_limits<uint32_t>::max()) {
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
    const auto indexed_size
      = index_.FindFileSize(data::loose_cooked::FileKind::kPhysicsTable);
    if (!indexed_size) {
      throw std::runtime_error("Index is missing physics.table size");
    }
    const auto size = *indexed_size;

    constexpr uint64_t kEntrySize
      = sizeof(data::pak::physics::PhysicsResourceDesc);
    if (kEntrySize == 0 || (size % kEntrySize) != 0) {
      throw std::runtime_error(
        "Invalid physics.table size: " + physics_table_path_->string());
    }

    const auto count = size / kEntrySize;
    if (count > std::numeric_limits<uint32_t>::max()) {
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

  auto InitializeScriptTable() -> void
  {
    if (!scripts_table_path_) {
      return;
    }
    const auto indexed_size
      = index_.FindFileSize(data::loose_cooked::FileKind::kScriptsTable);
    if (!indexed_size) {
      throw std::runtime_error("Index is missing scripts.table size");
    }
    const auto size = *indexed_size;

    constexpr uint64_t kEntrySize
      = sizeof(data::pak::scripting::ScriptResourceDesc);
    if (kEntrySize == 0 || (size % kEntrySize) != 0) {
      throw std::runtime_error(
        "Invalid scripts.table size: " + scripts_table_path_->string());
    }

    const auto count = size / kEntrySize;
    if (count > std::numeric_limits<uint32_t>::max()) {
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

  std::filesystem::path cooked_root_;
  std::string debug_name_;
  serio::FileLock generation_lock_;
  LooseCookedIndexImpl index_;

  std::optional<std::filesystem::path> buffers_table_path_;
  std::optional<std::filesystem::path> textures_table_path_;
  std::optional<std::filesystem::path> buffers_data_path_;
  std::optional<std::filesystem::path> textures_data_path_;
  std::optional<std::filesystem::path> scripts_table_path_;
  std::optional<std::filesystem::path> scripts_data_path_;
  std::optional<std::filesystem::path> physics_table_path_;
  std::optional<std::filesystem::path> physics_data_path_;

  std::optional<ResourceTable<data::BufferResource>> buffers_table_;
  std::optional<ResourceTable<data::TextureResource>> textures_table_;
  std::optional<ResourceTable<data::ScriptResource>> scripts_table_;
  std::optional<ResourceTable<data::PhysicsResource>> physics_table_;
};

} // namespace oxygen::content::internal
