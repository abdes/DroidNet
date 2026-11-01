//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/Stream.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

//! A buffered input stream over a region of a device.
/*!
 The stream's offsets are relative to the region; `Size()` and `Seek()` cost
 nothing. A contiguous region (memory, a mapped file) has no buffer: each
 read is one `ReadAt` of the device into the caller's span, so a mapped file
 copies under its guard and a memory file that grew or shrank since the
 stream was made is read as it is now. Otherwise reads fill a buffer,
 allocated on the first read and never larger than the region, and a read at
 least as large as the buffer goes straight into the caller's span with one
 `ReadAt`.

 A short device (a file truncated below its region) ends the stream early:
 `ReadSome` returns zero bytes and `ReadExactly` fails with `kEndOfData`.

 The device outlives the stream.

 @tparam D The device type.
*/
template <ReadableDevice D> class RegionReadStream {
public:
  //! The buffer size of a non-contiguous region by default.
  static constexpr ByteCount kDefaultBufferSize { 64ULL * 1024 };

  explicit RegionReadStream(const Region<D> region,
    const ByteCount buffer_size = kDefaultBufferSize) noexcept
    : region_(region)
    , direct_(region.Contiguous().has_value())
    , buffer_size_(static_cast<size_t>(
        (std::min)(buffer_size.get(), region.Range().count.get())))
  {
  }

  ~RegionReadStream() = default;

  OXYGEN_MAKE_NON_COPYABLE(RegionReadStream)
  OXYGEN_DEFAULT_MOVABLE(RegionReadStream)

  [[nodiscard]] auto ReadSome(const std::span<std::byte> out) noexcept
    -> Result<ByteCount>
  {
    const auto remaining = region_.Range().count.get() - position_.get();
    const auto want
      = static_cast<size_t>((std::min)(uint64_t { out.size() }, remaining));
    if (want == 0) {
      return ::oxygen::Ok(ByteCount { 0 });
    }
    if (const auto buffered = CopyBuffered(out.first(want)); buffered != 0) {
      return ::oxygen::Ok(CountOf(buffered));
    }
    if (direct_ || want >= buffer_size_) {
      const auto read = region_.ReadAt(position_, out.first(want));
      if (read) {
        position_ += *read;
      }
      return read;
    }
    CHECK_RESULT(Fill());
    return ::oxygen::Ok(CountOf(CopyBuffered(out.first(want))));
  }

  [[nodiscard]] auto Position() const noexcept -> ByteOffset
  {
    return position_;
  }

  [[nodiscard]] auto Size() const noexcept -> ByteCount
  {
    return region_.Range().count;
  }

  [[nodiscard]] auto Seek(const ByteOffset at) noexcept -> Result<void>
  {
    if (at.get() > region_.Range().count.get()) {
      return ::oxygen::Err(make_error_code(Error::kOutOfBounds));
    }
    position_ = at;
    return {};
  }

  //! The region the stream reads.
  [[nodiscard]] auto GetRegion() const noexcept -> const Region<D>&
  {
    return region_;
  }

private:
  //! Copies what the buffer holds at the cursor into `out`; returns the bytes
  //! copied, zero when the cursor is outside the buffered window.
  auto CopyBuffered(const std::span<std::byte> out) noexcept -> size_t
  {
    if (position_ < buffer_start_
      || position_.get() >= buffer_start_.get() + buffered_) {
      return 0;
    }
    const auto offset
      = static_cast<size_t>(position_.get() - buffer_start_.get());
    const auto count = (std::min)(out.size(), buffered_ - offset);
    std::memcpy(
      out.data(), buffer_.Bytes().subspan(offset, count).data(), count);
    position_ += CountOf(count);
    return count;
  }

  //! Fills the buffer from the cursor.
  auto Fill() noexcept -> Result<void>
  {
    if (buffer_.Empty()) {
      auto buffer = ByteBuffer::Allocate(CountOf(buffer_size_));
      if (!buffer) {
        return ::oxygen::Err(buffer.error());
      }
      buffer_ = std::move(*buffer);
    }
    buffered_ = 0;
    const auto read = region_.ReadAt(position_, buffer_.Bytes());
    if (!read) {
      return ::oxygen::Err(read.error());
    }
    buffer_start_ = position_;
    buffered_ = static_cast<size_t>(read->get());
    return {};
  }

  Region<D> region_;
  //! The region is contiguous: reads skip the buffer.
  bool direct_;
  size_t buffer_size_;
  ByteBuffer buffer_;
  ByteOffset buffer_start_ { 0 };
  size_t buffered_ { 0 };
  ByteOffset position_ { 0 };
};

//! A read stream over the whole of `device` at its current size.
/*!
 The one-call spelling of a region stream over a whole device, which a reader
 of a whole file needs:

 ```cpp
 auto file = NativeFile::Open(path, {});
 if (!file) { return Err(file.error()); }
 auto stream = OpenReadStream(*file);
 if (!stream) { return Err(stream.error()); }
 Reader<RegionReadStream<NativeFile>> reader(*stream);
 ```

 @return The stream, or the device's error when its size cannot be read.
*/
template <ReadableDevice D>
[[nodiscard]] auto OpenReadStream(const D& device,
  const ByteCount buffer_size
  = RegionReadStream<D>::kDefaultBufferSize) noexcept
  -> Result<RegionReadStream<D>>
{
  auto region = Region<D>::Whole(device);
  if (!region) {
    return ::oxygen::Err(region.error());
  }
  return ::oxygen::Ok(RegionReadStream<D>(*region, buffer_size));
}

//! A buffered output stream that writes a device from a start offset.
/*!
 Writes collect in a buffer, allocated on the first write, and reach the
 device with `WriteAt` when it fills and on `Flush()`; a write at least as
 large as the buffer goes straight to the device. `Position()` counts the
 bytes written since `start`. `PatchAt` overwrites bytes already written,
 buffered or not, without moving the cursor. A write whose device offsets
 would pass the largest offset fails with `value_too_large` and writes
 nothing.

 `Flush()` hands bytes to the device only; durability is the device's
 `Flush(Durability)`. A stream destroyed with buffered bytes flushes them and
 logs an error when that fails: call `Flush()` to see the error.

 The device outlives the stream.

 @tparam D The device type.
*/
template <WritableDevice D> class RegionWriteStream {
public:
  //! The buffer size by default.
  static constexpr ByteCount kDefaultBufferSize { 1024ULL * 1024 };

  explicit RegionWriteStream(D& device,
    const ByteOffset start = ByteOffset { 0 },
    const ByteCount buffer_size = kDefaultBufferSize) noexcept
    : device_(&device)
    , start_(start)
    , buffer_size_(static_cast<size_t>(buffer_size.get()))
  {
  }

  ~RegionWriteStream() noexcept
  {
    if (device_ != nullptr && buffered_ != 0) {
      if (const auto flushed = Flush(); !flushed) {
        LOG_F(ERROR, "serio: unflushed stream bytes lost: {}",
          flushed.error().message());
      }
    }
  }

  OXYGEN_MAKE_NON_COPYABLE(RegionWriteStream)

  RegionWriteStream(RegionWriteStream&& other) noexcept
    : device_(std::exchange(other.device_, nullptr))
    , start_(other.start_)
    , buffer_size_(other.buffer_size_)
    , buffer_(std::move(other.buffer_))
    , flushed_(std::exchange(other.flushed_, 0))
    , buffered_(std::exchange(other.buffered_, 0))
  {
  }

  auto operator=(RegionWriteStream&&) -> RegionWriteStream& = delete;

  [[nodiscard]] auto Write(const std::span<const std::byte> in) noexcept
    -> Result<void>
  {
    if (in.empty()) {
      return {};
    }
    if (const auto range = ByteRange::Make(
          start_ + ByteCount { Position().get() }, CountOf(in.size()));
      !range) {
      return ::oxygen::Err(range.error());
    }
    if (in.size() >= buffer_size_) {
      CHECK_RESULT(Flush());
      CHECK_RESULT(device_->WriteAt(start_ + CountOf(flushed_), in));
      flushed_ += in.size();
      return {};
    }
    if (buffer_.Empty()) {
      auto buffer = ByteBuffer::Allocate(CountOf(buffer_size_));
      if (!buffer) {
        return ::oxygen::Err(buffer.error());
      }
      buffer_ = std::move(*buffer);
    }
    if (in.size() > buffer_size_ - buffered_) {
      CHECK_RESULT(Flush());
    }
    std::memcpy(
      buffer_.Bytes().subspan(buffered_).data(), in.data(), in.size());
    buffered_ += in.size();
    return {};
  }

  [[nodiscard]] auto Flush() noexcept -> Result<void>
  {
    if (buffered_ == 0) {
      return {};
    }
    CHECK_RESULT(device_->WriteAt(
      start_ + CountOf(flushed_), buffer_.Bytes().first(buffered_)));
    flushed_ += buffered_;
    buffered_ = 0;
    return {};
  }

  [[nodiscard]] auto Position() const noexcept -> ByteOffset
  {
    return ByteOffset { flushed_ + buffered_ };
  }

  //! Overwrites written bytes at `at` with `in`, without moving the cursor;
  //! `kOutOfBounds` when they pass `Position()`.
  [[nodiscard]] auto PatchAt(
    const ByteOffset at, std::span<const std::byte> in) noexcept -> Result<void>
  {
    const auto end = Position().get();
    if (at.get() > end || in.size() > end - at.get()) {
      return ::oxygen::Err(make_error_code(Error::kOutOfBounds));
    }
    auto offset = static_cast<size_t>(at.get());
    if (offset < flushed_) {
      const auto count = (std::min)(in.size(), flushed_ - offset);
      CHECK_RESULT(device_->WriteAt(start_ + CountOf(offset), in.first(count)));
      in = in.subspan(count);
      offset += count;
    }
    if (!in.empty()) {
      std::memcpy(buffer_.Bytes().subspan(offset - flushed_).data(), in.data(),
        in.size());
    }
    return {};
  }

  //! The device the stream writes.
  [[nodiscard]] auto Device() noexcept -> D& { return *device_; }

private:
  D* device_;
  ByteOffset start_;
  size_t buffer_size_;
  ByteBuffer buffer_;
  //! Bytes written to the device, from `start_`.
  size_t flushed_ { 0 };
  //! Bytes in the buffer, after `flushed_`.
  size_t buffered_ { 0 };
};

} // namespace oxygen::serio
