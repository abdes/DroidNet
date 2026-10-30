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
#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Serio/Stream.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

//! A growable in-memory stream that reads and writes at one cursor.
/*!
 The stream owns its bytes, a `MemoryFile`, so it reports the same errors as
 every other stream. A write at the cursor overwrites and extends the bytes; a
 read at the cursor reads them back after a `Seek`. `Take()` moves the bytes
 out as a `ByteBuffer` without a copy.

 ```cpp
 MemoryStream stream;
 Writer<MemoryStream> writer(stream);
 CHECK_RESULT(writer.Write(value));
 auto bytes = stream.Take();
 ```
*/
class MemoryStream {
public:
  MemoryStream() noexcept = default;

  ~MemoryStream() = default;

  OXYGEN_MAKE_NON_COPYABLE(MemoryStream)

  MemoryStream(MemoryStream&& other) noexcept
    : file_(std::move(other.file_))
    , position_(std::exchange(other.position_, ByteOffset { 0 }))
  {
  }

  auto operator=(MemoryStream&& other) noexcept -> MemoryStream&
  {
    if (this != &other) {
      file_ = std::move(other.file_);
      position_ = std::exchange(other.position_, ByteOffset { 0 });
    }
    return *this;
  }

  [[nodiscard]] auto ReadSome(const std::span<std::byte> out) noexcept
    -> Result<ByteCount>
  {
    const auto read = file_.ReadAt(position_, out);
    if (read) {
      position_ += *read;
    }
    return read;
  }

  [[nodiscard]] auto Write(const std::span<const std::byte> in) noexcept
    -> Result<void>
  {
    CHECK_RESULT(file_.WriteAt(position_, in));
    position_ += CountOf(in.size());
    return {};
  }

  //! Does nothing: the bytes are already in memory.
  [[nodiscard]] auto Flush() noexcept -> Result<void> { return {}; }

  [[nodiscard]] auto Position() const noexcept -> ByteOffset
  {
    return position_;
  }

  [[nodiscard]] auto Size() const noexcept -> ByteCount
  {
    return CountOf(file_.Bytes().size());
  }

  [[nodiscard]] auto Seek(const ByteOffset at) noexcept -> Result<void>
  {
    if (at.get() > file_.Bytes().size()) {
      return ::oxygen::Err(make_error_code(Error::kOutOfBounds));
    }
    position_ = at;
    return {};
  }

  //! Overwrites bytes before the cursor without moving it; `kOutOfBounds`
  //! when they pass `Position()`.
  [[nodiscard]] auto PatchAt(const ByteOffset at,
    const std::span<const std::byte> in) noexcept -> Result<void>
  {
    if (at > position_ || in.size() > position_.get() - at.get()) {
      return ::oxygen::Err(make_error_code(Error::kOutOfBounds));
    }
    return file_.WriteAt(at, in);
  }

  //! Grows the storage to hold `size` bytes without changing `Size()`.
  [[nodiscard]] auto Reserve(const ByteCount size) noexcept -> Result<void>
  {
    return file_.Reserve(size);
  }

  //! The bytes written so far.
  [[nodiscard]] auto Data() const noexcept -> std::span<const std::byte>
  {
    return file_.Bytes();
  }

  //! Moves the bytes out, leaving the stream empty at position zero.
  [[nodiscard]] auto Take() noexcept -> ByteBuffer
  {
    position_ = ByteOffset { 0 };
    return file_.Take();
  }

  //! Empties the stream and rewinds it, releasing its storage.
  auto Clear() noexcept -> void
  {
    file_ = MemoryFile {};
    position_ = ByteOffset { 0 };
  }

private:
  MemoryFile file_;
  ByteOffset position_ { 0 };
};

static_assert(InputStream<MemoryStream>);
static_assert(PatchableOutputStream<MemoryStream>);

//! An input stream over bytes its caller owns.
/*!
 The bytes must outlive the stream. It reports the same errors as every other
 stream.
*/
class ReadOnlyMemoryStream {
public:
  explicit ReadOnlyMemoryStream(
    const std::span<const std::byte> bytes = {}) noexcept
    : bytes_(bytes)
  {
  }

  ~ReadOnlyMemoryStream() = default;

  OXYGEN_MAKE_NON_COPYABLE(ReadOnlyMemoryStream)
  OXYGEN_DEFAULT_MOVABLE(ReadOnlyMemoryStream)

  [[nodiscard]] auto ReadSome(const std::span<std::byte> out) noexcept
    -> Result<ByteCount>
  {
    const auto offset = static_cast<size_t>(position_.get());
    const auto count = (std::min)(out.size(), bytes_.size() - offset);
    if (count != 0) {
      std::memcpy(out.data(), bytes_.subspan(offset, count).data(), count);
    }
    position_ += CountOf(count);
    return ::oxygen::Ok(CountOf(count));
  }

  [[nodiscard]] auto Position() const noexcept -> ByteOffset
  {
    return position_;
  }

  [[nodiscard]] auto Size() const noexcept -> ByteCount
  {
    return CountOf(bytes_.size());
  }

  [[nodiscard]] auto Seek(const ByteOffset at) noexcept -> Result<void>
  {
    if (at.get() > bytes_.size()) {
      return ::oxygen::Err(make_error_code(Error::kOutOfBounds));
    }
    position_ = at;
    return {};
  }

  //! The bytes the stream reads.
  [[nodiscard]] auto Data() const noexcept -> std::span<const std::byte>
  {
    return bytes_;
  }

private:
  std::span<const std::byte> bytes_;
  ByteOffset position_ { 0 };
};

static_assert(InputStream<ReadOnlyMemoryStream>);

} // namespace oxygen::serio
