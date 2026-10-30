//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/AnyDevice.h, Serio/Detail/ErasedObject.h

#include <array>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::Result;
using oxygen::serio::AnyDevice;
using oxygen::serio::ByteCount;
using oxygen::serio::ByteOffset;
using oxygen::serio::Durability;
using oxygen::serio::MemoryFile;
using oxygen::serio::ReadExactlyAt;
using oxygen::serio::testing::BytesEqual;
using oxygen::serio::testing::PatternBytes;

//! A readable device that counts how many live instances it ends with, and
//! carries `kPayload` bytes so a test controls whether erasure stores it
//! inline or on the heap.
template <size_t kPayload> class TrackedDevice {
public:
  explicit TrackedDevice(int& live) noexcept
    : live_(&live)
  {
    ++*live_;
  }

  ~TrackedDevice()
  {
    if (live_ != nullptr) {
      --*live_;
    }
  }

  TrackedDevice(const TrackedDevice&) = delete;
  auto operator=(const TrackedDevice&) -> TrackedDevice& = delete;

  TrackedDevice(TrackedDevice&& other) noexcept
    : live_(std::exchange(other.live_, nullptr))
  {
  }

  auto operator=(TrackedDevice&& other) noexcept -> TrackedDevice&
  {
    if (this != &other) {
      if (live_ != nullptr) {
        --*live_;
      }
      live_ = std::exchange(other.live_, nullptr);
    }
    return *this;
  }

  [[nodiscard]] auto Size() const noexcept -> Result<ByteCount>
  {
    return ::oxygen::Ok(oxygen::serio::CountOf(payload_.size()));
  }

  [[nodiscard]] auto ReadAt(ByteOffset /*at*/,
    std::span<std::byte> /*out*/) const noexcept -> Result<ByteCount>
  {
    return ::oxygen::Ok(ByteCount { 0 });
  }

private:
  int* live_;
  std::array<std::byte, kPayload> payload_ {};
};

using InlineDevice = TrackedDevice<8>;
using HeapDevice = TrackedDevice<512>;

//! An erased device is destroyed exactly once, however often the wrapper
//! moves, whether it is stored inline or on the heap.
template <class Device> auto ExpectSingleLifetime() -> void
{
  auto live = 0;
  {
    auto first = AnyDevice(Device(live));
    auto second = std::move(first);
    auto third = AnyDevice(Device(live));
    third = std::move(second);

    EXPECT_EQ(live, 1);
    EXPECT_TRUE(third.Size());
  }
  EXPECT_EQ(live, 0);
}

NOLINT_TEST(AnyDeviceTest, InlineDeviceHasSingleLifetime)
{
  GCHECK_F(ExpectSingleLifetime<InlineDevice>());
}

NOLINT_TEST(AnyDeviceTest, HeapDeviceHasSingleLifetime)
{
  GCHECK_F(ExpectSingleLifetime<HeapDevice>());
}

//! Writes through the erasure reach the device, and so do reserve and flush.
NOLINT_TEST(AnyDeviceTest, WritesReachErasedDevice)
{
  const auto bytes = PatternBytes(64);
  auto device = AnyDevice(MemoryFile {});

  ASSERT_TRUE(device.Writable());
  ASSERT_TRUE(device.Reserve(ByteCount { 128 }));
  ASSERT_TRUE(device.WriteAt(ByteOffset { 0 }, bytes));
  ASSERT_TRUE(device.Flush(Durability::kStable));

  auto out = std::vector<std::byte>(bytes.size());
  ASSERT_TRUE(ReadExactlyAt(device, ByteOffset { 0 }, out));
  EXPECT_THAT(out, BytesEqual(bytes));
}

} // namespace
