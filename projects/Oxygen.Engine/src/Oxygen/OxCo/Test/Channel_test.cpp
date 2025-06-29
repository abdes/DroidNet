//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/OxCo/Channel.h>

#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>

#include "Utils/OxCoTestFixture.h"

using namespace std::chrono_literals;
using oxygen::co::Channel;
using oxygen::co::Co;
using oxygen::co::Run;
using oxygen::co::testing::OxCoTestFixture;

// NOLINTBEGIN(*-avoid-reference-coroutine-parameters)
// NOLINTBEGIN(*-avoid-capturing-lambda-coroutines)

namespace {

class BoundedChannelTest : public OxCoTestFixture {
protected:
  Channel<int> channel_ { 3 };
};

NOLINT_TEST_F(BoundedChannelTest, Smoke)
{
  ::Run(*el_, [this]() -> Co<> {
    std::vector<std::optional<int>> results;

    co_await channel_.Send(1);
    co_await channel_.Send(2);
    co_await channel_.Send(3);

    EXPECT_TRUE(channel_.Full());
    results.push_back(co_await channel_.Receive());
    results.push_back(co_await channel_.Receive());
    results.push_back(co_await channel_.Receive());
    EXPECT_TRUE(channel_.Empty());
    EXPECT_EQ(results, (std::vector<std::optional<int>> { 1, 2, 3 }));

    channel_.Close();

    const auto last = co_await channel_.Receive();
    EXPECT_EQ(last, std::nullopt);
  });
}

NOLINT_TEST_F(BoundedChannelTest, Blocking)
{
  ::Run(*el_, [this]() -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      bool ran_last = false;
      n.Start([&]() -> Co<> { co_await channel_.Send(1); });
      n.Start([&]() -> Co<> { co_await channel_.Send(2); });
      n.Start([&]() -> Co<> { co_await channel_.Send(3); });
      n.Start([&]() -> Co<> {
        co_await channel_.Send(4);
        ran_last = true;
      });

      co_await el_->Sleep(5ms);

      EXPECT_EQ(channel_.Size(), 3);
      EXPECT_TRUE(channel_.Full());
      EXPECT_FALSE(ran_last);

      std::array<std::optional<int>, 3> values;
      values[0] = co_await channel_.Receive();
      values[1] = co_await channel_.Receive();
      values[2] = co_await channel_.Receive();
      EXPECT_EQ(values, (std::array<std::optional<int>, 3> { 1, 2, 3 }));

      co_await oxygen::co::kYield;

      EXPECT_EQ(ran_last, true);
      EXPECT_EQ(channel_.Size(), 1);

      const int value = *co_await channel_.Receive();
      EXPECT_EQ(value, 4);
      EXPECT_TRUE(channel_.Empty());

      co_return oxygen::co::kJoin;
    };
  });
}

NOLINT_TEST_F(BoundedChannelTest, Alternating)
{
  ::Run(*el_, [this]() -> Co<> {
    std::vector<int> results;

    co_await oxygen::co::AllOf(
      [&]() -> Co<> {
        for (int i = 0; i < 10; i++) {
          co_await channel_.Send(i);
        }
        channel_.Close();
      },
      [&]() -> Co<> {
        while (std::optional<int> v = co_await channel_.Receive()) {
          if (*v % 2 == 0) {
            continue;
          }
          results.push_back(*v);
        }
      });

    EXPECT_EQ(results, (std::vector { 1, 3, 5, 7, 9 }));
  });
}

NOLINT_TEST_F(BoundedChannelTest, TrySendReceive)
{
  // False because channel empty
  EXPECT_TRUE(!channel_.TryReceive());

  // True because free space
  EXPECT_TRUE(channel_.TrySend(1));
  EXPECT_TRUE(channel_.TrySend(2));
  EXPECT_TRUE(channel_.TrySend(3));

  // False because full
  EXPECT_TRUE(!channel_.TrySend(4));

  // True because not empty
  EXPECT_TRUE(channel_.TryReceive());
  EXPECT_TRUE(channel_.TryReceive());

  channel_.Close();

  // We can still read remaining data while closed
  EXPECT_TRUE(channel_.TryReceive());

  // But we cannot write new data while closed
  EXPECT_TRUE(channel_.Empty());
  EXPECT_TRUE(channel_.Closed());
  EXPECT_TRUE(!channel_.TrySend(5));
}

NOLINT_TEST_F(BoundedChannelTest, Close)
{
  ::Run(*el_, [this]() -> Co<> {
    std::vector<std::optional<int>> results;

    OXCO_WITH_NURSERY(n)
    {
      co_await channel_.Send(1);
      co_await channel_.Send(2);
      co_await channel_.Send(3);

      // Discard result
      auto send = [&](int value) -> Co<> { co_await channel_.Send(value); };

      // These should remain blocked
      n.Start(send, 4);
      n.Start(send, 5);

      co_await oxygen::co::kYield;

      EXPECT_TRUE(channel_.Full());
      EXPECT_TRUE(!channel_.Closed());
      channel_.Close();
      EXPECT_TRUE(channel_.Full());
      EXPECT_TRUE(channel_.Closed());

      while (auto item = co_await channel_.Receive()) {
        results.emplace_back(*item);
      }

      co_return oxygen::co::kCancel;
    };

    EXPECT_TRUE(channel_.Empty());
    EXPECT_EQ(results, (std::vector<std::optional<int>> { 1, 2, 3 }));

    // More reads will return nullopt
    const std::optional<int> item = co_await channel_.Receive();
    EXPECT_EQ(item, std::nullopt);

    const bool sent = co_await channel_.Send(6);
    EXPECT_FALSE(sent);
  });
}

class UnboundedChannelTest : public OxCoTestFixture {
protected:
  Channel<int> channel_;
};

NOLINT_TEST_F(UnboundedChannelTest, Many)
{
  ::Run(*el_, [this]() -> Co<> {
    for (int i = 0; i < 10'000; i++) {
      co_await channel_.Send(i);
    }

    EXPECT_EQ(channel_.Size(), 10'000);

    for (int i = 0; i < 10'000; i++) {
      std::optional<int> v = co_await channel_.Receive();
      EXPECT_TRUE(v.has_value());
      EXPECT_EQ(*v, i);
    }

    EXPECT_EQ(channel_.Size(), 0);
    EXPECT_TRUE(channel_.Empty());
  });
}

NOLINT_TEST_F(UnboundedChannelTest, Close)
{
  ::Run(*el_, [this]() -> Co<> {
    std::vector<std::optional<int>> results;

    bool sent = co_await channel_.Send(1);
    EXPECT_TRUE(sent);
    channel_.Close();

    results.push_back(co_await channel_.Receive());
    EXPECT_TRUE(channel_.Empty());
    results.push_back(co_await channel_.Receive());
    EXPECT_TRUE(channel_.Empty());

    EXPECT_EQ(results, (std::vector<std::optional<int>> { 1, std::nullopt }));

    // More writes will fail
    sent = co_await channel_.Send(2);
    EXPECT_FALSE(sent);
  });
}

} // namespace

// NOLINTEND(*-avoid-capturing-lambda-coroutines)
// NOLINTEND(*-avoid-reference-coroutine-parameters)
