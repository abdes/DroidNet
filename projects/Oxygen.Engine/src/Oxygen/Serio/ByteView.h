//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Operation.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

//! A read-only device over bytes its caller owns: an embedded pak, a decoded
//! section. The bytes must outlive the view and every region of it.
class ByteView {
public:
  //! An empty view.
  ByteView() noexcept = default;

  explicit ByteView(const std::span<const std::byte> bytes) noexcept
    : bytes_(bytes)
  {
  }

  [[nodiscard]] auto Size() const noexcept -> Result<ByteCount>
  {
    return ::oxygen::Ok(CountOf(bytes_.size()));
  }

  [[nodiscard]] auto ReadAt(const ByteOffset at,
    const std::span<std::byte> out) const noexcept -> Result<ByteCount>
  {
    if (at.get() >= bytes_.size()) {
      return ::oxygen::Ok(ByteCount { 0 });
    }
    const auto offset = static_cast<size_t>(at.get());
    const auto count = (std::min)(out.size(), bytes_.size() - offset);
    if (count != 0) {
      std::memcpy(out.data(), bytes_.subspan(offset, count).data(), count);
    }
    return ::oxygen::Ok(CountOf(count));
  }

  //! `ReadAt` as an operation that completes without suspending.
  [[nodiscard]] auto AsyncReadAt(const ByteOffset at,
    const std::span<std::byte> out) const noexcept -> ReadOperation
  {
    return ReadOperation::Ready(ReadAt(at, out));
  }

  [[nodiscard]] auto Bytes() const noexcept -> std::span<const std::byte>
  {
    return bytes_;
  }

private:
  std::span<const std::byte> bytes_;
};

static_assert(ContiguousDevice<ByteView>);
static_assert(AsyncReadableDevice<ByteView>);
static_assert(!WritableDevice<ByteView>);

} // namespace oxygen::serio
