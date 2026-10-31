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
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/RegionStream.h>
#include <Oxygen/Serio/api_export.h>

namespace oxygen::serio {

//! A successful atomic replacement. A durability error means the replacement
//! committed, but synchronizing its parent directory failed afterward.
struct AtomicFileCommit {
  std::error_code durability_error {};
};

//! Streams a file's content into a unique sibling, then atomically replaces
//! the destination with it.
/*!
 The parent directory must exist. `Commit()` flushes the sibling to stable
 storage, replaces the destination while readers hold it (as
 `WriteFileAtomically` describes) and synchronizes the parent directory. A
 writer destroyed without `Commit()` removes its sibling, and the destination
 is untouched.

 ```cpp
 auto writer = AtomicFileWriter::Create(path);
 CHECK_RESULT(Writer(writer->Stream()).Write(index));
 auto commit = std::move(*writer).Commit();
 ```
*/
class AtomicFileWriter {
public:
  //! Creates the sibling to stream into; the destination need not exist.
  OXGN_SERIO_NDAPI static auto Create(const std::filesystem::path& path)
    -> Result<AtomicFileWriter>;

  OXGN_SERIO_API ~AtomicFileWriter() noexcept;

  OXYGEN_MAKE_NON_COPYABLE(AtomicFileWriter)
  OXGN_SERIO_API AtomicFileWriter(AtomicFileWriter&& other) noexcept;
  OXGN_SERIO_API auto operator=(AtomicFileWriter&& other) noexcept
    -> AtomicFileWriter&;

  //! The buffered, patchable stream of the file's content.
  OXGN_SERIO_NDAPI auto Stream() noexcept -> RegionWriteStream<NativeFile>&;

  //! Publishes what was written; the writer is spent whether it succeeds or
  //! not, and a failure leaves the destination as it was.
  OXGN_SERIO_NDAPI auto Commit() && -> Result<AtomicFileCommit>;

private:
  struct State;
  explicit AtomicFileWriter(std::unique_ptr<State> state) noexcept;

  std::unique_ptr<State> state_;
};

//! Writes and flushes a unique sibling file before atomically replacing the
//! destination, through an `AtomicFileWriter`. The parent directory must
//! exist. Existing destination access permissions are preserved. Callers own
//! locking and expected-revision checks.
/*!
 The replacement succeeds while readers hold the destination open, as long as
 they share it for deletion, as `ReadPublishedFile` does: they keep the bytes
 they opened. On Windows, a reader that does not share deletion makes the
 replacement fail with access denied.
*/
OXGN_SERIO_NDAPI auto WriteFileAtomically(const std::filesystem::path& path,
  std::span<const std::byte> bytes) -> Result<AtomicFileCommit>;

//! Opens a file that `WriteFileAtomically` publishes, to read what it needs.
/*!
 The file is opened sharing reads, writes and deletion, so a publication
 while it is open replaces the file instead of failing; reads return the
 bytes of the file it opened.
*/
OXGN_SERIO_NDAPI auto OpenPublishedFile(const std::filesystem::path& path)
  -> Result<NativeFile>;

//! Reads a whole file that `WriteFileAtomically` publishes
//! (`OpenPublishedFile`).
OXGN_SERIO_NDAPI auto ReadPublishedFile(const std::filesystem::path& path)
  -> Result<std::vector<std::byte>>;

} // namespace oxygen::serio
