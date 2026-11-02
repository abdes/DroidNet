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
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/MountName.h>
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
    + sizeof(uint16_t) + (4U * sizeof(uint32_t));
  constexpr uint64_t kEntrySize
    = AssetKey::kSizeBytes + sizeof(uint8_t) + (2U * kDigestSize);
  constexpr uint64_t kBaseSize
    = SourceKey::kSizeBytes + sizeof(uint16_t) + kDigestSize;
  constexpr uint64_t kRequirementSize
    = sizeof(uint8_t) + AssetKey::kSizeBytes + sizeof(uint8_t);
  //! A mount name is stored with a one-byte size.
  constexpr size_t kMaxMountSize = std::numeric_limits<uint8_t>::max();

  //! A catalog names only mounts a virtual path can name.
  auto IsValidMount(const std::string_view mount) noexcept -> bool
  {
    const auto spelled_as_a_mount = IsMountNameSpelling(mount);
    const auto fits_its_field = mount.size() <= kMaxMountSize;
    return spelled_as_a_mount && fits_its_field;
  }

  auto IsValidRequirement(const PakRequirement& requirement) noexcept -> bool
  {
    if (requirement.key.IsNil()) {
      return false;
    }
    switch (requirement.kind) {
    case KeyReferenceKind::kAsset:
      return requirement.type > AssetType::kUnknown
        && requirement.type <= AssetType::kMaxAssetType;
    case KeyReferenceKind::kTexture:
      return requirement.type == AssetType::kUnknown;
    case KeyReferenceKind::kPhysicsResource:
    case KeyReferenceKind::kLogical:
      return false;
    }
    return false;
  }

  auto RequirementOrder(const PakRequirement& requirement)
    -> std::pair<KeyReferenceKind, AssetKey>
  {
    return { requirement.kind, requirement.key };
  }

  auto ContentSize(const PakCatalog& catalog) noexcept -> uint64_t
  {
    auto size = kHeaderSize + sizeof(uint8_t) + catalog.mount.size()
      + (uint64_t { catalog.entries.size() } * kEntrySize)
      + (uint64_t { catalog.deleted.size() } * AssetKey::kSizeBytes)
      + (uint64_t { catalog.bases.size() } * kBaseSize);
    for (const auto& dependency : catalog.dependencies) {
      size += sizeof(uint8_t) + dependency.mount.size() + sizeof(uint32_t)
        + (uint64_t { dependency.requirements.size() } * kRequirementSize);
    }
    return size;
  }

  auto Refusal(const CookedDefect defect, const std::string& message)
    -> Result<void, CookedOpenError>
  {
    return Err(CookedOpenError(defect, message));
  }

  //! Why `catalog` breaks a rule of the format, or nothing when it does not.
  auto ValidateStructure(const PakCatalog& catalog)
    -> Result<void, CookedOpenError>
  {
    constexpr auto kMaxCount = std::numeric_limits<uint32_t>::max();
    if (catalog.source_key.IsNil()) {
      return Refusal(
        CookedDefect::kInvalidIdentity, "Invalid PAK catalog identity");
    }
    const auto counts_fit = catalog.entries.size() <= kMaxCount
      && catalog.deleted.size() <= kMaxCount
      && catalog.bases.size() <= kMaxCount
      && catalog.dependencies.size() <= kMaxCount;
    if (!counts_fit) {
      return Refusal(
        CookedDefect::kSizeMismatch, "PAK catalog holds too many records");
    }
    if (!IsValidMount(catalog.mount)) {
      return Refusal(
        CookedDefect::kInvalidPath, "Invalid PAK catalog mount name");
    }
    std::unordered_set<AssetKey> keys;
    for (const auto& entry : catalog.entries) {
      const auto names_an_asset = !entry.asset_key.IsNil()
        && entry.asset_type > AssetType::kUnknown
        && entry.asset_type <= AssetType::kMaxAssetType;
      if (!names_an_asset) {
        return Refusal(
          CookedDefect::kInvalidValue, "Invalid PAK catalog entry");
      }
      if (!keys.insert(entry.asset_key).second) {
        return Refusal(CookedDefect::kDuplicate, "Duplicate PAK catalog entry");
      }
    }
    for (const auto& key : catalog.deleted) {
      if (key.IsNil()) {
        return Refusal(CookedDefect::kInvalidValue, "Invalid PAK deletion");
      }
      if (!keys.insert(key).second) {
        return Refusal(
          CookedDefect::kDuplicate, "Duplicate or conflicting PAK deletion");
      }
    }
    std::unordered_set<SourceKey> bases;
    for (const auto& base : catalog.bases) {
      if (base.source_key.IsNil()) {
        return Refusal(CookedDefect::kInvalidIdentity, "Invalid PAK base");
      }
      if (base.source_key == catalog.source_key) {
        return Refusal(CookedDefect::kInconsistent, "PAK is its own base");
      }
      if (!bases.insert(base.source_key).second) {
        return Refusal(CookedDefect::kDuplicate, "Repeated PAK base");
      }
    }
    // A mount is required once, never the pak's own, and for something.
    std::unordered_set<std::string_view> mounts;
    for (const auto& dependency : catalog.dependencies) {
      if (!IsValidMount(dependency.mount)) {
        return Refusal(
          CookedDefect::kInvalidPath, "Invalid PAK dependency mount name");
      }
      if (dependency.mount == catalog.mount) {
        return Refusal(CookedDefect::kInconsistent, "PAK depends on its mount");
      }
      if (!mounts.insert(dependency.mount).second) {
        return Refusal(CookedDefect::kDuplicate, "Repeated PAK dependency");
      }
      if (dependency.requirements.empty()) {
        return Refusal(
          CookedDefect::kMissingPart, "PAK dependency requires nothing");
      }
      if (dependency.requirements.size() > kMaxCount) {
        return Refusal(
          CookedDefect::kSizeMismatch, "PAK dependency holds too many records");
      }
      auto required = std::set<std::pair<KeyReferenceKind, AssetKey>> {};
      for (const auto& requirement : dependency.requirements) {
        if (!IsValidRequirement(requirement)) {
          return Refusal(
            CookedDefect::kInvalidValue, "Invalid PAK requirement");
        }
        if (!required.insert(RequirementOrder(requirement)).second) {
          return Refusal(CookedDefect::kDuplicate, "Repeated PAK requirement");
        }
      }
    }
    return Result<void, CookedOpenError>::Ok();
  }

  //! Whether every record whose order is not semantic is in the order the
  //! encoder writes it: entries, deletions and dependencies by key or mount,
  //! each dependency's requirements by kind and key.
  auto IsCanonical(const PakCatalog& catalog) -> bool
  {
    return std::ranges::is_sorted(
             catalog.entries, {}, &PakCatalogEntry::asset_key)
      && std::ranges::is_sorted(catalog.deleted)
      && std::ranges::is_sorted(
        catalog.dependencies, {}, &PakCatalogDependency::mount)
      && std::ranges::all_of(catalog.dependencies,
        [](const PakCatalogDependency& dependency) -> bool {
          return std::ranges::is_sorted(
            dependency.requirements, {}, RequirementOrder);
        });
  }

  auto WriteMount(serio::Writer<serio::MemoryStream>& writer,
    const std::string_view mount) -> bool
  {
    return writer.Write(static_cast<uint8_t>(mount.size()))
      && writer.WriteBlob(std::as_bytes(std::span(mount)));
  }

  auto EncodeContents(const PakCatalog& catalog)
    -> Result<std::vector<std::byte>, std::string>
  {
    if (const auto valid = ValidateStructure(catalog); !valid) {
      return Err(std::string(valid.error().what()));
    }
    const auto size = ContentSize(catalog);
    if (size > std::numeric_limits<size_t>::max()) {
      return Err(std::string("PAK catalog exceeds addressable size"));
    }
    auto entries = catalog.entries;
    auto deleted = catalog.deleted;
    auto dependencies = catalog.dependencies;
    std::ranges::sort(entries, {}, &PakCatalogEntry::asset_key);
    std::ranges::sort(deleted);
    std::ranges::sort(dependencies, {}, &PakCatalogDependency::mount);
    for (auto& dependency : dependencies) {
      std::ranges::sort(dependency.requirements, {}, RequirementOrder);
    }
    serio::MemoryStream stream;
    serio::Writer writer(stream);
    const auto packed = writer.ScopedAlignment(1);
    if (!writer.Write(pak::version::kPakCatalogVersion)
      || !writer.WriteBlob(as_bytes(catalog.source_key))
      || !writer.Write(catalog.content_version)
      || !writer.Write(static_cast<uint32_t>(entries.size()))
      || !writer.Write(static_cast<uint32_t>(deleted.size()))
      || !writer.Write(static_cast<uint32_t>(catalog.bases.size()))
      || !writer.Write(static_cast<uint32_t>(dependencies.size()))
      || !WriteMount(writer, catalog.mount)) {
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
    // Base order is semantic precedence, unlike every other record's order.
    for (const auto& base : catalog.bases) {
      if (!writer.WriteBlob(as_bytes(base.source_key))
        || !writer.Write(base.content_version)
        || !writer.WriteBlob(std::as_bytes(std::span(base.catalog_digest)))) {
        return Err(std::string("Could not encode PAK catalog base"));
      }
    }
    for (const auto& dependency : dependencies) {
      if (!WriteMount(writer, dependency.mount)
        || !writer.Write(
          static_cast<uint32_t>(dependency.requirements.size()))) {
        return Err(std::string("Could not encode PAK catalog dependency"));
      }
      for (const auto& requirement : dependency.requirements) {
        if (!writer.Write(static_cast<uint8_t>(requirement.kind))
          || !writer.WriteBlob(as_bytes(requirement.key))
          || !writer.Write(static_cast<uint8_t>(requirement.type))) {
          return Err(std::string("Could not encode PAK catalog requirement"));
        }
      }
    }
    const auto written = stream.Data();
    return Ok(std::vector<std::byte>(written.begin(), written.end()));
  }

  auto Corrupt(const CookedDefect defect, const std::string& message)
    -> CookedOpenError
  {
    return { defect, message };
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
  -> Result<PakCatalog, CookedOpenError>
{
  if (bytes.size() < kHeaderSize + sizeof(uint8_t) + kDigestSize) {
    return Err(Corrupt(CookedDefect::kTruncated, "Truncated PAK catalog"));
  }
  serio::ReadOnlyMemoryStream stream(bytes);
  serio::Reader reader(stream);
  const auto packed = reader.ScopedAlignment(1);
  // The bytes left before the digest, so that no record count allocates more
  // records than the catalog can hold.
  const auto remaining = [&stream, &bytes] -> uint64_t {
    const auto position = stream.Position().get();
    return position + kDigestSize <= bytes.size()
      ? bytes.size() - kDigestSize - position
      : 0U;
  };
  const auto read_mount = [&reader](std::string& mount) -> bool {
    uint8_t size = 0;
    if (!reader.ReadInto(size)) {
      return false;
    }
    mount.resize(size);
    return reader.ReadBlobInto(std::as_writable_bytes(std::span(mount)))
      .has_value();
  };
  uint32_t version = 0;
  if (!reader.ReadInto(version)) {
    return Err(
      Corrupt(CookedDefect::kTruncated, "Could not decode PAK catalog header"));
  }
  if (version != pak::version::kPakCatalogVersion) {
    return Err(CookedOpenError(CookedDefect::kUnsupportedVersion,
      "Unsupported PAK catalog version; recook content"));
  }
  std::array<uint8_t, SourceKey::kSizeBytes> source_bytes {};
  uint16_t content_version = 0;
  uint32_t entry_count = 0;
  uint32_t deleted_count = 0;
  uint32_t base_count = 0;
  uint32_t dependency_count = 0;
  auto mount = std::string {};
  if (!reader.ReadBlobInto(std::as_writable_bytes(std::span(source_bytes)))
    || !reader.ReadInto(content_version) || !reader.ReadInto(entry_count)
    || !reader.ReadInto(deleted_count) || !reader.ReadInto(base_count)
    || !reader.ReadInto(dependency_count) || !read_mount(mount)) {
    return Err(
      Corrupt(CookedDefect::kTruncated, "Could not decode PAK catalog header"));
  }
  if ((uint64_t { entry_count } * kEntrySize)
      + (uint64_t { deleted_count } * AssetKey::kSizeBytes)
      + (uint64_t { base_count } * kBaseSize)
      + (uint64_t { dependency_count } * (sizeof(uint8_t) + sizeof(uint32_t)))
    > remaining()) {
    return Err(Corrupt(CookedDefect::kSizeMismatch,
      "PAK catalog size does not match record counts"));
  }
  const auto source = SourceKey::FromBytes(source_bytes);
  if (!source) {
    return Err(Corrupt(
      CookedDefect::kInvalidIdentity, "Invalid PAK catalog source identity"));
  }
  PakCatalog catalog {
    .source_key = *source,
    .content_version = content_version,
    .catalog_digest = {},
    .mount = std::move(mount),
    .entries = std::vector<PakCatalogEntry>(entry_count),
    .deleted = std::vector<AssetKey>(deleted_count),
    .bases = std::vector<PakCatalogBase>(base_count),
    .dependencies = std::vector<PakCatalogDependency>(dependency_count),
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
      return Err(Corrupt(
        CookedDefect::kTruncated, "Could not decode PAK catalog entry"));
    }
    entry.asset_key = AssetKey::FromBytes(key_bytes);
    entry.asset_type = static_cast<AssetType>(type);
  }
  for (auto& key : catalog.deleted) {
    std::array<uint8_t, AssetKey::kSizeBytes> key_bytes {};
    if (!reader.ReadBlobInto(std::as_writable_bytes(std::span(key_bytes)))) {
      return Err(Corrupt(
        CookedDefect::kTruncated, "Could not decode PAK catalog deletion"));
    }
    key = AssetKey::FromBytes(key_bytes);
  }
  for (auto& base : catalog.bases) {
    if (!reader.ReadBlobInto(std::as_writable_bytes(std::span(source_bytes)))
      || !reader.ReadInto(base.content_version)
      || !reader.ReadBlobInto(
        std::as_writable_bytes(std::span(base.catalog_digest)))) {
      return Err(
        Corrupt(CookedDefect::kTruncated, "Could not decode PAK catalog base"));
    }
    const auto base_key = SourceKey::FromBytes(source_bytes);
    if (!base_key) {
      return Err(Corrupt(
        CookedDefect::kInvalidIdentity, "Invalid PAK catalog base identity"));
    }
    base.source_key = *base_key;
  }
  for (auto& dependency : catalog.dependencies) {
    uint32_t requirement_count = 0;
    if (!read_mount(dependency.mount) || !reader.ReadInto(requirement_count)
      || uint64_t { requirement_count } * kRequirementSize > remaining()) {
      return Err(Corrupt(
        CookedDefect::kTruncated, "Could not decode PAK catalog dependency"));
    }
    dependency.requirements.resize(requirement_count);
    for (auto& requirement : dependency.requirements) {
      uint8_t kind = 0;
      std::array<uint8_t, AssetKey::kSizeBytes> key_bytes {};
      uint8_t type = 0;
      if (!reader.ReadInto(kind)
        || !reader.ReadBlobInto(std::as_writable_bytes(std::span(key_bytes)))
        || !reader.ReadInto(type)) {
        return Err(Corrupt(CookedDefect::kTruncated,
          "Could not decode PAK catalog requirement"));
      }
      requirement = {
        .kind = static_cast<KeyReferenceKind>(kind),
        .key = AssetKey::FromBytes(key_bytes),
        .type = static_cast<AssetType>(type),
      };
    }
  }
  if (remaining() != 0U) {
    return Err(Corrupt(CookedDefect::kSizeMismatch,
      "PAK catalog size does not match record counts"));
  }
  if (!reader.ReadBlobInto(
        std::as_writable_bytes(std::span(catalog.catalog_digest)))) {
    return Err(
      Corrupt(CookedDefect::kTruncated, "Could not decode PAK catalog digest"));
  }
  // The digest authenticates the bytes as received; canonical order makes
  // them the only encoding of the catalog they hold.
  if (base::ComputeSha256(bytes.first(bytes.size() - kDigestSize))
    != catalog.catalog_digest) {
    return Err(
      Corrupt(CookedDefect::kDigestMismatch, "PAK catalog digest mismatch"));
  }
  if (auto valid = ValidateStructure(catalog); !valid) {
    return Err(std::move(valid.error()));
  }
  if (!IsCanonical(catalog)) {
    return Err(Corrupt(CookedDefect::kNotCanonical,
      "PAK catalog records are not in canonical order"));
  }
  return Ok(std::move(catalog));
}

} // namespace oxygen::data
