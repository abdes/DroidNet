//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <span>
#include <system_error>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Serio/api_export.h>

namespace oxygen::serio {

namespace detail {
  class DriverState;

  //! What a backend keeps for one registered file; opaque to the file.
  struct BackendFileSlot {
    void* state { nullptr };
  };

  //! The file an operation targets: its driver, its handle and what the
  //! driver's backend keeps for it.
  struct IoTarget {
    DriverState* driver { nullptr };
    NativeHandle handle { NativeHandle::kInvalid };
    BackendFileSlot slot;
  };

  enum class IoKind : uint8_t {
    kRead,
    kWrite,
  };

  //! One operation's state, shared by its awaiter and the driver's backend.
  /*!
   The awaiter fills the arguments; the backend records progress, the
   pending OS request and the outcome, and resumes `waiter` once the OS has
   released the buffer.
  */
  struct IoRequest {
    IoTarget target;
    IoKind kind { IoKind::kRead };
    ByteOffset at { 0 };
    std::byte* read_into { nullptr };
    const std::byte* write_from { nullptr };
    size_t size { 0 };
    size_t done { 0 };
    std::error_code error;
    std::coroutine_handle<> waiter;
    //! The backend's handle on the pending OS request.
    void* in_flight { nullptr };
    bool cancel_requested { false };
    //! The OS aborted the request after a cancellation: the awaiting task
    //! ends cancelled instead of resuming.
    bool aborted { false };
  };

  //! Hands `request` to its driver's backend. True when it is pending and
  //! will resume its waiter; false when it completed at once.
  OXGN_SERIO_NDAPI auto Submit(IoRequest& request) noexcept -> bool;

  //! Asks the backend to cancel the pending `request`. Its waiter resumes
  //! once the OS completes or aborts it.
  OXGN_SERIO_API auto Cancel(IoRequest& request) noexcept -> void;

  //! The awaiter protocol common to reads and writes.
  class IoOperation {
  public:
    //! An operation with nothing to do (no bytes or a result known at
    //! once) completes without suspending.
    [[nodiscard]] auto await_ready() const noexcept -> bool
    {
      return request_.target.driver == nullptr || request_.size == 0;
    }

    [[nodiscard]] auto await_suspend(
      const std::coroutine_handle<> waiter) noexcept -> bool
    {
      request_.waiter = waiter;
      return Submit(request_);
    }

    //! Cancels the pending request; the waiter resumes when the OS is done
    //! with the buffer.
    [[nodiscard]] auto await_cancel(
      const std::coroutine_handle<> /*waiter*/) noexcept -> bool
    {
      Cancel(request_);
      return false;
    }

    [[nodiscard]] auto await_must_resume() const noexcept -> bool
    {
      return !request_.aborted;
    }

  protected:
    IoOperation() noexcept = default;

    IoOperation(
      const IoTarget& target, const IoKind kind, const ByteOffset at) noexcept
    {
      request_.target = target;
      request_.kind = kind;
      request_.at = at;
    }

    [[nodiscard]] auto Request() noexcept -> IoRequest& { return request_; }

    [[nodiscard]] auto Request() const noexcept -> const IoRequest&
    {
      return request_;
    }

  private:
    IoRequest request_;
  };
} // namespace detail

//! A read from a device, awaited in a coroutine; its result is the
//! `Result<ByteCount>` of the read.
/*!
 Every asynchronous device returns this one type, so erasing a device needs
 no coroutine frame. A memory device returns a ready operation: nothing
 suspends. A file opened through an `IoDriver` returns one bound to the
 driver: awaiting submits it, the coroutine suspends with no thread blocked,
 and the completion resumes it on the loop thread.

 Cancelling the awaiting task (a nursery, `AnyOf`, a timeout) cancels the OS
 request, and the task ends only after the OS has released the buffer. An
 operation is moved only before it is awaited, and awaited once.
*/
class ReadOperation : public detail::IoOperation {
public:
  //! A read whose result is known: it completes without suspending.
  [[nodiscard]] static auto Ready(const Result<ByteCount>& result) noexcept
    -> ReadOperation
  {
    auto operation = ReadOperation {};
    if (result) {
      operation.Request().done = static_cast<size_t>(result->get());
    } else {
      operation.Request().error = result.error();
    }
    return operation;
  }

  //! A read of up to `out.size()` bytes at `at` of `target`, submitted to
  //! its driver when awaited.
  ReadOperation(const detail::IoTarget& target, const ByteOffset at,
    const std::span<std::byte> out) noexcept
    : IoOperation(target, detail::IoKind::kRead, at)
  {
    Request().read_into = out.data();
    Request().size = out.size();
  }

  //! The bytes read, fewer than requested only when the device ends first.
  [[nodiscard]] auto await_resume() const noexcept -> Result<ByteCount>
  {
    if (Request().error) {
      return ::oxygen::Err(Request().error);
    }
    return ::oxygen::Ok(CountOf(Request().done));
  }

private:
  ReadOperation() noexcept = default;
};

//! A write to a device, awaited in a coroutine; it writes all of its bytes
//! or fails. See `ReadOperation` for how it completes and cancels.
class WriteOperation : public detail::IoOperation {
public:
  //! A write whose result is known: it completes without suspending.
  [[nodiscard]] static auto Ready(const Result<void>& result) noexcept
    -> WriteOperation
  {
    auto operation = WriteOperation {};
    if (!result) {
      operation.Request().error = result.error();
    }
    return operation;
  }

  //! A write of all of `in` at `at` of `target`, submitted to its driver
  //! when awaited.
  WriteOperation(const detail::IoTarget& target, const ByteOffset at,
    const std::span<const std::byte> in) noexcept
    : IoOperation(target, detail::IoKind::kWrite, at)
  {
    Request().write_from = in.data();
    Request().size = in.size();
  }

  [[nodiscard]] auto await_resume() const noexcept -> Result<void>
  {
    if (Request().error) {
      return ::oxygen::Err(Request().error);
    }
    return {};
  }

private:
  WriteOperation() noexcept = default;
};

} // namespace oxygen::serio
