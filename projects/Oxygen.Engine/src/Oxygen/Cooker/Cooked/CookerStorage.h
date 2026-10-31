//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Serio/AnyDevice.h>
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

  //! Opens the file `path` to read, as a contiguous device.
  [[nodiscard]] virtual auto Open(const std::filesystem::path& path) const
    -> Result<serio::AnyDevice> = 0;
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
   One step on one volume: a reader of `to` finds the file it named before or
   the moved one, never neither. Fails, changing nothing, when `from` names no
   file (`no_such_file_or_directory`), when `to` names a folder
   (`is_a_directory`), or when the parent folder of `to` does not exist. The
   two paths are siblings in practice: a staged artifact and its destination,
   or a destination and its backup.
  */
  [[nodiscard]] virtual auto Move(const std::filesystem::path& from,
    const std::filesystem::path& to) -> Result<void> = 0;

  //! The size of the file `path`; nothing when `path` names no file.
  /*!
   Measured without opening the file, so a file another writer holds open,
   such as a resource data file the jobs of a cooked root append to, is
   measured too.
  */
  [[nodiscard]] virtual auto SizeOf(const std::filesystem::path& path) const
    -> std::optional<serio::ByteCount> = 0;
};

//! The real file system: files are mapped to read and published through
//! `serio::AtomicFileWriter`.
OXGN_COOK_NDAPI auto RealCookerStorage() -> CookerStorage&;

//! The whole file `path`, read through `storage`.
OXGN_COOK_NDAPI auto ReadWholeFile(const CookerStorageReader& storage,
  const std::filesystem::path& path) -> Result<std::vector<std::byte>>;

//! Publishes `bytes` as the file `path` through `storage`.
OXGN_COOK_NDAPI auto PublishWholeFile(CookerStorage& storage,
  const std::filesystem::path& path, std::span<const std::byte> bytes)
  -> Result<void>;

} // namespace oxygen::content::cooked
