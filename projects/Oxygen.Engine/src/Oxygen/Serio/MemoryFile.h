//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <span>
#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Serio/api_export.h>

namespace oxygen::serio {

//! A growable, writable device in memory: a test's or a builder's file.
/*!
 A `MemoryFile` honours the device contract exactly as a `NativeFile` does,
 so code written against devices reads and writes memory and files alike. It
 owns its bytes; `Take()` hands them on as a `ByteBuffer` without a copy, and
 the adopting constructor takes a buffer the same way.

 `const` operations are safe from any number of threads at once; a write must
 not run concurrently with any other operation.
*/
class MemoryFile {
public:
  //! An empty file.
  MemoryFile() noexcept = default;

  //! A file holding `bytes`, adopted without a copy.
  OXGN_SERIO_API explicit MemoryFile(ByteBuffer bytes) noexcept;

  ~MemoryFile() = default;

  OXYGEN_MAKE_NON_COPYABLE(MemoryFile)

  MemoryFile(MemoryFile&& other) noexcept
    : storage_(std::move(other.storage_))
    , size_(std::exchange(other.size_, 0))
  {
  }

  auto operator=(MemoryFile&& other) noexcept -> MemoryFile&
  {
    if (this != &other) {
      storage_ = std::move(other.storage_);
      size_ = std::exchange(other.size_, 0);
    }
    return *this;
  }

  //! A file holding a copy of `bytes`.
  OXGN_SERIO_NDAPI static auto FromBytes(
    std::span<const std::byte> bytes) noexcept -> Result<MemoryFile>;

  [[nodiscard]] auto Size() const noexcept -> Result<ByteCount>
  {
    return ::oxygen::Ok(CountOf(size_));
  }

  OXGN_SERIO_NDAPI auto ReadAt(ByteOffset at,
    std::span<std::byte> out) const noexcept -> Result<ByteCount>;

  //! Writes all of `in` at `at`, growing the file; bytes between the old end
  //! and `at` read as zero.
  OXGN_SERIO_NDAPI auto WriteAt(
    ByteOffset at, std::span<const std::byte> in) noexcept -> Result<void>;

  //! Grows the storage to hold `size` bytes without changing `Size()`.
  OXGN_SERIO_NDAPI auto Reserve(ByteCount size) noexcept -> Result<void>;

  //! Does nothing: memory has no OS buffers to flush.
  [[nodiscard]] auto Flush(const Durability /*durability*/) noexcept
    -> Result<void>
  {
    return {};
  }

  //! The file's bytes.
  [[nodiscard]] auto Bytes() const noexcept -> std::span<const std::byte>
  {
    return storage_.Bytes().first(size_);
  }

  //! Moves the bytes out as a buffer of exactly `Size()` bytes, leaving the
  //! file empty.
  OXGN_SERIO_NDAPI auto Take() noexcept -> ByteBuffer;

private:
  //! Grows the storage to at least `capacity` bytes, keeping its content.
  auto Grow(size_t capacity) noexcept -> Result<void>;

  ByteBuffer storage_;
  size_t size_ { 0 };
};

static_assert(WritableDevice<MemoryFile>);
static_assert(ContiguousDevice<MemoryFile>);

} // namespace oxygen::serio
