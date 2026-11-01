//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <system_error>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/RegionStream.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Serio/api_export.h>

namespace oxygen::serio {

class IoDriver;

//! A successful replacement. A durability error means the name moved, but
//! making the move stable (flushing it, synchronizing the parent directory)
//! failed afterward.
struct FileReplacement {
  std::error_code durability_error {};
};

//! Moves the file `from` over `to` in one step, replacing the file `to` names,
//! if any.
/*!
 Readers that hold `to` open with shared deletion keep the file they opened;
 the name moves to the new file without waiting for them (POSIX semantics on
 Windows, a replacing move on a file system without them, `rename(2)`
 elsewhere). Both paths are on one volume. With `kStable`, the move is made
 durable before the call returns.

 A missing `from` fails with `kNotFound`, a folder at `from` or `to` with
 `kIsDirectory`, classified after the move failed; POSIX, whose rename moves
 folders, refuses a folder source before the move. A stable move between
 folders flushes both.

 A read-only `to` is replaced as `rename(2)` replaces it, when the caller may
 change its attributes (`kAccessDenied` otherwise). On a file system without
 POSIX renames, the replacing move fails while any reader holds `to`, even
 one that shares deletion, and fails on a read-only `to`: nothing clears the
 attribute in place. The caller retries or reports it.
*/
OXGN_SERIO_NDAPI auto MoveFileOver(const std::filesystem::path& from,
  const std::filesystem::path& to,
  Durability durability = Durability::kStable) noexcept
  -> Result<FileReplacement>;

//! Streams a file's content into a unique sibling, then atomically replaces
//! the destination with it.
/*!
 The parent directory must exist. `Commit()` flushes the sibling to the
 durability asked, replaces the destination while readers hold it (as
 `MoveFileOver` describes) and, for `kStable`, makes the replacement durable. A
 writer destroyed without `Commit()` removes its sibling, and the destination
 is untouched.

 `kBuffered` leaves the bytes to the OS cache and replaces at once: for a file
 whose content a stable record elsewhere validates, such as a cooked
 descriptor whose digest the loose cooked index holds.

 The sibling is `.oxygen-<uuid>.tmp` in the destination's folder. A process
 that dies before `Commit()` leaves it behind; nothing removes it later.

 ```cpp
 auto writer = AtomicFileWriter::Create(path);
 CHECK_RESULT(Writer(writer->Stream()).Write(index));
 auto commit = std::move(*writer).Commit();
 ```
*/
class AtomicFileWriter {
public:
  //! Creates the sibling to stream into; the destination need not exist.
  OXGN_SERIO_NDAPI static auto Create(const std::filesystem::path& path,
    Durability durability = Durability::kStable) -> Result<AtomicFileWriter>;

  OXGN_SERIO_API ~AtomicFileWriter() noexcept;

  OXYGEN_MAKE_NON_COPYABLE(AtomicFileWriter)
  OXGN_SERIO_API AtomicFileWriter(AtomicFileWriter&& other) noexcept;
  OXGN_SERIO_API auto operator=(AtomicFileWriter&& other) noexcept
    -> AtomicFileWriter&;

  //! The buffered, patchable stream of the file's content.
  OXGN_SERIO_NDAPI auto Stream() noexcept -> RegionWriteStream<NativeFile>&;
  OXGN_SERIO_NDAPI auto Stream() const noexcept
    -> const RegionWriteStream<NativeFile>&;

  //! Publishes what was written; the writer is spent whether it succeeds or
  //! not, and a failure leaves the destination as it was.
  OXGN_SERIO_NDAPI auto Commit() && -> Result<FileReplacement>;

private:
  struct State;
  explicit AtomicFileWriter(std::unique_ptr<State> state) noexcept;

  std::unique_ptr<State> state_;
};

//! Writes and flushes a unique sibling file before atomically replacing the
//! destination, through an `AtomicFileWriter`. The parent directory must
//! exist. Existing destination access permissions are preserved: the POSIX
//! mode, or the Windows DACL with the read-only and hidden attributes. Callers
//! own locking and expected-revision checks.
/*!
 The replacement succeeds while readers hold the destination open, as long as
 they share it for deletion, as `ReadPublishedFile` does: they keep the bytes
 they opened. On Windows, a reader that does not share deletion makes the
 replacement fail with access denied.
*/
OXGN_SERIO_NDAPI auto WriteFileAtomically(const std::filesystem::path& path,
  std::span<const std::byte> bytes, Durability durability = Durability::kStable)
  -> Result<FileReplacement>;

//! `WriteFileAtomically`, writing the sibling through `driver`.
/*!
 The sibling is opened through `driver` and written with one awaited write;
 its flush, the permission copy and the replacement are the blocking calls
 `WriteFileAtomically` makes, each one system call. `bytes` stays valid until
 the coroutine completes.
*/
OXGN_SERIO_NDAPI auto AsyncWriteFileAtomically(IoDriver& driver,
  std::filesystem::path path, std::span<const std::byte> bytes,
  Durability durability = Durability::kStable)
  -> co::Co<Result<FileReplacement>>;

//! Opens a file that `WriteFileAtomically` publishes, to read what it needs.
/*!
 The file is opened sharing reads, writes and deletion, so a publication
 while it is open replaces the file instead of failing; reads return the
 bytes of the file it opened.
*/
OXGN_SERIO_NDAPI auto OpenPublishedFile(const std::filesystem::path& path)
  -> Result<NativeFile>;

//! Reads a whole file that `WriteFileAtomically` publishes
//! (`OpenPublishedFile`); `kTooLarge`, before allocating, when it holds more
//! than `limit` bytes.
OXGN_SERIO_NDAPI auto ReadPublishedFile(
  const std::filesystem::path& path, ByteCount limit) -> Result<ByteBuffer>;

} // namespace oxygen::serio
