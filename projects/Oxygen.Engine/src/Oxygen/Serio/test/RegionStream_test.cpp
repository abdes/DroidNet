//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/RegionStream.h

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/RegionStream.h>
#include <Oxygen/Serio/Stream.h>
#include <Oxygen/Serio/Testing/FaultInjectingDevice.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedLogCapture.h>

namespace {

using oxygen::Result;
using oxygen::serio::ByteCount;
using oxygen::serio::ByteOffset;
using oxygen::serio::Error;
using oxygen::serio::MemoryFile;
using oxygen::serio::ReadExactly;
using oxygen::serio::Region;
using oxygen::serio::RegionReadStream;
using oxygen::serio::RegionWriteStream;
using oxygen::serio::testing::BytesEqual;
using oxygen::serio::testing::FaultInjectingDevice;
using oxygen::serio::testing::FaultOperation;
using oxygen::serio::testing::PatternBytes;
using oxygen::testing::ScopedLogCapture;

using CountedDevice = FaultInjectingDevice<MemoryFile>;

//! Stream contracts are in the stream conformance suite; these cases cover
//! how region streams batch device IO and report device failures, observed
//! through the calls a counting device receives.
class RegionReadStreamTest : public ::testing::Test {
protected:
  static constexpr ByteCount kBufferSize { 1024 };

  auto MakeStream() -> RegionReadStream<CountedDevice>
  {
    return RegionReadStream<CountedDevice>(
      Region<CountedDevice>::Whole(device_).value(), kBufferSize);
  }

  [[nodiscard]] auto Reads() const -> uint32_t
  {
    return device_.CallCount(FaultOperation::kRead);
  }

  auto Device() -> CountedDevice& { return device_; }
  [[nodiscard]] auto Bytes() const -> std::span<const std::byte>
  {
    return bytes_;
  }

private:
  const std::vector<std::byte> bytes_ = PatternBytes(10'000);
  CountedDevice device_ { MemoryFile::FromBytes(bytes_).value() };
};

//! Small reads inside the buffer cost one device read between them.
NOLINT_TEST_F(RegionReadStreamTest, SmallReadsShareOneDeviceRead)
{
  auto stream = MakeStream();
  auto out = std::vector<std::byte>(10);

  for (auto i = 0; i < 100; ++i) {
    ASSERT_TRUE(ReadExactly(stream, out));
  }

  EXPECT_EQ(Reads(), 1U);
}

//! A read at least as large as the buffer goes straight to the caller's span
//! in one device read.
NOLINT_TEST_F(RegionReadStreamTest, LargeReadBypassesBuffer)
{
  auto stream = MakeStream();
  auto out = std::vector<std::byte>(4 * kBufferSize.get());

  ASSERT_TRUE(ReadExactly(stream, out));

  EXPECT_EQ(Reads(), 1U);
  EXPECT_THAT(out, BytesEqual(std::span(Bytes()).first(out.size())));
}

//! Seeking within the buffered bytes reads from the buffer, not the device.
NOLINT_TEST_F(RegionReadStreamTest, SeekWithinBufferReusesIt)
{
  auto stream = MakeStream();
  auto out = std::vector<std::byte>(10);
  ASSERT_TRUE(ReadExactly(stream, out));

  ASSERT_TRUE(stream.Seek(ByteOffset { 500 }));
  ASSERT_TRUE(ReadExactly(stream, out));

  EXPECT_EQ(Reads(), 1U);
  EXPECT_THAT(out, BytesEqual(std::span(Bytes()).subspan(500, 10)));
}

//! A failed device read returns the device's error; the stream stays usable
//! and its cursor does not move.
NOLINT_TEST_F(RegionReadStreamTest, DeviceErrorLeavesStreamUsable)
{
  Device().FailOn(FaultOperation::kRead, 0, make_error_code(Error::kIo));
  auto stream = MakeStream();
  auto out = std::vector<std::byte>(10);

  const auto failed = stream.ReadSome(out);
  const auto position_after_failure = stream.Position();
  const auto retried = ReadExactly(stream, out);

  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), Error::kIo);
  EXPECT_EQ(position_after_failure.get(), 0U);
  EXPECT_TRUE(retried);
  EXPECT_THAT(out, BytesEqual(std::span(Bytes()).first(10)));
}

//! Opening a stream over a device whose size cannot be read fails with the
//! device's error.
NOLINT_TEST(OpenReadStreamTest, UnsizedDeviceFailsWithItsError)
{
  //! Fails to report its size, as a file on a lost network share does.
  struct UnsizedDevice {
    [[nodiscard]] auto Size() const noexcept -> Result<ByteCount>
    {
      return ::oxygen::Err(make_error_code(Error::kIo));
    }
    [[nodiscard]] auto ReadAt(ByteOffset /*at*/,
      std::span<std::byte> /*out*/) const noexcept -> Result<ByteCount>
    {
      return ::oxygen::Ok(ByteCount { 0 });
    }
  };
  const auto device = UnsizedDevice {};

  const auto stream = oxygen::serio::OpenReadStream(device);

  ASSERT_FALSE(stream);
  EXPECT_EQ(stream.error(), Error::kIo);
}

//! A device shorter than its region ends the stream early: an exact read
//! fails with kEndOfData instead of returning stale bytes.
NOLINT_TEST(RegionReadStreamShortDeviceTest, ShortDeviceEndsStreamEarly)
{
  //! Reports more bytes than it can read, as a file truncated by another
  //! process does.
  struct TruncatedDevice {
    MemoryFile content;

    [[nodiscard]] auto Size() const noexcept -> Result<ByteCount>
    {
      return ::oxygen::Ok(ByteCount { content.Size()->get() * 2 });
    }
    [[nodiscard]] auto ReadAt(const ByteOffset at,
      const std::span<std::byte> out) const noexcept -> Result<ByteCount>
    {
      return content.ReadAt(at, out);
    }
  };
  const auto bytes = PatternBytes(100);
  const auto device = TruncatedDevice { MemoryFile::FromBytes(bytes).value() };
  auto stream = RegionReadStream<TruncatedDevice>(
    Region<TruncatedDevice>::Whole(device).value());
  auto out = std::vector<std::byte>(150);

  const auto read = ReadExactly(stream, out);

  ASSERT_FALSE(read);
  EXPECT_EQ(read.error(), Error::kEndOfData);
}

class RegionWriteStreamTest : public ::testing::Test {
protected:
  static constexpr ByteCount kBufferSize { 1024 };

  auto MakeStream(const ByteOffset start = ByteOffset { 0 })
    -> RegionWriteStream<CountedDevice>
  {
    return RegionWriteStream<CountedDevice>(device_, start, kBufferSize);
  }

  [[nodiscard]] auto Writes() const -> uint32_t
  {
    return device_.CallCount(FaultOperation::kWrite);
  }

  [[nodiscard]] auto Content() const -> std::span<const std::byte>
  {
    return device_.Inner().Bytes();
  }

  auto Device() -> CountedDevice& { return device_; }
  [[nodiscard]] auto Bytes() const -> std::span<const std::byte>
  {
    return bytes_;
  }

private:
  const std::vector<std::byte> bytes_ = PatternBytes(4096);
  CountedDevice device_ { MemoryFile {} };
};

//! Small writes collect in the buffer and reach the device in one write.
NOLINT_TEST_F(RegionWriteStreamTest, SmallWritesShareOneDeviceWrite)
{
  auto stream = MakeStream();

  for (size_t i = 0; i < 100; ++i) {
    ASSERT_TRUE(stream.Write(std::span(Bytes()).subspan(i * 10, 10)));
  }
  ASSERT_TRUE(stream.Flush());

  EXPECT_EQ(Writes(), 1U);
  EXPECT_THAT(Content(), BytesEqual(std::span(Bytes()).first(1000)));
}

//! A write at least as large as the buffer flushes what is buffered, then
//! goes straight to the device.
NOLINT_TEST_F(RegionWriteStreamTest, LargeWriteGoesStraightToDevice)
{
  auto stream = MakeStream();

  ASSERT_TRUE(stream.Write(std::span(Bytes()).first(10)));
  ASSERT_TRUE(stream.Write(std::span(Bytes()).subspan(10)));

  EXPECT_EQ(Writes(), 2U);
  EXPECT_THAT(Content(), BytesEqual(Bytes()));
}

//! A stream started at an offset writes there and counts its position from
//! its own start.
NOLINT_TEST_F(RegionWriteStreamTest, StartOffsetPlacesBytes)
{
  const auto prefix
    = PatternBytes(100, oxygen::serio::testing::PatternSeed::kSecond);
  ASSERT_TRUE(Device().WriteAt(ByteOffset { 0 }, prefix));
  auto stream = MakeStream(ByteOffset { 100 });

  ASSERT_TRUE(stream.Write(std::span(Bytes()).first(50)));
  ASSERT_TRUE(stream.Flush());

  EXPECT_EQ(stream.Position().get(), 50U);
  EXPECT_THAT(Content().first(100), BytesEqual(prefix));
  EXPECT_THAT(Content().subspan(100), BytesEqual(std::span(Bytes()).first(50)));
}

//! A failed flush returns the device's error and keeps the buffered bytes,
//! so a retried flush writes them.
NOLINT_TEST_F(RegionWriteStreamTest, FailedFlushKeepsBufferedBytes)
{
  Device().FailOn(FaultOperation::kWrite, 0, make_error_code(Error::kIo));
  auto stream = MakeStream();
  ASSERT_TRUE(stream.Write(std::span(Bytes()).first(100)));

  const auto failed = stream.Flush();
  const auto retried = stream.Flush();

  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), Error::kIo);
  EXPECT_TRUE(retried);
  EXPECT_THAT(Content(), BytesEqual(std::span(Bytes()).first(100)));
}

//! A full device fails the write that reaches it with kNoSpace.
NOLINT_TEST_F(RegionWriteStreamTest, FullDeviceIsNoSpace)
{
  Device().LimitCapacity(ByteCount { 100 });
  auto stream = MakeStream();
  ASSERT_TRUE(stream.Write(std::span(Bytes()).first(200)));

  const auto flushed = stream.Flush();

  ASSERT_FALSE(flushed);
  EXPECT_EQ(flushed.error(), Error::kNoSpace);
}

//! A stream destroyed with buffered bytes writes them.
NOLINT_TEST_F(RegionWriteStreamTest, DestructionFlushesBufferedBytes)
{
  {
    auto stream = MakeStream();
    ASSERT_TRUE(stream.Write(std::span(Bytes()).first(100)));
  }

  EXPECT_THAT(Content(), BytesEqual(std::span(Bytes()).first(100)));
}

//! A stream destroyed with bytes it cannot write logs that they are lost.
NOLINT_TEST_F(RegionWriteStreamTest, DestructionLogsLostBytes)
{
  Device().FailOn(FaultOperation::kWrite, 0, make_error_code(Error::kIo));
  const auto capture = ScopedLogCapture("RegionWriteStreamTest");

  {
    auto stream = MakeStream();
    ASSERT_TRUE(stream.Write(std::span(Bytes()).first(100)));
  }

  EXPECT_TRUE(capture.Contains("unflushed stream bytes lost"));
  EXPECT_TRUE(Content().empty());
}

//! The stream's buffer never exceeds its region: one small read of a 100-byte
//! region of a larger device costs one device read, and the rest of the region
//! is served from that buffer.
NOLINT_TEST(RegionReadStreamBufferTest, BufferNeverExceedsTheRegion)
{
  const auto bytes = PatternBytes(10'000);
  const auto device = CountedDevice(MemoryFile::FromBytes(bytes).value());
  const auto region = Region<CountedDevice>::Make(device,
    oxygen::serio::ByteRange {
      .offset = ByteOffset { 200 },
      .count = ByteCount { 100 },
    });
  ASSERT_TRUE(region);
  auto stream = RegionReadStream<CountedDevice>(*region);
  auto first = std::vector<std::byte>(1);
  auto rest = std::vector<std::byte>(99);
  auto past = std::vector<std::byte>(10);

  const auto read_first = stream.ReadSome(first);
  const auto reads_after_first = device.CallCount(FaultOperation::kRead);
  const auto read_rest = ReadExactly(stream, rest);
  const auto read_past = stream.ReadSome(past);

  ASSERT_TRUE(read_first);
  EXPECT_EQ(read_first->get(), 1U);
  EXPECT_EQ(reads_after_first, 1U);
  EXPECT_TRUE(read_rest);
  EXPECT_EQ(device.CallCount(FaultOperation::kRead), 1U);
  EXPECT_THAT(first, BytesEqual(std::span(bytes).subspan(200, 1)));
  EXPECT_THAT(rest, BytesEqual(std::span(bytes).subspan(201, 99)));
  ASSERT_TRUE(read_past);
  EXPECT_EQ(read_past->get(), 0U);
}

} // namespace
