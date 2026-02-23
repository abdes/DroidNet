//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Content/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Content/LooseCooked/Writer.h>

namespace oxygen::content::lc {

struct Writer::Impl {
  explicit Impl(std::filesystem::path cooked_root)
    : writer(std::move(cooked_root))
  {
  }

  import::LooseCookedWriter writer;
};

Writer::Writer(std::filesystem::path cooked_root)
  : impl_(std::make_unique<Impl>(std::move(cooked_root)))
{
}

Writer::~Writer() = default;

Writer::Writer(Writer&&) noexcept = default;

auto Writer::operator=(Writer&&) noexcept -> Writer& = default;

auto Writer::SetSourceKey(std::optional<data::SourceKey> key) -> void
{
  impl_->writer.SetSourceKey(key);
}

auto Writer::SetContentVersion(const uint16_t version) -> void
{
  impl_->writer.SetContentVersion(version);
}

auto Writer::SetComputeSha256(const bool enabled) -> void
{
  impl_->writer.SetComputeSha256(enabled);
}

auto Writer::WriteAssetDescriptor(const data::AssetKey& key,
  const data::AssetType asset_type, const std::string_view virtual_path,
  const std::string_view descriptor_relpath,
  const std::span<const std::byte> bytes) -> void
{
  impl_->writer.WriteAssetDescriptor(
    key, asset_type, virtual_path, descriptor_relpath, bytes);
}

auto Writer::WriteFile(const FileKind kind, const std::string_view relpath,
  const std::span<const std::byte> bytes) -> void
{
  impl_->writer.WriteFile(kind, relpath, bytes);
}

auto Writer::RegisterExternalFile(
  const FileKind kind, const std::string_view relpath) -> void
{
  impl_->writer.RegisterExternalFile(kind, relpath);
}

auto Writer::RegisterExternalAssetDescriptor(const data::AssetKey& key,
  const data::AssetType asset_type, const std::string_view virtual_path,
  const std::string_view descriptor_relpath, const uint64_t descriptor_size,
  const std::optional<base::Sha256Digest> descriptor_sha256) -> void
{
  impl_->writer.RegisterExternalAssetDescriptor(key, asset_type, virtual_path,
    descriptor_relpath, descriptor_size, descriptor_sha256);
}

auto Writer::Finish() -> WriteResult
{
  const auto src = impl_->writer.Finish();
  WriteResult out {
    .cooked_root = src.cooked_root,
    .source_key = src.source_key,
    .content_version = src.content_version,
  };

  out.assets.reserve(src.assets.size());
  for (const auto& asset : src.assets) {
    out.assets.push_back(AssetRecord {
      .key = asset.key,
      .asset_type = asset.asset_type,
      .virtual_path = asset.virtual_path,
      .descriptor_relpath = asset.descriptor_relpath,
      .descriptor_size = asset.descriptor_size,
      .descriptor_sha256 = asset.descriptor_sha256,
    });
  }

  out.files.reserve(src.files.size());
  for (const auto& file : src.files) {
    out.files.push_back(FileRecord {
      .kind = file.kind,
      .relpath = file.relpath,
      .size = file.size,
    });
  }
  return out;
}

} // namespace oxygen::content::lc
