//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/AsyncFile.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Serio/api_export.h>

namespace asio {
class io_context;
} // namespace asio

namespace oxygen::serio {

//! Binds files to one event loop's completion mechanism, so their
//! operations can be awaited.
/*!
 One driver serves one `asio::io_context`, the loop whose coroutines await
 its files. It is not a request queue: an operation goes to the OS when it is
 awaited, and which operations run, how many are in flight and when they are
 cancelled is the awaiting code's structured concurrency. The driver holds
 what an OS backend differs in:

 - **handles:** a file opened through `Open` is registered with the backend
   and released when it closes;
 - **submission and completion:** awaited operations are submitted to the
   backend and complete on the loop thread;
 - **cancellation:** the backend's own, for one operation;
 - **buffers:** `AllocateBuffer` serves memory the backend reads into best.

 No public type names a backend; `Create` picks the platform's (IOCP on
 Windows). The driver outlives the files it opens, and the loop outlives the
 driver.

 ```cpp
 asio::io_context loop;
 auto driver = IoDriver::Create(loop);
 auto file = driver->Open(path, { .access = Access::kRead });
 co::Run(loop, [&]() -> co::Co<> {
   auto read = co_await file->AsyncReadAt(ByteOffset { 0 }, buffer);
 });
 ```
*/
class IoDriver {
public:
  //! A driver for the loop `loop`; the OS error when its backend cannot be
  //! created.
  OXGN_SERIO_NDAPI static auto Create(asio::io_context& loop) noexcept
    -> Result<IoDriver>;

  OXGN_SERIO_API ~IoDriver() noexcept;

  OXYGEN_MAKE_NON_COPYABLE(IoDriver)

  OXGN_SERIO_API IoDriver(IoDriver&& other) noexcept;
  OXGN_SERIO_API auto operator=(IoDriver&& other) noexcept -> IoDriver&;

  //! Opens the file at `path` as `spec` describes, bound to this driver.
  /*!
   @return The file, or the errors `NativeFile::Open` returns, or the
           backend's error registering the file.
  */
  OXGN_SERIO_NDAPI auto Open(const std::filesystem::path& path,
    const OpenSpec& spec) noexcept -> Result<AsyncFile>;

  //! A buffer of `size` bytes for this driver's operations, aligned for
  //! unbuffered IO.
  OXGN_SERIO_NDAPI auto AllocateBuffer(ByteCount size) noexcept
    -> Result<ByteBuffer>;

private:
  explicit IoDriver(std::unique_ptr<detail::DriverState> state) noexcept;

  std::unique_ptr<detail::DriverState> state_;
};

} // namespace oxygen::serio
