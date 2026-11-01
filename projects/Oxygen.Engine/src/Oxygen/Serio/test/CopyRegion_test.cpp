//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/CopyRegion.h, Serio/AsyncDevice.h

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include "Support/DeviceFactories.h"
#include <asio/io_context.hpp>

#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/asio.h> // IWYU pragma: keep (EventLoopTraits)
#include <Oxygen/Serio/CopyRegion.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/Testing/FaultInjectingDevice.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::serio::ByteCount;
using oxygen::serio::ByteOffset;
using oxygen::serio::ByteRange;
using oxygen::serio::CopyRegion;
using oxygen::serio::Error;
using oxygen::serio::MemoryFile;
using oxygen::serio::Region;
using oxygen::serio::test::Required;
using oxygen::serio::testing::BytesEqual;
using oxygen::serio::testing::FaultInjectingDevice;
using oxygen::serio::testing::FaultOperation;
using oxygen::serio::testing::PatternBytes;
namespace co = oxygen::co;

//! Copies between memory devices: their operations complete at once, so
//! these cases check the chunking and error handling, not overlap.
class CopyRegionTest : public ::testing::Test {
protected:
  static constexpr size_t kSize = 1000;

  [[nodiscard]] auto Source() const -> const MemoryFile& { return source_; }
  [[nodiscard]] auto Bytes() const -> const std::vector<std::byte>&
  {
    return bytes_;
  }

  [[nodiscard]] auto RegionOf(const ByteRange range) const -> Region<MemoryFile>
  {
    return Required(Region<MemoryFile>::Make(source_, range), "region");
  }

  template <class Task> auto Run(Task task)
  {
    return co::Run(loop_, std::move(task));
  }

private:
  asio::io_context loop_;
  std::vector<std::byte> bytes_ = PatternBytes(kSize);
  MemoryFile source_ = Required(MemoryFile::FromBytes(bytes_), "source");
};

//! A copy through scratch smaller than the region writes every chunk at its
//! offset, the last one short.
NOLINT_TEST_F(CopyRegionTest, CopiesInChunksAtTheTargetOffset)
{
  const auto range
    = ByteRange { .offset = ByteOffset { 17 }, .count = ByteCount { 950 } };
  auto target = MemoryFile {};
  auto scratch = std::vector<std::byte>(size_t { 2 } * 64);

  const auto copied
    = Run(CopyRegion(RegionOf(range), target, ByteOffset { 5 }, scratch));

  ASSERT_TRUE(copied);
  ASSERT_EQ(target.Bytes().size(), 955U);
  EXPECT_THAT(
    target.Bytes().subspan(5), BytesEqual(std::span(Bytes()).subspan(17, 950)));
}

//! The allocating form copies a region smaller than one chunk.
NOLINT_TEST_F(CopyRegionTest, AllocatingFormCopiesSmallRegion)
{
  const auto range
    = ByteRange { .offset = ByteOffset { 0 }, .count = ByteCount { kSize } };
  auto target = MemoryFile {};

  const auto copied
    = Run(CopyRegion(RegionOf(range), target, ByteOffset { 0 }));

  ASSERT_TRUE(copied);
  EXPECT_THAT(target.Bytes(), BytesEqual(Bytes()));
}

//! An empty region copies nothing and succeeds.
NOLINT_TEST_F(CopyRegionTest, EmptyRegionWritesNothing)
{
  const auto range
    = ByteRange { .offset = ByteOffset { 10 }, .count = ByteCount { 0 } };
  auto target = MemoryFile {};

  const auto copied
    = Run(CopyRegion(RegionOf(range), target, ByteOffset { 0 }));

  ASSERT_TRUE(copied);
  EXPECT_TRUE(target.Bytes().empty());
}

//! Scratch that cannot hold two chunks is refused.
NOLINT_TEST_F(CopyRegionTest, ScratchSmallerThanTwoBytesIsInvalid)
{
  const auto range
    = ByteRange { .offset = ByteOffset { 0 }, .count = ByteCount { 10 } };
  auto target = MemoryFile {};
  auto scratch = std::vector<std::byte>(1);

  const auto copied
    = Run(CopyRegion(RegionOf(range), target, ByteOffset { 0 }, scratch));

  ASSERT_FALSE(copied);
  EXPECT_EQ(copied.error(), std::errc::invalid_argument);
}

//! A copy within one device whose ranges overlap, in either direction, is
//! refused before anything is written; disjoint ranges of one device copy.
NOLINT_TEST_F(CopyRegionTest, OverlappingRangesOfOneDeviceAreRefused)
{
  auto file = Required(MemoryFile::FromBytes(Bytes()), "file");
  const auto head
    = ByteRange { .offset = ByteOffset { 0 }, .count = ByteCount { 3 } };
  const auto region = Required(Region<MemoryFile>::Make(file, head), "head");
  auto scratch = std::vector<std::byte>(2);

  const auto up = Run(CopyRegion(region, file, ByteOffset { 2 }, scratch));
  const auto down = Run(CopyRegion(
    Required(
      Region<MemoryFile>::Make(file,
        ByteRange { .offset = ByteOffset { 2 }, .count = ByteCount { 3 } }),
      "tail"),
    file, ByteOffset { 0 }, scratch));
  const auto unchanged = std::vector(file.Bytes().begin(), file.Bytes().end());
  const auto apart = Run(CopyRegion(region, file, ByteOffset { 500 }, scratch));

  ASSERT_FALSE(up);
  EXPECT_EQ(up.error(), std::errc::invalid_argument);
  ASSERT_FALSE(down);
  EXPECT_EQ(down.error(), std::errc::invalid_argument);
  EXPECT_THAT(unchanged, BytesEqual(Bytes()));
  ASSERT_TRUE(apart) << apart.error().message();
  EXPECT_THAT(
    file.Bytes().subspan(500, 3), BytesEqual(std::span(Bytes()).first(3)));
}

//! A destination range past the last offset is refused before anything is
//! written.
NOLINT_TEST_F(CopyRegionTest, DestinationPastTheLastOffsetIsRefused)
{
  const auto range
    = ByteRange { .offset = ByteOffset { 0 }, .count = ByteCount { 10 } };
  auto target = MemoryFile {};
  auto scratch = std::vector<std::byte>(4);

  const auto copied = Run(CopyRegion(RegionOf(range), target,
    ByteOffset { std::numeric_limits<uint64_t>::max() - 5 }, scratch));

  ASSERT_FALSE(copied);
  EXPECT_EQ(copied.error(), std::errc::value_too_large);
  EXPECT_TRUE(target.Bytes().empty());
}

//! Short reads still copy every byte.
NOLINT_TEST_F(CopyRegionTest, ShortReadsCopyEveryByte)
{
  auto source = FaultInjectingDevice<MemoryFile>(
    Required(MemoryFile::FromBytes(Bytes()), "source"));
  source.LimitReads(ByteCount { 7 });
  const auto region = Required(
    Region<FaultInjectingDevice<MemoryFile>>::Whole(source), "region");
  auto target = MemoryFile {};
  auto scratch = std::vector<std::byte>(size_t { 2 } * 100);

  const auto copied
    = Run(CopyRegion(region, target, ByteOffset { 0 }, scratch));

  ASSERT_TRUE(copied);
  EXPECT_THAT(target.Bytes(), BytesEqual(Bytes()));
}

//! A failed read ends the copy with the device's error.
NOLINT_TEST_F(CopyRegionTest, ReadErrorEndsTheCopy)
{
  auto source = FaultInjectingDevice<MemoryFile>(
    Required(MemoryFile::FromBytes(Bytes()), "source"));
  source.FailOn(
    FaultOperation::kRead, 2, std::make_error_code(std::errc::io_error));
  const auto region = Required(
    Region<FaultInjectingDevice<MemoryFile>>::Whole(source), "region");
  auto target = MemoryFile {};
  auto scratch = std::vector<std::byte>(size_t { 2 } * 100);

  const auto copied
    = Run(CopyRegion(region, target, ByteOffset { 0 }, scratch));

  ASSERT_FALSE(copied);
  EXPECT_EQ(copied.error(), std::errc::io_error);
}

//! A full target ends the copy with kNoSpace.
NOLINT_TEST_F(CopyRegionTest, FullTargetEndsTheCopy)
{
  const auto range
    = ByteRange { .offset = ByteOffset { 0 }, .count = ByteCount { kSize } };
  auto target = FaultInjectingDevice<MemoryFile>(MemoryFile {});
  target.LimitCapacity(ByteCount { 300 });
  auto scratch = std::vector<std::byte>(size_t { 2 } * 128);

  const auto copied
    = Run(CopyRegion(RegionOf(range), target, ByteOffset { 0 }, scratch));

  ASSERT_FALSE(copied);
  EXPECT_EQ(copied.error(), Error::kNoSpace);
}

} // namespace
