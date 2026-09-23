//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <limits>
#include <thread>
#include <vector>

#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Support/ResourceCreationCounter.h>

namespace oxygen::vortex::testing {
namespace {
  NOLINT_TEST(ResourceCreationCounterTest,
    CountsConcurrentCreationsWithoutRetainingResources)
  {
    auto counter = ResourceCreationCounter {};
    auto workers = std::vector<std::jthread> {};
    for (unsigned worker = 0U; worker < 4U; ++worker) {
      workers.emplace_back([&] -> void {
        for (unsigned index = 0U; index < 500U; ++index) {
          counter.RecordBuffer(SizeBytes { 256U });
          counter.RecordTexture();
        }
      });
    }
    workers.clear();
    const auto result = counter.Snapshot();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->buffers, 2000U);
    EXPECT_EQ(result->textures, 2000U);
    EXPECT_EQ(result->requested_buffer_bytes.get(), 512000U);
  }

  NOLINT_TEST(ResourceCreationCounterTest, OverflowInvalidatesAllLaterSnapshots)
  {
    auto counter = ResourceCreationCounter {};
    const auto empty = counter.Snapshot();
    ASSERT_TRUE(empty.has_value());
    EXPECT_EQ(empty->buffers, 0U);
    EXPECT_EQ(empty->textures, 0U);
    counter.RecordBuffer(
      SizeBytes { std::numeric_limits<std::uint64_t>::max() });
    ASSERT_TRUE(counter.Snapshot().has_value());
    counter.RecordBuffer(SizeBytes { 1U });
    EXPECT_FALSE(counter.Snapshot().has_value());
    counter.RecordTexture();
    EXPECT_FALSE(counter.Snapshot().has_value());
  }
} // namespace
} // namespace oxygen::vortex::testing
