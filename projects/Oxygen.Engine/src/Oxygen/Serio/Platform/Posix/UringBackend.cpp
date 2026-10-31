//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cerrno>
#include <cstddef>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

#include <asio/buffer.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <asio/random_access_file.hpp>
#include <fcntl.h>
#include <unistd.h>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/SystemError.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Operation.h>
#include <Oxygen/Serio/Platform/Posix/UringBackend.h>
#include <Oxygen/Serio/Types.h>

#ifndef ASIO_HAS_IO_URING
#  error "Serio's POSIX backend needs asio built with ASIO_HAS_IO_URING (OxCo)"
#endif

namespace oxygen::serio::detail {
namespace {

  //! The bytes of the request's buffer not transferred yet.
  template <class Byte>
  auto Remaining(Byte* const buffer, const IoRequest& request) noexcept
    -> std::span<Byte>
  {
    return std::span(buffer, request.size).subspan(request.done);
  }

  auto FileOf(const IoRequest& request) noexcept -> asio::random_access_file&
  {
    return *static_cast<asio::random_access_file*>(request.target.slot.state);
  }

  auto SignalOf(const IoRequest& request) noexcept -> asio::cancellation_signal&
  {
    return *static_cast<asio::cancellation_signal*>(request.in_flight);
  }

  //! Whether `error` is `expected`. Compares the value and the category's
  //! name: header-only asio may give another module its own instance of a
  //! category (see `co::IsOperationAborted`).
  auto Is(const std::error_code& error,
    const std::error_code& expected) noexcept -> bool
  {
    return error.value() == expected.value()
      && std::string_view { error.category().name() }
      == expected.category().name();
  }

  //! The OS error asio reports, in the standard system category that the
  //! `serio::Error` conditions recognize: asio reports errno values in its
  //! own system category.
  auto SystemError(const std::error_code& error) noexcept -> std::error_code
  {
    if (std::string_view { error.category().name() } == "asio.system") {
      return { error.value(), std::system_category() };
    }
    return error;
  }

  //! Ends the request: releases its cancellation signal and resumes its
  //! waiter.
  auto Finish(IoRequest& request) noexcept -> void
  {
    std::default_delete<asio::cancellation_signal> {}(&SignalOf(request));
    request.in_flight = nullptr;
    std::exchange(request.waiter, {}).resume();
  }

} // namespace

struct UringBackend::Completion {
  UringBackend* backend;
  IoRequest* request;

  using cancellation_slot_type = asio::cancellation_slot;

  [[nodiscard]] auto get_cancellation_slot() const noexcept
    -> cancellation_slot_type
  {
    return SignalOf(*request).slot();
  }

  auto operator()(const std::error_code& error,
    const size_t transferred) const noexcept -> void
  {
    backend->Complete(*request, error, transferred);
  }
};

auto UringBackend::Register(const NativeHandle handle) noexcept
  -> Result<BackendFileSlot>
{
  // asio's file owns a duplicate, so the file keeps its own descriptor.
  const auto duplicate = ::fcntl(static_cast<int>(handle), F_DUPFD_CLOEXEC, 0);
  if (duplicate < 0) {
    return ::oxygen::Err(base::LastSystemError());
  }
  auto file = std::unique_ptr<asio::random_access_file>(
    new (std::nothrow) asio::random_access_file(*loop_));
  if (!file) {
    ::close(duplicate);
    return ::oxygen::Err(std::make_error_code(std::errc::not_enough_memory));
  }
  auto status = std::error_code {};
  if (const auto error = file->assign(duplicate, status)) {
    ::close(duplicate);
    return ::oxygen::Err(SystemError(error));
  }
  return ::oxygen::Ok(BackendFileSlot { .state = file.release() });
}

auto UringBackend::Unregister(const BackendFileSlot slot) noexcept -> void
{
  // Closes the duplicate; the file's own descriptor stays open.
  std::default_delete<asio::random_access_file> {}(
    static_cast<asio::random_access_file*>(slot.state));
}

auto UringBackend::Submit(IoRequest& request) noexcept -> bool
{
  // One signal serves every chunk of the request.
  request.in_flight = new (std::nothrow) asio::cancellation_signal();
  if (request.in_flight == nullptr) {
    request.error = std::make_error_code(std::errc::not_enough_memory);
    return false;
  }
  if (!Issue(request)) {
    std::default_delete<asio::cancellation_signal> {}(&SignalOf(request));
    request.in_flight = nullptr;
    return false;
  }
  return true;
}

auto UringBackend::Cancel(IoRequest& request) noexcept -> void
{
  request.cancel_requested = true;
  if (request.in_flight != nullptr) {
    // A request that already completed ignores the signal; its completion
    // is queued and resumes the waiter with its result.
    SignalOf(request).emit(asio::cancellation_type::all);
  }
}

auto UringBackend::AllocateBuffer(const ByteCount size) noexcept
  -> Result<ByteBuffer>
{
  return ByteBuffer::Allocate(size);
}

auto UringBackend::Issue(IoRequest& request) noexcept -> bool
{
  try {
    const auto at = request.at.get() + request.done;
    auto handler = Completion { .backend = this, .request = &request };
    if (request.kind == IoKind::kRead) {
      const auto out = Remaining(request.read_into, request);
      FileOf(request).async_read_some_at(
        at, asio::buffer(out.data(), out.size()), std::move(handler));
    } else {
      const auto in = Remaining(request.write_from, request);
      FileOf(request).async_write_some_at(
        at, asio::buffer(in.data(), in.size()), std::move(handler));
    }
    return true;
  } catch (const std::bad_alloc&) {
    request.error = std::make_error_code(std::errc::not_enough_memory);
    return false;
  } catch (const std::system_error& error) {
    // asio reports a failure of its io_context this way.
    request.error = error.code();
    return false;
  }
}

auto UringBackend::Complete(IoRequest& request, const std::error_code& error,
  const size_t transferred) noexcept -> void
{
  if (error) {
    if (Is(error, std::error_code { asio::error::operation_aborted })
      && request.cancel_requested) {
      request.aborted = true;
    } else if (request.kind != IoKind::kRead
      || !Is(error, std::error_code { asio::error::eof })) {
      request.error = SystemError(error);
    }
    Finish(request);
    return;
  }
  request.done += transferred;
  if (transferred == 0) {
    // A read at the end of the file; a write that wrote nothing failed.
    if (request.kind == IoKind::kWrite) {
      request.error = std::make_error_code(std::errc::io_error);
    }
  } else if (request.done < request.size) {
    if (request.cancel_requested) {
      request.aborted = true;
    } else if (Issue(request)) {
      return;
    }
  }
  Finish(request);
}

} // namespace oxygen::serio::detail
