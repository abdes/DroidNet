//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/TimeUtils.h>

#include <chrono>

#include <Oxygen/Testing/GTest.h>
#include <gmock/gmock.h>

using namespace std::chrono_literals;

using testing::Return;

using oxygen::ChangePerSecondType;
using oxygen::DeltaTimeType;
using oxygen::Duration;
using oxygen::ElapsedTimeType;
using oxygen::TimePoint;

class MockNow {
public:
  // NOLINTBEGIN
  MOCK_METHOD(Duration, Now, (), (const));
  // NOLINTEND
};
struct MockTime {
  inline static MockNow* mock { nullptr };
  // This class is passed as template type during class B object creation in
  // unit test environment
  static auto Now() -> TimePoint { return mock->Now(); }
};

class TimeTest : public testing::Test {
protected:
  void SetUp() override { MockTime::mock = &mock_now; }
  void TearDown() override { MockTime::mock = nullptr; }

public:
  MockNow mock_now;
};

using ElapsedTimeTest = TimeTest;

NOLINT_TEST_F(ElapsedTimeTest, StartTime)
{
  EXPECT_CALL(mock_now, Now).Times(1).WillOnce(Return(10us));

  const ElapsedTimeType<MockTime> elapsed;
  EXPECT_EQ(elapsed.StartTime(), 10us);
}

NOLINT_TEST_F(ElapsedTimeTest, ElapsedTime)
{
  EXPECT_CALL(mock_now, Now)
    .Times(2)
    .WillOnce(Return(10us))
    .WillOnce(Return(25us));

  const ElapsedTimeType<MockTime> elapsed;
  EXPECT_TRUE(elapsed.StartTime() == 10us);
  EXPECT_EQ(elapsed.ElapsedTime(), 25us - 10us);
}

using DeltaTimeTest = TimeTest;

NOLINT_TEST_F(DeltaTimeTest, AtCreation)
{
  EXPECT_CALL(mock_now, Now).Times(1).WillOnce(Return(10us));

  const DeltaTimeType<MockTime> delta;
  EXPECT_TRUE(delta.LastStepTime() == 10us);
  EXPECT_EQ(delta.Delta(), 0us);
}

NOLINT_TEST_F(DeltaTimeTest, AfterUpdate)
{
  EXPECT_CALL(mock_now, Now)
    .Times(2)
    .WillOnce(Return(10us))
    .WillOnce(Return(30us));

  DeltaTimeType<MockTime> delta;
  delta.Update();
  EXPECT_TRUE(delta.LastStepTime() == 30us);
  EXPECT_EQ(delta.Delta(), 30us - 10us);
}

using ChangePerSecondTest = TimeTest;

NOLINT_TEST_F(ChangePerSecondTest, AtCreation)
{
  EXPECT_CALL(mock_now, Now).Times(1).WillOnce(Return(10us));

  const ChangePerSecondType<MockTime> cps;
  EXPECT_EQ(cps.Value(), 0U);
  EXPECT_TRUE(cps.ValueTime() == 10us);
}

NOLINT_TEST_F(ChangePerSecondTest, AfterUpdate)
{
  EXPECT_CALL(mock_now, Now)
    .Times(4)
    .WillOnce(Return(0us))
    .WillOnce(Return(10us))
    .WillOnce(Return(1s))
    .WillOnce(Return(2s + 10us));

  ChangePerSecondType<MockTime> cps;
  cps.Update();
  EXPECT_EQ(cps.Value(), 0U);
  EXPECT_EQ(cps.ValueTime(), 10us);
  cps.Update();
  EXPECT_EQ(cps.Value(), 2U);
  EXPECT_EQ(cps.ValueTime(), 1s);
  cps.Update();
  EXPECT_EQ(cps.Value(), 1U);
  EXPECT_EQ(cps.ValueTime(), 2s + 10us);
}
