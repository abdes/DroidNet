//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/LooseCookedInspection.h>

#include <array>
#include <utility>

#include <Oxygen/Content/Internal/LooseCookedIndex.h>

namespace oxygen::content {

struct LooseCookedInspection::Impl {
  std::vector<AssetEntry> assets;
  std::vector<FileEntry> files;
  data::SourceKey guid = {};
};

LooseCookedInspection::LooseCookedInspection()
  : impl_(std::make_unique<Impl>())
{
}

LooseCookedInspection::~LooseCookedInspection() = default;

auto LooseCookedInspection::LoadFromRoot(
  const std::filesystem::path& cooked_root) -> void
{
  LoadFromFile(cooked_root / "container.index.bin");
}

auto LooseCookedInspection::LoadFromFile(
  const std::filesystem::path& index_path) -> void
{
  const internal::LooseCookedIndex index
    = internal::LooseCookedIndex::LoadFromFile(index_path);

  impl_->assets.clear();
  impl_->files.clear();
  impl_->guid = index.Guid();

  for (const auto& key : index.GetAllAssetKeys()) {
    AssetEntry out;
    out.key = key;

    if (const auto vpath = index.FindVirtualPath(key); vpath) {
      out.virtual_path = std::string(*vpath);
    }

    if (const auto rel = index.FindDescriptorRelPath(key); rel) {
      out.descriptor_relpath = std::string(*rel);
    }

    if (const auto size = index.FindDescriptorSize(key); size) {
      out.descriptor_size = *size;
    }

    if (const auto type = index.FindAssetType(key); type) {
      out.asset_type = *type;
    }

    if (const auto sha = index.FindDescriptorSha256(key); sha) {
      base::Sha256Digest digest = {};
      std::copy_n(sha->begin(), digest.size(), digest.begin());
      out.descriptor_sha256 = digest;
    }

    impl_->assets.push_back(std::move(out));
  }

  for (const auto kind : index.GetAllFileKinds()) {
    const auto rel = index.FindFileRelPath(kind);
    if (!rel) {
      continue;
    }

    FileEntry out;
    out.kind = kind;
    out.relpath = std::string(*rel);

    if (const auto size = index.FindFileSize(kind); size) {
      out.size = *size;
    }

    impl_->files.push_back(std::move(out));
  }
}

auto LooseCookedInspection::Assets() const noexcept
  -> std::span<const AssetEntry>
{
  return impl_->assets;
}

auto LooseCookedInspection::Files() const noexcept -> std::span<const FileEntry>
{
  return impl_->files;
}

auto LooseCookedInspection::Guid() const noexcept -> data::SourceKey
{
  return impl_->guid;
}

} // namespace oxygen::content
