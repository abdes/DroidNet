//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/Import/FileInfo.h>
#include <Oxygen/Cooker/Import/ImportLimits.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {
class IoDriver;
} // namespace oxygen::serio

namespace oxygen::content::import {

//! Options for async file read operations.
/*!
 Selects the byte range of a read. The default reads the whole file.
*/
struct ReadOptions {
  //! Byte offset to start reading from (0 = beginning of file).
  uint64_t offset = 0;

  //! Maximum bytes to read (0 = read entire file from offset).
  uint64_t max_bytes = 0;
};

//! Async file reader interface.
/*!
 The import's seam for reading sources: `ImportSourceSnapshot` decorates it to
 record and replay reads, and tests substitute it. Its one implementation,
 `SerioFileReader`, opens each source through the import loop's
 `serio::IoDriver`. `GetFileInfo` and `Exists` are file system metadata, not
 bytes, and stay `std::filesystem` queries.

 ### Thread Safety

 Implementations must be safe to call from the import event loop thread.
 Multiple concurrent reads to different files are supported.

 ### Cancellation

 All operations respect coroutine cancellation: cancelling the awaiting task
 cancels the pending read.

 ### Error Handling

 All operations return `Result<T>` with `std::error_code` errors; callers
 compare them with Serio's conditions (`serio::Error::kNotFound`,
 `serio::Error::kIsDirectory`) and name the path in their diagnostics. No
 exceptions are thrown.

 ### Example

 ```cpp
 auto reader = CreateAsyncFileReader(loop.Driver());
 auto result = co_await reader->ReadFile("path/to/file.bin");
 if (result) {
   const auto bytes = result->Bytes();
   // Process bytes...
 } else {
   LOG_F(ERROR, "{}: {}", path.string(), result.error().message());
 }
 ```
*/
class IAsyncFileReader {
public:
  IAsyncFileReader() = default;
  virtual ~IAsyncFileReader() = default;

  OXYGEN_DEFAULT_COPYABLE(IAsyncFileReader)
  OXYGEN_DEFAULT_MOVABLE(IAsyncFileReader)

  //! Read the whole file into memory.
  [[nodiscard]] auto ReadFile(const std::filesystem::path& path)
    -> co::Co<Result<serio::ByteBuffer>>
  {
    return ReadFile(path, ReadOptions {});
  }

  //! Read a file (or a range of it) into memory.
  /*!
   @param path    Absolute or relative path to the file.
   @param options Byte range to read. An offset past the end reads nothing,
                  and a range reaching past the end is clamped to it.
   @return The bytes read on success, or the error.

   ### Errors
   - `serio::Error::kTooLarge`, before anything is allocated, when the bytes
     the read would return pass `SourceLimit()`: the whole file, or the
     clamped range. A read is refused, never clipped to the limit.
   - `serio::Error::kNotFound` if the file does not exist.
   - `serio::Error::kAccessDenied` if read permission is denied.
   - `serio::Error::kIsDirectory` if the path names a directory.
   - `std::errc::invalid_argument` for an empty path.
   - `std::errc::operation_canceled` if the operation was canceled.
  */
  [[nodiscard]] virtual auto ReadFile(const std::filesystem::path& path,
    ReadOptions options) -> co::Co<Result<serio::ByteBuffer>> = 0;

  //! Get file metadata without reading contents.
  /*!
   @param path Absolute or relative path to the file.
   @return File metadata on success, or the error.

   ### Errors
   - `serio::Error::kNotFound` if the file does not exist.
   - `serio::Error::kAccessDenied` if stat permission is denied.
  */
  [[nodiscard]] virtual auto GetFileInfo(const std::filesystem::path& path)
    -> co::Co<Result<FileInfo>> = 0;

  //! Check if a file exists.
  /*!
   @param path Absolute or relative path to check.
   @return True if the file exists, false otherwise. Error only on I/O failure.

   Note: This returns `false` for non-existent files without an error.
  */
  [[nodiscard]] virtual auto Exists(const std::filesystem::path& path)
    -> co::Co<Result<bool>> = 0;

  //! The most bytes one read returns; a larger read fails with
  //! `serio::Error::kTooLarge`.
  [[nodiscard]] virtual auto SourceLimit() const noexcept -> serio::ByteCount
    = 0;
};

//! Create the async file reader of the import loop.
/*!
 @param driver The driver of the import loop's `asio::io_context`; it outlives
               the reader.
 @param source_limit The most bytes one read returns (`SourceLimit()`).
 @return Unique pointer to async file reader.
*/
OXGN_COOK_NDAPI auto CreateAsyncFileReader(serio::IoDriver& driver,
  serio::ByteCount source_limit = kDefaultMaxSourceBytes)
  -> std::unique_ptr<IAsyncFileReader>;

} // namespace oxygen::content::import
