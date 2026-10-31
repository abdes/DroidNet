//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <system_error>
#include <utility>

// asio includes the Windows socket headers, which must precede <Windows.h>
// (Handles.h): this block sorts before the Oxygen block.
#include <asio/io_context.hpp> // IWYU pragma: keep (complete io_context)
#include <asio/windows/overlapped_ptr.hpp>
#include <asio/windows/random_access_handle.hpp>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/SystemError.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Operation.h>
#include <Oxygen/Serio/Platform/Windows/Handles.h>
#include <Oxygen/Serio/Platform/Windows/IocpBackend.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio::detail {
namespace {

  //! The bytes of the request's buffer not transferred yet.
  template <class Byte>
  auto Remaining(Byte* const buffer, const IoRequest& request) noexcept
    -> std::span<Byte>
  {
    return std::span(buffer, request.size).subspan(request.done);
  }

  //! The largest transfer one `ReadFile`/`WriteFile` call takes.
  constexpr auto kMaxTransfer
    = static_cast<size_t>(std::numeric_limits<DWORD>::max());

  auto Resume(IoRequest& request) noexcept -> void
  {
    std::exchange(request.waiter, {}).resume();
  }

} // namespace

struct IocpBackend::Completion {
  IocpBackend* backend;
  IoRequest* request;

  auto operator()(const std::error_code& error,
    const size_t transferred) const noexcept -> void
  {
    backend->Complete(*request, error, transferred);
  }
};

auto IocpBackend::Register(const NativeHandle handle) noexcept
  -> Result<BackendFileSlot>
{
  // The asio handle that associates the file with the loop's port owns a
  // duplicate: the association belongs to the file object both handles
  // share, and the file keeps its own handle.
  HANDLE duplicate = nullptr;
  auto* const process = GetCurrentProcess();
  if (DuplicateHandle(process, windows::ToHandle(handle), process, &duplicate,
        0, FALSE, DUPLICATE_SAME_ACCESS)
    == FALSE) {
    return ::oxygen::Err(base::LastSystemError());
  }
  auto association = std::unique_ptr<asio::windows::random_access_handle>(
    new (std::nothrow) asio::windows::random_access_handle(*loop_));
  if (!association) {
    CloseHandle(duplicate);
    return ::oxygen::Err(std::make_error_code(std::errc::not_enough_memory));
  }
  auto status = std::error_code {};
  if (const auto error = association->assign(duplicate, status)) {
    CloseHandle(duplicate);
    return ::oxygen::Err(error);
  }
  return ::oxygen::Ok(BackendFileSlot { .state = association.release() });
}

auto IocpBackend::Unregister(const BackendFileSlot slot) noexcept -> void
{
  // Closes the duplicate; the file's own handle stays open.
  std::default_delete<asio::windows::random_access_handle> {}(
    static_cast<asio::windows::random_access_handle*>(slot.state));
}

auto IocpBackend::Submit(IoRequest& request) noexcept -> bool
{
  return Issue(request);
}

auto IocpBackend::Cancel(IoRequest& request) noexcept -> void
{
  request.cancel_requested = true;
  if (request.in_flight != nullptr) {
    // A request that already completed is not found; its completion is
    // queued and resumes the waiter with its result.
    CancelIoEx(windows::ToHandle(request.target.handle),
      static_cast<OVERLAPPED*>(request.in_flight));
  }
}

auto IocpBackend::AllocateBuffer(const ByteCount size) noexcept
  -> Result<ByteBuffer>
{
  return ByteBuffer::Allocate(size);
}

auto IocpBackend::Issue(IoRequest& request) noexcept -> bool
{
  try {
    auto operation = asio::windows::overlapped_ptr(
      *loop_, Completion { .backend = this, .request = &request });
    auto* const overlapped = operation.get();
    const auto at = request.at.get() + request.done;
    overlapped->Offset = static_cast<DWORD>(at & 0xFFFF'FFFFU);
    overlapped->OffsetHigh = static_cast<DWORD>(at >> 32U);
    const auto chunk = static_cast<DWORD>(
      (std::min)(request.size - request.done, kMaxTransfer));
    auto* const handle = windows::ToHandle(request.target.handle);
    const auto started = request.kind == IoKind::kRead
      ? ReadFile(handle, Remaining(request.read_into, request).data(), chunk,
          nullptr, overlapped)
      : WriteFile(handle, Remaining(request.write_from, request).data(), chunk,
          nullptr, overlapped);
    const auto error
      = started != FALSE ? DWORD { ERROR_SUCCESS } : GetLastError();
    if (started != FALSE || error == ERROR_IO_PENDING) {
      // The port receives the completion, synchronous ones included.
      request.in_flight = operation.release();
      return true;
    }
    // Failed at once: no completion is queued.
    operation.reset();
    if (request.kind != IoKind::kRead || error != ERROR_HANDLE_EOF) {
      request.error
        = std::error_code(static_cast<int>(error), std::system_category());
    }
    return false;
  } catch (const std::bad_alloc&) {
    request.error = std::make_error_code(std::errc::not_enough_memory);
    return false;
  } catch (const std::system_error& error) {
    // asio reports a failure of its io_context this way.
    request.error = error.code();
    return false;
  }
}

auto IocpBackend::Complete(IoRequest& request, const std::error_code& error,
  const size_t transferred) noexcept -> void
{
  request.in_flight = nullptr;
  if (error) {
    if (error.value() == ERROR_OPERATION_ABORTED && request.cancel_requested) {
      request.aborted = true;
    } else if (request.kind != IoKind::kRead
      || error.value() != ERROR_HANDLE_EOF) {
      request.error = error;
    }
    Resume(request);
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
  Resume(request);
}

} // namespace oxygen::serio::detail
