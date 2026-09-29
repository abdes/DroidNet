//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <ios>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Loose/Types.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/TextureResourceDescriptor.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::lc {

struct Inspection::Impl {
  std::vector<AssetEntry> assets;
  std::vector<FileEntry> files;
  data::SourceKey source_identity = {};
  std::filesystem::path root;
};

Inspection::Inspection()
  : impl_(std::make_unique<Impl>())
{
}

Inspection::~Inspection() = default;

Inspection::Inspection(Inspection&&) noexcept = default;

auto Inspection::operator=(Inspection&&) noexcept -> Inspection& = default;

auto Inspection::LoadFromRoot(const std::filesystem::path& cooked_root) -> void
{
  LoadFromFile(cooked_root / "container.index.bin");
}

auto Inspection::LoadFromFile(const std::filesystem::path& index_path) -> void
{
  const auto index = LooseCookedIndex::LoadFromFile(index_path);

  impl_->assets.clear();
  impl_->files.clear();
  impl_->source_identity = index.Guid();
  impl_->root = index_path.parent_path();

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

  for (const auto& file : index.GetFileInventory()) {
    if (file.kind.has_value()) {
      impl_->files.push_back(FileEntry {
        .kind = *file.kind,
        .relpath = file.relative_path,
        .size = file.size,
        .sha256 = file.sha256,
      });
    }
  }
  std::ranges::sort(
    impl_->files, [](const FileEntry& left, const FileEntry& right) {
      return std::tie(left.kind, left.relpath)
        < std::tie(right.kind, right.relpath);
    });
}

auto Inspection::Assets() const noexcept -> std::span<const AssetEntry>
{
  return impl_->assets;
}

auto Inspection::Files() const noexcept -> std::span<const FileEntry>
{
  return impl_->files;
}

auto Inspection::Guid() const noexcept -> data::SourceKey
{
  return impl_->source_identity;
}

auto Inspection::ReadTextureDescriptor(
  const std::string_view relative_path) const -> data::TextureResourceDescriptor
{
  const auto file
    = std::ranges::find(impl_->files, relative_path, &FileEntry::relpath);
  const auto table = std::ranges::find(
    impl_->files, FileKind::kTexturesTable, &FileEntry::kind);
  constexpr auto entry_size = sizeof(data::pak::core::TextureResourceDesc);
  if (file == impl_->files.end() || file->kind != FileKind::kAuxiliary
    || file->size != data::kTextureResourceDescriptorSize
    || table == impl_->files.end() || table->size % entry_size != 0) {
    throw std::runtime_error("Texture descriptor or table metadata is invalid");
  }
  serio::FileStream<> stream(impl_->root / file->relpath, std::ios::in);
  serio::Reader reader(stream);
  const auto bytes = reader.ReadBlob(data::kTextureResourceDescriptorSize);
  if (!bytes) {
    throw std::runtime_error("Cannot read texture descriptor");
  }
  const auto decoded = data::DecodeTextureResourceDescriptor(bytes.value());
  if (!decoded) {
    throw std::runtime_error(decoded.error());
  }
  if (decoded->index == data::pak::core::kNoResourceIndex
    || decoded->index >= table->size / entry_size) {
    throw std::runtime_error(
      "Texture descriptor index is outside its resource table");
  }
  serio::FileStream<> table_stream(impl_->root / table->relpath, std::ios::in);
  serio::Reader table_reader(table_stream);
  if (!table_reader.Seek(static_cast<uint64_t>(decoded->index) * entry_size)) {
    throw std::runtime_error("Cannot seek texture resource table");
  }
  const auto expected = table_reader.ReadBlob(entry_size);
  if (!expected
    || std::memcmp(expected->data(), &decoded->descriptor, entry_size) != 0) {
    throw std::runtime_error(
      "Texture descriptor does not match its resource table");
  }
  return decoded.value();
}

} // namespace oxygen::content::lc
