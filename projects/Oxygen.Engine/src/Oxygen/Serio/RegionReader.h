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
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/AlignmentGuard.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/RegionStream.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

//! An `AnyReader` of a region of a device.
/*!
 Positions are relative to the region's start, and every read is bounded by
 the region. A region of a contiguous device (a mapped file, memory) is read
 by copies from its view, with no stream buffer; any other is read through a
 buffer no larger than the region.

 The reader either views a region of a device that outlives it, or owns a
 device and reads all of it (a descriptor file, bytes held in memory):

 ```cpp
 auto file = NativeFile::Open(path, {});
 if (!file) { return Err(file.error()); }
 auto reader = RegionReader<NativeFile>::Owning(std::move(*file));
 if (!reader) { return Err(reader.error()); }
 CHECK_RESULT(Load(**reader, header));
 ```

 It is neither copyable nor movable: its reader refers to the stream beside
 it.

 @tparam D The device type.
*/
template <ReadableDevice D> class RegionReader final : public AnyReader {
public:
  //! Reads `region`; its device outlives the reader.
  explicit RegionReader(const Region<D> region) noexcept
    : stream_(region)
    , reader_(stream_)
  {
  }

  //! Reads all of `device`, which the reader owns.
  /*!
   @return The reader; the device's size error, or `not_enough_memory`.
  */
  [[nodiscard]] static auto Owning(D device) noexcept
    -> Result<std::unique_ptr<RegionReader>>
  {
    auto owned = std::unique_ptr<D>(new (std::nothrow) D(std::move(device)));
    if (!owned) {
      return ::oxygen::Err(std::make_error_code(std::errc::not_enough_memory));
    }
    const auto whole = Region<D>::Whole(*owned);
    if (!whole) {
      return ::oxygen::Err(whole.error());
    }
    auto reader = std::unique_ptr<RegionReader>(
      new (std::nothrow) RegionReader(std::move(owned), *whole));
    if (!reader) {
      return ::oxygen::Err(std::make_error_code(std::errc::not_enough_memory));
    }
    return ::oxygen::Ok(std::move(reader));
  }

  ~RegionReader() override = default;

  OXYGEN_MAKE_NON_COPYABLE(RegionReader)
  OXYGEN_MAKE_NON_MOVABLE(RegionReader)

  [[nodiscard]] auto ReadBlob(const size_t size) noexcept
    -> Result<std::vector<std::byte>> override
  {
    return reader_.ReadBlob(size);
  }

  [[nodiscard]] auto ReadBlobInto(const std::span<std::byte> buffer) noexcept
    -> Result<void> override
  {
    return reader_.ReadBlobInto(buffer);
  }

  [[nodiscard]] auto Position() noexcept -> Result<size_t> override
  {
    return reader_.Position();
  }

  [[nodiscard]] auto Remaining() noexcept -> ByteCount override
  {
    return reader_.Remaining();
  }

  [[nodiscard]] auto AlignTo(const size_t alignment) noexcept
    -> Result<void> override
  {
    return reader_.AlignTo(alignment);
  }

  [[nodiscard]] auto ScopedAlignment(const uint16_t alignment)
    -> AlignmentGuard override
  {
    return reader_.ScopedAlignment(alignment);
  }

  [[nodiscard]] auto Forward(const size_t count) noexcept
    -> Result<void> override
  {
    return reader_.Forward(count);
  }

  [[nodiscard]] auto Seek(const size_t position) noexcept
    -> Result<void> override
  {
    return reader_.Seek(position);
  }

private:
  using Stream = RegionReadStream<D>;

  RegionReader(std::unique_ptr<D> owned, const Region<D> whole) noexcept
    : owned_(std::move(owned))
    , stream_(whole)
    , reader_(stream_)
  {
  }

  //! Declared first, so it is destroyed last: the stream views it.
  std::unique_ptr<D> owned_;
  Stream stream_;
  Reader<Stream> reader_;
};

} // namespace oxygen::serio
