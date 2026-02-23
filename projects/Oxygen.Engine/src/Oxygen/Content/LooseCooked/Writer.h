//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/LooseCooked/Types.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::lc {

struct AssetRecord final {
  data::AssetKey key {};
  data::AssetType asset_type = data::AssetType::kUnknown;
  std::string virtual_path;
  std::string descriptor_relpath;
  uint64_t descriptor_size = 0;
  std::optional<base::Sha256Digest> descriptor_sha256;
};

struct FileRecord final {
  FileKind kind = FileKind::kUnknown;
  std::string relpath;
  uint64_t size = 0;
};

struct WriteResult final {
  std::filesystem::path cooked_root;
  data::SourceKey source_key {};
  uint16_t content_version = 0;
  std::vector<AssetRecord> assets {}; // NOLINT
  std::vector<FileRecord> files {}; // NOLINT
};

class Writer final {
public:
  OXGN_CNTT_API explicit Writer(std::filesystem::path cooked_root);
  OXGN_CNTT_API ~Writer();

  Writer(const Writer&) = delete;
  auto operator=(const Writer&) -> Writer& = delete;
  OXGN_CNTT_API Writer(Writer&&) noexcept;
  OXGN_CNTT_API auto operator=(Writer&&) noexcept -> Writer&;

  OXGN_CNTT_API auto SetSourceKey(std::optional<data::SourceKey> key) -> void;
  OXGN_CNTT_API auto SetContentVersion(uint16_t version) -> void;
  OXGN_CNTT_API auto SetComputeSha256(bool enabled) -> void;
  OXGN_CNTT_API auto WriteAssetDescriptor(const data::AssetKey& key,
    data::AssetType asset_type, std::string_view virtual_path,
    std::string_view descriptor_relpath, std::span<const std::byte> bytes)
    -> void;
  OXGN_CNTT_API auto WriteFile(FileKind kind, std::string_view relpath,
    std::span<const std::byte> bytes) -> void;
  OXGN_CNTT_API auto RegisterExternalFile(
    FileKind kind, std::string_view relpath) -> void;
  OXGN_CNTT_API auto RegisterExternalAssetDescriptor(const data::AssetKey& key,
    data::AssetType asset_type, std::string_view virtual_path,
    std::string_view descriptor_relpath, uint64_t descriptor_size,
    std::optional<base::Sha256Digest> descriptor_sha256 = std::nullopt) -> void;
  OXGN_CNTT_NDAPI auto Finish() -> WriteResult;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::content::lc
