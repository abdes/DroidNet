//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Content/Internal/LooseCookedIndexImpl.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/SourceKey.h>

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

auto LooseCookedIndex::FindAssetReferences(const data::AssetKey& key) const
  -> std::optional<data::AssetReferences>
{
  return impl_->FindAssetReferences(key);
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

auto LooseCookedIndex::GetFileInventory() const -> std::vector<FileIntegrity>
{
  return impl_->GetFileInventory();
}

auto LooseCookedIndex::CheckContent(const std::filesystem::path& cooked_root,
  const IntegrityCheck check) const -> std::vector<FileIntegrityIssue>
{
  return impl_->CheckContent(cooked_root, check);
}

auto LooseCookedIndex::ValidateContent(const std::filesystem::path& cooked_root,
  const IntegrityCheck check) const -> void
{
  impl_->ValidateContent(cooked_root, check);
}

auto LooseCookedIndex::FindFileSha256(const FileKind kind) const noexcept
  -> std::optional<std::span<const uint8_t, data::loose_cooked::kSha256Size>>
{
  return impl_->FindFileSha256(kind);
}

} // namespace oxygen::content::lc
