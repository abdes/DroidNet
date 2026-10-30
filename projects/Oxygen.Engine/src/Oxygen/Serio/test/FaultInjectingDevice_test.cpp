//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/Testing/FaultInjectingDevice.h

#include <cstddef>
#include <system_error>
#include <vector>

#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Serio/Testing/FaultInjectingDevice.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::serio::ByteCount;
using oxygen::serio::ByteOffset;
using oxygen::serio::Durability;
using oxygen::serio::Error;
using oxygen::serio::MemoryFile;
using oxygen::serio::testing::BytesEqual;
using oxygen::serio::testing::FaultInjectingDevice;
using oxygen::serio::testing::FaultOperation;
using oxygen::serio::testing::PatternBytes;

//! The fault schedule other suites rely on: what fails, when, and what the
//! device counts. Behaving as its inner device when nothing is scheduled is
//! checked by the device conformance suite.
class FaultInjectingDeviceTest : public ::testing::Test {
protected:
  const std::vector<std::byte> bytes_ = PatternBytes(64);
  FaultInjectingDevice<MemoryFile> device_ {
    MemoryFile::FromBytes(bytes_).value()
  };
  std::vector<std::byte> out_ = std::vector<std::byte>(16);
};

//! The nth call from scheduling fails with the scheduled error, once.
NOLINT_TEST_F(FaultInjectingDeviceTest, FailsTheScheduledCallOnce)
{
  ASSERT_TRUE(device_.ReadAt(ByteOffset { 0 }, out_));
  device_.FailOn(FaultOperation::kRead, 1, make_error_code(Error::kIo));

  const auto first = device_.ReadAt(ByteOffset { 0 }, out_);
  const auto second = device_.ReadAt(ByteOffset { 0 }, out_);
  const auto third = device_.ReadAt(ByteOffset { 0 }, out_);

  EXPECT_TRUE(first);
  ASSERT_FALSE(second);
  EXPECT_EQ(second.error(), Error::kIo);
  EXPECT_TRUE(third);
}

//! Each operation has its own schedule and its own count.
NOLINT_TEST_F(FaultInjectingDeviceTest, OperationsAreScheduledApart)
{
  device_.FailOn(
    FaultOperation::kFlush, 0, std::make_error_code(std::errc::io_error));

  const auto read = device_.ReadAt(ByteOffset { 0 }, out_);
  const auto write = device_.WriteAt(ByteOffset { 0 }, out_);
  const auto flush = device_.Flush(Durability::kStable);

  EXPECT_TRUE(read);
  EXPECT_TRUE(write);
  EXPECT_FALSE(flush);
  EXPECT_EQ(device_.CallCount(FaultOperation::kRead), 1U);
  EXPECT_EQ(device_.CallCount(FaultOperation::kWrite), 1U);
  EXPECT_EQ(device_.CallCount(FaultOperation::kFlush), 1U);
  EXPECT_EQ(device_.CallCount(FaultOperation::kReserve), 0U);
}

//! Limited reads return at most the limit, starting at the right offset.
NOLINT_TEST_F(FaultInjectingDeviceTest, LimitReadsShortensEveryRead)
{
  device_.LimitReads(ByteCount { 5 });

  const auto read = device_.ReadAt(ByteOffset { 10 }, out_);

  ASSERT_TRUE(read);
  ASSERT_EQ(read->get(), 5U);
  EXPECT_THAT(
    std::span(out_).first(5), BytesEqual(std::span(bytes_).subspan(10, 5)));
}

//! Writes and reservations past the capacity fail with kNoSpace; within it
//! they succeed.
NOLINT_TEST_F(FaultInjectingDeviceTest, LimitCapacityIsAFullDisk)
{
  device_.LimitCapacity(ByteCount { 100 });

  const auto within = device_.WriteAt(ByteOffset { 84 }, out_);
  const auto past = device_.WriteAt(ByteOffset { 85 }, out_);
  const auto reserve = device_.Reserve(ByteCount { 101 });

  EXPECT_TRUE(within);
  ASSERT_FALSE(past);
  EXPECT_EQ(past.error(), Error::kNoSpace);
  ASSERT_FALSE(reserve);
  EXPECT_EQ(reserve.error(), Error::kNoSpace);
}

} // namespace
