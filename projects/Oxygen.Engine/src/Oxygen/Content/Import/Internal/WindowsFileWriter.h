//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include <Oxygen/Content/Import/IAsyncFileWriter.h>
#include <Oxygen/Content/api_export.h>

namespace oxygen::content::import {

//! Windows IOCP-based async file writer implementation.
/*!
 Uses Windows I/O Completion Ports (IOCP) via ASIO's random_access_handle
 for true async file I/O without blocking any threads.

 ### How It Works

 1. Files are opened with `FILE_FLAG_OVERLAPPED` for async I/O
 2. ASIO's `windows::random_access_handle` wraps the native handle
 3. Write operations use IOCP to notify completion
 4. The ImportEventLoop's io_context processes completions

 ### Thread Safety

 This class is thread-safe for concurrent writes to different files.
 Multiple writes to the same file are supported via WriteAt/WriteAtAsync when
 all writes target non-overlapping byte ranges and WriteOptions::share_write is
 true.

 ### Pending Operation Tracking

 The writer tracks pending async operations via an atomic counter.
 `Flush()` waits for all pending operations to complete.
 `CancelAll()` cancels pending operations where possible.

 ### Performance

 - Zero-copy where possible (writes directly from caller's buffer)
 - No thread pool threads blocked during I/O
 - Completion notifications handled by event loop
*/
class WindowsFileWriter final : public IAsyncFileWriter {
public:
  //! Construct a Windows file writer.
  /*!
   @param loop The import event loop for async I/O completion handling.
  */
  OXGN_CNTT_API explicit WindowsFileWriter(ImportEventLoop& loop);

  OXGN_CNTT_API ~WindowsFileWriter() override;

  // Non-copyable, non-movable (holds references)
  WindowsFileWriter(const WindowsFileWriter&) = delete;
  auto operator=(const WindowsFileWriter&) -> WindowsFileWriter& = delete;
  WindowsFileWriter(WindowsFileWriter&&) = delete;
  auto operator=(WindowsFileWriter&&) -> WindowsFileWriter& = delete;

  //! Write data to file, waiting for completion.
  OXGN_CNTT_NDAPI auto Write(const std::filesystem::path& path,
    std::span<const std::byte> data, WriteOptions options = {})
    -> co::Co<Result<uint64_t, FileErrorInfo>> override;

  //! Write data asynchronously with callback notification.
  OXGN_CNTT_API void WriteAsync(const std::filesystem::path& path,
    std::span<const std::byte> data, WriteOptions options,
    WriteCompletionCallback callback) override;

  //! Write data at a specific byte offset, waiting for completion.
  OXGN_CNTT_NDAPI auto WriteAt(const std::filesystem::path& path,
    uint64_t offset, std::span<const std::byte> data, WriteOptions options = {})
    -> co::Co<Result<uint64_t, FileErrorInfo>> override;

  //! Write data at a specific byte offset asynchronously with callback
  //! notification.
  OXGN_CNTT_API void WriteAtAsync(const std::filesystem::path& path,
    uint64_t offset, std::span<const std::byte> data, WriteOptions options,
    WriteCompletionCallback callback) override;

  //! Wait for all pending operations to complete.
  OXGN_CNTT_NDAPI auto Flush() -> co::Co<Result<void, FileErrorInfo>> override;

  //! Cancel all pending operations.
  OXGN_CNTT_API void CancelAll() override;

  //! Get number of pending async operations.
  [[nodiscard]] OXGN_CNTT_API auto PendingCount() const -> size_t override;

private:
  //! Open or create a file for writing.
  /*!
   @param path     File path.
   @param options  Write options.
   @param truncate True for overwrite+truncate, false for preserve/OPEN_ALWAYS.
   @return File handle on success, or error.
  */
  auto OpenFile(const std::filesystem::path& path, const WriteOptions& options,
    bool truncate) -> Result<void*, FileErrorInfo>;

  //! Ensure parent directories exist.
  auto EnsureDirectories(const std::filesystem::path& path)
    -> Result<void, FileErrorInfo>;

  //! The import event loop for IOCP processing.
  ImportEventLoop& loop_;

  //! Counter for pending async operations.
  std::atomic<size_t> pending_count_ { 0 };

  //! Flag to signal cancellation.
  std::atomic<bool> cancel_requested_ { false };

  //! First error encountered during async operations (for Flush reporting).
  std::mutex first_error_mutex_;
  std::optional<FileErrorInfo> first_error_;
};

} // namespace oxygen::content::import
