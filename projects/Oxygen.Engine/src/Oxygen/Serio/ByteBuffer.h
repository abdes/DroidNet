//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

namespace detail {
  //! Releases memory allocated with the matching aligned `operator new[]`.
  struct AlignedBytesDeleter {
    size_t alignment { kIoAlignment };

    auto operator()(std::byte* memory) const noexcept -> void
    {
      ::operator delete[](memory, std::align_val_t { alignment });
    }
  };
} // namespace detail

//! An owned, uninitialized, aligned allocation of bytes.
/*!
 A buffer about to be filled by a read is never zero-filled first: its bytes
 are indeterminate until written. The default alignment, `kIoAlignment`, lets
 the same buffer serve an unbuffered read of a sector-aligned region.

 The buffer is move-only. `Truncate` shrinks its size without reallocating, so
 a growable owner (`MemoryStream`, `MemoryFile`) hands its storage on with
 `Take()` and no copy; `MemoryFile` adopts a buffer the same way.

 ```cpp
 auto buffer = ByteBuffer::Allocate(ByteCount { size });
 if (!buffer) { return Err(buffer.error()); }
 CHECK_RESULT(ReadExactlyAt(file, offset, buffer->Bytes()));
 ```
*/
class ByteBuffer {
public:
  //! An empty buffer; it owns no allocation.
  ByteBuffer() noexcept = default;

  ~ByteBuffer() = default;

  OXYGEN_MAKE_NON_COPYABLE(ByteBuffer)

  ByteBuffer(ByteBuffer&& other) noexcept
    : storage_(std::move(other.storage_))
    , size_(std::exchange(other.size_, 0))
  {
  }

  auto operator=(ByteBuffer&& other) noexcept -> ByteBuffer&
  {
    if (this != &other) {
      storage_ = std::move(other.storage_);
      size_ = std::exchange(other.size_, 0);
    }
    return *this;
  }

  //! Allocates `size` uninitialized bytes aligned to `alignment`.
  /*!
   @param size The number of bytes; zero allocates nothing.
   @param alignment A power of two.
   @return The buffer; `invalid_argument` for an alignment that is not a power
           of two, `not_enough_memory` when the allocation fails.
  */
  [[nodiscard]] static auto Allocate(const ByteCount size,
    const size_t alignment = kIoAlignment) noexcept -> Result<ByteBuffer>
  {
    if (!std::has_single_bit(alignment)) {
      return ::oxygen::Err(std::make_error_code(std::errc::invalid_argument));
    }
    if (size.get() == 0) {
      return ::oxygen::Ok(ByteBuffer {});
    }
    if (size.get() > std::numeric_limits<size_t>::max()) {
      return ::oxygen::Err(std::make_error_code(std::errc::not_enough_memory));
    }
    const auto bytes = static_cast<size_t>(size.get());
    void* memory
      = ::operator new[](bytes, std::align_val_t { alignment }, std::nothrow);
    if (memory == nullptr) {
      return ::oxygen::Err(std::make_error_code(std::errc::not_enough_memory));
    }
    return ::oxygen::Ok(ByteBuffer(
      Storage(static_cast<std::byte*>(memory), Deleter { alignment }), bytes));
  }

  //! The buffer's bytes.
  [[nodiscard]] auto Bytes() noexcept -> std::span<std::byte>
  {
    return { storage_.get(), size_ };
  }

  //! The buffer's bytes, read-only.
  [[nodiscard]] auto Bytes() const noexcept -> std::span<const std::byte>
  {
    return { storage_.get(), size_ };
  }

  //! The number of bytes the buffer holds.
  [[nodiscard]] auto Size() const noexcept -> ByteCount
  {
    return CountOf(size_);
  }

  //! Whether the buffer holds no bytes.
  [[nodiscard]] auto Empty() const noexcept -> bool { return size_ == 0; }

  //! Shrinks the buffer to its first `size` bytes, keeping the allocation. A
  //! size at or above the current size changes nothing.
  auto Truncate(const ByteCount size) noexcept -> void
  {
    if (size.get() < size_) {
      size_ = static_cast<size_t>(size.get());
    }
  }

private:
  using Deleter = detail::AlignedBytesDeleter;
  // The array form owns a runtime-sized allocation.
  // NOLINTNEXTLINE(modernize-avoid-c-arrays)
  using Storage = std::unique_ptr<std::byte[], Deleter>;

  ByteBuffer(Storage storage, const size_t size) noexcept
    : storage_(std::move(storage))
    , size_(size)
  {
  }

  Storage storage_;
  size_t size_ { 0 };
};

} // namespace oxygen::serio
