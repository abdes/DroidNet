//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/Import/FileInfo.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/Internal/SerioFileReader.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Serio/AsyncDevice.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::import {

namespace {

  //! Whether `ec` says the file or a folder of its path is absent.
  auto IsAbsent(const std::error_code& ec) -> bool
  {
    return ec == serio::Error::kNotFound;
  }
} // namespace

SerioFileReader::SerioFileReader(serio::IoDriver& driver)
  : driver_(make_observer(&driver))
{
  DLOG_F(INFO, "Created");
}

SerioFileReader::~SerioFileReader() = default;

auto SerioFileReader::ReadFile(const std::filesystem::path& path,
  const ReadOptions options) -> co::Co<Result<serio::ByteBuffer>>
{
  if (path.empty()) {
    co_return Err(std::errc::invalid_argument);
  }

  const auto file = driver_->Open(path,
    serio::OpenSpec {
      .access = serio::Access::kRead,
      .creation = serio::Creation::kOpenExisting,
      .sharing = serio::Sharing::kRead,
      .hint = serio::AccessHint::kSequential,
    });
  if (!file) {
    co_return Err(file.error());
  }

  if (options.offset == 0 && options.max_bytes == 0) {
    co_return co_await serio::AsyncReadAll(*file);
  }

  const auto size = file->Size();
  if (!size) {
    co_return Err(size.error());
  }
  // The range is clamped to the file: an offset past the end reads nothing.
  const auto total = size->get();
  const auto start = std::min(options.offset, total);
  const auto available = total - start;
  const auto count = options.max_bytes > 0
    ? std::min(options.max_bytes, available)
    : available;

  auto buffer = serio::ByteBuffer::Allocate(serio::ByteCount { count });
  if (!buffer) {
    co_return Err(buffer.error());
  }
  if (const auto read = co_await serio::AsyncReadExactlyAt(
        *file, serio::ByteOffset { start }, buffer->Bytes());
    !read) {
    co_return Err(read.error());
  }
  co_return Ok(std::move(*buffer));
}

auto SerioFileReader::GetFileInfo(const std::filesystem::path& path)
  -> co::Co<Result<FileInfo>>
{
  if (path.empty()) {
    co_return Err(std::errc::invalid_argument);
  }

  // Metadata through std::filesystem: synchronous, but one system call.
  auto ec = std::error_code {};
  const auto native_path = base::ToNativePath(path);
  const auto link_status = std::filesystem::symlink_status(native_path, ec);
  if (ec) {
    co_return Err(ec);
  }
  const auto is_symlink = std::filesystem::is_symlink(link_status);
  const auto status
    = is_symlink ? std::filesystem::status(native_path, ec) : link_status;
  if (ec) {
    co_return Err(ec);
  }

  FileInfo info {
    .size = 0U,
    .last_modified = {},
    .is_directory = std::filesystem::is_directory(status),
    .is_symlink = is_symlink,
  };
  if (!info.is_directory) {
    info.size = std::filesystem::file_size(native_path, ec);
    if (ec) {
      co_return Err(ec);
    }
  }

  info.last_modified = std::filesystem::last_write_time(native_path, ec);
  if (ec) {
    co_return Err(ec);
  }

  co_return Ok(info);
}

auto SerioFileReader::Exists(const std::filesystem::path& path)
  -> co::Co<Result<bool>>
{
  if (path.empty()) {
    co_return Err(std::errc::invalid_argument);
  }

  auto ec = std::error_code {};
  const bool exists = std::filesystem::exists(base::ToNativePath(path), ec);
  // Only true errors are reported, not "doesn't exist".
  if (ec && !IsAbsent(ec)) {
    co_return Err(ec);
  }
  co_return Ok(exists);
}

auto CreateAsyncFileReader(serio::IoDriver& driver)
  -> std::unique_ptr<IAsyncFileReader>
{
  return std::make_unique<SerioFileReader>(driver);
}

} // namespace oxygen::content::import
