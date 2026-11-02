//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <utility>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Content/Internal/ResourceTypes.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/LooseCookedResources.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/FileLock.h>
#include <Oxygen/Serio/MappedFile.h>
#include <Oxygen/Serio/Region.h>

namespace oxygen::data {
struct TextureResourceDescriptor;
} // namespace oxygen::data

namespace oxygen::serio {
class IoDriver;
} // namespace oxygen::serio

namespace oxygen::content::internal {

//! A mounted loose cooked root.
/*!
 Mounting maps the resource tables and opens the data files (through the
 loader's IO driver when it has one), so a load opens nothing but the asset
 descriptor it reads: the loose layout keeps each in a file of its own. An
 index-only source, for lookups while a cook has unpublished files, opens
 nothing but the index.
*/
class LooseCookedSource final : public IContentSource {
public:
  OXYGEN_TYPED(LooseCookedSource)

public:
  //! Index-only opening supports lookups while a cook has unpublished files.
  enum class OpenMode : uint8_t {
    kIndexOnly,
    kValidateMetadata,
    kVerifyContent,
  };

  //! Mounts the root `cooked_root`; with `driver`, its payloads are awaited
  //! through it.
  explicit LooseCookedSource(std::filesystem::path cooked_root,
    const OpenMode mode, serio::FileLock generation_lock = {},
    std::shared_ptr<serio::IoDriver> driver = {})
    : cooked_root_(std::move(cooked_root))
    , debug_name_(cooked_root_.string())
    , generation_lock_(std::move(generation_lock))
    , driver_(std::move(driver))
    , index_(LooseCookedIndexImpl::LoadFromFile(
        cooked_root_ / oxygen::data::loose_cooked::kIndexFileName))
  {
    using oxygen::data::loose_cooked::FileKind;

    if (mode != OpenMode::kIndexOnly) {
      index_.ValidateContent(cooked_root_,
        mode == OpenMode::kVerifyContent ? lc::IntegrityCheck::kFull
                                         : lc::IntegrityCheck::kMetadata);
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kBuffersTable); rel) {
      buffers_table_path_ = cooked_root_ / std::filesystem::path(*rel);
      buffers_table_.emplace(TableOf(FileKind::kBuffersTable));
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kTexturesTable);
      rel) {
      textures_table_path_ = cooked_root_ / std::filesystem::path(*rel);
      textures_table_.emplace(TableOf(FileKind::kTexturesTable));
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kBuffersData); rel) {
      buffers_data_path_ = cooked_root_ / std::filesystem::path(*rel);
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kTexturesData); rel) {
      textures_data_path_ = cooked_root_ / std::filesystem::path(*rel);
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kScriptsTable); rel) {
      scripts_table_path_ = cooked_root_ / std::filesystem::path(*rel);
      scripts_table_.emplace(TableOf(FileKind::kScriptsTable));
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kScriptsData); rel) {
      scripts_data_path_ = cooked_root_ / std::filesystem::path(*rel);
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kPhysicsTable); rel) {
      physics_table_path_ = cooked_root_ / std::filesystem::path(*rel);
      physics_table_.emplace(TableOf(FileKind::kPhysicsTable));
    }

    if (const auto rel = index_.FindFileRelPath(FileKind::kPhysicsData); rel) {
      physics_data_path_ = cooked_root_ / std::filesystem::path(*rel);
    }

    if (mode != OpenMode::kIndexOnly) {
      OpenFiles();
    }
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
    return *std::ranges::next(keys.begin(), index);
  }

  [[nodiscard]] auto CreateAssetDescriptorReader(
    const data::AssetKey& key) const
    -> std::unique_ptr<serio::AnyReader> override
  {
    const auto rel = index_.FindDescriptorRelPath(key);
    if (!rel.has_value()) {
      return nullptr;
    }
    return OpenDescriptor(cooked_root_ / std::filesystem::path(*rel));
  }

  OXGN_CNTT_NDAPI auto FindTextureIndexByVirtualPath(
    std::string_view virtual_path) const
    -> std::optional<data::pak::core::ResourceIndexT> override;

  [[nodiscard]] auto TextureVirtualPaths() const
    -> std::vector<std::string> override
  {
    return index_.TextureDescriptorVirtualPaths();
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

  OXGN_CNTT_NDAPI auto PayloadRegion(TypeId resource_type) const noexcept
    -> std::optional<serio::Region<serio::AnyAsyncDevice>> override;

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
  //! The files of one resource type, opened at mount.
  struct ResourceFiles {
    std::optional<serio::MappedFile> table;
    std::optional<serio::AnyAsyncDevice> data;
    //! All of `data` as the mount found it: the payloads the mounted index
    //! names lie in it, whatever an import appends later.
    std::optional<serio::Region<serio::AnyAsyncDevice>> payloads;
  };

  OXGN_CNTT_NDAPI auto ResourceRowSpace(TypeId resource_type) const noexcept
    -> std::span<const std::byte> override;

  //! The files of the resource type `resource_type` names; null for any other
  //! type.
  [[nodiscard]] auto FilesOf(TypeId resource_type) const noexcept
    -> const ResourceFiles*;

  //! The files of the resource type `T`.
  template <typename T> [[nodiscard]] auto FilesOf() noexcept -> ResourceFiles&
  {
    return files_.at(kResourceTypeIndex<T>);
  }

  OXGN_CNTT_API auto OpenFiles() -> void;

  OXGN_CNTT_NDAPI static auto OpenDescriptor(const std::filesystem::path& path)
    -> std::unique_ptr<serio::AnyReader>;

  //! Checks that the decoded descriptor agrees with its source texture table.
  auto ValidateTextureDescriptorAgainstTable(const std::filesystem::path& path,
    const data::TextureResourceDescriptor& descriptor) const -> void;

  //! The metadata of the indexed resource table file `kind`; the index
  //! admits only tables of whole rows, at most 2^32 of them.
  [[nodiscard]] auto TableOf(const data::loose_cooked::FileKind kind) const
    -> data::pak::core::ResourceTable
  {
    const auto row_size = data::loose_cooked::ResourceRowSize(kind);
    const auto rows = index_.FindFileSize(kind).value_or(0U) / row_size;
    return {
      .offset = 0,
      .count = static_cast<uint32_t>(rows),
      .entry_size = static_cast<uint32_t>(row_size),
    };
  }

  std::filesystem::path cooked_root_;
  std::string debug_name_;
  serio::FileLock generation_lock_;
  //! Declared before the files: the driver outlives the files it opened.
  std::shared_ptr<serio::IoDriver> driver_;
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
  //! Indexed by `kResourceTypeIndex`.
  std::array<ResourceFiles, kResourceTypeCount> files_ {};
};

} // namespace oxygen::content::internal
