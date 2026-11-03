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

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/CookedFormatError.h> // IWYU pragma: export
#include <Oxygen/Data/LooseCookedIndexEncoder.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/AnyDevice.h>

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
  //! A file no commit lists that a writer produces (a content-named file or
  //! an atomic publication's sibling): a running session's, or one a crashed
  //! session left for the next writer to delete. Mounting accepts it.
  kUncommitted,
  //! A data file longer than the length its commit records: bytes a session
  //! appended and has not committed. Mounting accepts it.
  kUncommittedTail,
};

//! Whether a mount accepts a root with this issue: a writer's work in
//! progress, or what a crashed writer left for the next one, never damage.
[[nodiscard]] constexpr auto IsUncommitted(
  const IntegrityFailure reason) noexcept -> bool
{
  return reason == IntegrityFailure::kUncommitted
    || reason == IntegrityFailure::kUncommittedTail;
}

//! A requirement of one asset of the index on another mount.
struct AssetRequirement final {
  data::AssetKey asset;
  data::loose_cooked::IndexedRequirement requirement;

  auto operator==(const AssetRequirement&) const -> bool = default;
};

struct FileIntegrityIssue final {
  std::string relative_path;
  IntegrityFailure reason = IntegrityFailure::kMissing;
  //! The table row of the resource a `kResource*` failure is about.
  std::optional<uint32_t> resource_row;
  //! What makes the rows of a `kMalformedTable` unreadable.
  std::optional<data::CookedDefect> table_defect;

  auto operator==(const FileIntegrityIssue&) const -> bool = default;
};

//! What one entry below a loose cooked root is.
enum class RootEntryKind : uint8_t {
  kFile,
  kFolder,
  //! A symbolic link, junction or other reparse point; it is not followed.
  kLink,
  //! Anything else, such as a device or a pipe.
  kOther,
};

//! One entry below a loose cooked root.
struct RootEntry final {
  //! Its path below the root, with `/` separators.
  std::string relative_path;
  RootEntryKind kind = RootEntryKind::kFile;
  //! The size of a `kFile`; 0 for every other kind.
  uint64_t size = 0;

  auto operator==(const RootEntry&) const -> bool = default;
};

//! The files of one loose cooked root, as an integrity check reads them: the
//! real file system for the engine, the cooker's storage for the cooker.
class RootFiles {
public:
  RootFiles() = default;
  virtual ~RootFiles() = default;

  OXYGEN_MAKE_NON_COPYABLE(RootFiles)
  OXYGEN_DEFAULT_MOVABLE(RootFiles)

  //! Every entry below the root, recursively; a link is listed and nothing
  //! below it.
  /*!
   @throw std::runtime_error When the root cannot be listed.
  */
  [[nodiscard]] virtual auto List() const -> std::vector<RootEntry> = 0;

  //! Opens the file `relative_path` below the root to read.
  [[nodiscard]] virtual auto Open(std::string_view relative_path) const
    -> Result<serio::AnyDevice> = 0;
};

//! Every entry below the folder `root` of the real file system, as
//! `RootFiles::List` lists it: a symbolic link, or a path whose canonical form
//! leaves it such as a junction, is a `kLink`.
/*!
 @throw std::filesystem::filesystem_error When `root` cannot be listed.
*/
OXGN_CNTT_NDAPI auto ListFolder(const std::filesystem::path& root)
  -> std::vector<RootEntry>;

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

  //! The commit the index is, from 1.
  OXGN_CNTT_NDAPI auto Commit() const noexcept -> uint64_t;

  //! The files earlier commits listed and this one does not, by path.
  OXGN_CNTT_NDAPI auto RetiredFiles() const noexcept
    -> std::span<const data::loose_cooked::RetiredFile>;

  //! What the assets require of other mounts, in asset order.
  OXGN_CNTT_NDAPI auto Requirements() const noexcept
    -> std::span<const AssetRequirement>;

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

  //! Inspect every member of the root `files` reads, as the path overload.
  OXGN_CNTT_NDAPI auto CheckContent(const RootFiles& files,
    IntegrityCheck check) const -> std::vector<FileIntegrityIssue>;

  //! Validate membership, sizes and that each resource lies in its data
  //! file; kFull additionally verifies every digest: the digest of each file,
  //! and of each resource in a resource data file against its table row.
  /*!
   A writer's uncommitted files and data tails (`IsUncommitted`) are no
   damage, so a root a writer is cooking, or one a crash left, validates.

   @throw IntegrityError with the first other issue `CheckContent` finds.
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
