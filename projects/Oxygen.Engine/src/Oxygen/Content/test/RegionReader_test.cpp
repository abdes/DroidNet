//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
// Covers: Content/Internal/RegionReader.h, Content/Internal/BlockingReads.h

#include <array>
#include <cstddef>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Content/Internal/BlockingReads.h>
#include <Oxygen/Content/Internal/RegionReader.h>
#include <Oxygen/Serio/ByteView.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/Testing/FaultInjectingDevice.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::internal::BlockingReads;
using oxygen::content::internal::RegionReader;
using oxygen::serio::ByteCount;
using oxygen::serio::ByteOffset;
using oxygen::serio::ByteRange;
using oxygen::serio::ByteView;
using oxygen::serio::Region;
using oxygen::serio::testing::BytesEqual;
using oxygen::serio::testing::FaultInjectingDevice;
using oxygen::serio::testing::PatternBytes;

using SlowDevice = FaultInjectingDevice<oxygen::serio::MemoryFile>;

//! The 32 bytes at offset 16 of `device`.
template <class D> auto Middle(const D& device) -> Region<D>
{
  return Region<D>::Make(device,
    ByteRange { .offset = ByteOffset { 16 }, .count = ByteCount { 32 } })
    .value();
}

//! A device without a view, so its region is read through a stream buffer.
auto Slow(const std::span<const std::byte> bytes) -> SlowDevice
{
  return SlowDevice(oxygen::serio::MemoryFile::FromBytes(bytes).value());
}

//! Positions start at the region's start, whatever its offset in the device.
NOLINT_TEST(RegionReaderTest, PositionsAreRelativeToTheRegion)
{
  const auto bytes = PatternBytes(64);
  const auto device = ByteView(bytes);
  auto reader = RegionReader<ByteView>(Middle(device));

  EXPECT_EQ(reader.Position().value(), 0U);
  EXPECT_EQ(reader.Remaining(), ByteCount { 32 });
  ASSERT_HAS_VALUE(reader.Seek(8));
  const auto read = reader.ReadBlob(4);

  ASSERT_HAS_VALUE(read);
  EXPECT_THAT(*read, BytesEqual(std::span(bytes).subspan(24, 4)));
  EXPECT_EQ(reader.Position().value(), 12U);
}

//! No read reaches past the region, though the device holds more bytes.
NOLINT_TEST(RegionReaderTest, ReadsStopAtTheRegionEnd)
{
  const auto bytes = PatternBytes(64);
  const auto device = ByteView(bytes);
  auto reader = RegionReader<ByteView>(Middle(device));
  ASSERT_HAS_VALUE(reader.Seek(30));

  const auto past_end = reader.ReadBlob(4);
  const auto seek_past_end = reader.Seek(33);

  ASSERT_FALSE(past_end.has_value());
  EXPECT_EQ(past_end.error(), oxygen::serio::Error::kTooLarge);
  EXPECT_FALSE(seek_past_end.has_value());
}

//! A device without a view reads the same bytes, through a buffer.
NOLINT_TEST(RegionReaderTest, ReadsADeviceWithoutAView)
{
  const auto bytes = PatternBytes(64);
  const auto device = Slow(bytes);
  auto reader = RegionReader<SlowDevice>(Middle(device));
  auto out = std::array<std::byte, 32> {};

  ASSERT_HAS_VALUE(reader.ReadBlobInto(out));

  EXPECT_THAT(out, BytesEqual(std::span(bytes).subspan(16, 32)));
}

//! An owning reader reads all of the device it keeps.
NOLINT_TEST(RegionReaderTest, OwningReaderReadsAllOfItsDevice)
{
  const auto bytes = PatternBytes(48);
  const auto reader = RegionReader<SlowDevice>::Owning(Slow(bytes));

  const auto read = reader->ReadBlob(bytes.size());

  ASSERT_HAS_VALUE(read);
  EXPECT_THAT(*read, BytesEqual(bytes));
  EXPECT_EQ(reader->Remaining(), ByteCount { 0 });
}

//! An awaited read of the adapter is ready at once, with the blocking read's
//! result.
NOLINT_TEST(BlockingReadsTest, AwaitedReadsCompleteAtOnce)
{
  const auto bytes = PatternBytes(64);
  const auto device = BlockingReads(Slow(bytes));
  auto out = std::array<std::byte, 8> {};

  auto read = device.AsyncReadAt(ByteOffset { 60 }, out);

  ASSERT_TRUE(read.await_ready());
  const auto result = read.await_resume();
  ASSERT_HAS_VALUE(result);
  EXPECT_EQ(*result, ByteCount { 4 });
  EXPECT_THAT(std::span(out).first(4), BytesEqual(std::span(bytes).last(4)));
}

//! A failed blocking read is the awaited read's failure.
NOLINT_TEST(BlockingReadsTest, AwaitedReadsReportTheDeviceError)
{
  const auto bytes = PatternBytes(16);
  auto slow = Slow(bytes);
  slow.FailOn(oxygen::serio::testing::FaultOperation::kRead, 0,
    std::make_error_code(std::errc::io_error));
  const auto device = BlockingReads(std::move(slow));
  auto out = std::array<std::byte, 4> {};

  auto read = device.AsyncReadAt(ByteOffset { 0 }, out);

  ASSERT_TRUE(read.await_ready());
  EXPECT_FALSE(read.await_resume().has_value());
}

} // namespace
