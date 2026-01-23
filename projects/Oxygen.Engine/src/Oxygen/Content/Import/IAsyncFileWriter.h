//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Content/Import/FileError.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import {

//! Options for async file write operations.
/*!
 Controls write behavior including alignment requirements, atomic semantics,
 and directory creation.
*/
struct WriteOptions {
  //! Buffer alignment requirement for source data (0 = no requirement).
  /*!
   Some sources (e.g., GPU readback) require aligned buffers.
   The implementation verifies alignment if non-zero.
  */
  size_t alignment = 0;

  //! Create parent directories if they don't exist.
  /*!
   When true, behaves like `mkdir -p` before writing.
   When false, fails with kNotFound if parent directory doesn't exist.
  */
  bool create_directories = true;

  //! Overwrite existing file if present.
  /*!
   When true, existing file is truncated and overwritten.
   When false, fails with kAlreadyExists if file exists.

     @note This option affects Write()/WriteAsync() only.
   WriteAt()/WriteAtAsync() never truncate existing content.
  */
  bool overwrite = true;

  //! Allow concurrent write access to the same file.
  /*!
   When true, the file is opened with FILE_SHARE_WRITE. This is required for
   explicit-offset writes where multiple operations may target the same file.
   When false (default), exclusive write access is required.
  */
  bool share_write = false;
};

//! Callback invoked when an async write operation completes.
/*!
 @param error Error information, or FileError::kOk if successful.
 @param bytes_written Number of bytes written (0 on error).
*/
using WriteCompletionCallback
  = std::function<void(const FileErrorInfo& error, uint64_t bytes_written)>;

//! Async file writer interface.
/*!
 Platform-agnostic interface for asynchronous file writing operations.
 Implementations may use native async I/O (IOCP on Windows, io_uring on
 Linux) or ThreadPool-based blocking I/O as a fallback.

 ### Write Modes

 Two write APIs are provided:

 1. **Coroutine-based Write**: Suspends until write completes.
    Suitable when you need to know the result before proceeding.

 2. **Callback-based WriteAsync**: Returns immediately, invokes callback
    on completion. Suitable for fire-and-forget scenarios like the
    emitter pattern where you assign an index immediately and let I/O
    complete in the background.

 ### Thread Safety

 Implementations must be safe to call from the import event loop thread.
 Multiple concurrent writes to different files are supported.
 Concurrent writes to the same file are supported via WriteAt/WriteAtAsync as
 long as all writes target non-overlapping byte ranges and
 WriteOptions::share_write is true.

 ### Cancellation

 Coroutine-based operations respect coroutine cancellation. When canceled,
 pending operations return `FileError::kCancelled`. Callback-based
 operations cannot be individually canceled, but `CancelAll()` will
 cancel pending operations.

 ### Error Handling

 All operations return or report `FileErrorInfo`. No exceptions are thrown.

 ### Example

 ```cpp
 auto writer = CreateAsyncFileWriter(loop);

 // Coroutine-based (waits for completion)
 auto result = co_await writer->Write("output.bin", data);
 if (!result.has_value()) {
   LOG_F(ERROR, "%s", result.error().ToString().c_str());
 }

 // Callback-based (fire-and-forget)
 writer->WriteAsync("texture.dds", texture_data, {},
   [](const FileErrorInfo& err, uint64_t bytes) {
     if (err.IsError()) {
       LOG_F(ERROR, "Write failed: %s", err.ToString().c_str());
     }
   });

 // Wait for all pending writes
 co_await writer->Flush();
 ```
*/
class IAsyncFileWriter {
public:
  virtual ~IAsyncFileWriter() = default;

  //! Write data to file, waiting for completion.
  /*!
   Creates or overwrites the file with the provided data. Suspends the
   coroutine until the write completes.

   @param path    Absolute or relative path to the file.
   @param data    Data to write.
   @param options Write options (alignment, overwrite behavior).
   @return Number of bytes written on success, or error information.

   ### Errors
   - `kNotFound` if parent directory doesn't exist (when
   create_directories=false).
   - `kAlreadyExists` if file exists (when overwrite=false).
   - `kAccessDenied` if write permission is denied.
   - `kNoSpace` or `kDiskFull` if insufficient disk space.
   - `kReadOnly` if filesystem is read-only.
   - `kCancelled` if operation was canceled.
   - `kIOError` for other I/O failures.
  */
  [[nodiscard]] virtual auto Write(const std::filesystem::path& path,
    std::span<const std::byte> data, WriteOptions options)
    -> co::Co<Result<uint64_t, FileErrorInfo>>
    = 0;

  //! Write data to file asynchronously with callback notification.
  /*!
   Creates or overwrites the file with the provided data. Returns
   immediately; the callback is invoked when the write completes
   (or fails).

   @param path     Absolute or relative path to the file.
   @param data     Data to write. Caller must ensure data remains valid
                   until the callback is invoked.
   @param options  Write options (alignment, overwrite behavior).
   @param callback Invoked on completion with error status and bytes written.

   ### Data Lifetime

   The caller is responsible for ensuring the data span remains valid
   until the callback is invoked. Typically, the caller owns the data
   buffer and captures it in the callback lambda.

   ### Callback Thread

   The callback is invoked on the import event loop thread, never on
   a background I/O thread.

   ### Errors

   Same as Write(), but reported via callback rather than return value.
  */
  virtual void WriteAsync(const std::filesystem::path& path,
    std::span<const std::byte> data, WriteOptions options,
    WriteCompletionCallback callback)
    = 0;

  //! Write data at a specific byte offset, waiting for completion.
  /*!
   Writes `data` to `path` starting at `offset` without changing the file
   pointer. Creates the file if it does not exist.

   This API exists to support the emitter pattern where offsets are computed
   ahead of time. It enables safe parallelism against a shared `*.data` file
   as long as:

   - All writes target **non-overlapping** byte ranges.
   - The caller uses `WriteOptions{.share_write = true}`.

   Overlapping writes have undefined behavior.

   @param path    Absolute or relative path to the file.
   @param offset  Byte offset where the write begins.
   @param data    Data to write.
   @param options Write options (alignment, directory creation, share_write).
   @return Number of bytes written on success, or error information.
  */
  [[nodiscard]] virtual auto WriteAt(const std::filesystem::path& path,
    uint64_t offset, std::span<const std::byte> data, WriteOptions options)
    -> co::Co<Result<uint64_t, FileErrorInfo>>
    = 0;

  //! Write data at a specific byte offset asynchronously.
  /*!
   See WriteAt() for semantics and concurrency requirements.

   @param path     Absolute or relative path to the file.
   @param offset   Byte offset where the write begins.
   @param data     Data to write. Caller must keep it alive until callback.
   @param options  Write options.
   @param callback Invoked on completion with error status and bytes written.
  */
  virtual void WriteAtAsync(const std::filesystem::path& path, uint64_t offset,
    std::span<const std::byte> data, WriteOptions options,
    WriteCompletionCallback callback)
    = 0;

  //! Wait for all pending async operations to complete.
  /*!
    Suspends the coroutine until all pending WriteAsync and WriteAtAsync
   operations have completed.

   @return Ok if all writes succeeded, or the first error encountered.

   ### Usage

   Call this before finalizing an import session to ensure all
   data has been written to disk.
  */
  [[nodiscard]] virtual auto Flush() -> co::Co<Result<void, FileErrorInfo>> = 0;

  //! Cancel all pending async operations.
  /*!
   Cancels pending operations. Already-started I/O may complete normally
   or be aborted. Callbacks for canceled operations will be invoked
   with `FileError::kCancelled`.
  */
  virtual void CancelAll() = 0;

  //! Get number of pending async operations.
  /*!
    @return Number of WriteAsync/WriteAtAsync calls that haven't completed.
  */
  [[nodiscard]] virtual auto PendingCount() const -> size_t = 0;

  //! Check if any async operations are pending.
  /*!
   @return True if PendingCount() > 0.
  */
  [[nodiscard]] auto HasPending() const -> bool { return PendingCount() > 0; }
};

// Forward declaration
class ImportEventLoop;

//! Create a platform-appropriate async file writer.
/*!
 On Windows, returns a WindowsFileWriter using IOCP.
 On other platforms, returns an appropriate implementation.

 @param loop The import event loop.
 @return Unique pointer to async file writer.
*/
OXGN_CNTT_NDAPI auto CreateAsyncFileWriter(ImportEventLoop& loop)
  -> std::unique_ptr<IAsyncFileWriter>;

} // namespace oxygen::content::import
