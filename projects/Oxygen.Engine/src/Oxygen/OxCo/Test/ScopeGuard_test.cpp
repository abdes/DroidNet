//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/OxCo/Detail/ScopeGuard.h>

#include <Oxygen/Testing/GTest.h>

using namespace oxygen::co::detail;

namespace {

NOLINT_TEST(ScopeGuardTest, ExecutesFunctionOnScopeExit)
{
    bool called = false;
    {
        auto guard = ScopeGuard([&called]() noexcept {
            called = true;
        });
        EXPECT_FALSE(called);
    }
    EXPECT_TRUE(called);
}

NOLINT_TEST(ScopeGuardTest, ExecutesFunctionWithExceptionSafety)
{
    bool called = false;
    try {
        auto guard = ScopeGuard([&called]() noexcept {
            called = true;
        });
        throw std::runtime_error("Test exception");
    } catch (const std::runtime_error& ex) {
        // Exception caught
        (void)ex;
    }
    EXPECT_TRUE(called);
}

NOLINT_TEST(ScopeGuardTest, ExecutesFunctionWithMultipleGuards)
{
    bool called1 = false;
    bool called2 = false;
    {
        auto guard1 = ScopeGuard([&called1]() noexcept {
            called1 = true;
        });
        auto guard2 = ScopeGuard([&called2]() noexcept {
            called2 = true;
        });
        EXPECT_FALSE(called1);
        EXPECT_FALSE(called2);
    }
    EXPECT_TRUE(called1);
    EXPECT_TRUE(called2);
}

} // namespace
