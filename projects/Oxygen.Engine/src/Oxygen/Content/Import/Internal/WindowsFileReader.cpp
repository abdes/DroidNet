//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <asio/read_at.hpp>
#include <asio/windows/random_access_handle.hpp>

#include <Windows.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Internal/WindowsFileReader.h>
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

} // namespace

namespace oxygen::content::import {

WindowsFileReader::WindowsFileReader(ImportEventLoop& loop)
  : loop_(loop)
{
  DLOG_F(INFO, "WindowsFileReader created");
}

WindowsFileReader::~WindowsFileReader()
{
  // No cleanup needed - loop_ is a reference, not owned
}

auto WindowsFileReader::ReadFile(const std::filesystem::path& path,
  ReadOptions options) -> co::Co<Result<std::vector<std::byte>, FileErrorInfo>>
{
  // Validate path
  if (path.empty()) {
    co_return Err(FileErrorInfo {
      .code = FileError::kInvalidPath,
      .path = path,
      .message = "Empty path",
    });
  }

  // Open file with FILE_FLAG_OVERLAPPED for async I/O via IOCP
  HANDLE file_handle = CreateFileW(path.c_str(), GENERIC_READ,
    FILE_SHARE_READ, // Allow concurrent reads
    nullptr, OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN,
    nullptr);

  if (file_handle == INVALID_HANDLE_VALUE) {
    co_return Err(MakeCurrentError(path));
  }

  // RAII wrapper for file handle
  struct HandleGuard {
    HANDLE handle;
    ~HandleGuard()
    {
      if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
      }
    }
  } guard { file_handle };

  // Get file size
  LARGE_INTEGER file_size;
  if (!GetFileSizeEx(file_handle, &file_size)) {
    co_return Err(MakeCurrentError(path));
  }

  // Calculate read range
  const uint64_t total_size = static_cast<uint64_t>(file_size.QuadPart);
  const uint64_t start_offset = std::min(options.offset, total_size);
  const uint64_t available = total_size - start_offset;
  const uint64_t bytes_to_read = (options.max_bytes > 0)
    ? std::min(options.max_bytes, available)
    : available;

  if (bytes_to_read == 0) {
    co_return Ok(std::vector<std::byte> {});
  }

  // Allocate buffer
  std::vector<std::byte> buffer;
  try {
    buffer.resize(static_cast<size_t>(bytes_to_read));
  } catch (const std::bad_alloc&) {
    co_return Err(FileErrorInfo {
      .code = FileError::kUnknown,
      .path = path,
      .message = "Failed to allocate read buffer",
    });
  }

  // Wrap handle with ASIO's random_access_handle for IOCP integration
  asio::windows::random_access_handle handle(loop_.IoContext(), file_handle);

  // Transfer ownership to ASIO (don't close in guard)
  guard.handle = INVALID_HANDLE_VALUE;

  // Async read via IOCP using OxCo's ASIO integration
  auto [ec, bytes_read] = co_await asio::async_read_at(handle, start_offset,
    asio::buffer(buffer.data(), buffer.size()), co::asio_nothrow_awaitable);

  // Handle closes automatically via ASIO destructor

  if (ec) {
    co_return Err(FileErrorInfo {
      .code = FileError::kIOError,
      .path = path,
      .system_error = ec,
      .message = ec.message(),
    });
  }

  // Resize if we read less than expected (shouldn't happen normally)
  if (bytes_read < buffer.size()) {
    buffer.resize(bytes_read);
  }

  co_return Ok(std::move(buffer));
}

auto WindowsFileReader::GetFileInfo(const std::filesystem::path& path)
  -> co::Co<Result<FileInfo, FileErrorInfo>>
{
  // Validate path
  if (path.empty()) {
    co_return Err(FileErrorInfo {
      .code = FileError::kInvalidPath,
      .path = path,
      .message = "Empty path",
    });
  }

  // Use std::filesystem for metadata (synchronous but fast)
  std::error_code ec;
  auto status = std::filesystem::status(path, ec);
  if (ec) {
    const FileError code = std::filesystem::exists(path, ec)
      ? FileError::kIOError
      : FileError::kNotFound;
    co_return Err(FileErrorInfo {
      .code = code,
      .path = path,
      .system_error = ec,
      .message = ec.message(),
    });
  }

  FileInfo info {};
  info.is_directory = std::filesystem::is_directory(status);
  info.is_symlink = std::filesystem::is_symlink(status);

  if (!info.is_directory) {
    info.size = std::filesystem::file_size(path, ec);
    if (ec) {
      co_return Err(FileErrorInfo {
        .code = FileError::kIOError,
        .path = path,
        .system_error = ec,
        .message = ec.message(),
      });
    }
  }

  info.last_modified = std::filesystem::last_write_time(path, ec);
  if (ec) {
    // Non-fatal, just log warning
    DLOG_F(WARNING, "Failed to get last_write_time for %s: %s",
      path.string().c_str(), ec.message().c_str());
  }

  co_return Ok(std::move(info));
}

auto WindowsFileReader::Exists(const std::filesystem::path& path)
  -> co::Co<Result<bool, FileErrorInfo>>
{
  // Validate path
  if (path.empty()) {
    co_return Err(FileErrorInfo {
      .code = FileError::kInvalidPath,
      .path = path,
      .message = "Empty path",
    });
  }

  std::error_code ec;
  const bool exists = std::filesystem::exists(path, ec);
  if (ec) {
    // Only report true errors, not "doesn't exist"
    if (ec.value() != ENOENT && ec.value() != ERROR_FILE_NOT_FOUND
      && ec.value() != ERROR_PATH_NOT_FOUND) {
      co_return Err(FileErrorInfo {
        .code = FileError::kIOError,
        .path = path,
        .system_error = ec,
        .message = ec.message(),
      });
    }
  }

  co_return Ok(exists);
}

auto CreateAsyncFileReader(ImportEventLoop& loop)
  -> std::unique_ptr<IAsyncFileReader>
{
  return std::make_unique<WindowsFileReader>(loop);
}

} // namespace oxygen::content::import
