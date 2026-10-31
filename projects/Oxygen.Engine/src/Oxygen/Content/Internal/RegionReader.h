//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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

namespace oxygen::content::internal {

//! The erased reader a loader is handed for one region of a device: an asset
//! descriptor, a resource table row, a payload region.
/*!
 Positions are relative to the region's start, and every read is bounded by
 the region. A region of a contiguous device (a mapped pak, memory) is read
 by copies from its view, with no stream buffer; any other gets a buffer no
 larger than the region.

 The reader either views a region of a device that outlives it, or owns a
 device and reads all of it (a loose descriptor file, cooked bytes).

 @tparam D The device type.
*/
template <serio::ReadableDevice D>
class RegionReader final : public serio::AnyReader {
public:
  //! Reads `region`; its device outlives the reader.
  explicit RegionReader(const serio::Region<D> region) noexcept
    : stream_(region, BufferSize(region))
    , reader_(stream_)
  {
  }

  //! Reads all of `device`, which the reader owns.
  /*!
   @throw std::system_error When the size of `device` cannot be read.
  */
  [[nodiscard]] static auto Owning(D device) -> std::unique_ptr<RegionReader>
  {
    return std::unique_ptr<RegionReader>(new RegionReader(std::move(device)));
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

  [[nodiscard]] auto Remaining() noexcept -> serio::ByteCount override
  {
    return reader_.Remaining();
  }

  [[nodiscard]] auto AlignTo(const size_t alignment) noexcept
    -> Result<void> override
  {
    return reader_.AlignTo(alignment);
  }

  [[nodiscard]] auto ScopedAlignment(const uint16_t alignment)
    -> serio::AlignmentGuard override
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
  using Stream = serio::RegionReadStream<D>;

  explicit RegionReader(D device)
    : owned_(std::move(device))
    , stream_(WholeOf(*owned_), BufferSize(WholeOf(*owned_)))
    , reader_(stream_)
  {
  }

  static auto WholeOf(const D& device) -> serio::Region<D>
  {
    auto whole = serio::Region<D>::Whole(device);
    if (!whole) {
      throw std::system_error(whole.error(), "size of a content region");
    }
    return *whole;
  }

  static auto BufferSize(const serio::Region<D>& region) noexcept
    -> serio::ByteCount
  {
    return (std::min)(region.Range().count, Stream::kDefaultBufferSize);
  }

  //! Declared first: the stream views it.
  std::optional<D> owned_;
  Stream stream_;
  serio::Reader<Stream> reader_;
};

} // namespace oxygen::content::internal
