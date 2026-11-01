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
class SerioFileReader final : public IAsyncFileReader {
public:
  //! A reader whose files are bound to `driver`, which outlives it.
  OXGN_COOK_API explicit SerioFileReader(serio::IoDriver& driver);

  OXGN_COOK_API ~SerioFileReader() override;

  OXYGEN_MAKE_NON_COPYABLE(SerioFileReader)
  OXYGEN_MAKE_NON_MOVABLE(SerioFileReader)

  using IAsyncFileReader::ReadFile;

  OXGN_COOK_NDAPI auto ReadFile(const std::filesystem::path& path,
    ReadOptions options) -> co::Co<Result<serio::ByteBuffer>> override;

  OXGN_COOK_NDAPI auto GetFileInfo(const std::filesystem::path& path)
    -> co::Co<Result<FileInfo>> override;

  OXGN_COOK_NDAPI auto Exists(const std::filesystem::path& path)
    -> co::Co<Result<bool>> override;

private:
  observer_ptr<serio::IoDriver> driver_;
};

} // namespace oxygen::content::import
