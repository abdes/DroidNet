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
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Content/PakFile.h>
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
  //! The open pak when the source is one; descriptors are read through it.
  std::unique_ptr<PakFile> pak;
};

Inspection::Inspection()
  : impl_(std::make_unique<Impl>())
{
}

Inspection::~Inspection() = default;

Inspection::Inspection(Inspection&&) noexcept = default;

auto Inspection::operator=(Inspection&&) noexcept -> Inspection& = default;

auto Inspection::Load(const std::filesystem::path& source) -> void
{
  if (std::filesystem::is_regular_file(source)) {
    LoadFromPak(source);
  } else {
    LoadFromRoot(source);
  }
}

auto Inspection::IsCookedSource(const std::filesystem::path& source) noexcept
  -> bool
{
  std::error_code ec;
  return std::filesystem::is_regular_file(source, ec)
    || std::filesystem::is_regular_file(source / "container.index.bin", ec);
}

auto Inspection::LoadFromPak(const std::filesystem::path& pak_path) -> void
{
  auto pak = std::make_unique<PakFile>(pak_path);
  auto paths = std::unordered_map<data::AssetKey, std::string> {};
  for (const auto& entry : pak->BrowseIndex()) {
    paths.insert_or_assign(entry.asset_key, entry.virtual_path);
  }

  impl_->assets.clear();
  impl_->files.clear();
  impl_->source_identity = pak->Catalog().source_key;
  impl_->root = pak_path;
  for (const auto& entry : pak->Directory()) {
    auto out = AssetEntry {
      .key = entry.asset_key,
      .virtual_path = {},
      .descriptor_relpath = {},
      .descriptor_size = entry.desc_size,
      .asset_type = static_cast<uint8_t>(entry.asset_type),
      .descriptor_sha256 = std::nullopt,
      .references = pak->ReadAssetReferences(entry.asset_key),
      .source_path = {},
    };
    if (const auto path = paths.find(entry.asset_key); path != paths.end()) {
      out.virtual_path = path->second;
    }
    impl_->assets.push_back(std::move(out));
  }
  impl_->pak = std::move(pak);
}

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
  impl_->pak.reset();

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
    if (auto references = index.FindAssetReferences(key); references) {
      out.references = std::move(*references);
    }
    if (const auto source = index.FindSourcePath(key); source) {
      out.source_path = std::string(*source);
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
        .virtual_path = file.virtual_path,
        .source_path = file.source_path,
      });
    }
  }
  std::ranges::sort(
    impl_->files, [](const FileEntry& left, const FileEntry& right) -> bool {
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

auto Inspection::IsPak() const noexcept -> bool
{
  return impl_->pak != nullptr;
}

auto Inspection::ReadAssetDescriptor(const data::AssetKey& key) const
  -> std::vector<std::byte>
{
  const auto asset = std::ranges::find(impl_->assets, key, &AssetEntry::key);
  if (asset == impl_->assets.end()) {
    throw std::runtime_error("The cooked source does not list the asset");
  }
  const auto size = static_cast<size_t>(asset->descriptor_size);
  if (impl_->pak) {
    const auto entry = impl_->pak->FindEntry(key);
    if (!entry) {
      throw std::runtime_error("The pak directory does not list the asset");
    }
    auto reader = impl_->pak->CreateReader(*entry);
    auto bytes = reader.ReadBlob(size);
    if (!bytes) {
      throw std::runtime_error("Cannot read the asset descriptor from the pak");
    }
    return std::move(bytes).value();
  }
  serio::FileStream<> stream(
    impl_->root / std::filesystem::path(asset->descriptor_relpath),
    std::ios::in);
  serio::Reader reader(stream);
  auto bytes = reader.ReadBlob(size);
  if (!bytes) {
    throw std::runtime_error("Cannot read the asset descriptor");
  }
  return std::move(bytes).value();
}

auto Inspection::FindTexture(const std::string_view virtual_path) const
  -> std::optional<data::pak::core::TextureResourceDesc>
{
  if (impl_->pak) {
    const auto index
      = impl_->pak->ResolveTextureIndexByVirtualPath(virtual_path);
    if (!index) {
      return std::nullopt;
    }
    const auto offset = impl_->pak->TexturesTable().GetResourceOffset(*index);
    if (!offset) {
      throw std::runtime_error(
        "Pak texture entry is outside its resource table");
    }
    serio::FileStream<> stream(impl_->pak->FilePath(), std::ios::in);
    serio::Reader reader(stream);
    auto descriptor = data::pak::core::TextureResourceDesc {};
    if (!reader.Seek(*offset)
      || !reader.ReadBlobInto(
        std::as_writable_bytes(std::span(&descriptor, 1)))) {
      throw std::runtime_error("Cannot read the pak texture descriptor");
    }
    return descriptor;
  }
  const auto file
    = std::ranges::find(impl_->files, virtual_path, &FileEntry::virtual_path);
  if (file == impl_->files.end()
    || file->kind != FileKind::kTextureDescriptor) {
    return std::nullopt;
  }
  return ReadTextureDescriptor(file->relpath).descriptor;
}

auto Inspection::ReadTextureDescriptor(
  const std::string_view relative_path) const -> data::TextureResourceDescriptor
{
  const auto file
    = std::ranges::find(impl_->files, relative_path, &FileEntry::relpath);
  const auto table = std::ranges::find(
    impl_->files, FileKind::kTexturesTable, &FileEntry::kind);
  constexpr auto entry_size = sizeof(data::pak::core::TextureResourceDesc);
  if (file == impl_->files.end() || file->kind != FileKind::kTextureDescriptor
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
