//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/OxCo/Algorithms.h"

#include <chrono>

#include <Oxygen/Testing/GTest.h>

#include "Oxygen/OxCo/Co.h"
#include "Oxygen/OxCo/Run.h"
#include "Utils/OxCoTestFixture.h"
#include "Utils/TestEventLoop.h"

using namespace std::chrono_literals;
using namespace oxygen::co::detail;
using namespace oxygen::co;
using namespace oxygen::co::testing;

namespace {

// ReSharper disable CppClangTidyCppcoreguidelinesAvoidCapturingLambdaCoroutines

class MuxRangeTest : public OxCoTestFixture {
protected:
    auto MakeTask(const int x) const -> Co<int>
    {
        co_await el_->Sleep(milliseconds(x));
        co_return x * 100;
    }
};

NOLINT_TEST_F(MuxRangeTest, AnyOf)
{
    oxygen::co::Run(*el_, [this]() -> Co<> {
        std::vector<Co<int>> v {};
        v.push_back(MakeTask(3));
        v.push_back(MakeTask(2));
        v.push_back(MakeTask(5));

        const auto ret = co_await AnyOf(v);
        EXPECT_EQ(el_->Now(), 2ms);
        EXPECT_EQ(ret.size(), 3);
        EXPECT_FALSE(ret[0]);
        EXPECT_EQ(*ret[1], 200);
        EXPECT_FALSE(ret[2]);
    });
}

NOLINT_TEST_F(MuxRangeTest, AnyOfImmediateFront)
{
    oxygen::co::Run(*el_, [this]() -> Co<> {
        auto imm = []() -> Co<int> { co_return 0; };
        std::vector<Co<int>> v {};
        v.push_back(imm());
        v.push_back(MakeTask(2));
        co_await AnyOf(v);
    });
}

NOLINT_TEST_F(MuxRangeTest, AnyOfImmediateBack)
{
    oxygen::co::Run(*el_, [this]() -> Co<> {
        auto imm = []() -> Co<int> { co_return 0; };
        std::vector<Co<int>> v {};
        v.push_back(MakeTask(2));
        v.push_back(imm());
        co_await AnyOf(v);
    });
}

NOLINT_TEST_F(MuxRangeTest, AnyOfEmpty)
{
    oxygen::co::Run(*el_, []() -> Co<> {
        std::vector<Co<int>> v {};
        co_await AnyOf(v);
    });
}

NOLINT_TEST_F(MuxRangeTest, AllOfEmpty)
{
    oxygen::co::Run(*el_, []() -> Co<> {
        std::vector<Co<int>> v {};
        co_await AllOf(v);
    });
}

NOLINT_TEST_F(MuxRangeTest, AllOf)
{
    oxygen::co::Run(*el_, [this]() -> Co<> {
        std::vector<Co<int>> v {};
        v.push_back(MakeTask(3));
        v.push_back(MakeTask(2));
        v.push_back(MakeTask(5));

        const auto ret = co_await AllOf(v);
        EXPECT_EQ(el_->Now(), 5ms);
        EXPECT_EQ(ret[0], 300);
        EXPECT_EQ(ret[1], 200);
        EXPECT_EQ(ret[2], 500);
    });
}

} // namespace
