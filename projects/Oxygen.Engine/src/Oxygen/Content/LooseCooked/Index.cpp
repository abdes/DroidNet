//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Content/Internal/LooseCookedIndexImpl.h>
#include <Oxygen/Content/LooseCooked/Index.h>

namespace oxygen::content::lc {

Index::Index(std::unique_ptr<internal::LooseCookedIndexImpl> impl)
  : impl_(std::move(impl))
{
}

Index::~Index() = default;

Index::Index(Index&&) noexcept = default;

auto Index::operator=(Index&&) noexcept -> Index& = default;

auto Index::Guid() const noexcept -> data::SourceKey { return impl_->Guid(); }

auto Index::FindDescriptorRelPath(const data::AssetKey& key) const noexcept
  -> std::optional<std::string_view>
{
  return impl_->FindDescriptorRelPath(key);
}

auto Index::FindDescriptorSize(const data::AssetKey& key) const noexcept
  -> std::optional<uint64_t>
{
  return impl_->FindDescriptorSize(key);
}

auto Index::FindDescriptorSha256(const data::AssetKey& key) const noexcept
  -> std::optional<std::span<const uint8_t, data::loose_cooked::kSha256Size>>
{
  return impl_->FindDescriptorSha256(key);
}

auto Index::FindVirtualPath(const data::AssetKey& key) const noexcept
  -> std::optional<std::string_view>
{
  return impl_->FindVirtualPath(key);
}

auto Index::FindAssetType(const data::AssetKey& key) const noexcept
  -> std::optional<uint8_t>
{
  return impl_->FindAssetType(key);
}

auto Index::FindAssetKeyByVirtualPath(
  std::string_view virtual_path) const noexcept -> std::optional<data::AssetKey>
{
  return impl_->FindAssetKeyByVirtualPath(virtual_path);
}

auto Index::GetAllAssetKeys() const noexcept -> std::span<const data::AssetKey>
{
  return impl_->GetAllAssetKeys();
}

auto Index::GetAllFileKinds() const noexcept -> std::span<const FileKind>
{
  return impl_->GetAllFileKinds();
}

auto Index::FindFileRelPath(FileKind kind) const noexcept
  -> std::optional<std::string_view>
{
  return impl_->FindFileRelPath(kind);
}

auto Index::FindFileSize(FileKind kind) const noexcept
  -> std::optional<uint64_t>
{
  return impl_->FindFileSize(kind);
}

} // namespace oxygen::content::lc
