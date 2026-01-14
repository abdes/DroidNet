//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Result.h>
#include <Oxygen/Content/Import/Async/FileError.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/OxCo/Co.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace oxygen::content::import {

//! Options for async file read operations.
/*!
 Controls offset, size limits, and optimization hints for file reads.
*/
struct ReadOptions {
  //! Byte offset to start reading from (0 = beginning of file).
  uint64_t offset = 0;

  //! Maximum bytes to read (0 = read entire file from offset).
  uint64_t max_bytes = 0;

  //! Hint for expected file size to enable buffer pre-allocation.
  /*!
   If provided and accurate, reduces memory reallocations during read.
   Zero means no hint is provided.
  */
  uint64_t size_hint = 0;

  //! Buffer alignment requirement for read buffer (0 = no requirement).
  /*!
   Some applications (e.g., GPU uploads) require aligned buffers.
   When non-zero, the returned buffer will be aligned to this boundary.
  */
  size_t alignment = 0;
};

//! File metadata information.
struct FileInfo {
  //! File size in bytes.
  uint64_t size = 0;

  //! Last modification time.
  std::filesystem::file_time_type last_modified {};

  //! True if path is a directory.
  bool is_directory = false;

  //! True if path is a symbolic link.
  bool is_symlink = false;
};

//! Async file reader interface.
/*!
 Platform-agnostic interface for asynchronous file reading operations.
 Implementations may use native async I/O (IOCP on Windows, io_uring on
 Linux) or ThreadPool-based blocking I/O as a fallback.

 ### Thread Safety

 Implementations must be safe to call from the import event loop thread.
 Multiple concurrent reads to different files are supported.

 ### Cancellation

 All operations respect coroutine cancellation. When a coroutine is
 cancelled, pending operations return `FileError::kCancelled`.

 ### Error Handling

 All operations return `Result<T, FileErrorInfo>`. No exceptions are thrown.

 ### Example

 ```cpp
 auto reader = CreateAsyncFileReader(pool);
 co_await Run(loop, [&]() -> Co<> {
   auto result = co_await reader->ReadFile("path/to/file.bin");
   if (result.has_value()) {
     auto& bytes = result.value();
     // Process bytes...
   } else {
     LOG_F(ERROR, "%s", result.error().ToString().c_str());
   }
   co_return;
 });
 ```
*/
class IAsyncFileReader {
public:
  virtual ~IAsyncFileReader() = default;

  //! Read entire file contents into memory.
  /*!
   Reads the specified file (or portion of it) into a byte vector.

   @param path    Absolute or relative path to the file.
   @param options Read options (offset, max_bytes, hints).
   @return File contents on success, or error information on failure.

   ### Errors
   - `kNotFound` if file does not exist.
   - `kAccessDenied` if read permission is denied.
   - `kIsDirectory` if path points to a directory.
   - `kCancelled` if operation was cancelled.
   - `kIOError` for other I/O failures.
  */
  [[nodiscard]] virtual auto ReadFile(
    const std::filesystem::path& path, ReadOptions options = {})
    -> co::Co<Result<std::vector<std::byte>, FileErrorInfo>>
    = 0;

  //! Get file metadata without reading contents.
  /*!
   Retrieves file size, modification time, and type information.

   @param path Absolute or relative path to the file.
   @return File metadata on success, or error information on failure.

   ### Errors
   - `kNotFound` if file does not exist.
   - `kAccessDenied` if stat permission is denied.
  */
  [[nodiscard]] virtual auto GetFileInfo(const std::filesystem::path& path)
    -> co::Co<Result<FileInfo, FileErrorInfo>>
    = 0;

  //! Check if a file exists.
  /*!
   @param path Absolute or relative path to check.
   @return True if file exists, false otherwise. Error only on I/O failure.

   Note: This returns `false` for non-existent files without an error.
   An error is only returned if the existence check itself fails.
  */
  [[nodiscard]] virtual auto Exists(const std::filesystem::path& path)
    -> co::Co<Result<bool, FileErrorInfo>>
    = 0;
};

// Forward declaration
class ImportEventLoop;

//! Create a platform-appropriate async file reader.
/*!
 On Windows, returns a WindowsFileReader using IOCP.
 On other platforms, returns an appropriate implementation.

 @param loop The import event loop.
 @return Unique pointer to async file reader.
*/
OXGN_CNTT_NDAPI auto CreateAsyncFileReader(ImportEventLoop& loop)
  -> std::unique_ptr<IAsyncFileReader>;

} // namespace oxygen::content::import
