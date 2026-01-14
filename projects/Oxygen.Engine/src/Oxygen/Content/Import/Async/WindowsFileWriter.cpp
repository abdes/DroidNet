//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <asio/windows/random_access_handle.hpp>
#include <asio/write_at.hpp>

#include <Windows.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Async/WindowsFileWriter.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/asio.h>

namespace {

using oxygen::content::import::FileError;
using oxygen::content::import::FileErrorInfo;

//! Convert Windows error code to FileError.
auto MapWindowsError(DWORD error) -> FileError
{
  switch (error) {
  case ERROR_SUCCESS:
    return FileError::kOk;
  case ERROR_FILE_NOT_FOUND:
  case ERROR_PATH_NOT_FOUND:
    return FileError::kNotFound;
  case ERROR_ACCESS_DENIED:
    return FileError::kAccessDenied;
  case ERROR_FILE_EXISTS:
  case ERROR_ALREADY_EXISTS:
    return FileError::kAlreadyExists;
  case ERROR_DIRECTORY_NOT_SUPPORTED:
    return FileError::kIsDirectory;
  case ERROR_TOO_MANY_OPEN_FILES:
    return FileError::kTooManyOpenFiles;
  case ERROR_DISK_FULL:
  case ERROR_HANDLE_DISK_FULL:
    return FileError::kDiskFull;
  case ERROR_WRITE_PROTECT:
    return FileError::kReadOnly;
  case ERROR_INVALID_NAME:
  case ERROR_BAD_PATHNAME:
    return FileError::kInvalidPath;
  case ERROR_BUFFER_OVERFLOW:
    return FileError::kPathTooLong;
  case ERROR_OPERATION_ABORTED:
  case ERROR_CANCELLED:
    return FileError::kCancelled;
  default:
    return FileError::kUnknown;
  }
}

//! Create FileErrorInfo from Windows error.
auto MakeError(FileError code, const std::filesystem::path& path,
  DWORD win_error = 0) -> FileErrorInfo
{
  return FileErrorInfo {
    .code = code,
    .path = path,
    .system_error
    = std::error_code(static_cast<int>(win_error), std::system_category()),
    .message = std::system_category().message(static_cast<int>(win_error)),
  };
}

//! Create FileErrorInfo from current Windows error.
auto MakeCurrentError(const std::filesystem::path& path) -> FileErrorInfo
{
  const DWORD win_error = GetLastError();
  return MakeError(MapWindowsError(win_error), path, win_error);
}

//! RAII wrapper for Windows HANDLE.
struct HandleGuard {
  HANDLE handle = INVALID_HANDLE_VALUE;

  ~HandleGuard()
  {
    if (handle != INVALID_HANDLE_VALUE) {
      CloseHandle(handle);
    }
  }

  void Release() { handle = INVALID_HANDLE_VALUE; }
};

} // namespace

namespace oxygen::content::import {

WindowsFileWriter::WindowsFileWriter(ImportEventLoop& loop)
  : loop_(loop)
{
  DLOG_F(INFO, "WindowsFileWriter created");
}

WindowsFileWriter::~WindowsFileWriter()
{
  // Cancel any pending operations
  if (pending_count_.load() > 0) {
    DLOG_F(WARNING, "WindowsFileWriter destroyed with {} pending operations",
      pending_count_.load());
    CancelAll();
  }
}

auto WindowsFileWriter::EnsureDirectories(const std::filesystem::path& path)
  -> Result<void, FileErrorInfo>
{
  const auto parent = path.parent_path();
  if (parent.empty() || std::filesystem::exists(parent)) {
    return Result<void, FileErrorInfo>::Ok();
  }

  std::error_code ec;
  std::filesystem::create_directories(parent, ec);
  if (ec) {
    return Err(FileErrorInfo {
      .code = FileError::kIOError,
      .path = parent,
      .system_error = ec,
      .message = ec.message(),
    });
  }

  return Result<void, FileErrorInfo>::Ok();
}

auto WindowsFileWriter::OpenFile(const std::filesystem::path& path,
  const WriteOptions& options, bool truncate) -> Result<void*, FileErrorInfo>
{
  // Validate path
  if (path.empty()) {
    return Err(FileErrorInfo {
      .code = FileError::kInvalidPath,
      .path = path,
      .message = "Empty path",
    });
  }

  // Create directories if requested
  if (options.create_directories) {
    auto result = EnsureDirectories(path);
    if (!result.has_value()) {
      return Err(result.error());
    }
  }

  // Determine creation disposition
  DWORD creation_disposition = 0;
  if (truncate) {
    if (options.overwrite) {
      // Always create new, truncate if exists
      creation_disposition = CREATE_ALWAYS;
    } else {
      // Create only if doesn't exist
      creation_disposition = CREATE_NEW;
    }
  } else {
    // Preserve existing content / allow sparse offset writes.
    // Open existing or create new.
    creation_disposition = OPEN_ALWAYS;
  }

  // Determine share mode
  DWORD share_mode = options.share_write ? FILE_SHARE_WRITE : 0;

  // Open file with FILE_FLAG_OVERLAPPED for async I/O
  HANDLE file_handle = CreateFileW(path.c_str(), GENERIC_WRITE, share_mode,
    nullptr, creation_disposition, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
    nullptr);

  if (file_handle == INVALID_HANDLE_VALUE) {
    return Err(MakeCurrentError(path));
  }

  return Ok(static_cast<void*>(file_handle));
}

auto WindowsFileWriter::Write(const std::filesystem::path& path,
  std::span<const std::byte> data, WriteOptions options)
  -> co::Co<Result<uint64_t, FileErrorInfo>>
{
  // Check for cancellation
  if (cancel_requested_.load()) {
    co_return Err(FileErrorInfo {
      .code = FileError::kCancelled,
      .path = path,
      .message = "Write cancelled",
    });
  }

  // Open file in overwrite/create mode (truncate behavior depends on
  // options.overwrite)
  auto open_result = OpenFile(path, options, true /*truncate*/);
  if (!open_result.has_value()) {
    co_return Err(open_result.error());
  }

  HandleGuard guard { static_cast<HANDLE>(open_result.value()) };

  // Handle empty data
  if (data.empty()) {
    co_return Ok(static_cast<uint64_t>(0));
  }

  // Wrap handle with ASIO for IOCP integration
  asio::windows::random_access_handle handle(loop_.IoContext(), guard.handle);
  guard.Release();

  // Async write at offset 0
  auto [ec, bytes_written] = co_await asio::async_write_at(handle, 0,
    asio::buffer(data.data(), data.size()), co::asio_nothrow_awaitable);

  if (ec) {
    co_return Err(FileErrorInfo {
      .code = FileError::kIOError,
      .path = path,
      .system_error = ec,
      .message = ec.message(),
    });
  }

  co_return Ok(static_cast<uint64_t>(bytes_written));
}

void WindowsFileWriter::WriteAsync(const std::filesystem::path& path,
  std::span<const std::byte> data, WriteOptions options,
  WriteCompletionCallback callback)
{
  // Check for cancellation before starting
  if (cancel_requested_.load()) {
    if (callback) {
      callback(
        FileErrorInfo {
          .code = FileError::kCancelled,
          .path = path,
          .message = "Write cancelled",
        },
        0);
    }
    return;
  }

  // Increment pending count
  pending_count_.fetch_add(1, std::memory_order_relaxed);

  // Open file synchronously (fast operation)
  auto open_result = OpenFile(path, options, true /*truncate*/);
  if (!open_result.has_value()) {
    if (callback) {
      callback(open_result.error(), 0);
    }
    // Record first error for Flush
    {
      std::lock_guard lock(first_error_mutex_);
      if (!first_error_.has_value()) {
        first_error_ = open_result.error();
      }
    }
    pending_count_.fetch_sub(1, std::memory_order_relaxed);
    return;
  }

  // Handle empty data
  if (data.empty()) {
    CloseHandle(static_cast<HANDLE>(open_result.value()));
    if (callback) {
      callback(FileErrorInfo { .code = FileError::kOk }, 0);
    }
    pending_count_.fetch_sub(1, std::memory_order_relaxed);
    return;
  }

  // Create a shared state to hold the ASIO handle and callback
  struct WriteState {
    asio::windows::random_access_handle handle;
    WriteCompletionCallback callback;
    std::filesystem::path path;
    WindowsFileWriter* writer;

    WriteState(asio::io_context& ctx, HANDLE h, WriteCompletionCallback cb,
      std::filesystem::path p, WindowsFileWriter* w)
      : handle(ctx, h)
      , callback(std::move(cb))
      , path(std::move(p))
      , writer(w)
    {
    }
  };

  auto state = std::make_shared<WriteState>(loop_.IoContext(),
    static_cast<HANDLE>(open_result.value()), std::move(callback), path, this);

  // Start async write at offset 0
  asio::async_write_at(state->handle, 0, asio::buffer(data.data(), data.size()),
    [state](const std::error_code& ec, size_t bytes_written) {
      if (state->callback) {
        if (ec) {
          FileErrorInfo error {
            .code = FileError::kIOError,
            .path = state->path,
            .system_error = ec,
            .message = ec.message(),
          };
          // Record first error for Flush
          {
            std::lock_guard lock(state->writer->first_error_mutex_);
            if (!state->writer->first_error_.has_value()) {
              state->writer->first_error_ = error;
            }
          }
          state->callback(error, 0);
        } else {
          state->callback(
            FileErrorInfo { .code = FileError::kOk }, bytes_written);
        }
      }

      // Decrement pending count after callback. This ensures Flush waits for
      // callbacks that may schedule additional operations.
      state->writer->pending_count_.fetch_sub(1, std::memory_order_relaxed);
      // Handle closes automatically when state is destroyed
    });
}

auto WindowsFileWriter::WriteAt(const std::filesystem::path& path,
  uint64_t offset, std::span<const std::byte> data, WriteOptions options)
  -> co::Co<Result<uint64_t, FileErrorInfo>>
{
  // Check for cancellation
  if (cancel_requested_.load()) {
    co_return Err(FileErrorInfo {
      .code = FileError::kCancelled,
      .path = path,
      .message = "Write cancelled",
    });
  }

  // Open file preserving existing content (OPEN_ALWAYS). WriteAt never
  // truncates; use Write/WriteAsync for overwrite semantics.
  auto open_result = OpenFile(path, options, false /*truncate*/);
  if (!open_result.has_value()) {
    co_return Err(open_result.error());
  }

  HandleGuard guard { static_cast<HANDLE>(open_result.value()) };

  // Handle empty data
  if (data.empty()) {
    co_return Ok(static_cast<uint64_t>(0));
  }

  // Wrap handle with ASIO for IOCP integration
  asio::windows::random_access_handle handle(loop_.IoContext(), guard.handle);
  guard.Release();

  // Async write at specific offset
  auto [ec, bytes_written] = co_await asio::async_write_at(handle, offset,
    asio::buffer(data.data(), data.size()), co::asio_nothrow_awaitable);

  if (ec) {
    co_return Err(FileErrorInfo {
      .code = FileError::kIOError,
      .path = path,
      .system_error = ec,
      .message = ec.message(),
    });
  }

  co_return Ok(static_cast<uint64_t>(bytes_written));
}

void WindowsFileWriter::WriteAtAsync(const std::filesystem::path& path,
  uint64_t offset, std::span<const std::byte> data, WriteOptions options,
  WriteCompletionCallback callback)
{
  // Check for cancellation before starting
  if (cancel_requested_.load()) {
    if (callback) {
      callback(
        FileErrorInfo {
          .code = FileError::kCancelled,
          .path = path,
          .message = "Write cancelled",
        },
        0);
    }
    return;
  }

  // Increment pending count
  pending_count_.fetch_add(1, std::memory_order_relaxed);

  // Open file synchronously (fast operation)
  auto open_result = OpenFile(path, options, false /*truncate*/);
  if (!open_result.has_value()) {
    if (callback) {
      callback(open_result.error(), 0);
    }
    // Record first error for Flush
    {
      std::lock_guard lock(first_error_mutex_);
      if (!first_error_.has_value()) {
        first_error_ = open_result.error();
      }
    }
    pending_count_.fetch_sub(1, std::memory_order_relaxed);
    return;
  }

  // Handle empty data
  if (data.empty()) {
    CloseHandle(static_cast<HANDLE>(open_result.value()));
    if (callback) {
      callback(FileErrorInfo { .code = FileError::kOk }, 0);
    }
    pending_count_.fetch_sub(1, std::memory_order_relaxed);
    return;
  }

  // Create a shared state to hold the ASIO handle and callback
  struct WriteAtState {
    asio::windows::random_access_handle handle;
    WriteCompletionCallback callback;
    std::filesystem::path path;
    WindowsFileWriter* writer;

    WriteAtState(asio::io_context& ctx, HANDLE h, WriteCompletionCallback cb,
      std::filesystem::path p, WindowsFileWriter* w)
      : handle(ctx, h)
      , callback(std::move(cb))
      , path(std::move(p))
      , writer(w)
    {
    }
  };

  auto state = std::make_shared<WriteAtState>(loop_.IoContext(),
    static_cast<HANDLE>(open_result.value()), std::move(callback), path, this);

  asio::async_write_at(state->handle, offset,
    asio::buffer(data.data(), data.size()),
    [state](const std::error_code& ec, size_t bytes_written) {
      if (state->callback) {
        if (ec) {
          FileErrorInfo error {
            .code = FileError::kIOError,
            .path = state->path,
            .system_error = ec,
            .message = ec.message(),
          };
          {
            std::lock_guard lock(state->writer->first_error_mutex_);
            if (!state->writer->first_error_.has_value()) {
              state->writer->first_error_ = error;
            }
          }
          state->callback(error, 0);
        } else {
          state->callback(
            FileErrorInfo { .code = FileError::kOk }, bytes_written);
        }
      }

      // Decrement pending count after callback. This ensures Flush waits for
      // callbacks that may schedule additional operations.
      state->writer->pending_count_.fetch_sub(1, std::memory_order_relaxed);
    });
}

auto WindowsFileWriter::Flush() -> co::Co<Result<void, FileErrorInfo>>
{
  using namespace std::chrono_literals;

  // Wait for pending operations by yielding to ASIO event loop
  while (pending_count_.load(std::memory_order_acquire) > 0) {
    // Use SleepFor to yield control to ASIO and allow completions to process
    co_await co::SleepFor(loop_.IoContext(), 0ms);
  }

  // Check if any errors occurred during async operations
  {
    std::lock_guard lock(first_error_mutex_);
    if (first_error_.has_value()) {
      auto error = std::move(first_error_.value());
      first_error_.reset();
      co_return Err(std::move(error));
    }
  }

  co_return Result<void, FileErrorInfo>::Ok();
}

void WindowsFileWriter::CancelAll()
{
  cancel_requested_.store(true, std::memory_order_release);

  // Note: Actual cancellation of in-flight IOCP operations is complex
  // and platform-specific. For now, we just prevent new operations
  // and let in-flight operations complete. The cancel flag will cause
  // Write/WriteAt to return kCancelled if checked before starting I/O.

  DLOG_F(INFO, "WindowsFileWriter::CancelAll() called, {} pending ops",
    pending_count_.load());
}

auto WindowsFileWriter::PendingCount() const -> size_t
{
  return pending_count_.load(std::memory_order_relaxed);
}

auto CreateAsyncFileWriter(ImportEventLoop& loop)
  -> std::unique_ptr<IAsyncFileWriter>
{
  return std::make_unique<WindowsFileWriter>(loop);
}

} // namespace oxygen::content::import
