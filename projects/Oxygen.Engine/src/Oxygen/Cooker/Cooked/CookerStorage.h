//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::cooked {

//! What a path names.
enum class PathKind : uint8_t {
  kNone = 0,
  kFile,
  kFolder,
};

//! A file being published: a patchable output stream whose bytes replace the
//! destination only when `Commit` succeeds.
/*!
 It satisfies `serio::PatchableOutputStream`, so a writer streams, hashes and
 patches through it as through any Serio stream. Destroying it without a
 commit leaves the destination as it was.
*/
class Publication {
public:
  Publication() = default;
  virtual ~Publication() = default;

  OXYGEN_MAKE_NON_COPYABLE(Publication)
  OXYGEN_MAKE_NON_MOVABLE(Publication)

  [[nodiscard]] virtual auto Write(std::span<const std::byte> in) noexcept
    -> Result<void> = 0;

  [[nodiscard]] virtual auto Flush() noexcept -> Result<void> = 0;

  [[nodiscard]] virtual auto Position() const noexcept -> serio::ByteOffset = 0;

  [[nodiscard]] virtual auto PatchAt(serio::ByteOffset at,
    std::span<const std::byte> in) noexcept -> Result<void> = 0;

  //! Publishes what was written; a failure leaves the destination as it was.
  [[nodiscard]] virtual auto Commit() noexcept -> Result<void> = 0;
};

//! A lock a storage holds on a file for as long as it lives.
class StorageLock {
public:
  StorageLock() = default;
  virtual ~StorageLock() = default;

  OXYGEN_MAKE_NON_COPYABLE(StorageLock)
  OXYGEN_MAKE_NON_MOVABLE(StorageLock)
};

//! Reads the files of cooked sources: what paths name, and files as devices.
/*!
 The part of the Cooker file system that every reader needs, a mount table
 included; it cannot publish.
*/
class CookerStorageReader {
public:
  CookerStorageReader() = default;
  virtual ~CookerStorageReader() = default;

  OXYGEN_MAKE_NON_COPYABLE(CookerStorageReader)
  OXYGEN_MAKE_NON_MOVABLE(CookerStorageReader)

  //! What `path` names.
  [[nodiscard]] virtual auto KindOf(const std::filesystem::path& path) const
    -> PathKind = 0;

  //! Opens the file `path` to read, as a contiguous device, while other
  //! handles may write it (an import's resource data files).
  [[nodiscard]] virtual auto Open(const std::filesystem::path& path) const
    -> Result<serio::AnyDevice> = 0;

  //! Every entry below the folder `path`, recursively, by its path below it
  //! (`content::lc::RootFiles::List`); a link is listed and nothing below it.
  [[nodiscard]] virtual auto List(const std::filesystem::path& path) const
    -> Result<std::vector<content::lc::RootEntry>> = 0;
};

//! Where the Cooker reads cooked sources and publishes paks, manifests and
//! catalogs: the real file system, or memory for tests.
/*!
 Readers open a file once as a Serio device and read regions of it; writers
 publish a whole file, which replaces its destination atomically. A build
 reads what an earlier build published, whatever holds the storage.
*/
class CookerStorage : public virtual CookerStorageReader {
public:
  //! Starts publishing the file `path`; its parent folders are created.
  [[nodiscard]] virtual auto Publish(const std::filesystem::path& path)
    -> Result<std::unique_ptr<Publication>> = 0;

  //! Removes the file `path`; a path that names nothing is a success.
  [[nodiscard]] virtual auto Remove(const std::filesystem::path& path)
    -> Result<void> = 0;

  //! Removes the folder `path` when it holds nothing, and says whether it
  //! did. A folder that holds a file or a folder, a file, and a path that
  //! names nothing stay as they are and are a success (`false`).
  /*!
   A writer that removes a file removes the folders it leaves empty, so a
   later write names its folders anew: on a file system that ignores case, a
   kept folder would keep a name renamed only in case.
  */
  [[nodiscard]] virtual auto RemoveEmptyFolder(
    const std::filesystem::path& path) -> Result<bool> = 0;

  //! Moves the file `from` to `to`, replacing the file `to` names, if any.
  /*!
   One step on one volume (`serio::MoveFileOver`): a reader of `to` finds the
   file it named before or the moved one, never neither. Fails, changing
   nothing, when `from` names no file or the parent folder of `to` does not
   exist (`serio::Error::kNotFound`), or when either names a folder
   (`serio::Error::kIsDirectory`). The
   two paths are siblings in practice: a staged artifact and its destination,
   or a destination and its backup.
  */
  [[nodiscard]] virtual auto Move(const std::filesystem::path& from,
    const std::filesystem::path& to) -> Result<void> = 0;

  //! Locks the file `path` exclusively, waiting for its other holders, in
  //! this process and in others; the file and its parent folders are created
  //! when missing. The lock file stays when the lock is released.
  [[nodiscard]] virtual auto LockExclusive(const std::filesystem::path& path)
    -> Result<std::unique_ptr<StorageLock>> = 0;

  //! The size of the file `path`; nothing when `path` names no file.
  /*!
   Measured without opening the file, so a file another writer holds open,
   such as a resource data file the jobs of a cooked root append to, is
   measured too.
  */
  [[nodiscard]] virtual auto SizeOf(const std::filesystem::path& path) const
    -> std::optional<serio::ByteCount> = 0;
};

//! The storage an import writes a cooked root through: a `CookerStorage` that
//! also awaits its writes.
/*!
 Only the import awaits cooked writes, from its event loop, so only the
 storages an import runs on implement them: the real file system and the
 in-memory one of tests. A pak build or script sealing publishes through
 `CookerStorage` alone.
*/
class ImportStorage : public CookerStorage {
public:
  //! Publishes `bytes` as the whole file `path` (parent folders created),
  //! replacing it atomically; the write is awaited through `driver`.
  /*!
   Buffered durability: the loose index, published stably, holds the digests
   of the files published this way. A cancelled or failed publication leaves
   the destination as it was. `bytes` stays valid until the coroutine
   completes.
  */
  [[nodiscard]] virtual auto PublishAsync(serio::IoDriver& driver,
    std::filesystem::path path, std::span<const std::byte> bytes)
    -> co::Co<Result<void>> = 0;

  //! Opens the data file `path` for writes at reserved offsets: created when
  //! missing (parent folders created), never truncated, sharing reads and
  //! writes, awaitable through `driver`.
  /*!
   The device writes the file that `Open`, `SizeOf` and `KindOf` see, so a
   reader finds the bytes written through it once the awaited write
   completes. It must not outlive `driver`.
  */
  [[nodiscard]] virtual auto OpenData(serio::IoDriver& driver,
    const std::filesystem::path& path) -> Result<serio::AnyAsyncDevice> = 0;
};

//! The real file system: files are mapped to read and published through
//! `serio::AtomicFileWriter`.
OXGN_COOK_NDAPI auto RealCookerStorage() -> ImportStorage&;

//! The most bytes the Cooker reads whole from a cooked metadata file: an
//! index, a descriptor, a resource table or a pak catalog.
inline constexpr serio::ByteCount kMaxMetadataBytes {
  uint64_t { 64 } * 1024 * 1024,
};

//! The most bytes the Cooker reads whole from a cooked resource data file.
inline constexpr serio::ByteCount kMaxDataFileBytes {
  static_cast<uint64_t>(std::numeric_limits<int32_t>::max()),
};

//! The whole file `path`, read through `storage`.
/*!
 A file larger than `limit` is `serio::Error::kTooLarge`, refused before
 anything is allocated.
*/
OXGN_COOK_NDAPI auto ReadWholeFile(const CookerStorageReader& storage,
  const std::filesystem::path& path, serio::ByteCount limit)
  -> Result<std::vector<std::byte>>;

//! Publishes `bytes` as the file `path` through `storage`.
OXGN_COOK_NDAPI auto PublishWholeFile(CookerStorage& storage,
  const std::filesystem::path& path, std::span<const std::byte> bytes)
  -> Result<void>;

} // namespace oxygen::content::cooked
