//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Serio/api_export.h>

namespace oxygen::serio {

//! A file opened from a path, with positional, blocking IO.
/*!
 Every operation names its offset (`ReadFile`/`WriteFile` with an offset,
 `pread`/`pwrite`), so one `NativeFile` serves any number of concurrent
 readers, or writers of disjoint ranges, without a lock. Nothing is buffered
 in the process: a write is handed to the OS when it returns.

 `Open` takes a logical path and converts it with `base::ToNativePath` only at
 the OS call, so long and UNC paths work whatever the machine's long-path
 policy; the file keeps the logical path for diagnostics. Errors are the OS
 error (`base::LastSystemError()`), unchanged, and compare equal to the
 `serio::Error` conditions. Nothing throws.

 A `NativeFile` has no awaitable operations; a file opened through an
 `IoDriver` does.

 @see OpenSpec, ReadExactlyAt
*/
class NativeFile {
public:
  //! Opens the file at `path` as `spec` describes.
  /*!
   @param path A logical path; relative paths resolve against the current
          directory.
   @param spec Access, creation, sharing, access hint and caching. Sharing
          defaults to read and delete, so an atomic replacement of the file
          succeeds while it is open.
   @return The open file, or the OS error (`kNotFound`, `kAccessDenied`,
           `kSharingViolation` compare equal to it); `kIsDirectory` on
           every platform when `path` names a folder.
  */
  OXGN_SERIO_NDAPI static auto Open(const std::filesystem::path& path,
    const OpenSpec& spec) noexcept -> Result<NativeFile>;

  OXGN_SERIO_API ~NativeFile() noexcept;

  OXYGEN_MAKE_NON_COPYABLE(NativeFile)

  OXGN_SERIO_API NativeFile(NativeFile&& other) noexcept;
  OXGN_SERIO_API auto operator=(NativeFile&& other) noexcept -> NativeFile&;

  //! The file's current size.
  OXGN_SERIO_NDAPI auto Size() const noexcept -> Result<ByteCount>;

  //! Reads up to `out.size()` bytes at `at`; fewer when the file ends first.
  OXGN_SERIO_NDAPI auto ReadAt(ByteOffset at,
    std::span<std::byte> out) const noexcept -> Result<ByteCount>;

  //! Writes all of `in` at `at`, extending the file when it ends before.
  OXGN_SERIO_NDAPI auto WriteAt(
    ByteOffset at, std::span<const std::byte> in) noexcept -> Result<void>;

  //! Preallocates `size` bytes of storage without changing `Size()`, so a
  //! producer that knows its size fails early on a full disk and writes
  //! unfragmented. A size at or below the allocation changes nothing.
  OXGN_SERIO_NDAPI auto Reserve(ByteCount size) noexcept -> Result<void>;

  //! Makes the written bytes as durable as `durability` asks. Writes are
  //! already handed to the OS, so `kBuffered` does nothing; `kStable` waits
  //! for stable storage (`FlushFileBuffers`, `fsync`).
  OXGN_SERIO_NDAPI auto Flush(Durability durability) noexcept -> Result<void>;

  //! The logical path the file was opened from.
  [[nodiscard]] auto Path() const noexcept -> const std::filesystem::path&
  {
    return path_;
  }

  //! The OS handle, for code that hands the file to another OS API.
  [[nodiscard]] auto Handle() const noexcept -> NativeHandle { return handle_; }

private:
  friend class IoDriver;

  NativeFile(NativeHandle handle, std::filesystem::path path) noexcept;

  //! Opens the file for an `IoDriver`: overlapped on Windows, so its
  //! operations complete through the driver's backend.
  static auto OpenForDriver(const std::filesystem::path& path,
    const OpenSpec& spec) noexcept -> Result<NativeFile>;

  //! The file owning `handle`, opened from `path`; the open's error passes
  //! through.
  static auto Adopt(const std::filesystem::path& path,
    const Result<NativeHandle>& handle) noexcept -> Result<NativeFile>;

  auto Close() noexcept -> void;

  NativeHandle handle_ { NativeHandle::kInvalid };
  std::filesystem::path path_;
};

static_assert(WritableDevice<NativeFile>);

} // namespace oxygen::serio
