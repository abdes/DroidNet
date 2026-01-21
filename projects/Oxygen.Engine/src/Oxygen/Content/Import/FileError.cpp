//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <format>

#ifdef _WIN32
#  include <Windows.h>
#endif

#include <Oxygen/Content/Import/FileError.h>

namespace oxygen::content::import {

auto FileErrorInfo::ToString() const -> std::string
{
  if (code == FileError::kOk) {
    return "OK";
  }

  std::string result;

  // Error code name
  switch (code) {
  case FileError::kOk:
    result = "OK";
    break;
  case FileError::kNotFound:
    result = "NotFound";
    break;
  case FileError::kAccessDenied:
    result = "AccessDenied";
    break;
  case FileError::kAlreadyExists:
    result = "AlreadyExists";
    break;
  case FileError::kIsDirectory:
    result = "IsDirectory";
    break;
  case FileError::kNotDirectory:
    result = "NotDirectory";
    break;
  case FileError::kTooManyOpenFiles:
    result = "TooManyOpenFiles";
    break;
  case FileError::kNoSpace:
    result = "NoSpace";
    break;
  case FileError::kDiskFull:
    result = "DiskFull";
    break;
  case FileError::kReadOnly:
    result = "ReadOnly";
    break;
  case FileError::kInvalidPath:
    result = "InvalidPath";
    break;
  case FileError::kPathTooLong:
    result = "PathTooLong";
    break;
  case FileError::kIOError:
    result = "IOError";
    break;
  case FileError::kCancelled:
    result = "Cancelled";
    break;
  case FileError::kUnknown:
    result = "Unknown";
    break;
  }

  // Add path if present
  if (!path.empty()) {
    result += std::format(": '{}'", path.string());
  }

  // Add custom message if present
  if (!message.empty()) {
    result += std::format(" - {}", message);
  }

  // Add system error if present
  if (system_error) {
    result += std::format(" (system: {})", system_error.message());
  }

  return result;
}

auto MapSystemError(std::error_code ec) -> FileError
{
  if (!ec) {
    return FileError::kOk;
  }

  // Map std::errc values (POSIX-style)
  if (ec.category() == std::generic_category()) {
    switch (static_cast<std::errc>(ec.value())) {
    case std::errc::no_such_file_or_directory:
      return FileError::kNotFound;
    case std::errc::permission_denied:
      return FileError::kAccessDenied;
    case std::errc::file_exists:
      return FileError::kAlreadyExists;
    case std::errc::is_a_directory:
      return FileError::kIsDirectory;
    case std::errc::not_a_directory:
      return FileError::kNotDirectory;
    case std::errc::too_many_files_open:
    case std::errc::too_many_files_open_in_system:
      return FileError::kTooManyOpenFiles;
    case std::errc::no_space_on_device:
      return FileError::kNoSpace;
    case std::errc::read_only_file_system:
      return FileError::kReadOnly;
    case std::errc::filename_too_long:
      return FileError::kPathTooLong;
    case std::errc::io_error:
      return FileError::kIOError;
    case std::errc::operation_canceled:
      return FileError::kCancelled;
    case std::errc::invalid_argument:
      return FileError::kInvalidPath;
    default:
      return FileError::kUnknown;
    }
  }

  // Map Windows system errors
#ifdef _WIN32
  if (ec.category() == std::system_category()) {
    switch (ec.value()) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return FileError::kNotFound;
    case ERROR_ACCESS_DENIED:
      return FileError::kAccessDenied;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
      return FileError::kAlreadyExists;
    case ERROR_DIRECTORY:
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
#endif

  return FileError::kUnknown;
}

auto MakeFileError(const std::filesystem::path& path, std::error_code ec)
  -> FileErrorInfo
{
  return FileErrorInfo {
    .code = MapSystemError(ec),
    .path = path,
    .system_error = ec,
    .message = ec.message(),
  };
}

auto MakeFileError(const std::filesystem::path& path, FileError code,
  std::string message) -> FileErrorInfo
{
  return FileErrorInfo {
    .code = code,
    .path = path,
    .system_error = {},
    .message = std::move(message),
  };
}

} // namespace oxygen::content::import
