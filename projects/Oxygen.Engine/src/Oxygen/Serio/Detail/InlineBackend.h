//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Detail/BlockingIo.h>
#include <Oxygen/Serio/Operation.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio::detail {

//! The backend of a driver without a loop: each request completes on the
//! awaiting thread, which blocks for it.
/*!
 Its files are opened as ordinary, non-overlapped files. `Submit` performs
 the request with the file's blocking read or write and returns false, so
 the awaiter never suspends and there is nothing to cancel.
*/
class InlineBackend {
public:
  [[nodiscard]] static auto Register(const NativeHandle /*handle*/) noexcept
    -> Result<BackendFileSlot>
  {
    return ::oxygen::Ok(BackendFileSlot {});
  }

  static auto Unregister(const BackendFileSlot /*slot*/) noexcept -> void { }

  [[nodiscard]] static auto Submit(IoRequest& request) noexcept -> bool
  {
    const auto at = request.at;
    if (request.kind == IoKind::kRead) {
      const auto read = ReadAtHandle(
        request.target.handle, at, std::span(request.read_into, request.size));
      if (read) {
        request.done = static_cast<size_t>(read->get());
      } else {
        request.error = read.error();
      }
      return false;
    }
    const auto written = WriteAtHandle(
      request.target.handle, at, std::span(request.write_from, request.size));
    if (written) {
      request.done = request.size;
    } else {
      request.error = written.error();
    }
    return false;
  }

  static auto Cancel(IoRequest& /*request*/) noexcept -> void { }

  [[nodiscard]] static auto AllocateBuffer(const ByteCount size) noexcept
    -> Result<ByteBuffer>
  {
    return ByteBuffer::Allocate(size);
  }
};

} // namespace oxygen::serio::detail
