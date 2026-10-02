//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormatVersions.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::data {
namespace {

  constexpr uint64_t kDigestSize = 32U;
  constexpr uint64_t kHeaderSize = sizeof(uint32_t) + SourceKey::kSizeBytes
    + sizeof(uint16_t) + (3U * sizeof(uint32_t));
  constexpr uint64_t kEntrySize
    = AssetKey::kSizeBytes + sizeof(uint8_t) + (2U * kDigestSize);
  constexpr uint64_t kBaseSize
    = SourceKey::kSizeBytes + sizeof(uint16_t) + kDigestSize;

  auto ContentSize(const uint32_t entries, const uint32_t deleted,
    const uint32_t bases) noexcept -> uint64_t
  {
    return kHeaderSize + (uint64_t { entries } * kEntrySize)
      + (uint64_t { deleted } * AssetKey::kSizeBytes)
      + (uint64_t { bases } * kBaseSize);
  }

  auto ValidateStructure(const PakCatalog& catalog) -> Result<void, std::string>
  {
    constexpr auto kMaxCount = std::numeric_limits<uint32_t>::max();
    if (catalog.source_key.IsNil() || catalog.entries.size() > kMaxCount
      || catalog.deleted.size() > kMaxCount
      || catalog.bases.size() > kMaxCount) {
      return Err(std::string("Invalid PAK catalog identity or record count"));
    }
    std::unordered_set<AssetKey> keys;
    for (const auto& entry : catalog.entries) {
      if (entry.asset_key.IsNil() || entry.asset_type <= AssetType::kUnknown
        || entry.asset_type > AssetType::kMaxAssetType
        || !keys.insert(entry.asset_key).second) {
        return Err(std::string("Invalid or duplicate PAK catalog entry"));
      }
    }
    for (const auto& key : catalog.deleted) {
      if (key.IsNil() || !keys.insert(key).second) {
        return Err(std::string("Duplicate or conflicting PAK deletion"));
      }
    }
    std::unordered_set<SourceKey> base_keys;
    for (const auto& base : catalog.bases) {
      if (base.source_key.IsNil() || base.source_key == catalog.source_key
        || !base_keys.insert(base.source_key).second) {
        return Err(
          std::string("Invalid, repeated or self-referencing PAK base"));
      }
    }
    return Result<void, std::string>::Ok();
  }

  auto EncodeContents(const PakCatalog& catalog)
    -> Result<std::vector<std::byte>, std::string>
  {
    if (const auto valid = ValidateStructure(catalog); !valid) {
      return Err(valid.error());
    }
    const auto entry_count = static_cast<uint32_t>(catalog.entries.size());
    const auto deleted_count = static_cast<uint32_t>(catalog.deleted.size());
    const auto base_count = static_cast<uint32_t>(catalog.bases.size());
    const auto size = ContentSize(entry_count, deleted_count, base_count);
    if (size > std::numeric_limits<size_t>::max()) {
      return Err(std::string("PAK catalog exceeds addressable size"));
    }
    auto entries = catalog.entries;
    auto deleted = catalog.deleted;
    std::ranges::sort(entries, {}, &PakCatalogEntry::asset_key);
    std::ranges::sort(deleted);
    std::vector<std::byte> bytes(static_cast<size_t>(size));
    serio::MemoryStream stream { std::span(bytes) };
    serio::Writer writer(stream);
    const auto packed = writer.ScopedAlignment(1);
    if (!writer.Write(pak::version::kPakCatalogVersion)
      || !writer.WriteBlob(as_bytes(catalog.source_key))
      || !writer.Write(catalog.content_version) || !writer.Write(entry_count)
      || !writer.Write(deleted_count) || !writer.Write(base_count)) {
      return Err(std::string("Could not encode PAK catalog header"));
    }
    for (const auto& entry : entries) {
      if (!writer.WriteBlob(as_bytes(entry.asset_key))
        || !writer.Write(static_cast<uint8_t>(entry.asset_type))
        || !writer.WriteBlob(std::as_bytes(std::span(entry.descriptor_digest)))
        || !writer.WriteBlob(
          std::as_bytes(std::span(entry.transitive_resource_digest)))) {
        return Err(std::string("Could not encode PAK catalog entry"));
      }
    }
    for (const auto& key : deleted) {
      if (!writer.WriteBlob(as_bytes(key))) {
        return Err(std::string("Could not encode PAK catalog deletion"));
      }
    }
    // Base order is semantic precedence, unlike entry and deletion ordering.
    for (const auto& base : catalog.bases) {
      if (!writer.WriteBlob(as_bytes(base.source_key))
        || !writer.Write(base.content_version)
        || !writer.WriteBlob(std::as_bytes(std::span(base.catalog_digest)))) {
        return Err(std::string("Could not encode PAK catalog base"));
      }
    }
    return Ok(std::move(bytes));
  }

} // namespace

auto PakCatalog::ComputeDigest() const
  -> Result<base::Sha256Digest, std::string>
{
  const auto contents = EncodeContents(*this);
  if (!contents) {
    return Err(contents.error());
  }
  return Ok(base::ComputeSha256(*contents));
}

auto PakCatalog::Validate() const -> Result<void, std::string>
{
  const auto digest = ComputeDigest();
  if (!digest) {
    return Err(digest.error());
  }
  if (*digest != catalog_digest) {
    return Err(std::string("PAK catalog digest mismatch"));
  }
  return Result<void, std::string>::Ok();
}

auto PakCatalog::Encode() const -> Result<std::vector<std::byte>, std::string>
{
  auto contents = EncodeContents(*this);
  if (!contents) {
    return Err(contents.error());
  }
  if (base::ComputeSha256(*contents) != catalog_digest) {
    return Err(std::string("PAK catalog digest mismatch"));
  }
  const auto digest_bytes = std::as_bytes(std::span(catalog_digest));
  contents->insert(contents->end(), digest_bytes.begin(), digest_bytes.end());
  return Ok(std::move(*contents));
}

auto PakCatalog::ValidateBaseLayers(
  const std::span<const PakCatalogBase> lower_layers) const
  -> Result<void, std::string>
{
  if (bases.size() > lower_layers.size()) {
    return Err(std::string("PAK patch is missing required base layers"));
  }
  const auto baseline = lower_layers.last(bases.size());
  if (!std::ranges::equal(bases, baseline)) {
    return Err(std::string(
      "PAK patch baseline identity, version, digest or order mismatch; "
      "declared bases must be immediately below the patch"));
  }
  return Result<void, std::string>::Ok();
}

auto PakCatalog::Decode(const std::span<const std::byte> bytes)
  -> Result<PakCatalog, std::string>
{
  if (bytes.size() < kHeaderSize + kDigestSize) {
    return Err(std::string("Truncated PAK catalog"));
  }
  serio::ReadOnlyMemoryStream stream(bytes);
  serio::Reader reader(stream);
  const auto packed = reader.ScopedAlignment(1);
  uint32_t version = 0;
  std::array<uint8_t, SourceKey::kSizeBytes> source_bytes {};
  uint16_t content_version = 0;
  uint32_t entry_count = 0;
  uint32_t deleted_count = 0;
  uint32_t base_count = 0;
  if (!reader.ReadInto(version)
    || !reader.ReadBlobInto(std::as_writable_bytes(std::span(source_bytes)))
    || !reader.ReadInto(content_version) || !reader.ReadInto(entry_count)
    || !reader.ReadInto(deleted_count) || !reader.ReadInto(base_count)) {
    return Err(std::string("Could not decode PAK catalog header"));
  }
  if (version != pak::version::kPakCatalogVersion) {
    return Err(std::string("Unsupported PAK catalog version; recook content"));
  }
  if (ContentSize(entry_count, deleted_count, base_count) + kDigestSize
    != bytes.size()) {
    return Err(std::string("PAK catalog size does not match record counts"));
  }
  const auto source = SourceKey::FromBytes(source_bytes);
  if (!source) {
    return Err(std::string("Invalid PAK catalog source identity"));
  }
  PakCatalog catalog {
    .source_key = *source,
    .content_version = content_version,
    .catalog_digest = {},
    .entries = std::vector<PakCatalogEntry>(entry_count),
    .deleted = std::vector<AssetKey>(deleted_count),
    .bases = std::vector<PakCatalogBase>(base_count),
  };
  for (auto& entry : catalog.entries) {
    std::array<uint8_t, AssetKey::kSizeBytes> key_bytes {};
    uint8_t type = 0;
    if (!reader.ReadBlobInto(std::as_writable_bytes(std::span(key_bytes)))
      || !reader.ReadInto(type)
      || !reader.ReadBlobInto(
        std::as_writable_bytes(std::span(entry.descriptor_digest)))
      || !reader.ReadBlobInto(
        std::as_writable_bytes(std::span(entry.transitive_resource_digest)))) {
      return Err(std::string("Could not decode PAK catalog entry"));
    }
    entry.asset_key = AssetKey::FromBytes(key_bytes);
    entry.asset_type = static_cast<AssetType>(type);
  }
  for (auto& key : catalog.deleted) {
    std::array<uint8_t, AssetKey::kSizeBytes> key_bytes {};
    if (!reader.ReadBlobInto(std::as_writable_bytes(std::span(key_bytes)))) {
      return Err(std::string("Could not decode PAK catalog deletion"));
    }
    key = AssetKey::FromBytes(key_bytes);
  }
  for (auto& base : catalog.bases) {
    if (!reader.ReadBlobInto(std::as_writable_bytes(std::span(source_bytes)))
      || !reader.ReadInto(base.content_version)
      || !reader.ReadBlobInto(
        std::as_writable_bytes(std::span(base.catalog_digest)))) {
      return Err(std::string("Could not decode PAK catalog base"));
    }
    const auto base_key = SourceKey::FromBytes(source_bytes);
    if (!base_key) {
      return Err(std::string("Invalid PAK catalog base identity"));
    }
    base.source_key = *base_key;
  }
  if (!reader.ReadBlobInto(
        std::as_writable_bytes(std::span(catalog.catalog_digest)))) {
    return Err(std::string("Could not decode PAK catalog digest"));
  }
  if (const auto valid = catalog.Validate(); !valid) {
    return Err(valid.error());
  }
  return Ok(std::move(catalog));
}

} // namespace oxygen::data
