//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <utility>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Operation.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::internal {

//! A device whose awaited reads are its blocking reads.
/*!
 Content awaits payload reads. A file opened through the loader's
 `serio::IoDriver` completes them on the loop thread without blocking it; a
 loader with no driver (a unit test's loop, a tool) reads through this
 adapter instead, so each awaited read completes at once, having blocked the
 awaiting thread for its duration. Memory devices need no adapter: their
 awaited reads are ready at once by themselves.

 @tparam D The device read; a `serio::Region` views a device the adapter
   does not own.
*/
template <serio::ReadableDevice D> class BlockingReads {
public:
  explicit BlockingReads(D device) noexcept
    : device_(std::move(device))
  {
  }

  [[nodiscard]] auto Size() const noexcept -> Result<serio::ByteCount>
  {
    return device_.Size();
  }

  [[nodiscard]] auto ReadAt(const serio::ByteOffset at,
    const std::span<std::byte> out) const noexcept -> Result<serio::ByteCount>
  {
    return device_.ReadAt(at, out);
  }

  //! `ReadAt`, as an operation that completes without suspending.
  [[nodiscard]] auto AsyncReadAt(const serio::ByteOffset at,
    const std::span<std::byte> out) const noexcept -> serio::ReadOperation
  {
    return serio::ReadOperation::Ready(device_.ReadAt(at, out));
  }

  //! The view of a contiguous device; empty for any other.
  [[nodiscard]] auto Contiguous() const noexcept
    -> std::optional<std::span<const std::byte>>
  {
    if constexpr (serio::ContiguousDevice<D>) {
      return device_.Bytes();
    } else if constexpr (requires { device_.Contiguous(); }) {
      return device_.Contiguous();
    } else {
      return std::nullopt;
    }
  }

private:
  D device_;
};

} // namespace oxygen::content::internal
