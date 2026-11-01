//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/AlignmentGuard.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/RegionStream.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::serio {

//! An `AnyWriter` of a device from a start offset.
/*!
 Writes go through a `RegionWriteStream`, so they collect in its buffer and
 reach the device when it fills and on `Flush()`. The writer either writes a
 device that outlives it, or owns its device:

 ```cpp
 auto writer = RegionWriter<MemoryFile>::Owning(MemoryFile {});
 if (!writer) { return Err(writer.error()); }
 CHECK_RESULT(Store(**writer, header));
 CHECK_RESULT((*writer)->Flush());
 const auto bytes = (*writer)->Device().Bytes();
 ```

 It is neither copyable nor movable: its writer refers to the stream beside
 it. An owning writer declares its device first, so the stream, which
 flushes what it still buffers, is destroyed before the device.

 @tparam D The device type.
*/
template <WritableDevice D> class RegionWriter final : public AnyWriter {
public:
  //! Writes `device` from `start`; the device outlives the writer.
  explicit RegionWriter(
    D& device, const ByteOffset start = ByteOffset { 0 }) noexcept
    : stream_(device, start)
    , writer_(stream_)
  {
  }

  //! Writes `device`, which the writer owns, from `start`.
  /*!
   @return The writer; `not_enough_memory` when it cannot be allocated.
  */
  [[nodiscard]] static auto Owning(
    D device, const ByteOffset start = ByteOffset { 0 }) noexcept
    -> Result<std::unique_ptr<RegionWriter>>
  {
    auto owned = std::unique_ptr<D>(new (std::nothrow) D(std::move(device)));
    if (!owned) {
      return ::oxygen::Err(std::make_error_code(std::errc::not_enough_memory));
    }
    auto writer = std::unique_ptr<RegionWriter>(
      new (std::nothrow) RegionWriter(std::move(owned), start));
    if (!writer) {
      return ::oxygen::Err(std::make_error_code(std::errc::not_enough_memory));
    }
    return ::oxygen::Ok(std::move(writer));
  }

  ~RegionWriter() override = default;

  OXYGEN_MAKE_NON_COPYABLE(RegionWriter)
  OXYGEN_MAKE_NON_MOVABLE(RegionWriter)

  [[nodiscard]] auto WriteBlob(const std::span<const std::byte> blob) noexcept
    -> Result<void> override
  {
    return writer_.WriteBlob(blob);
  }

  [[nodiscard]] auto Position() const noexcept -> Result<size_t> override
  {
    return writer_.Position();
  }

  [[nodiscard]] auto AlignTo(const size_t alignment) noexcept
    -> Result<void> override
  {
    return writer_.AlignTo(alignment);
  }

  [[nodiscard]] auto ScopedAlignment(const uint16_t alignment)
    -> AlignmentGuard override
  {
    return writer_.ScopedAlignment(alignment);
  }

  [[nodiscard]] auto Flush() noexcept -> Result<void> override
  {
    return writer_.Flush();
  }

  //! The stream the writer writes, for positional patches.
  [[nodiscard]] auto Stream() noexcept -> RegionWriteStream<D>&
  {
    return stream_;
  }

  //! The device written.
  [[nodiscard]] auto Device() noexcept -> D& { return stream_.Device(); }

private:
  RegionWriter(std::unique_ptr<D> owned, const ByteOffset start) noexcept
    : owned_(std::move(owned))
    , stream_(*owned_, start)
    , writer_(stream_)
  {
  }

  //! Declared first, so it is destroyed last: the stream writes it.
  std::unique_ptr<D> owned_;
  RegionWriteStream<D> stream_;
  Writer<RegionWriteStream<D>> writer_;
};

} // namespace oxygen::serio
