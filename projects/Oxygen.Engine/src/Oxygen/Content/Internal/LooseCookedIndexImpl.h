//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Content/api_export.h> // For tests only
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/FileLock.h>
#include <Oxygen/Serio/MappedFile.h>

namespace oxygen::content::internal {

//! Parsed representation of a loose cooked `container.index.bin`.
class LooseCookedIndexImpl final {
public:
  struct AssetInfo {
    uint32_t descriptor_relpath_offset = 0;
    uint32_t virtual_path_offset = 0;
    uint32_t source_path_offset = 0;
    uint64_t descriptor_size = 0;
    uint8_t asset_type = 0;
    std::array<uint8_t, data::loose_cooked::kSha256Size> descriptor_sha256 = {};
    data::pak::core::AssetReferenceTable references {};
  };

  //! Load and validate a published index without taking the writer lock.
  /*!
   The file is mapped and parsed in place: the index keeps the mapping, and
   its strings and references are views of it.

   @param index_path Path to `container.index.bin`.
   @return Parsed index.

   @throw std::runtime_error if the file cannot be mapped; the errors of
     LoadFromBytes() if it fails validation.
  */
  OXGN_CNTT_NDAPI static auto LoadFromFile(
    const std::filesystem::path& index_path) -> LooseCookedIndexImpl;

  //! Load and validate the bytes of an index file, which the index copies.
  /*!
   @throw data::CookedFormatError `kWrongFormat` for bytes that are not an
     index, `kUnsupportedVersion` for another index version, `kCorrupt` for
     a malformed index.
  */
  OXGN_CNTT_NDAPI static auto LoadFromBytes(std::span<const std::byte> bytes)
    -> LooseCookedIndexImpl;

  //! Load and validate the bytes of an index file, which the index takes.
  OXGN_CNTT_NDAPI static auto LoadFromBytes(std::vector<std::byte>&& bytes)
    -> LooseCookedIndexImpl;

  OXGN_CNTT_NDAPI auto Guid() const noexcept -> data::SourceKey;

  [[nodiscard]] auto Commit() const noexcept -> uint64_t { return commit_; }

  [[nodiscard]] auto RetiredFiles() const noexcept
    -> std::span<const data::loose_cooked::RetiredFile>
  {
    return retired_;
  }

  [[nodiscard]] auto Requirements() const noexcept
    -> std::span<const lc::AssetRequirement>
  {
    return requirements_;
  }

  //! The cook-defined content version of the index header.
  [[nodiscard]] auto ContentVersion() const noexcept -> uint16_t
  {
    return content_version_;
  }

  OXGN_CNTT_NDAPI auto FindDescriptorRelPath(
    const data::AssetKey& key) const noexcept
    -> std::optional<std::string_view>;

  OXGN_CNTT_NDAPI auto FindDescriptorSize(
    const data::AssetKey& key) const noexcept -> std::optional<uint64_t>;

  OXGN_CNTT_NDAPI auto FindDescriptorSha256(
    const data::AssetKey& key) const noexcept
    -> std::optional<std::span<const uint8_t, data::loose_cooked::kSha256Size>>;

  OXGN_CNTT_NDAPI auto FindVirtualPath(const data::AssetKey& key) const noexcept
    -> std::optional<std::string_view>;

  OXGN_CNTT_NDAPI auto FindAssetType(const data::AssetKey& key) const noexcept
    -> std::optional<uint8_t>;
  //! Virtual path of the source that produced the asset.
  OXGN_CNTT_NDAPI auto FindSourcePath(const data::AssetKey& key) const noexcept
    -> std::optional<std::string_view>;
  OXGN_CNTT_NDAPI auto HasKeyReferences(
    const data::AssetKey& key) const noexcept -> bool;
  OXGN_CNTT_NDAPI auto FindAssetReferences(const data::AssetKey& key) const
    -> std::optional<data::AssetReferences>;

  OXGN_CNTT_NDAPI auto FindAssetKeyByVirtualPath(
    std::string_view virtual_path) const noexcept
    -> std::optional<data::AssetKey>;

  //! Relative path of the texture descriptor recorded under `virtual_path`.
  OXGN_CNTT_NDAPI auto FindTextureDescriptorRelPath(
    std::string_view virtual_path) const noexcept
    -> std::optional<std::string_view>;

  //! Virtual paths of every texture descriptor the index records.
  OXGN_CNTT_NDAPI auto TextureDescriptorVirtualPaths() const
    -> std::vector<std::string>;

  [[nodiscard]] auto GetAllAssetKeys() const noexcept
    -> std::span<const data::AssetKey>
  {
    return asset_keys_;
  }

  OXGN_CNTT_NDAPI auto GetAllFileKinds() const noexcept
    -> std::span<const data::loose_cooked::FileKind>;

  OXGN_CNTT_NDAPI auto FindFileRelPath(
    data::loose_cooked::FileKind kind) const noexcept
    -> std::optional<std::string_view>;

  OXGN_CNTT_NDAPI auto FindFileSize(
    data::loose_cooked::FileKind kind) const noexcept
    -> std::optional<uint64_t>;

  OXGN_CNTT_NDAPI auto GetFileInventory() const
    -> std::vector<lc::FileIntegrity>;
  OXGN_CNTT_NDAPI auto CheckContent(const std::filesystem::path& cooked_root,
    lc::IntegrityCheck check) const -> std::vector<lc::FileIntegrityIssue>;
  OXGN_CNTT_NDAPI auto CheckContent(const lc::RootFiles& files,
    lc::IntegrityCheck check) const -> std::vector<lc::FileIntegrityIssue>;
  OXGN_CNTT_API auto ValidateContent(const std::filesystem::path& cooked_root,
    lc::IntegrityCheck check) const -> void;
  OXGN_CNTT_NDAPI auto FindFileSha256(
    data::loose_cooked::FileKind kind) const noexcept
    -> std::optional<std::span<const uint8_t, data::loose_cooked::kSha256Size>>;

private:
  struct IndexLoadContext;

  //! The bytes the index was parsed from: its mapped file, or a copy of the
  //! bytes it was loaded from.
  using IndexBytes = std::variant<std::vector<std::byte>, serio::MappedFile>;

  struct FileInfo {
    uint32_t relpath_offset = 0;
    uint32_t virtual_path_offset = 0;
    uint32_t source_path_offset = 0;
    uint64_t size = 0;
    base::Sha256Digest sha256 {};
  };

  struct FileKindHash {
    auto operator()(data::loose_cooked::FileKind k) const noexcept -> size_t
    {
      return static_cast<size_t>(k);
    }
  };

  //! The string table and the reference tail are views of these bytes, which
  //! move with the index and are never copied.
  IndexBytes bytes_;
  std::string_view string_table_;
  uint64_t reference_tail_offset_ = 0;
  std::span<const std::byte> reference_tail_;
  uint16_t content_version_ = 0;
  std::vector<data::AssetKey> asset_keys_;
  std::unordered_map<data::AssetKey, AssetInfo> key_to_asset_info_;
  std::unordered_map<uint32_t, data::AssetKey> virtual_path_offset_to_key_;
  std::vector<data::loose_cooked::FileKind> file_kinds_;
  std::unordered_map<data::loose_cooked::FileKind, FileInfo, FileKindHash>
    kind_to_file_;
  std::vector<FileInfo> auxiliary_files_;
  std::vector<FileInfo> texture_descriptors_;
  data::SourceKey guid_;
  uint64_t commit_ = 0;
  std::vector<data::loose_cooked::RetiredFile> retired_;
  std::vector<lc::AssetRequirement> requirements_;

  static auto Load(IndexBytes bytes) -> LooseCookedIndexImpl;
  auto Parse() -> void;
  static auto LoadAndValidateHeader(IndexLoadContext& context) -> void;
  static auto ReadStringTable(IndexLoadContext& context) -> void;
  static auto ReadAssetEntries(IndexLoadContext& context) -> void;
  static auto ReadFileRecords(IndexLoadContext& context) -> void;
  static auto ReadRetiredRecords(IndexLoadContext& context) -> void;
  static auto ReadRequirementRecords(IndexLoadContext& context) -> void;
  static auto ValidateFilePairs(const LooseCookedIndexImpl& index) -> void;
};

//! The published index of a plain loose root and the reader lease of its
//! commit.
struct LeasedLooseCookedIndex final {
  LooseCookedIndexImpl index;
  //! Unlocked when the root admits no lease (a reader without write access).
  serio::FileLock lease;
};

//! Loads the index of the plain loose root `cooked_root` under a shared
//! lease of its commit, so no writer reclaims a file that commit lists while
//! the lease is held.
/*!
 The reader maps the index, takes `.leases/<commit>.lock` shared, then reads
 the current index's commit again: if a writer published a newer one in
 between, the lease may have come too late, and the reader starts over. A
 root whose lease the reader cannot create (access denied) is read unleased:
 no writer with the reader's permissions can write it either.

 @throw std::system_error `device_or_resource_busy` after eight commits in a
   row raced the lease; the errors of `LoadFromFile`.
*/
OXGN_CNTT_NDAPI auto LoadLeasedIndex(const std::filesystem::path& cooked_root)
  -> LeasedLooseCookedIndex;

} // namespace oxygen::content::internal
