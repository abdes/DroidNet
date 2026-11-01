//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include <asio/io_context.hpp>

#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/asio.h> // IWYU pragma: keep (EventLoopTraits)
#include <Oxygen/Serio/AsyncDevice.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::serio::testing {

//! The typed conformance suite of asynchronous devices.
/*!
 Every asynchronous device awaits with the contract its blocking operations
 honour, whether its operations complete at once (memory) or through an
 `IoDriver` (files). A device joins with a factory, as in `DeviceConformance`:

 ```cpp
 using Factories = ::testing::Types<MyAsyncDeviceFactory>;
 INSTANTIATE_TYPED_TEST_SUITE_P(MyModule, AsyncDeviceConformance, Factories);
 ```

 A factory whose devices complete through a driver also has
 `auto Loop() -> asio::io_context&`, the loop its driver serves; the suite
 runs every case on it. Other factories run on the suite's own loop. A
 factory whose devices write joins `AsyncWritableDeviceConformance` too.

 @tparam Factory The device factory; its devices satisfy
         `AsyncReadableDevice`.
*/
template <class Factory> class AsyncDeviceConformance : public ::testing::Test {
protected:
  using Device = Factory::Device;

  static constexpr size_t kSize = 4096 + 123;

  auto MakeDevice(const std::span<const std::byte> bytes) -> Device
  {
    return factory_.Make(bytes);
  }

  //! Awaits `operation` on the devices' loop and returns its result.
  template <class Operation> auto Await(Operation operation)
  {
    using Value = decltype(std::declval<Operation&>().await_resume());
    return co::Run(Loop(), [](Operation awaited) -> co::Co<Value> {
      co_return co_await std::move(awaited);
    }(std::move(operation)));
  }

  //! Runs `task` on the devices' loop and returns its result.
  template <class Task> auto Run(Task task)
  {
    return co::Run(Loop(), std::move(task));
  }

private:
  auto Loop() -> asio::io_context&
  {
    if constexpr (requires { factory_.Loop(); }) {
      return factory_.Loop();
    } else {
      return loop_;
    }
  }

  asio::io_context loop_;
  Factory factory_ {};
};

TYPED_TEST_SUITE_P(AsyncDeviceConformance);

//! An awaited read returns what the blocking read returns: the bytes inside,
//! the existing bytes across the end, nothing at or past the end.
TYPED_TEST_P(AsyncDeviceConformance, AsyncReadMatchesReadAt) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  const auto device = this->MakeDevice(bytes);
  auto inside = std::vector<std::byte>(100);
  auto across = std::vector<std::byte>(50);
  auto past = std::vector<std::byte>(8);

  const auto read_inside
    = this->Await(device.AsyncReadAt(ByteOffset { 1000 }, inside));
  const auto read_across
    = this->Await(device.AsyncReadAt(ByteOffset { bytes.size() - 20 }, across));
  const auto read_at_end
    = this->Await(device.AsyncReadAt(ByteOffset { bytes.size() }, past));
  const auto read_past
    = this->Await(device.AsyncReadAt(ByteOffset { bytes.size() + 100 }, past));

  ASSERT_TRUE(read_inside);
  EXPECT_EQ(read_inside->get(), inside.size());
  EXPECT_THAT(
    inside, BytesEqual(std::span(bytes).subspan(1000, inside.size())));
  ASSERT_TRUE(read_across);
  ASSERT_EQ(read_across->get(), 20U);
  EXPECT_THAT(
    std::span(across).first(20), BytesEqual(std::span(bytes).last(20)));
  ASSERT_TRUE(read_at_end);
  EXPECT_EQ(read_at_end->get(), 0U);
  ASSERT_TRUE(read_past);
  EXPECT_EQ(read_past->get(), 0U);
}

//! An awaited zero-length read succeeds and reads nothing.
TYPED_TEST_P(AsyncDeviceConformance, ZeroLengthAsyncReadSucceeds) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  const auto device = this->MakeDevice(bytes);

  const auto read = this->Await(device.AsyncReadAt(ByteOffset { 10 }, {}));

  ASSERT_TRUE(read);
  EXPECT_EQ(read->get(), 0U);
}

//! An exact awaited read across the end fails with kEndOfData.
TYPED_TEST_P(
  AsyncDeviceConformance, AsyncReadExactlyAcrossEndIsEndOfData) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  const auto device = this->MakeDevice(bytes);
  auto out = std::vector<std::byte>(64);

  const auto read = this->Run(
    AsyncReadExactlyAt(device, ByteOffset { bytes.size() - 10 }, out));

  ASSERT_FALSE(read);
  EXPECT_EQ(read.error(), Error::kEndOfData);
}

//! Reads awaited together each fill their own buffer.
TYPED_TEST_P(
  AsyncDeviceConformance, ConcurrentAsyncReadsAreIndependent) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  const auto device = this->MakeDevice(bytes);
  constexpr size_t kReads = 16;
  constexpr size_t kLength = 97;
  auto buffers = std::vector<std::vector<std::byte>>(
    kReads, std::vector<std::byte>(kLength));
  const auto offset_of = [](const size_t i) -> size_t {
    return (i * 251) % (TestFixture::kSize - kLength);
  };

  const auto results = this->Run([&] -> co::Co<std::vector<Result<void>>> {
    auto reads = std::vector<co::Co<Result<void>>> {};
    for (size_t i = 0; i < kReads; ++i) {
      reads.push_back(AsyncReadExactlyAt(
        device, ByteOffset { offset_of(i) }, std::span(buffers[i])));
    }
    co_return co_await co::AllOf(std::move(reads));
  }());

  for (size_t i = 0; i < kReads; ++i) {
    ASSERT_TRUE(results[i]) << "read " << i;
    EXPECT_THAT(
      buffers[i], BytesEqual(std::span(bytes).subspan(offset_of(i), kLength)))
      << "read " << i;
  }
}

//! A region bounds awaited reads by its own range.
TYPED_TEST_P(AsyncDeviceConformance, RegionBoundsAsyncReads) // NOLINT
{
  using Device = TestFixture::Device;
  const auto bytes = PatternBytes(TestFixture::kSize);
  const auto device = this->MakeDevice(bytes);
  const auto region = Region<Device>::Make(device,
    ByteRange { .offset = ByteOffset { 100 }, .count = ByteCount { 50 } });
  ASSERT_TRUE(region);
  auto out = std::vector<std::byte>(20);

  const auto across = this->Await(region->AsyncReadAt(ByteOffset { 40 }, out));
  const auto past = this->Await(region->AsyncReadAt(ByteOffset { 50 }, out));

  ASSERT_TRUE(across);
  ASSERT_EQ(across->get(), 10U);
  EXPECT_THAT(
    std::span(out).first(10), BytesEqual(std::span(bytes).subspan(140, 10)));
  ASSERT_TRUE(past);
  EXPECT_EQ(past->get(), 0U);
}

//! AsyncReadAll returns every byte of the device, as ReadAll does.
TYPED_TEST_P(AsyncDeviceConformance, AsyncReadAllReadsTheWholeDevice) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  const auto device = this->MakeDevice(bytes);

  const auto all = this->Run(AsyncReadAll(device, CountOf(bytes.size())));

  ASSERT_TRUE(all);
  EXPECT_THAT(all->Bytes(), BytesEqual(bytes));
}

//! AsyncReadAll refuses a device one byte larger than the limit with
//! kTooLarge, before reading or allocating.
TYPED_TEST_P(AsyncDeviceConformance,
  AsyncReadAllRefusesADeviceLargerThanTheLimit) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  const auto device = this->MakeDevice(bytes);

  const auto all = this->Run(AsyncReadAll(device, CountOf(bytes.size() - 1)));

  ASSERT_FALSE(all);
  EXPECT_EQ(all.error(), Error::kTooLarge);
}

//! AsyncReadAll of a region reads only the region's range.
TYPED_TEST_P(
  AsyncDeviceConformance, AsyncReadAllOfARegionReadsItsRange) // NOLINT
{
  using Device = TestFixture::Device;
  const auto bytes = PatternBytes(TestFixture::kSize);
  const auto device = this->MakeDevice(bytes);
  const auto region = Region<Device>::Make(device,
    ByteRange { .offset = ByteOffset { 100 }, .count = ByteCount { 500 } });
  ASSERT_TRUE(region);

  const auto all = this->Run(AsyncReadAll(*region, ByteCount { 500 }));

  ASSERT_TRUE(all);
  EXPECT_THAT(all->Bytes(), BytesEqual(std::span(bytes).subspan(100, 500)));
}

//! AsyncReadAll of an empty device is an empty buffer.
TYPED_TEST_P(
  AsyncDeviceConformance, AsyncReadAllOfAnEmptyDeviceIsEmpty) // NOLINT
{
  const auto device = this->MakeDevice({});

  const auto all = this->Run(AsyncReadAll(device, ByteCount { 0 }));

  ASSERT_TRUE(all);
  EXPECT_TRUE(all->Empty());
}

REGISTER_TYPED_TEST_SUITE_P(AsyncDeviceConformance, AsyncReadMatchesReadAt,
  ZeroLengthAsyncReadSucceeds, AsyncReadExactlyAcrossEndIsEndOfData,
  ConcurrentAsyncReadsAreIndependent, RegionBoundsAsyncReads,
  AsyncReadAllReadsTheWholeDevice, AsyncReadAllRefusesADeviceLargerThanTheLimit,
  AsyncReadAllOfARegionReadsItsRange, AsyncReadAllOfAnEmptyDeviceIsEmpty);

//! The cases of devices whose writes are awaited: `AsyncDeviceConformance`'s
//! fixture, for factories whose devices satisfy `AsyncWritableDevice` and
//! write.
template <class Factory>
class AsyncWritableDeviceConformance : public AsyncDeviceConformance<Factory> {
  static_assert(AsyncWritableDevice<typename Factory::Device>);
};

TYPED_TEST_SUITE_P(AsyncWritableDeviceConformance);

//! An awaited write overwrites and extends; the blocking read sees it.
TYPED_TEST_P(AsyncWritableDeviceConformance, AsyncWriteIsReadBack) // NOLINT
{
  const auto bytes = PatternBytes(64);
  auto device = this->MakeDevice(bytes);
  const auto patch = PatternBytes(40, PatternSeed::kSecond);
  auto out = std::vector<std::byte>(patch.size());

  const auto written
    = this->Await(device.AsyncWriteAt(ByteOffset { 48 }, patch));
  const auto read = device.ReadAt(ByteOffset { 48 }, out);

  ASSERT_TRUE(written);
  ASSERT_TRUE(read);
  EXPECT_THAT(out, BytesEqual(patch));
  const auto size = device.Size();
  ASSERT_TRUE(size);
  EXPECT_EQ(size->get(), 88U);
}

REGISTER_TYPED_TEST_SUITE_P(
  AsyncWritableDeviceConformance, AsyncWriteIsReadBack);

} // namespace oxygen::serio::testing
