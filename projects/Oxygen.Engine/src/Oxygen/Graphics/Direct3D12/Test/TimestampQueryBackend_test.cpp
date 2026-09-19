//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>

#include <Oxygen/Graphics/Common/TimestampQueryProvider.h>
#include <Oxygen/Graphics/Direct3D12/Test/Fixtures/OffscreenTestFixture.h>
#include <Oxygen/Testing/GTest.h>

namespace {

class TimestampQueryBackendTest
  : public oxygen::graphics::d3d12::testing::OffscreenTestFixture {
protected:
  auto BackendConfigJson() const -> std::string override
  {
    return R"({"enable_debug_layer":true})";
  }
};

NOLINT_TEST_F(TimestampQueryBackendTest, NonzeroResolveKeepsIndependentRanges)
{
  const auto provider = Backend().GetTimestampQueryProvider();
  ASSERT_NE(provider, nullptr);
  ASSERT_TRUE(provider->EnsureCapacity(8U));
  {
    auto recorder = AcquireRecorder("Timestamp initial ranges");
    for (const auto slot : { 0U, 1U, 6U, 7U })
      ASSERT_TRUE(provider->WriteTimestamp(*recorder, slot));
    ASSERT_TRUE(provider->RecordResolve(*recorder, 2U));
    ASSERT_TRUE(provider->RecordResolve(*recorder, 2U, 6U));
  }
  WaitForQueueIdle();
  const auto first = provider->GetResolvedTicks();
  ASSERT_GE(first.size(), 8U);
  const auto saved
    = std::array<uint64_t, 4> { first[0], first[1], first[6], first[7] };
  EXPECT_GT(saved[0], 0U);
  EXPECT_GE(saved[1], saved[0]);
  EXPECT_GE(saved[2], saved[1]);
  EXPECT_GE(saved[3], saved[2]);

  {
    auto recorder = AcquireRecorder("Timestamp second range only");
    ASSERT_TRUE(provider->WriteTimestamp(*recorder, 6U));
    ASSERT_TRUE(provider->WriteTimestamp(*recorder, 7U));
    ASSERT_TRUE(provider->RecordResolve(*recorder, 2U, 6U));
  }
  WaitForQueueIdle();
  const auto second = provider->GetResolvedTicks();
  EXPECT_EQ(second[0], saved[0]);
  EXPECT_EQ(second[1], saved[1]);
  EXPECT_GT(second[6], saved[3]);
  EXPECT_GE(second[7], second[6]);
}

NOLINT_TEST_F(TimestampQueryBackendTest, RejectsResolveOutsideCapacity)
{
  const auto provider = Backend().GetTimestampQueryProvider();
  ASSERT_NE(provider, nullptr);
  const auto capacity = provider->GetCapacity();
  ASSERT_GT(capacity, 0U);
  auto recorder = AcquireRecorder("Timestamp invalid ranges");
  EXPECT_FALSE(provider->RecordResolve(*recorder, 1U, capacity));
  EXPECT_FALSE(provider->RecordResolve(*recorder, capacity, 1U));
  EXPECT_FALSE(provider->RecordResolve(*recorder, 0U, capacity + 1U));
  EXPECT_TRUE(provider->RecordResolve(*recorder, 0U, capacity));
}

} // namespace
