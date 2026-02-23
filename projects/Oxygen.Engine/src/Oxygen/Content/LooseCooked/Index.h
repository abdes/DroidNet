//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include <Oxygen/Content/LooseCooked/Types.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::internal {
class LooseCookedIndexImpl;
}

namespace oxygen::content::lc {

class Index final {
public:
  Index() = delete;
  OXGN_CNTT_API ~Index();

  OXGN_CNTT_API Index(Index&&) noexcept;
  OXGN_CNTT_API auto operator=(Index&&) noexcept -> Index&;

  Index(const Index&) = delete;
  auto operator=(const Index&) -> Index& = delete;

  OXGN_CNTT_NDAPI auto Guid() const noexcept -> data::SourceKey;

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
  OXGN_CNTT_NDAPI auto FindAssetKeyByVirtualPath(
    std::string_view virtual_path) const noexcept
    -> std::optional<data::AssetKey>;
  OXGN_CNTT_NDAPI auto GetAllAssetKeys() const noexcept
    -> std::span<const data::AssetKey>;
  OXGN_CNTT_NDAPI auto GetAllFileKinds() const noexcept
    -> std::span<const FileKind>;
  OXGN_CNTT_NDAPI auto FindFileRelPath(FileKind kind) const noexcept
    -> std::optional<std::string_view>;
  OXGN_CNTT_NDAPI auto FindFileSize(FileKind kind) const noexcept
    -> std::optional<uint64_t>;

private:
  friend class Reader;
  explicit Index(std::unique_ptr<internal::LooseCookedIndexImpl> impl);

  std::unique_ptr<internal::LooseCookedIndexImpl> impl_;
};

} // namespace oxygen::content::lc
