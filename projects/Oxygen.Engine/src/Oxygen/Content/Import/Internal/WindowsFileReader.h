//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Content/Import/IAsyncFileReader.h>
#include <Oxygen/Content/api_export.h>

namespace oxygen::content::import {

//! Windows IOCP-based async file reader implementation.
/*!
 Uses Windows I/O Completion Ports (IOCP) via ASIO's random_access_handle
 for true async file I/O without blocking any threads.

 ### How It Works

 1. Files are opened with `FILE_FLAG_OVERLAPPED` for async I/O
 2. ASIO's `windows::random_access_handle` wraps the native handle
 3. Read operations use IOCP to notify completion
 4. The ImportEventLoop's io_context processes completions

 ### Thread Safety

 This class is thread-safe for concurrent reads to different files.
 Multiple reads to the same file should be serialized by the caller.

 ### Performance

 - Zero-copy where possible (reads directly into caller's buffer)
 - No thread pool threads blocked during I/O
 - Completion notifications handled by event loop
*/
class WindowsFileReader final : public IAsyncFileReader {
public:
  //! Construct a Windows file reader.
  /*!
   @param loop The import event loop for async I/O completion handling.
  */
  OXGN_CNTT_API explicit WindowsFileReader(ImportEventLoop& loop);

  OXGN_CNTT_API ~WindowsFileReader() override;

  // Non-copyable, non-movable (holds references)
  WindowsFileReader(const WindowsFileReader&) = delete;
  auto operator=(const WindowsFileReader&) -> WindowsFileReader& = delete;
  WindowsFileReader(WindowsFileReader&&) = delete;
  auto operator=(WindowsFileReader&&) -> WindowsFileReader& = delete;

  //! Read entire file contents asynchronously.
  /*!
   @param path    Absolute path to file.
   @param options Read options (offset, max_bytes, etc.)
   @return File contents or error.
  */
  OXGN_CNTT_NDAPI auto ReadFile(
    const std::filesystem::path& path, ReadOptions options = {})
    -> co::Co<Result<std::vector<std::byte>, FileErrorInfo>> override;

  //! Get file metadata without reading contents.
  /*!
   @param path Absolute path to file.
   @return File info or error.
  */
  OXGN_CNTT_NDAPI auto GetFileInfo(const std::filesystem::path& path)
    -> co::Co<Result<FileInfo, FileErrorInfo>> override;

  //! Check if file exists.
  /*!
   @param path Absolute path to file.
   @return True if exists, false if not, or error.
  */
  OXGN_CNTT_NDAPI auto Exists(const std::filesystem::path& path)
    -> co::Co<Result<bool, FileErrorInfo>> override;

private:
  ImportEventLoop& loop_;
};

} // namespace oxygen::content::import
