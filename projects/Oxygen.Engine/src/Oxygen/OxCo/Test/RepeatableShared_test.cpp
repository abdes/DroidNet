//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/OxCo/RepeatableShared.h>

#include "Utils/OxCoTestFixture.h"
#include "Utils/TestEventLoop.h"
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Testing/GTest.h>

using namespace std::chrono_literals;

using oxygen::co::Co;
using oxygen::co::RepeatableShared;
using oxygen::co::testing::OxCoTestFixture;
using oxygen::co::testing::TestEventLoop;

// NOLINTBEGIN(*-avoid-reference-coroutine-parameters)
// NOLINTBEGIN(*-avoid-capturing-lambda-coroutines)

namespace {

class RepeatableSharedTest : public OxCoTestFixture {
protected:
  using UseFunction = std::function<Co<int>(milliseconds)>;

  std::unique_ptr<RepeatableShared<int>> shared_ {};
  std::unique_ptr<UseFunction> use_ {};

  void SetUp() override
  {
    OxCoTestFixture::SetUp();

    shared_ = std::make_unique<RepeatableShared<int>>([&]() -> Co<int> {
      co_await el_->Sleep(5ms);
      co_return 42;
    });

    use_ = std::make_unique<UseFunction>(
      [&](const milliseconds delay = 0ms) -> Co<int> {
        if (delay != 0ms) {
          co_await el_->Sleep(delay);
        }
        int ret = co_await shared_->Next();
        EXPECT_EQ(el_->Now(), 5ms);
        EXPECT_EQ(ret, 42);
        co_return ret;
      });
  }

  [[nodiscard]] auto Use(const milliseconds delay = 0ms) const -> Co<int>
  {
    return (*use_)(delay);
  }
};

// ReSharper disable CppClangTidyCppcoreguidelinesAvoidCapturingLambdaCoroutines

NOLINT_TEST_F(RepeatableSharedTest, Smoke)
{
  oxygen::co::Run(*el_, [this]() -> Co<> {
    auto [x, y] = co_await AllOf(Use(), Use(1ms));
    EXPECT_EQ(x, 42);
    EXPECT_EQ(y, 42);
    EXPECT_EQ(el_->Now(), 5ms);
  });
}

NOLINT_TEST_F(RepeatableSharedTest, WeatherMonitoring)
{
  struct WeatherData {
    float temperature;
    float humidity;
    std::chrono::milliseconds timestamp;
  };

  // Create the fetcher function
  auto make_fetcher = [this]() -> Co<WeatherData> {
    co_await el_->Sleep(100ms);
    co_return WeatherData { .temperature = 20.0f, // Fixed value for testing
      .humidity = 65.0f, // Fixed value for testing
      .timestamp = el_->Now() };
  };

  // Create repeatable shared operation using the fetch function
  RepeatableShared<WeatherData> weather { make_fetcher };
  std::vector<WeatherData> temp_readings;
  std::vector<WeatherData> humid_readings;

  auto temp_monitor = [&]() -> Co<> {
    for (int i = 0; i < 3; i++) {
      auto data = co_await weather.Next();
      auto lock = co_await weather.Lock();
      temp_readings.push_back(data);
      co_await el_->Sleep(50ms);
    }
  };

  auto humid_monitor = [&]() -> Co<> {
    for (int i = 0; i < 3; i++) {
      auto data = co_await weather.Next();
      auto lock = co_await weather.Lock();
      humid_readings.push_back(data);
      co_await el_->Sleep(75ms);
    }
  };

  oxygen::co::Run(
    *el_, [&]() -> Co<> { co_await AllOf(temp_monitor(), humid_monitor()); });

  ASSERT_EQ(temp_readings.size(), 3);
  ASSERT_EQ(humid_readings.size(), 3);

  // Verify both monitors got the same data in each iteration
  for (size_t i = 0; i < 3; i++) {
    EXPECT_EQ(temp_readings[i].temperature, humid_readings[i].temperature);
    EXPECT_EQ(temp_readings[i].humidity, humid_readings[i].humidity);
    EXPECT_EQ(temp_readings[i].timestamp, humid_readings[i].timestamp);
  }

  // Verify timestamps are monotonically increasing
  for (size_t i = 1; i < 3; i++) {
    EXPECT_GT(temp_readings[i].timestamp, temp_readings[i - 1].timestamp);
  }
}

class RepeatableSharedConstructionTest : public OxCoTestFixture { };

NOLINT_TEST_F(RepeatableSharedConstructionTest, AsyncNoArguments)
{
  auto producer = [this]() -> Co<int> {
    co_await el_->Sleep(1ms);
    co_return 42;
  };

  RepeatableShared<int> shared { std::move(producer) };

  oxygen::co::Run(*el_, [&]() -> Co<> {
    EXPECT_EQ(co_await shared.Next(), 42);
    EXPECT_EQ(co_await shared.Next(), 42);
  });
}

NOLINT_TEST_F(RepeatableSharedConstructionTest, AsyncWithArguments)
{
  auto producer = [this](int value) -> Co<int> {
    co_await el_->Sleep(1ms);
    co_return value;
  };

  // Create RepeatableShared with producer and initial argument
  RepeatableShared<int> shared { producer, 42 };

  oxygen::co::Run(*el_, [&]() -> Co<> {
    // First iteration should return 42
    EXPECT_EQ(co_await shared.Next(), 42);

    // Second iteration should also return 42 since args are stored
    EXPECT_EQ(co_await shared.Next(), 42);

    // Create a new instance with different argument
    auto producer2 = producer;
    RepeatableShared<int> shared2 { std::move(producer2), 84 };
    EXPECT_EQ(co_await shared2.Next(), 84);
  });
}

} // namespace

// NOLINTEND(*-avoid-capturing-lambda-coroutines)
// NOLINTEND(*-avoid-reference-coroutine-parameters)
