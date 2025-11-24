//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Engine/ModuleManager.h>
#include <Oxygen/Engine/Test/ModuleManager_helpers.h>

namespace oxygen::engine::test {

using ::testing::Test;

//! Verify that ModuleManager destructor invokes OnShutdown for all registered
//! modules in reverse-order and that modules' OnShutdown finishes normally.
TEST(ModuleManagerShutdown, CallsOnShutdownForAllModules)
{
  std::atomic<int> a { 0 };
  std::atomic<int> b { 0 };

  {
    ModuleManager mm(observer_ptr<AsyncEngine> {});

    auto mod_a = std::make_unique<InitModule>(a);
    auto mod_b = std::make_unique<InitModule>(b);

    EXPECT_TRUE(mm.RegisterModule(std::move(mod_a)));
    EXPECT_TRUE(mm.RegisterModule(std::move(mod_b)));

    // At this point both modules were attached; OnAttached should set the
    // external state to 1 for each module.
    EXPECT_EQ(a.load(), 1);
    EXPECT_EQ(b.load(), 1);
  }

  // ModuleManager went out of scope and destructor ran. InitModule::OnShutdown
  // sets the external variable to 2.
  EXPECT_EQ(a.load(), 2);
  EXPECT_EQ(b.load(), 2);
}

} // namespace oxygen::engine::test
