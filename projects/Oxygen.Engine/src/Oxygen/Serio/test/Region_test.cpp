//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/Region.h

#include <cstddef>
#include <cstdint>
#include <span>
#include <system_error>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/ByteView.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/Testing/FaultInjectingDevice.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::serio::AnyDevice;
using oxygen::serio::ByteCount;
using oxygen::serio::ByteOffset;
using oxygen::serio::ByteRange;
using oxygen::serio::ByteView;
using oxygen::serio::Error;
using oxygen::serio::MemoryFile;
using oxygen::serio::Region;
using oxygen::serio::testing::BytesEqual;
using oxygen::serio::testing::FaultInjectingDevice;
using oxygen::serio::testing::PatternBytes;

auto Range(const uint64_t offset, const uint64_t count) -> ByteRange
{
  return { .offset = ByteOffset { offset }, .count = ByteCount { count } };
}

//! Reads through a region are covered by the device conformance suite, which
//! runs every device as a region too; these cases cover making regions and
//! their views.
class RegionTest : public ::testing::Test {
protected:
  const std::vector<std::byte> bytes_ = PatternBytes(1000);
  const ByteView device_ { bytes_ };
};

//! Whole covers the device at its current size.
NOLINT_TEST_F(RegionTest, WholeCoversDevice)
{
  const auto region = Region<ByteView>::Whole(device_);

  ASSERT_TRUE(region);
  EXPECT_EQ(region->Range().offset.get(), 0U);
  EXPECT_EQ(region->Range().count.get(), bytes_.size());
}

//! A range reaching the device's end is a region; one past it is refused.
NOLINT_TEST_F(RegionTest, MakeChecksDeviceBounds)
{
  const auto to_end = Region<ByteView>::Make(device_, Range(900, 100));
  const auto past_end = Region<ByteView>::Make(device_, Range(900, 101));

  EXPECT_TRUE(to_end);
  ASSERT_FALSE(past_end);
  EXPECT_EQ(past_end.error(), Error::kOutOfBounds);
}

//! A sub-region's offsets are relative to its parent, and it views the same
//! device; one past its parent is refused.
NOLINT_TEST_F(RegionTest, SubIsRelativeToParent)
{
  const auto parent = Region<ByteView>::Make(device_, Range(100, 500));
  ASSERT_TRUE(parent);

  const auto child = parent->Sub(Range(50, 20));
  const auto outside = parent->Sub(Range(490, 11));

  ASSERT_TRUE(child);
  EXPECT_EQ(child->Range().offset.get(), 150U);
  EXPECT_EQ(&child->Device(), &device_);
  EXPECT_THAT(child->Bytes(), BytesEqual(std::span(bytes_).subspan(150, 20)));
  ASSERT_FALSE(outside);
  EXPECT_EQ(outside.error(), Error::kOutOfBounds);
}

//! A contiguous device's region views its slice of the device's bytes.
NOLINT_TEST_F(RegionTest, ContiguousRegionViewsSlice)
{
  const auto region = Region<ByteView>::Make(device_, Range(10, 30));
  ASSERT_TRUE(region);

  const auto view = region->Contiguous();

  ASSERT_HAS_VALUE(view) << "the region should be contiguous";
  EXPECT_EQ(view->data(), std::span(bytes_).subspan(10).data());
  EXPECT_EQ(view->size(), 30U);
}

//! An erased contiguous device keeps its view at run time; a device that is
//! not contiguous has none.
NOLINT_TEST_F(RegionTest, ContiguityIsKnownThroughErasure)
{
  const auto erased = AnyDevice(ByteView(bytes_));
  const auto opaque
    = FaultInjectingDevice<MemoryFile>(MemoryFile::FromBytes(bytes_).value());
  const auto erased_region = Region<AnyDevice>::Make(erased, Range(10, 30));
  const auto opaque_region
    = Region<FaultInjectingDevice<MemoryFile>>::Whole(opaque);
  ASSERT_TRUE(erased_region);
  ASSERT_TRUE(opaque_region);

  const auto erased_view = erased_region->Contiguous();

  ASSERT_HAS_VALUE(erased_view) << "the erased region should be contiguous";
  EXPECT_EQ(erased_view->data(), std::span(bytes_).subspan(10).data());
  EXPECT_FALSE(opaque_region->Contiguous().has_value());
}

//! A failing device fails region creation with the device's own error.
NOLINT_TEST_F(RegionTest, DeviceErrorsPropagate)
{
  struct UnsizedDevice {
    [[nodiscard]] auto Size() const noexcept -> oxygen::Result<ByteCount>
    {
      return ::oxygen::Err(std::make_error_code(std::errc::io_error));
    }
    [[nodiscard]] auto ReadAt(ByteOffset /*at*/,
      std::span<std::byte> /*out*/) const noexcept -> oxygen::Result<ByteCount>
    {
      return ::oxygen::Ok(ByteCount { 0 });
    }
  };
  const auto device = UnsizedDevice {};

  const auto region = Region<UnsizedDevice>::Whole(device);

  ASSERT_FALSE(region);
  EXPECT_EQ(region.error(), Error::kIo);
}

} // namespace
