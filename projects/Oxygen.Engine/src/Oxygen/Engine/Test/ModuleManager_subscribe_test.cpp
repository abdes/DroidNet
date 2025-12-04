//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Engine/ModuleManager.h>
#include <Oxygen/Engine/Test/ModuleManager_helpers.h>

namespace {

using namespace oxygen::engine;
using namespace oxygen::engine::test;

TEST(ModuleManagerSubscribe, SubscribeWithReplay_ReturnsExisting)
{
  ModuleManager mgr { nullptr };

  // Pre-register two modules
  mgr.RegisterModule(std::make_unique<DummyModule>("a", ModulePriority { 10 },
    MakeModuleMask<oxygen::core::PhaseId::kInput>()));
  mgr.RegisterModule(std::make_unique<DummyModule>("b", ModulePriority { 20 },
    MakeModuleMask<oxygen::core::PhaseId::kInput>()));

  std::vector<std::string> seen;
  auto sub = mgr.SubscribeModuleAttached(
    [&seen](ModuleEvent const& ev) { seen.push_back(ev.name); },
    /*replay_existing=*/true);

  // Replay should deliver both existing modules in attach order
  ASSERT_EQ(seen.size(), 2u);
  EXPECT_EQ(seen[0], "a");
  EXPECT_EQ(seen[1], "b");

  // New module should also be delivered
  mgr.RegisterModule(std::make_unique<DummyModule>("c", ModulePriority { 30 },
    MakeModuleMask<oxygen::core::PhaseId::kInput>()));
  ASSERT_EQ(seen.size(), 3u);
  EXPECT_EQ(seen.back(), "c");
}

TEST(ModuleManagerSubscribe, SubscribeWithoutReplay_OnlyFuture)
{
  ModuleManager mgr { nullptr };

  std::vector<std::string> seen;
  auto sub = mgr.SubscribeModuleAttached(
    [&seen](ModuleEvent const& ev) { seen.push_back(ev.name); },
    /*replay_existing=*/false);

  // Register a module after subscribe -> should be delivered
  mgr.RegisterModule(std::make_unique<DummyModule>("x", ModulePriority { 5 },
    MakeModuleMask<oxygen::core::PhaseId::kInput>()));
  ASSERT_EQ(seen.size(), 1u);
  EXPECT_EQ(seen[0], "x");

  // Pre-registered modules are not delivered (none present)
}

TEST(ModuleManagerSubscribe, UnsubscribeStopsNotifications)
{
  ModuleManager mgr { nullptr };

  std::vector<std::string> seen;
  auto sub = mgr.SubscribeModuleAttached(
    [&seen](ModuleEvent const& ev) { seen.push_back(ev.name); },
    /*replay_existing=*/false);

  // Cancel subscription
  sub.Cancel();

  mgr.RegisterModule(std::make_unique<DummyModule>("z", ModulePriority { 7 },
    MakeModuleMask<oxygen::core::PhaseId::kInput>()));
  EXPECT_TRUE(seen.empty());
}

} // namespace
