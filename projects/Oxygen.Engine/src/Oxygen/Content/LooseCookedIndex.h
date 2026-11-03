//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/CookedFormatError.h> // IWYU pragma: export
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::internal {
class LooseCookedIndexImpl;
}

namespace oxygen::content::lc {

enum class IntegrityCheck : uint8_t { kMetadata, kFull };

//! One indexed descriptor or resource file. The index itself is selected
//! externally.
struct FileIntegrity final {
  std::string relative_path;
  uint64_t size = 0;
  base::Sha256Digest sha256 {};
  std::optional<data::loose_cooked::FileKind> kind;
  //! Virtual path of a texture descriptor; empty for every other file.
  std::string virtual_path;
  //! Mount path of the source that produced a texture descriptor or an
  //! auxiliary file; empty for every other file.
  std::string source_path;
};

enum class IntegrityFailure : uint8_t {
  kMissing,
  kUnexpected,
  kSizeMismatch,
  kDigestMismatch,
  kLinkedPath,
  kNotRegular,
  //! A resource table whose rows cannot be read.
  kMalformedTable,
  //! A resource whose table row places it outside its data file.
  kResourceOutOfBounds,
  //! A resource whose bytes do not hash to its table row's SHA-256.
  kResourceDigestMismatch,
};

struct FileIntegrityIssue final {
  std::string relative_path;
  IntegrityFailure reason = IntegrityFailure::kMissing;
  //! The table row of the resource a `kResource*` failure is about.
  std::optional<uint32_t> resource_row;

  auto operator==(const FileIntegrityIssue&) const -> bool = default;
};

//! Loose cooked content that fails `LooseCookedIndex::ValidateContent`, and
//! its first issue.
class IntegrityError final : public std::runtime_error {
public:
  IntegrityError(const FileIntegrityIssue& issue, const std::string& message)
    : std::runtime_error(message)
    , reason_(issue.reason)
    , resource_row_(issue.resource_row)
  {
  }

  [[nodiscard]] auto Reason() const noexcept -> IntegrityFailure
  {
    return reason_;
  }

  //! The table row of the resource a `kResource*` failure is about.
  [[nodiscard]] auto ResourceRow() const noexcept -> std::optional<uint32_t>
  {
    return resource_row_;
  }

private:
  IntegrityFailure reason_;
  std::optional<uint32_t> resource_row_;
};

class LooseCookedIndex final {
public:
  using FileKind = data::loose_cooked::FileKind;

  LooseCookedIndex() = delete;
  OXGN_CNTT_API ~LooseCookedIndex();

  OXGN_CNTT_API LooseCookedIndex(LooseCookedIndex&&) noexcept;
  OXGN_CNTT_API auto operator=(LooseCookedIndex&&) noexcept
    -> LooseCookedIndex&;

  LooseCookedIndex(const LooseCookedIndex&) = delete;
  auto operator=(const LooseCookedIndex&) -> LooseCookedIndex& = delete;

  OXGN_CNTT_API static auto LoadFromRoot(
    const std::filesystem::path& cooked_root) -> LooseCookedIndex;
  OXGN_CNTT_API static auto LoadFromFile(
    const std::filesystem::path& index_path) -> LooseCookedIndex;
  //! Loads the bytes of an index file, which the index copies.
  /*!
   @throw data::CookedFormatError `kWrongFormat` for bytes that are not an
     index, `kUnsupportedVersion` for another index version, `kCorrupt` for
     a malformed index. `Defect()` names the rule the bytes break.
  */
  OXGN_CNTT_API static auto LoadFromBytes(std::span<const std::byte> bytes)
    -> LooseCookedIndex;
  //! Loads the bytes of an index file, which the index takes and parses in
  //! place.
  OXGN_CNTT_API static auto LoadFromBytes(std::vector<std::byte>&& bytes)
    -> LooseCookedIndex;

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
  //! Virtual path of the source that produced the asset.
  OXGN_CNTT_NDAPI auto FindSourcePath(const data::AssetKey& key) const noexcept
    -> std::optional<std::string_view>;
  //! Decode one asset's inventory without loading descriptor/resource payloads.
  OXGN_CNTT_NDAPI auto FindAssetReferences(const data::AssetKey& key) const
    -> std::optional<data::AssetReferences>;
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

  //! Snapshot of all content members, sorted by canonical relative path.
  OXGN_CNTT_NDAPI auto GetFileInventory() const -> std::vector<FileIntegrity>;

  //! Inspect every member, retaining per-file failures for incremental repair.
  OXGN_CNTT_NDAPI auto CheckContent(const std::filesystem::path& cooked_root,
    IntegrityCheck check) const -> std::vector<FileIntegrityIssue>;

  //! Validate membership, sizes and that each resource lies in its data
  //! file; kFull additionally verifies every digest: the digest of each file,
  //! and of each resource in a resource data file against its table row.
  /*!
   @throw IntegrityError with the first issue `CheckContent` finds.
  */
  //! The caller protects files for the duration and any subsequent result
  //! reuse.
  OXGN_CNTT_API auto ValidateContent(const std::filesystem::path& cooked_root,
    IntegrityCheck check) const -> void;

  OXGN_CNTT_NDAPI auto FindFileSha256(FileKind kind) const noexcept
    -> std::optional<std::span<const uint8_t, data::loose_cooked::kSha256Size>>;

private:
  explicit LooseCookedIndex(
    std::unique_ptr<internal::LooseCookedIndexImpl> impl);

  std::unique_ptr<internal::LooseCookedIndexImpl> impl_;
};

} // namespace oxygen::content::lc
