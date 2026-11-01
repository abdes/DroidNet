//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::import {

//! The import's source reader: each file is opened through a
//! `serio::IoDriver` and read with awaited operations.
/*!
 A whole-file read awaits `serio::AsyncReadAll`; a range awaits
 `serio::AsyncReadExactlyAt` into a buffer of the clamped size. No thread
 blocks on the device: the driver's completion resumes the awaiting coroutine
 on the loop thread, and cancelling the awaiting task cancels the pending
 read.

 `GetFileInfo` and `Exists` are file system metadata, not bytes, and stay
 `std::filesystem` queries.

 ### Thread Safety

 Safe for concurrent reads of different files on the loop thread of the
 driver.
*/
class FileSourceReader final : public IAsyncFileReader {
public:
  //! A reader whose files are bound to `driver`, which outlives it, and
  //! whose reads return at most `source_limit` bytes.
  OXGN_COOK_API FileSourceReader(
    serio::IoDriver& driver, serio::ByteCount source_limit);

  OXGN_COOK_API ~FileSourceReader() override;

  OXYGEN_MAKE_NON_COPYABLE(FileSourceReader)
  OXYGEN_MAKE_NON_MOVABLE(FileSourceReader)

  using IAsyncFileReader::ReadFile;

  OXGN_COOK_NDAPI auto ReadFile(const std::filesystem::path& path,
    ReadOptions options) -> co::Co<Result<serio::ByteBuffer>> override;

  OXGN_COOK_NDAPI auto GetFileInfo(const std::filesystem::path& path)
    -> co::Co<Result<FileInfo>> override;

  OXGN_COOK_NDAPI auto Exists(const std::filesystem::path& path)
    -> co::Co<Result<bool>> override;

  [[nodiscard]] auto SourceLimit() const noexcept -> serio::ByteCount override
  {
    return source_limit_;
  }

private:
  observer_ptr<serio::IoDriver> driver_;
  serio::ByteCount source_limit_;
};

} // namespace oxygen::content::import
