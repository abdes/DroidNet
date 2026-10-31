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

//! Overlapped IO completed through the I/O completion port of the loop's
//! `asio::io_context`.
/*!
 A registered file's handle is associated with the loop's port through an
 asio handle that owns a duplicate of it, so the file keeps its own handle
 for blocking operations. Each request is one `ReadFile`/`WriteFile` per
 chunk on an `asio::windows::overlapped_ptr`, whose completion runs on the
 loop thread; a cancellation is `CancelIoEx` of that one request.
*/
class IocpBackend {
public:
  explicit IocpBackend(asio::io_context& loop) noexcept
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
  //! failed at once, with the error recorded.
  [[nodiscard]] auto Issue(IoRequest& request) noexcept -> bool;

  //! Records a chunk's completion and issues the next one, or resumes the
  //! waiter when the request is done.
  auto Complete(IoRequest& request, const std::error_code& error,
    size_t transferred) noexcept -> void;

  asio::io_context* loop_;
};

} // namespace oxygen::serio::detail
