//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <variant>

#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Detail/InlineBackend.h>
#include <Oxygen/Serio/Operation.h>
#include <Oxygen/Serio/Types.h>

#ifdef OXYGEN_WINDOWS
#  include <Oxygen/Serio/Platform/Windows/IocpBackend.h>
#else
#  include <Oxygen/Serio/Platform/Posix/UringBackend.h>
#endif

namespace oxygen::serio::detail {

//! What an IO backend provides to its driver.
/*!
 - `Register` binds an open file's handle to the backend; `Unregister`
   releases what it kept, before the handle closes.
 - `Submit` starts a request, and returns false when it completed at once
   (its waiter is then not resumed); a pending request resumes its waiter on
   the loop thread once the OS is done with its buffer.
 - `Cancel` asks the OS to abort one pending request.
 - `AllocateBuffer` serves memory its operations read into best.
*/
template <class B>
concept IoBackend = requires(
  B& b, NativeHandle handle, BackendFileSlot slot, IoRequest& request) {
  { b.Register(handle) } noexcept -> std::same_as<Result<BackendFileSlot>>;
  { b.Unregister(slot) } noexcept -> std::same_as<void>;
  { b.Submit(request) } noexcept -> std::same_as<bool>;
  { b.Cancel(request) } noexcept -> std::same_as<void>;
  {
    b.AllocateBuffer(ByteCount {})
  } noexcept -> std::same_as<Result<ByteBuffer>>;
};

//! The backends of this platform, inline first; the driver holds one of them.
static_assert(IoBackend<InlineBackend>);
#ifdef OXYGEN_WINDOWS
static_assert(IoBackend<IocpBackend>);
using Backend = std::variant<InlineBackend, IocpBackend>;
#else
static_assert(IoBackend<UringBackend>);
using Backend = std::variant<InlineBackend, UringBackend>;
#endif

// A backend is a handful of pointers; the driver copies it into its state.
static_assert(std::is_trivially_copyable_v<Backend>);

//! Calls `operation` with the backend `backend` holds.
/*!
 A `std::visit` that cannot throw: every backend is trivially copyable, so the
 variant is never valueless and `bad_variant_access` cannot happen.
*/
template <size_t kIndex = 0, class Operation>
auto VisitBackend(Backend& backend, Operation&& operation) noexcept
  -> decltype(auto)
{
  if constexpr (kIndex + 1 == std::variant_size_v<Backend>) {
    return std::forward<Operation>(operation)(*std::get_if<kIndex>(&backend));
  } else {
    if (auto* const alternative = std::get_if<kIndex>(&backend)) {
      return std::forward<Operation>(operation)(*alternative);
    }
    return VisitBackend<kIndex + 1>(
      backend, std::forward<Operation>(operation));
  }
}

//! An `IoDriver`'s state, which its files and pending operations point to.
class DriverState {
public:
  explicit DriverState(const detail::Backend selected) noexcept
    : backend(selected)
  {
  }

  detail::Backend backend;
  //! The files open on the driver; none may remain when it is destroyed.
  std::atomic<size_t> open_files { 0 };
};

} // namespace oxygen::serio::detail
