//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <gtest/gtest.h>

#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>

#include "Oxygen/OxCo/Algorithms.h"
#include "Oxygen/OxCo/Co.h"
#include "Oxygen/OxCo/Run.h"
#include "Oxygen/OxCo/asio.h"

using namespace oxygen::co;

namespace {

using Clock = std::chrono::high_resolution_clock;
using namespace std::chrono_literals;

class AsioTestFixture : public testing::Test {
protected:
    std::unique_ptr<asio::io_context> io_ {};

    void SetUp() override
    {
        testing::internal::CaptureStderr();
        io_ = std::make_unique<asio::io_context>();
    }

    void TearDown() override
    {
        io_.reset();
        const auto captured_stderr = testing::internal::GetCapturedStderr();
        std::cout << "Captured stderr:\n"
                  << captured_stderr << '\n';
    }
};

// ReSharper disable CppClangTidyCppcoreguidelinesAvoidCapturingLambdaCoroutines

class AsioTest : public AsioTestFixture { };

TEST_F(AsioTest, SmokeTest)
{
    ::Run(*io_, [&]() -> Co<> {
        asio::steady_timer t(*io_);
        t.expires_from_now(100ms);
        const auto from = Clock::now();
        co_await t.async_wait(asio_awaitable);
        EXPECT_GE(Clock::now() - from, 90ms);
    });
}

TEST_F(AsioTest, AnyOf)
{
    ::Run(*io_, [&]() -> Co<> {
        asio::steady_timer t1(*io_), t2(*io_);
        t1.expires_from_now(100ms);
        t2.expires_from_now(500ms);
        const auto from = Clock::now();
        auto [s1, s2] = co_await AnyOf(
            t1.async_wait(asio_awaitable),
            t2.async_wait(asio_awaitable));

        const auto d = Clock::now() - from;
        EXPECT_GE(d, 90ms);
        EXPECT_LE(d, 150ms);

        EXPECT_TRUE(s1);
        EXPECT_FALSE(s2);
    });
}

TEST_F(AsioTest, SleepFor)
{
    ::Run(*io_, [&]() -> Co<> {
        const auto from = Clock::now();
        co_await SleepFor(*io_, 100ms);
        const auto d = Clock::now() - from;
        EXPECT_GE(d, 100ms);
    });
}

TEST_F(AsioTest, SocketSmoke)
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
                co_await async_write(sock, asio::buffer("hello, world"), asio_awaitable);
            },
            [&]() -> Co<> {
                tcp::socket sock(*io_);
                DLOG_F(WARNING, "connecting");
                co_await sock.async_connect(
                    tcp::endpoint(asio::ip::address::from_string("127.0.0.1"), 8078),
                    asio_awaitable);
                DLOG_F(WARNING, "connected");
                char buf[12];
                const size_t n = co_await async_read(sock, asio::buffer(buf), asio_awaitable);
                EXPECT_EQ(std::string(buf, n), "hello, world");
            });
    });
}

} // namespace
