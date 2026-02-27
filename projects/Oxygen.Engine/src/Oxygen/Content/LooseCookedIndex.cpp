//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Content/Internal/LooseCookedIndexImpl.h>
#include <Oxygen/Content/LooseCookedIndex.h>

namespace oxygen::content::lc {

LooseCookedIndex::LooseCookedIndex(
  std::unique_ptr<internal::LooseCookedIndexImpl> impl)
  : impl_(std::move(impl))
{
}

LooseCookedIndex::~LooseCookedIndex() = default;

LooseCookedIndex::LooseCookedIndex(LooseCookedIndex&&) noexcept = default;

auto LooseCookedIndex::operator=(LooseCookedIndex&&) noexcept
  -> LooseCookedIndex& = default;

auto LooseCookedIndex::LoadFromRoot(const std::filesystem::path& cooked_root)
  -> LooseCookedIndex
{
  return LoadFromFile(cooked_root / "container.index.bin");
}

auto LooseCookedIndex::LoadFromFile(const std::filesystem::path& index_path)
  -> LooseCookedIndex
{
  auto index = std::make_unique<internal::LooseCookedIndexImpl>(
    internal::LooseCookedIndexImpl::LoadFromFile(index_path));
  return LooseCookedIndex(std::move(index));
}

auto LooseCookedIndex::Guid() const noexcept -> data::SourceKey
{
  return impl_->Guid();
}

auto LooseCookedIndex::FindDescriptorRelPath(
  const data::AssetKey& key) const noexcept -> std::optional<std::string_view>
{
  return impl_->FindDescriptorRelPath(key);
}

auto LooseCookedIndex::FindDescriptorSize(
  const data::AssetKey& key) const noexcept -> std::optional<uint64_t>
{
  return impl_->FindDescriptorSize(key);
}

auto LooseCookedIndex::FindDescriptorSha256(
  const data::AssetKey& key) const noexcept
  -> std::optional<std::span<const uint8_t, data::loose_cooked::kSha256Size>>
{
  return impl_->FindDescriptorSha256(key);
}

auto LooseCookedIndex::FindVirtualPath(const data::AssetKey& key) const noexcept
  -> std::optional<std::string_view>
{
  return impl_->FindVirtualPath(key);
}

auto LooseCookedIndex::FindAssetType(const data::AssetKey& key) const noexcept
  -> std::optional<uint8_t>
{
  return impl_->FindAssetType(key);
}

auto LooseCookedIndex::FindAssetKeyByVirtualPath(
  std::string_view virtual_path) const noexcept -> std::optional<data::AssetKey>
{
  return impl_->FindAssetKeyByVirtualPath(virtual_path);
}

auto LooseCookedIndex::GetAllAssetKeys() const noexcept
  -> std::span<const data::AssetKey>
{
  return impl_->GetAllAssetKeys();
}

auto LooseCookedIndex::GetAllFileKinds() const noexcept
  -> std::span<const FileKind>
{
  return impl_->GetAllFileKinds();
}

auto LooseCookedIndex::FindFileRelPath(FileKind kind) const noexcept
  -> std::optional<std::string_view>
{
  return impl_->FindFileRelPath(kind);
}

auto LooseCookedIndex::FindFileSize(FileKind kind) const noexcept
  -> std::optional<uint64_t>
{
  return impl_->FindFileSize(kind);
}

} // namespace oxygen::content::lc
