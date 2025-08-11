//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#define ASIO_NO_TYPEID
#include <asio/ip/address.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>

#include <Oxygen/Testing/GTest.h>
#include <random>

#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Semaphore.h>

#include <Oxygen/OxCo/asio.h>

#include <Oxygen/OxCo/ThreadPool.h>

using namespace oxygen::co;

namespace {

using Clock = std::chrono::high_resolution_clock;
using namespace std::chrono_literals;

class AsioTestFixture : public testing::Test {
protected:
  std::unique_ptr<asio::io_context> io_ {};

  auto SetUp() -> void override
  {
    testing::internal::CaptureStderr();
    io_ = std::make_unique<asio::io_context>();
  }

  auto TearDown() -> void override
  {
    io_.reset();
    const auto captured_stderr = testing::internal::GetCapturedStderr();
    std::cout << "Captured stderr:\n" << captured_stderr << '\n';
  }
};

// ReSharper disable CppClangTidyCppcoreguidelinesAvoidCapturingLambdaCoroutines

class AsioTest : public AsioTestFixture { };

NOLINT_TEST_F(AsioTest, SmokeTest)
{
  ::Run(*io_, [&]() -> Co<> {
    asio::steady_timer t(*io_);
    t.expires_after(100ms);
    const auto from = Clock::now();
    co_await t.async_wait(asio_awaitable);
    EXPECT_GE(Clock::now() - from, 90ms);
  });
}

NOLINT_TEST_F(AsioTest, AnyOf)
{
  ::Run(*io_, [&]() -> Co<> {
    asio::steady_timer t1(*io_), t2(*io_);
    t1.expires_after(100ms);
    t2.expires_after(500ms);
    const auto from = Clock::now();
    auto [s1, s2] = co_await AnyOf(
      t1.async_wait(asio_awaitable), t2.async_wait(asio_awaitable));

    const auto d = Clock::now() - from;
    EXPECT_GE(d, 90ms);
    EXPECT_LE(d, 150ms);

    EXPECT_TRUE(s1);
    EXPECT_FALSE(s2);
  });
}

NOLINT_TEST_F(AsioTest, SleepFor)
{
  ::Run(*io_, [&]() -> Co<> {
    const auto from = Clock::now();
    co_await SleepFor(*io_, 100ms);
    const auto d = Clock::now() - from;
    EXPECT_GE(d, 100ms);
  });
}

NOLINT_TEST_F(AsioTest, SocketSmoke)
{
  using tcp = asio::ip::tcp;
  ::Run(*io_, [&]() -> Co<> {
    tcp::acceptor acceptor(*io_, tcp::endpoint(tcp::v4(), 8078));
    co_await AllOf(
      [&]() -> Co<> {
        tcp::socket sock(*io_);
        DLOG_F(WARNING, "accepting");
        co_await acceptor.async_accept(sock, asio_awaitable);
        DLOG_F(WARNING, "accepted");
        co_await async_write(
          sock, asio::buffer("hello, world"), asio_awaitable);
      },
      [&]() -> Co<> {
        tcp::socket sock(*io_);
        DLOG_F(WARNING, "connecting");
        auto endpoint = tcp::endpoint(asio::ip::address_v4(0x7f000001), 8078);
        co_await sock.async_connect(endpoint, asio_awaitable);
        DLOG_F(WARNING, "connected");
        char buf[12];
        const size_t n
          = co_await async_read(sock, asio::buffer(buf), asio_awaitable);
        EXPECT_EQ(std::string(buf, n), "hello, world");
      });
  });
}

class AsioThreadPoolTest : public AsioTestFixture {
public:
  static auto DoWork(const int length)
  {
    static std::atomic<uint32_t> thread_id { 0 };
    thread_local std::mt19937 rng { ++thread_id };
    const double cutoff = 1.0 / length;
    uint64_t c = 0;
    while (std::generate_canonical<double, 32>(rng) > cutoff) {
      ++c;
    }
    return c;
  }

protected:
  auto SetUp() -> void override
  {
    AsioTestFixture::SetUp();
    tp_ = std::make_unique<ThreadPool>(*io_, 4);
  }

  std::unique_ptr<ThreadPool> tp_ {};
};

NOLINT_TEST_F(AsioThreadPoolTest, Smoke)
{
  ::Run(*io_, [&]() -> Co<> {
    const auto tid
      = co_await tp_->Run([] { return std::this_thread::get_id(); });
    EXPECT_NE(tid, std::this_thread::get_id());
  });
}

NOLINT_TEST_F(AsioThreadPoolTest, Exception)
{
  ::Run(*io_, [&]() -> Co<> {
    try {
      co_await tp_->Run([] { throw std::runtime_error("Boom!"); });
    } catch (const std::exception&) {
      // Expected exception, do nothing
      (void)0;
    }
  });
}

NOLINT_TEST_F(AsioThreadPoolTest, Cancellation_Confirmed)
{
  ::Run(*io_, [&]() -> Co<> {
    std::atomic<bool> confirmed { false };
    auto body = [&]() -> Co<> {
      co_await tp_->Run([&](ThreadPool::CancelToken cancelled) {
        while (!cancelled) { }
        confirmed = true;
      });
      ADD_FAILURE() << "should never reach here";
    };
    co_await AnyOf(body, SleepFor(*io_, 1ms));
    EXPECT_TRUE(confirmed.load());
  });
}

NOLINT_TEST_F(AsioThreadPoolTest, Cancellation_UnConfirmed)
{
  ::Run(*io_, [&]() -> Co<> {
    std::atomic<bool> cancelled { false };
    auto body = [&]() -> Co<int> {
      int ret = co_await tp_->Run([&](ThreadPool::CancelToken) {
        while (!cancelled.load(std::memory_order_relaxed)) { }
        return 42;
      });
      // Cancel token was not consumed, so this line should get executed
      co_return ret;
    };

    auto [ret, _] = co_await AnyOf(body, [&]() -> Co<> {
      co_await SleepFor(*io_, 1ms);
      cancelled = true;
    });
    EXPECT_EQ(ret, 42);
  });
}

NOLINT_TEST_F(AsioThreadPoolTest, Stress)
{
  ::Run(*io_, [&]() -> Co<> {
    auto run_stress = [&](int length, int count) -> Co<uint64_t> {
      uint64_t ret = 0;
      Semaphore sem { 1000 };
      OXCO_WITH_NURSERY(n)
      {
        while (count--) {
          n.Start([&sem, &ret, this, length]() -> Co<> {
            auto lk = co_await sem.Lock(); // limit concurrency
            ret += co_await tp_->Run([this, length] { return DoWork(length); });
          });
        }
        co_return kJoin;
      };
      co_return ret;
    };

    // These tasks run around 1us each
    co_await run_stress(500, 1000);
  });
}

} // namespace
