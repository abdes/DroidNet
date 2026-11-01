//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/RegionReader.h

#include <array>
#include <concepts>
#include <cstddef>
#include <span>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/ByteView.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/RegionReader.h>
#include <Oxygen/Serio/Testing/FaultInjectingDevice.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::serio::AnyReader;
using oxygen::serio::ByteCount;
using oxygen::serio::ByteOffset;
using oxygen::serio::ByteRange;
using oxygen::serio::ByteView;
using oxygen::serio::Error;
using oxygen::serio::MemoryFile;
using oxygen::serio::Region;
using oxygen::serio::RegionReader;
using oxygen::serio::testing::BytesEqual;
using oxygen::serio::testing::FaultInjectingDevice;
using oxygen::serio::testing::PatternBytes;

using SlowDevice = FaultInjectingDevice<MemoryFile>;

static_assert(std::derived_from<RegionReader<ByteView>, AnyReader>);

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
  return SlowDevice(MemoryFile::FromBytes(bytes).value());
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
  EXPECT_EQ(past_end.error(), Error::kTooLarge);
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

  ASSERT_HAS_VALUE(reader);
  const auto read = (*reader)->ReadBlob(bytes.size());

  ASSERT_HAS_VALUE(read);
  EXPECT_THAT(*read, BytesEqual(bytes));
  EXPECT_EQ((*reader)->Remaining(), ByteCount { 0 });
}

//! An owning reader of a device whose size cannot be read reports that error
//! instead of throwing.
NOLINT_TEST(RegionReaderTest, OwningReportsTheDevicesSizeError)
{
  //! Fails to report its size, as a file on a lost network share does.
  struct UnsizedDevice {
    [[nodiscard]] auto Size() const noexcept -> oxygen::Result<ByteCount>
    {
      return ::oxygen::Err(make_error_code(Error::kIo));
    }
    [[nodiscard]] auto ReadAt(ByteOffset /*at*/,
      std::span<std::byte> /*out*/) const noexcept -> oxygen::Result<ByteCount>
    {
      return ::oxygen::Ok(ByteCount { 0 });
    }
  };

  const auto reader = RegionReader<UnsizedDevice>::Owning(UnsizedDevice {});

  ASSERT_FALSE(reader);
  EXPECT_EQ(reader.error(), Error::kIo);
}

//! The reader is usable through the AnyReader interface.
NOLINT_TEST(RegionReaderTest, ReadsThroughTheAnyReaderInterface)
{
  const auto bytes = PatternBytes(64);
  const auto device = ByteView(bytes);
  auto reader = RegionReader<ByteView>(Middle(device));
  AnyReader& any = reader;

  const auto read = any.ReadBlob(8);

  ASSERT_HAS_VALUE(read);
  EXPECT_THAT(*read, BytesEqual(std::span(bytes).subspan(16, 8)));
  EXPECT_EQ(any.Remaining(), ByteCount { 24 });
}

} // namespace
