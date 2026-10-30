//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

//! A bounded, non-owning view of a device: a section, a payload, a record.
/*!
 A region is a device and a byte range of it, cheap to copy. Its offsets are
 relative to its start and its reads are bounds-checked against the region,
 not the device: a read across the region's end returns the bytes inside it.
 A region satisfies `ReadableDevice` itself, so a stream over a region and a
 hash of a region are the same code as over a device; a region of a region is
 a `Region<D>` again (`Sub`).

 Like `std::span`, a region does not own: the device outlives it.

 @tparam D The device type.
*/
template <ReadableDevice D> class Region {
public:
  //! The region `range` of `device`; `kOutOfBounds` when the range does not
  //! lie within the device's current size.
  [[nodiscard]] static auto Make(
    const D& device, const ByteRange range) noexcept -> Result<Region>
  {
    const auto size = device.Size();
    if (!size) {
      return ::oxygen::Err(size.error());
    }
    const auto whole = ByteRange { .offset = ByteOffset { 0 }, .count = *size };
    if (!whole.Contains(range)) {
      return ::oxygen::Err(make_error_code(Error::kOutOfBounds));
    }
    return ::oxygen::Ok(Region(device, range));
  }

  //! The region covering all of `device` at its current size.
  [[nodiscard]] static auto Whole(const D& device) noexcept -> Result<Region>
  {
    const auto size = device.Size();
    if (!size) {
      return ::oxygen::Err(size.error());
    }
    return ::oxygen::Ok(
      Region(device, ByteRange { .offset = ByteOffset { 0 }, .count = *size }));
  }

  //! The device the region views.
  [[nodiscard]] auto Device() const noexcept -> const D& { return *device_; }

  //! The region's range within its device.
  [[nodiscard]] auto Range() const noexcept -> ByteRange { return range_; }

  //! The region `relative` within this one, as a region of the same device;
  //! `kOutOfBounds` when it does not lie within this region.
  [[nodiscard]] auto Sub(const ByteRange relative) const noexcept
    -> Result<Region>
  {
    const auto whole
      = ByteRange { .offset = ByteOffset { 0 }, .count = range_.count };
    if (!whole.Contains(relative)) {
      return ::oxygen::Err(make_error_code(Error::kOutOfBounds));
    }
    return ::oxygen::Ok(Region(*device_,
      ByteRange {
        .offset = range_.offset + ByteCount { relative.offset.get() },
        .count = relative.count,
      }));
  }

  //! The region's size, which never fails.
  [[nodiscard]] auto Size() const noexcept -> Result<ByteCount>
  {
    return ::oxygen::Ok(range_.count);
  }

  //! Reads up to `out.size()` bytes at `relative`; fewer when the region ends
  //! first, none at or past its end.
  [[nodiscard]] auto ReadAt(const ByteOffset relative,
    const std::span<std::byte> out) const noexcept -> Result<ByteCount>
  {
    if (relative.get() >= range_.count.get()) {
      return ::oxygen::Ok(ByteCount { 0 });
    }
    const auto remaining = range_.count.get() - relative.get();
    const auto count
      = static_cast<size_t>((std::min)(uint64_t { out.size() }, remaining));
    return device_->ReadAt(
      range_.offset + ByteCount { relative.get() }, out.first(count));
  }

  //! The region's bytes, a subspan of a contiguous device's view.
  [[nodiscard]] auto Bytes() const noexcept -> std::span<const std::byte>
    requires ContiguousDevice<D>
  {
    return device_->Bytes().subspan(static_cast<size_t>(range_.offset.get()),
      static_cast<size_t>(range_.count.get()));
  }

  //! The region's bytes when its device is contiguous, whether known from the
  //! type or, for an `AnyDevice`, at run time; empty otherwise.
  [[nodiscard]] auto Contiguous() const noexcept
    -> std::optional<std::span<const std::byte>>
  {
    if constexpr (ContiguousDevice<D>) {
      return Bytes();
    } else if constexpr (std::is_same_v<D, AnyDevice>) {
      const auto bytes = device_->Contiguous();
      if (!bytes) {
        return std::nullopt;
      }
      return bytes->subspan(static_cast<size_t>(range_.offset.get()),
        static_cast<size_t>(range_.count.get()));
    } else {
      return std::nullopt;
    }
  }

private:
  Region(const D& device, const ByteRange range) noexcept
    : device_(&device)
    , range_(range)
  {
  }

  const D* device_;
  ByteRange range_;
};

//! A region of an erased device.
using AnyRegion = Region<AnyDevice>;

static_assert(ReadableDevice<Region<AnyDevice>>);

} // namespace oxygen::serio
