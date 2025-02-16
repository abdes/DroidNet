//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/OxCo/BroadcastChannel.h>

#include <array>

#include <Oxygen/Testing/GTest.h>
#include <gmock/gmock-matchers.h>

#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>

#include "Utils/OxCoTestFixture.h"

using namespace std::chrono_literals;
using oxygen::co::BroadcastChannel;
using oxygen::co::Co;
using oxygen::co::Run;
using oxygen::co::testing::OxCoTestFixture;

// NOLINTBEGIN(*-avoid-reference-coroutine-parameters)
// NOLINTBEGIN(*-avoid-capturing-lambda-coroutines)

namespace {

class BroadcastChannelTest : public OxCoTestFixture {
public:
    BroadcastChannelTest()
        : channel_()
    {
    }

protected:
    BroadcastChannel<int> channel_;
};

// Split into these test cases:
NOLINT_TEST_F(BroadcastChannelTest, SendsValueToAllReaders)
{
    ::Run(*el_, [this]() -> Co<> {
        auto reader1 = channel_.ForRead();
        auto reader2 = channel_.ForRead();
        auto& writer = channel_.ForWrite();

        co_await writer.Send(42);

        EXPECT_EQ(*co_await reader1.Receive(), 42);
        EXPECT_EQ(*co_await reader2.Receive(), 42);
    });
}

NOLINT_TEST_F(BroadcastChannelTest, MaintainsMessageOrder)
{
    ::Run(*el_, [this]() -> Co<> {
        auto reader = channel_.ForRead();
        auto& writer = channel_.ForWrite();

        co_await writer.Send(1);
        co_await writer.Send(2);
        co_await writer.Send(3);

        EXPECT_EQ(*co_await reader.Receive(), 1);
        EXPECT_EQ(*co_await reader.Receive(), 2);
        EXPECT_EQ(*co_await reader.Receive(), 3);
    });
}

NOLINT_TEST_F(BroadcastChannelTest, ClosedChannelReturnsNullopt)
{
    ::Run(*el_, [this]() -> Co<> {
        auto reader = channel_.ForRead();
        channel_.Close();
        EXPECT_EQ(co_await reader.Receive(), nullptr);
    });
}

NOLINT_TEST_F(BroadcastChannelTest, BlocksWhenBufferFull)
{
    BroadcastChannel<int> bounded_channel(2);
    ::Run(*el_, [&]() -> Co<> {
        auto reader = bounded_channel.ForRead();
        auto& writer = bounded_channel.ForWrite();

        EXPECT_TRUE(co_await writer.Send(1));
        EXPECT_TRUE(co_await writer.Send(2));

        // Verify blocking behavior
        bool sent = false;
        OXCO_WITH_NURSERY(n)
        {
            n.Start([&]() -> Co<> {
                sent = co_await writer.Send(3);
            });

            co_await el_->Sleep(5ms);
            EXPECT_FALSE(sent); // Should be blocked
            co_return oxygen::co::kCancel;
        };
    });
}

NOLINT_TEST_F(BroadcastChannelTest, UnblocksWhenSpaceAvailable)
{
    BroadcastChannel<int> bounded_channel(2);
    ::Run(*el_, [&]() -> Co<> {
        auto reader = bounded_channel.ForRead();
        auto& writer = bounded_channel.ForWrite();

        co_await writer.Send(1);
        co_await writer.Send(2);

        bool sent = false;
        OXCO_WITH_NURSERY(n)
        {
            n.Start([&]() -> Co<> {
                sent = co_await writer.Send(3);
            });

            EXPECT_EQ(*co_await reader.Receive(), 1);
            co_await el_->Sleep(5ms);
            EXPECT_TRUE(sent);
            co_return oxygen::co::kJoin;
        };
    });
}

NOLINT_TEST_F(BroadcastChannelTest, ReaderCleanupOnDestruction)
{
    {
        auto reader = channel_.ForRead();
        EXPECT_EQ(channel_.ReaderCount(), 1);
    }
    EXPECT_EQ(channel_.ReaderCount(), 0);
}

NOLINT_TEST_F(BroadcastChannelTest, ReaderRefCountingOnCopyAndMove)
{
    auto reader1 = channel_.ForRead();
    EXPECT_EQ(channel_.ReaderCount(), 1);

    auto reader2 = reader1; // Copy
    EXPECT_EQ(channel_.ReaderCount(), 1);

    auto reader3 = std::move(reader1); // Move
    EXPECT_EQ(channel_.ReaderCount(), 1);
}

NOLINT_TEST_F(BroadcastChannelTest, BroadcastsToAllReaders)
{
    ::Run(*el_, [this]() -> Co<> {
        constexpr size_t reader_count = 5;
        std::vector<oxygen::co::ReaderContext<int>> readers;

        // Create multiple readers
        readers.reserve(reader_count);
        for (size_t i = 0; i < reader_count; ++i) {
            readers.push_back(channel_.ForRead());
        }

        EXPECT_EQ(channel_.ReaderCount(), reader_count);

        // Send a value
        auto& writer = channel_.ForWrite();
        co_await writer.Send(42);

        // All readers should get the value
        for (auto& reader : readers) {
            auto value = co_await reader.Receive();
            EXPECT_EQ(*value, 42);
        }
    });
}

NOLINT_TEST_F(BroadcastChannelTest, CloseWithPendingReaders)
{
    ::Run(*el_, [this]() -> Co<> {
        auto reader1 = channel_.ForRead();
        auto reader2 = channel_.ForRead();

        OXCO_WITH_NURSERY(n)
        {
            // Start blocked reads
            bool reader1_done = false;
            bool reader2_done = false;

            n.Start([&]() -> Co<> {
                co_await reader1.Receive();
                reader1_done = true;
            });

            n.Start([&]() -> Co<> {
                co_await reader2.Receive();
                reader2_done = true;
            });

            co_await el_->Sleep(5ms);
            EXPECT_FALSE(reader1_done);
            EXPECT_FALSE(reader2_done);

            // Close should wake up all readers
            channel_.Close();
            co_await el_->Sleep(5ms);

            EXPECT_TRUE(reader1_done);
            EXPECT_TRUE(reader2_done);

            co_return oxygen::co::kJoin;
        };
    });
}

NOLINT_TEST_F(BroadcastChannelTest, NonBlockingTrySendSucceeds)
{
    auto reader = channel_.ForRead();
    auto& writer = channel_.ForWrite();

    EXPECT_TRUE(writer.TrySend(1));
    EXPECT_FALSE(reader.Empty());
}

NOLINT_TEST_F(BroadcastChannelTest, NonBlockingTryReceiveReturnsValue)
{
    auto reader = channel_.ForRead();
    auto& writer = channel_.ForWrite();

    writer.TrySend(1);
    const auto value = reader.TryReceive();
    EXPECT_NE(value, nullptr);
    EXPECT_EQ(*value, 1);
}

// Test space calculation
NOLINT_TEST_F(BroadcastChannelTest, SpaceCalculationReflectsAllReaders)
{
    BroadcastChannel<int> bounded_channel(2);
    ::Run(*el_, [&]() -> Co<> {
        auto reader1 = bounded_channel.ForRead();
        auto reader2 = bounded_channel.ForRead();
        auto& writer = bounded_channel.ForWrite();

        EXPECT_EQ(bounded_channel.Space(), 2);
        co_await writer.Send(1);
        EXPECT_EQ(bounded_channel.Space(), 1);

        // Even if reader1 consumes, space should reflect reader2's buffer
        EXPECT_EQ(*co_await reader1.Receive(), 1);
        EXPECT_EQ(bounded_channel.Space(), 1);
    });
}

// Test multiple writers
NOLINT_TEST_F(BroadcastChannelTest, MultipleWritersCanSendConcurrently)
{
    ::Run(*el_, [this]() -> Co<> {
        auto reader = channel_.ForRead();
        auto& writer = channel_.ForWrite();

        OXCO_WITH_NURSERY(n)
        {
            n.Start([&]() -> Co<> {
                co_await writer.Send(1);
            });
            n.Start([&]() -> Co<> {
                co_await writer.Send(2);
            });

            std::vector<int> received;
            received.reserve(2);
            for (int i = 0; i < 2; ++i) {
                received.push_back(*co_await reader.Receive());
            }

            // Both values should be received in some order
            EXPECT_THAT(received, testing::UnorderedElementsAre(1, 2));
            co_return oxygen::co::kJoin;
        };
    });
}

// Test late reader behavior
NOLINT_TEST_F(BroadcastChannelTest, LateReadersMissEarlierMessages)
{
    ::Run(*el_, [this]() -> Co<> {
        auto& writer = channel_.ForWrite();
        co_await writer.Send(1);

        auto late_reader = channel_.ForRead();
        co_await writer.Send(2);

        // Late reader should only see messages after subscription
        EXPECT_EQ(*co_await late_reader.Receive(), 2);
    });
}
NOLINT_TEST_F(BroadcastChannelTest, Close)
{
    ::Run(*el_, [this]() -> Co<> {
        auto reader = channel_.ForRead();
        auto& writer = channel_.ForWrite();

        // Send a value before closing
        EXPECT_TRUE(co_await writer.Send(1));
        channel_.Close();

        // Should be able to read pending value
        EXPECT_EQ(*co_await reader.Receive(), 1);
        // Subsequent reads return nullptr
        EXPECT_EQ(co_await reader.Receive(), nullptr);

        // More writes will fail silently
        EXPECT_FALSE(co_await writer.Send(2));
    });
}

NOLINT_TEST_F(BroadcastChannelTest, CloseWithMultipleReaders)
{
    ::Run(*el_, [this]() -> Co<> {
        auto reader1 = channel_.ForRead();
        auto reader2 = channel_.ForRead();
        auto& writer = channel_.ForWrite();

        // Send a value before closing
        EXPECT_TRUE(co_await writer.Send(1));
        channel_.Close();

        // Both readers should get the pending value then nullptr
        EXPECT_EQ(*co_await reader1.Receive(), 1);
        EXPECT_EQ(co_await reader1.Receive(), nullptr);

        EXPECT_EQ(*co_await reader2.Receive(), 1);
        EXPECT_EQ(co_await reader2.Receive(), nullptr);
    });
}

NOLINT_TEST_F(BroadcastChannelTest, CloseWithPendingWriters)
{
    BroadcastChannel<int> bounded_channel(1); // Use bounded channel to force blocking
    ::Run(*el_, [&]() -> Co<> {
        auto reader = bounded_channel.ForRead();
        auto& writer = bounded_channel.ForWrite();
        bool write_completed = false;

        OXCO_WITH_NURSERY(n)
        {
            // Fill the channel first
            EXPECT_TRUE(co_await writer.Send(0));

            // Start a blocked write
            n.Start([&]() -> Co<> {
                write_completed = co_await writer.Send(1);
            });

            // Give time for the write to block
            co_await el_->Sleep(5ms);

            // Close channel while write is blocked
            bounded_channel.Close();

            // Give time for write to be cancelled
            co_await el_->Sleep(5ms);

            // Write should have been cancelled
            EXPECT_FALSE(write_completed);

            // Reader should get the first write (0) and then nullptr after close
            EXPECT_EQ(*co_await reader.Receive(), 0);
            EXPECT_EQ(co_await reader.Receive(), nullptr);

            co_return oxygen::co::kJoin;
        };
    });
}

NOLINT_TEST_F(BroadcastChannelTest, StressTestWithManyReadersAndWriters)
{
    ::Run(*el_, [this]() -> Co<> {
        constexpr size_t num_readers = 10;

        // Fix vector declaration with full type
        std::vector<oxygen::co::ReaderContext<int>> readers;
        readers.reserve(num_readers); // Pre-allocate space

        for (size_t i = 0; i < num_readers; ++i) {
            readers.push_back(channel_.ForRead());
        }

        auto& writer = channel_.ForWrite();
        OXCO_WITH_NURSERY(n)
        {
            constexpr size_t msg_per_writer = 100;
            constexpr size_t num_writers = 10;

            // Start multiple writers
            for (size_t w = 0; w < num_writers; ++w) {
                n.Start([&, w]() -> Co<> {
                    for (size_t m = 0; m < msg_per_writer; ++m) {
                        EXPECT_TRUE(co_await writer.Send(static_cast<int>(w * msg_per_writer + m)));
                    }
                });
            }

            // Verify all readers get all messages
            for (auto& reader : readers) {
                size_t received = 0;
                while (received < num_writers * msg_per_writer) {
                    auto value = co_await reader.Receive();
                    EXPECT_NE(value, nullptr);
                    received++;
                }
            }

            co_return oxygen::co::kJoin;
        };
    });
}

} // namespace

// NOLINTEND(*-avoid-capturing-lambda-coroutines)
// NOLINTEND(*-avoid-reference-coroutine-parameters)
