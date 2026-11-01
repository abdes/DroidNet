//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/Integrity.h (AsyncHash, AsyncComputeCrc32,
// AsyncComputeSha256)

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "Support/DeviceFactories.h"
#include "gtest/gtest.h"
#include <asio/io_context.hpp>

#include <Oxygen/Base/Crc32.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/asio.h> // IWYU pragma: keep (EventLoopTraits)
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/Integrity.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/Testing/FaultInjectingDevice.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::serio::testing {
namespace {

  // The awaited hashes over every awaitable source: memory, a file of an
  // inline driver and a file of a loop driver.
  using Factories = ::testing::Types<test::MemoryFileFactory,
    test::InlineAsyncFileFactory, test::AsyncFileFactory>;

  //! More than two hash buffers, so a source is hashed in several pieces.
  constexpr auto kLargeSize
    = (2 * static_cast<size_t>(kHashBufferSize.get())) + 17;

  template <class Factory> class AsyncIntegrityTest : public ::testing::Test {
  protected:
    using Device = Factory::Device;

    auto MakeDevice(const std::span<const std::byte> bytes) -> Device
    {
      return factory_.Make(bytes);
    }

    //! Runs `task` on a loop and returns its result.
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

  TYPED_TEST_SUITE(AsyncIntegrityTest, Factories);

  //! Awaited digests equal the blocking ones, over data hashed in one piece.
  NOLINT_TYPED_TEST(AsyncIntegrityTest, DigestsEqualTheBlockingOnes)
  {
    const auto bytes = PatternBytes(10'000);
    const auto device = this->MakeDevice(bytes);

    const auto crc = this->Run(AsyncComputeCrc32(device));
    const auto sha = this->Run(AsyncComputeSha256(device));

    ASSERT_TRUE(crc);
    ASSERT_TRUE(sha);
    EXPECT_EQ(*crc, base::ComputeCrc32(bytes));
    EXPECT_EQ(*sha, base::ComputeSha256(bytes));
    EXPECT_EQ(*crc, ComputeCrc32(device).value());
    EXPECT_EQ(*sha, ComputeSha256(device).value());
  }

  //! Data of more than two hash buffers is hashed piece by piece into the
  //! same digests as a single pass; both digests come from one read.
  NOLINT_TYPED_TEST(AsyncIntegrityTest, LargeDataIsHashedInPieces)
  {
    const auto bytes = PatternBytes(kLargeSize);
    const auto device = this->MakeDevice(bytes);

    const auto digests
      = this->Run(AsyncHash<base::Crc32, base::Sha256>(device));

    ASSERT_TRUE(digests);
    EXPECT_EQ(std::get<0>(*digests), base::ComputeCrc32(bytes));
    EXPECT_EQ(std::get<1>(*digests), base::ComputeSha256(bytes));
  }

  //! Skipped ranges are left out: changing the skipped bytes does not change
  //! the digest, and the digest equals the blocking one with the same skips,
  //! also for a skip that straddles two pieces.
  NOLINT_TYPED_TEST(AsyncIntegrityTest, SkippedRangesAreExcluded)
  {
    const auto bytes = PatternBytes(kLargeSize);
    const auto straddling = ByteRange {
      .offset = ByteOffset { kHashBufferSize.get() - 10 },
      .count = ByteCount { 100 },
    };
    const auto skips = std::vector<ByteRange> {
      ByteRange { .offset = ByteOffset { 5 }, .count = ByteCount { 20 } },
      straddling,
    };
    auto changed = bytes;
    for (const auto& skip : skips) {
      for (auto i = skip.offset.get(); i < skip.End().get(); ++i) {
        changed[static_cast<size_t>(i)] = std::byte { 0xEE };
      }
    }
    const auto original = this->MakeDevice(bytes);
    const auto altered = this->MakeDevice(changed);

    const auto original_crc = this->Run(AsyncComputeCrc32(original, skips));
    const auto altered_crc = this->Run(AsyncComputeCrc32(altered, skips));
    const auto original_sha = this->Run(AsyncComputeSha256(original, skips));
    const auto altered_sha = this->Run(AsyncComputeSha256(altered, skips));

    ASSERT_TRUE(original_crc);
    ASSERT_TRUE(altered_crc);
    ASSERT_TRUE(original_sha);
    ASSERT_TRUE(altered_sha);
    EXPECT_EQ(*original_crc, *altered_crc);
    EXPECT_EQ(*original_sha, *altered_sha);
    EXPECT_EQ(*original_crc, ComputeCrc32(original, skips).value());
    EXPECT_EQ(*original_sha, ComputeSha256(original, skips).value());
    EXPECT_NE(*original_crc, base::ComputeCrc32(bytes));
  }

  //! An empty source hashes to the digests of no bytes.
  NOLINT_TYPED_TEST(AsyncIntegrityTest, EmptySourceHashesToTheEmptyDigest)
  {
    const auto device = this->MakeDevice({});

    const auto crc = this->Run(AsyncComputeCrc32(device));
    const auto sha = this->Run(AsyncComputeSha256(device));

    ASSERT_TRUE(crc);
    ASSERT_TRUE(sha);
    EXPECT_EQ(*crc, base::ComputeCrc32(std::span<const std::byte> {}));
    EXPECT_EQ(*sha, base::ComputeSha256(std::span<const std::byte> {}));
  }

  //! A region hashes only its own range.
  NOLINT_TYPED_TEST(AsyncIntegrityTest, RegionHashesOnlyItsRange)
  {
    const auto bytes = PatternBytes(5000);
    const auto device = this->MakeDevice(bytes);
    const auto region = Region<typename TestFixture::Device>::Make(device,
      ByteRange { .offset = ByteOffset { 100 }, .count = ByteCount { 1000 } });
    ASSERT_TRUE(region);

    const auto sha = this->Run(AsyncComputeSha256(*region));

    ASSERT_TRUE(sha);
    EXPECT_EQ(*sha, base::ComputeSha256(std::span(bytes).subspan(100, 1000)));
  }

  //! A read error is the hash's error, in the first piece and in a later one.
  NOLINT_TYPED_TEST(AsyncIntegrityTest, ReadErrorPropagates)
  {
    const auto bytes = PatternBytes(kLargeSize);
    auto first = FaultInjectingDevice<typename TestFixture::Device>(
      this->MakeDevice(bytes));
    auto later = FaultInjectingDevice<typename TestFixture::Device>(
      this->MakeDevice(bytes));
    first.FailOn(FaultOperation::kRead, 0, make_error_code(Error::kIo));
    later.FailOn(FaultOperation::kRead, 1, make_error_code(Error::kIo));

    const auto first_failed = this->Run(AsyncComputeCrc32(first));
    const auto later_failed = this->Run(AsyncComputeSha256(later));

    ASSERT_FALSE(first_failed);
    EXPECT_EQ(first_failed.error(), Error::kIo);
    ASSERT_FALSE(later_failed);
    EXPECT_EQ(later_failed.error(), Error::kIo);
  }

} // namespace
} // namespace oxygen::serio::testing
