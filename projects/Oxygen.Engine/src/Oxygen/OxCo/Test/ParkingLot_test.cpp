//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/OxCo/ParkingLot.h"

#include <array>
#include <coroutine>
#include <gtest/gtest.h>

using namespace oxygen::co;

class MockCoroutine {
public:
    struct promise_type {
        // ReSharper disable CppMemberFunctionMayBeStatic
        // NOLINTNEXTLINE(*-convert-member-functions-to-static)
        auto get_return_object() -> MockCoroutine { return {}; }
        // NOLINTNEXTLINE(*-convert-member-functions-to-static)
        auto initial_suspend() -> std::suspend_never { return {}; }
        // NOLINTNEXTLINE(*-convert-member-functions-to-static)
        auto final_suspend() noexcept -> std::suspend_never { return {}; }
        void unhandled_exception() { }
        void return_void() { }
        // ReSharper restore CppMemberFunctionMayBeStatic
    };
};

TEST(ParkingLotTest, ParkAndUnParkOne)
{
    ParkingLot lot;
    constexpr size_t num_coroutines = 10;
    std::array<bool, num_coroutines> parked_flags { true };

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)s
    auto create_coroutine = [&](const size_t index) -> MockCoroutine {
        co_await lot.Park();
        parked_flags[index] = false;
    };

    std::array<MockCoroutine, num_coroutines> coroutines {};
    for (size_t i = 0; i < num_coroutines; ++i) {
        parked_flags[i] = true;
        coroutines[i] = create_coroutine(i);
    }

    EXPECT_FALSE(lot.Empty());

    for (size_t i = 0; i < num_coroutines; ++i) {
        lot.UnParkOne();
        EXPECT_EQ(parked_flags[i], false);
    }

    EXPECT_TRUE(lot.Empty());
}

TEST(ParkingLotTest, ParkAndUnParkAll)
{
    ParkingLot lot;
    constexpr size_t num_coroutines = 10;
    std::array<bool, num_coroutines> parked_flags { true };

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)s
    auto create_coroutine = [&](const size_t index) -> MockCoroutine {
        co_await lot.Park();
        parked_flags[index] = false;
    };

    std::array<MockCoroutine, num_coroutines> coroutines {};
    for (size_t i = 0; i < num_coroutines; ++i) {
        parked_flags[i] = true;
        coroutines[i] = create_coroutine(i);
    }

    EXPECT_FALSE(lot.Empty());

    lot.UnParkAll();

    EXPECT_TRUE(lot.Empty());
    for (size_t i = 0; i < num_coroutines; ++i) {
        EXPECT_FALSE(parked_flags[i]);
    }
}

TEST(ParkingLotTest, Empty)
{
    ParkingLot lot;
    bool parked { true };

    EXPECT_TRUE(lot.Empty());

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)s
    auto coro = [&lot, &parked]() -> MockCoroutine {
        co_await lot.Park();
        parked = false;
    };
    coro();
    EXPECT_FALSE(lot.Empty());
    EXPECT_TRUE(parked);

    lot.UnParkOne();
    EXPECT_TRUE(lot.Empty());
    EXPECT_FALSE(parked);
}
