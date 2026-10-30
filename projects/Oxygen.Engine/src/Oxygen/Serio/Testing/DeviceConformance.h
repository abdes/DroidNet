//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>
#include <type_traits>
#include <vector>

#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::serio::testing {

//! Whether `device` writes: known from its type, or at run time for an
//! erased device, whose type satisfies `WritableDevice` either way.
template <ReadableDevice D>
[[nodiscard]] auto IsWritable([[maybe_unused]] const D& device) noexcept -> bool
{
  if constexpr (std::is_same_v<D, AnyDevice>) {
    return device.Writable();
  } else {
    return WritableDevice<D>;
  }
}

//! The typed device conformance suite.
/*!
 Every device runs the same cases, so memory and files cannot drift apart. A
 device joins with a factory type and one line:

 ```cpp
 struct MyDeviceFactory {
   using Device = MyDevice;
   auto Make(std::span<const std::byte> bytes) -> Device;
 };
 using Factories = ::testing::Types<MyDeviceFactory>;
 INSTANTIATE_TYPED_TEST_SUITE_P(MyModule, DeviceConformance, Factories);
 ```

 `Make` returns a device holding `bytes`; a writable device is also writable.
 The fixture owns one factory per test, which owns whatever the devices need
 (temporary files, backing memory). Cases that need writes skip read-only
 devices.

 @tparam Factory The device factory.
*/
template <class Factory> class DeviceConformance : public ::testing::Test {
protected:
  using Device = Factory::Device;

  static constexpr size_t kSize = 4096 + 123;

  auto MakeDevice(const std::span<const std::byte> bytes) -> Device
  {
    return factory_.Make(bytes);
  }

private:
  Factory factory_ {};
};

TYPED_TEST_SUITE_P(DeviceConformance);

//! Size reports the bytes the device holds.
TYPED_TEST_P(DeviceConformance, SizeReportsContent) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  const auto device = this->MakeDevice(bytes);

  const auto size = device.Size();

  ASSERT_TRUE(size);
  EXPECT_EQ(size->get(), bytes.size());
}

//! A read inside the device returns exactly the bytes asked for.
TYPED_TEST_P(DeviceConformance, ReadAtInsideReturnsBytes) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  const auto device = this->MakeDevice(bytes);
  auto out = std::vector<std::byte>(100);

  const auto read = device.ReadAt(ByteOffset { 1000 }, out);

  ASSERT_TRUE(read);
  EXPECT_EQ(read->get(), out.size());
  EXPECT_THAT(out, BytesEqual(std::span(bytes).subspan(1000, out.size())));
}

//! A read across the end returns the bytes that exist.
TYPED_TEST_P(DeviceConformance, ReadAtAcrossEndReturnsExistingBytes) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  const auto device = this->MakeDevice(bytes);
  auto out = std::vector<std::byte>(50);

  const auto read = device.ReadAt(ByteOffset { bytes.size() - 20 }, out);

  ASSERT_TRUE(read);
  ASSERT_EQ(read->get(), 20U);
  EXPECT_THAT(std::span(out).first(20), BytesEqual(std::span(bytes).last(20)));
}

//! A read at or past the end returns no bytes and no error.
TYPED_TEST_P(DeviceConformance, ReadAtPastEndReturnsNothing) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  const auto device = this->MakeDevice(bytes);
  auto out = std::vector<std::byte>(8);

  const auto at_end = device.ReadAt(ByteOffset { bytes.size() }, out);
  const auto past_end = device.ReadAt(ByteOffset { bytes.size() + 100 }, out);

  ASSERT_TRUE(at_end);
  EXPECT_EQ(at_end->get(), 0U);
  ASSERT_TRUE(past_end);
  EXPECT_EQ(past_end->get(), 0U);
}

//! A zero-length read succeeds anywhere and reads nothing.
TYPED_TEST_P(DeviceConformance, ZeroLengthReadSucceeds) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  const auto device = this->MakeDevice(bytes);

  const auto inside = device.ReadAt(ByteOffset { 10 }, {});
  const auto past_end = device.ReadAt(ByteOffset { bytes.size() + 1 }, {});

  ASSERT_TRUE(inside);
  EXPECT_EQ(inside->get(), 0U);
  ASSERT_TRUE(past_end);
  EXPECT_EQ(past_end->get(), 0U);
}

//! An exact read the device cannot satisfy fails with kEndOfData.
TYPED_TEST_P(DeviceConformance, ReadExactlyAtAcrossEndIsEndOfData) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  const auto device = this->MakeDevice(bytes);
  auto out = std::vector<std::byte>(64);

  const auto read = ReadExactlyAt(device, ByteOffset { bytes.size() - 8 }, out);

  ASSERT_FALSE(read);
  EXPECT_EQ(read.error(), Error::kEndOfData);
}

//! Concurrent positional reads of one device each get their own bytes.
TYPED_TEST_P(DeviceConformance, ConcurrentReadsAreIndependent) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  const auto device = this->MakeDevice(bytes);
  constexpr size_t kThreads = 8;
  constexpr size_t kReads = 200;
  auto failures = std::atomic<size_t> { 0 };

  {
    auto threads = std::vector<std::jthread> {};
    for (size_t t = 0; t < kThreads; ++t) {
      threads.emplace_back([&, t] {
        auto out = std::array<std::byte, 97> {};
        for (size_t i = 0; i < kReads; ++i) {
          const auto at = ((t * 509) + (i * 37)) % (bytes.size() - out.size());
          const auto read = ReadExactlyAt(device, ByteOffset { at }, out);
          if (!read
            || !std::equal(out.begin(), out.end(),
              bytes.begin() + static_cast<std::ptrdiff_t>(at))) {
            failures.fetch_add(1);
          }
        }
      });
    }
  }

  EXPECT_EQ(failures.load(), 0U);
}

//! A region bounds reads by its own range, not the device's.
TYPED_TEST_P(DeviceConformance, RegionBoundsReads) // NOLINT
{
  const auto bytes = PatternBytes(TestFixture::kSize);
  const auto device = this->MakeDevice(bytes);
  using Device = TestFixture::Device;
  const auto region = Region<Device>::Make(device,
    ByteRange { .offset = ByteOffset { 100 }, .count = ByteCount { 50 } });
  ASSERT_TRUE(region);
  auto out = std::vector<std::byte>(80);

  const auto read = region->ReadAt(ByteOffset { 30 }, out);
  const auto outside = Region<Device>::Make(device,
    ByteRange {
      .offset = ByteOffset { bytes.size() - 10 },
      .count = ByteCount { 11 },
    });

  ASSERT_TRUE(read);
  ASSERT_EQ(read->get(), 20U);
  EXPECT_THAT(
    std::span(out).first(20), BytesEqual(std::span(bytes).subspan(130, 20)));
  ASSERT_FALSE(outside);
  EXPECT_EQ(outside.error(), Error::kOutOfBounds);
}

//! An AnyDevice forwards reads and reports the erased capabilities.
TYPED_TEST_P(DeviceConformance, AnyDeviceForwards) // NOLINT
{
  using Device = TestFixture::Device;
  if constexpr (std::is_same_v<Device, AnyDevice>) {
    GTEST_SKIP() << "already erased";
  } else {
    const auto bytes = PatternBytes(TestFixture::kSize);
    auto any = AnyDevice(this->MakeDevice(bytes));
    auto out = std::vector<std::byte>(16);

    const auto read = any.ReadAt(ByteOffset { 16 }, out);

    ASSERT_TRUE(read);
    EXPECT_THAT(out, BytesEqual(std::span(bytes).subspan(16, 16)));
    EXPECT_EQ(any.Writable(), WritableDevice<Device>);
    EXPECT_EQ(any.Contiguous().has_value(), ContiguousDevice<Device>);
    if constexpr (!WritableDevice<Device>) {
      const auto write = any.WriteAt(ByteOffset { 0 }, out);
      ASSERT_FALSE(write);
      EXPECT_EQ(write.error(), std::errc::operation_not_supported);
    }
  }
}

//! A contiguous device's view holds its bytes.
TYPED_TEST_P(DeviceConformance, ContiguousViewHoldsBytes) // NOLINT
{
  using Device = TestFixture::Device;
  if constexpr (!ContiguousDevice<Device>) {
    GTEST_SKIP() << "not contiguous";
  } else {
    const auto bytes = PatternBytes(TestFixture::kSize);
    const auto device = this->MakeDevice(bytes);

    EXPECT_THAT(device.Bytes(), BytesEqual(bytes));
  }
}

//! Writes overwrite and extend; a gap past the old end reads as zero; a
//! zero-length write does nothing.
TYPED_TEST_P(DeviceConformance, WritePastEndExtendsWithZeros) // NOLINT
{
  using Device = TestFixture::Device;
  if constexpr (!WritableDevice<Device>) {
    GTEST_SKIP() << "not writable";
  } else {
    const auto bytes = PatternBytes(64);
    auto device = this->MakeDevice(bytes);
    if (!IsWritable(device)) {
      GTEST_SKIP() << "not writable at run time";
    }
    const auto tail = PatternBytes(8);

    ASSERT_TRUE(device.WriteAt(ByteOffset { 100 }, tail));
    ASSERT_TRUE(device.WriteAt(ByteOffset { 1000 }, {}));
    ASSERT_TRUE(device.Flush(Durability::kBuffered));

    const auto size = device.Size();
    ASSERT_TRUE(size);
    EXPECT_EQ(size->get(), 108U);
    auto out = std::vector<std::byte>(108);
    ASSERT_TRUE(ReadExactlyAt(device, ByteOffset { 0 }, out));
    EXPECT_THAT(std::span(out).first(64), BytesEqual(bytes));
    EXPECT_THAT(std::span(out).subspan(64, 36),
      ::testing::Each(::testing::Eq(std::byte { 0 })));
    EXPECT_THAT(std::span(out).last(8), BytesEqual(tail));
  }
}

//! Reserve preallocates without changing the size; a stable flush keeps the
//! bytes.
TYPED_TEST_P(DeviceConformance, ReserveKeepsSizeAndFlushKeepsBytes) // NOLINT
{
  using Device = TestFixture::Device;
  if constexpr (!WritableDevice<Device>) {
    GTEST_SKIP() << "not writable";
  } else {
    const auto bytes = PatternBytes(32);
    auto device = this->MakeDevice(bytes);
    if (!IsWritable(device)) {
      GTEST_SKIP() << "not writable at run time";
    }

    ASSERT_TRUE(device.Reserve(ByteCount { 1024ULL * 1024 }));
    ASSERT_TRUE(device.Flush(Durability::kStable));

    const auto size = device.Size();
    ASSERT_TRUE(size);
    EXPECT_EQ(size->get(), bytes.size());
    auto out = std::vector<std::byte>(bytes.size());
    ASSERT_TRUE(ReadExactlyAt(device, ByteOffset { 0 }, out));
    EXPECT_THAT(out, BytesEqual(bytes));
  }
}

REGISTER_TYPED_TEST_SUITE_P(DeviceConformance, SizeReportsContent,
  ReadAtInsideReturnsBytes, ReadAtAcrossEndReturnsExistingBytes,
  ReadAtPastEndReturnsNothing, ZeroLengthReadSucceeds,
  ReadExactlyAtAcrossEndIsEndOfData, ConcurrentReadsAreIndependent,
  RegionBoundsReads, AnyDeviceForwards, ContiguousViewHoldsBytes,
  WritePastEndExtendsWithZeros, ReserveKeepsSizeAndFlushKeepsBytes);

} // namespace oxygen::serio::testing
