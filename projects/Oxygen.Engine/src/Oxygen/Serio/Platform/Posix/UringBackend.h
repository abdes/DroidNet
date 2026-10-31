//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <system_error>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Operation.h>
#include <Oxygen/Serio/Types.h>

namespace asio {
class io_context;
} // namespace asio

namespace oxygen::serio::detail {

//! io_uring file IO through the loop's `asio::io_context`.
/*!
 OxCo builds asio with `ASIO_HAS_IO_URING` on Linux, so the loop's context
 owns an io_uring and asio's `random_access_file` submits positional reads
 and writes to it. A registered file is such a file over a duplicate of the
 file's descriptor, so the file keeps its own descriptor for blocking
 operations. Each request carries its own cancellation signal: cancelling
 one submits an io_uring cancel of that request alone. Completions run on
 the loop thread.
*/
class UringBackend {
public:
  explicit UringBackend(asio::io_context& loop) noexcept
    : loop_(&loop)
  {
  }

  [[nodiscard]] auto Register(NativeHandle handle) noexcept
    -> Result<BackendFileSlot>;
  auto Unregister(BackendFileSlot slot) noexcept -> void;
  [[nodiscard]] auto Submit(IoRequest& request) noexcept -> bool;
  auto Cancel(IoRequest& request) noexcept -> void;
  [[nodiscard]] auto AllocateBuffer(ByteCount size) noexcept
    -> Result<ByteBuffer>;

private:
  //! The handler of a chunk's completion, run on the loop thread.
  struct Completion;

  //! Issues the request's next chunk. True when it is pending; false when it
  //! could not be issued, with the error recorded.
  [[nodiscard]] auto Issue(IoRequest& request) noexcept -> bool;

  //! Records a chunk's completion and issues the next one, or resumes the
  //! waiter when the request is done.
  auto Complete(IoRequest& request, const std::error_code& error,
    size_t transferred) noexcept -> void;

  asio::io_context* loop_;
};

} // namespace oxygen::serio::detail
