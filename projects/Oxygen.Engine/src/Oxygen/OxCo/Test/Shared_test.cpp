//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Oxygen/OxCo/Algorithms.h"
#include "Oxygen/OxCo/Awaitables.h"
#include "Oxygen/OxCo/Co.h"
#include "Oxygen/OxCo/Run.h"
#include "Oxygen/OxCo/Shared.h"
#include "Utils/OxCoTestFixture.h"
#include "Utils/TestEventLoop.h"

using namespace std::chrono_literals;
using namespace oxygen::co::detail;
using namespace oxygen::co;
using namespace oxygen::co::testing;

namespace {

class SharedTest : public OxCoTestFixture {
protected:
    using UseFunction = std::function<Co<int>(std::chrono::milliseconds)>;
    using SharedProducer = std::function<Co<int>()>;

    Shared<SharedProducer> shared_ {};
    std::unique_ptr<UseFunction> use_ {};

    void SetUp() override
    {
        OxCoTestFixture::SetUp();

        shared_ = Shared<SharedProducer>(std::in_place, [&]() -> Co<int> {
            co_await el_->Sleep(5ms);
            co_return 42;
        });

        use_ = std::make_unique<UseFunction>([&](milliseconds delay = 0ms) -> Co<int> {
            if (delay != 0ms) {
                co_await el_->Sleep(delay);
            }
            int ret = co_await shared_;
            EXPECT_EQ(el_->Now(), 5ms);
            EXPECT_EQ(ret, 42);
            co_return std::move(ret);
        });
    }

    [[nodiscard]] auto Use(const milliseconds delay = 0ms) const -> Co<int> { return (*use_)(delay); }
};

// ReSharper disable CppClangTidyCppcoreguidelinesAvoidCapturingLambdaCoroutines

TEST_F(SharedTest, Smoke)
{
    oxygen::co::Run(*el_, [this]() -> Co<> {
        auto [x, y] = co_await AllOf(Use(), Use(1ms));
        EXPECT_EQ(x, 42);
        EXPECT_EQ(y, 42);
        EXPECT_EQ(el_->Now(), 5ms);
    });
}

} // namespace
