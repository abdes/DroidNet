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
#include <vector>

#include <Oxygen/Content/api_export.h> // For tests only
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::detail {

//! Parsed representation of a loose cooked `container.index.bin`.
class LooseCookedIndex final {
public:
  struct AssetInfo {
    uint32_t descriptor_relpath_offset = 0;
    uint32_t virtual_path_offset = 0;
    uint64_t descriptor_size = 0;
    uint8_t asset_type = 0;
    std::array<uint8_t, data::loose_cooked::v1::kSha256Size> descriptor_sha256
      = {};
  };

  //! Load and validate an index file.
  /*!
   @param index_path Path to `container.index.bin`.
   @return Parsed index.

   @throw std::runtime_error if the file cannot be read or fails validation.
  */
  OXGN_CNTT_NDAPI static auto LoadFromFile(
    const std::filesystem::path& index_path) -> LooseCookedIndex;

  OXGN_CNTT_NDAPI auto Guid() const noexcept -> data::SourceKey;

  OXGN_CNTT_NDAPI auto FindDescriptorRelPath(
    const data::AssetKey& key) const noexcept
    -> std::optional<std::string_view>;

  OXGN_CNTT_NDAPI auto FindDescriptorSize(
    const data::AssetKey& key) const noexcept -> std::optional<uint64_t>;

  OXGN_CNTT_NDAPI auto FindDescriptorSha256(
    const data::AssetKey& key) const noexcept
    -> std::optional<
      std::span<const uint8_t, data::loose_cooked::v1::kSha256Size>>;

  OXGN_CNTT_NDAPI auto FindVirtualPath(const data::AssetKey& key) const noexcept
    -> std::optional<std::string_view>;

  OXGN_CNTT_NDAPI auto FindAssetType(const data::AssetKey& key) const noexcept
    -> std::optional<uint8_t>;

  OXGN_CNTT_NDAPI auto FindAssetKeyByVirtualPath(
    std::string_view virtual_path) const noexcept
    -> std::optional<data::AssetKey>;

  [[nodiscard]] auto GetAllAssetKeys() const noexcept
    -> std::span<const data::AssetKey>
  {
    return asset_keys_;
  }

  OXGN_CNTT_NDAPI auto GetAllFileKinds() const noexcept
    -> std::span<const data::loose_cooked::v1::FileKind>;

  OXGN_CNTT_NDAPI auto FindFileRelPath(
    data::loose_cooked::v1::FileKind kind) const noexcept
    -> std::optional<std::string_view>;

  OXGN_CNTT_NDAPI auto FindFileSize(
    data::loose_cooked::v1::FileKind kind) const noexcept
    -> std::optional<uint64_t>;

private:
  struct FileInfo {
    uint32_t relpath_offset = 0;
    uint64_t size = 0;
  };

  struct FileKindHash {
    auto operator()(data::loose_cooked::v1::FileKind k) const noexcept -> size_t
    {
      return static_cast<size_t>(k);
    }
  };

  std::string string_storage_;
  std::vector<data::AssetKey> asset_keys_;
  std::unordered_map<data::AssetKey, AssetInfo> key_to_asset_info_;
  std::unordered_map<uint32_t, data::AssetKey> virtual_path_offset_to_key_;
  std::vector<data::loose_cooked::v1::FileKind> file_kinds_;
  std::unordered_map<data::loose_cooked::v1::FileKind, FileInfo, FileKindHash>
    kind_to_file_;
  data::SourceKey guid_;
};

} // namespace oxygen::content::detail
