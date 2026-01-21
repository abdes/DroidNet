//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

#include <Oxygen/Content/api_export.h>

namespace oxygen::content::import {

//! File I/O error codes for async import operations.
/*!
 Provides a cross-platform set of file error codes that abstract over
 Windows and POSIX error codes.

 ### Thread Safety
 Error codes and FileErrorInfo are value types and can be freely copied
 between threads.

 ### Example
 ```cpp
 FileErrorInfo error{
   .code = FileError::kNotFound,
   .path = "/some/missing/file.txt",
   .system_error = std::make_error_code(std::errc::no_such_file_or_directory),
   .message = "File does not exist"
 };
 LOG_F(ERROR, "%s", error.ToString().c_str());
 ```
*/
enum class FileError : uint32_t {
  //! No error - operation succeeded.
  kOk = 0,

  //! File or directory not found.
  kNotFound,

  //! Permission denied.
  kAccessDenied,

  //! File or directory already exists.
  kAlreadyExists,

  //! Expected a file but found a directory.
  kIsDirectory,

  //! Expected a directory but found a file.
  kNotDirectory,

  //! Too many open file descriptors.
  kTooManyOpenFiles,

  //! No space left on device.
  kNoSpace,

  //! Disk quota exceeded.
  kDiskFull,

  //! File system is read-only.
  kReadOnly,

  //! Invalid file path (malformed, empty, etc.)
  kInvalidPath,

  //! Path exceeds maximum length.
  kPathTooLong,

  //! General I/O error during read/write.
  kIOError,

  //! Operation was canceled.
  kCancelled,

  //! Unknown or unmapped error.
  kUnknown,
};

//! Detailed file error information.
/*!
 Contains the error code, affected path, underlying system error, and
 a human-readable message. Used as the error type in Result<T, FileErrorInfo>.
*/
struct FileErrorInfo {
  //! High-level error code.
  FileError code = FileError::kUnknown;

  //! Path that caused the error.
  std::filesystem::path path {};

  //! Underlying system error code (e.g., from errno or GetLastError).
  std::error_code system_error {};

  //! Human-readable error message.
  std::string message {};

  //! Format error as a human-readable string.
  /*!
   @return Formatted string including error code, path, and message.
  */
  OXGN_CNTT_NDAPI auto ToString() const -> std::string;

  //! Check if this represents an actual error (code != kOk).
  [[nodiscard]] auto IsError() const -> bool { return code != FileError::kOk; }
};

//! Map a system error code to a FileError.
/*!
 Converts platform-specific error codes to our cross-platform FileError enum.

 @param ec The system error code to map.
 @return Corresponding FileError, or kUnknown if no mapping exists.
*/
OXGN_CNTT_NDAPI auto MapSystemError(std::error_code ec) -> FileError;

//! Create a FileErrorInfo from a system error code.
/*!
 Convenience function to create a fully-populated FileErrorInfo.

 @param path   The file path that caused the error.
 @param ec     The system error code.
 @return FileErrorInfo with mapped code and system message.
*/
OXGN_CNTT_NDAPI auto MakeFileError(
  const std::filesystem::path& path, std::error_code ec) -> FileErrorInfo;

//! Create a FileErrorInfo with a custom message.
/*!
 @param path    The file path that caused the error.
 @param code    The FileError code.
 @param message Human-readable error message.
 @return FileErrorInfo with the specified values.
*/
OXGN_CNTT_NDAPI auto MakeFileError(const std::filesystem::path& path,
  FileError code, std::string message) -> FileErrorInfo;

} // namespace oxygen::content::import
