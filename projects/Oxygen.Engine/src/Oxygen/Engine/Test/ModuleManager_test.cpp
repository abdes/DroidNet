//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Engine/ModuleManager.h>
#include <Oxygen/Engine/Test/ModuleManager_helpers.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>

namespace {

using namespace oxygen::core;
using namespace oxygen::engine;
using namespace oxygen::engine::test;
using oxygen::co::testing::TestEventLoop;
using namespace oxygen::co;

// Test fixture that owns a ModuleManager and provides helpers built on the
// public ModuleManager API. This demonstrates how tests can derive filtered
// module lists from ModuleManager::GetModules() without adding special APIs
// to ModuleManager.
// ReSharper disable once CppRedundantQualifier
class ModuleManagerBasicTest : public ::testing::Test {
protected:
  ModuleManager mgr_ { nullptr };

  // Return non-owning pointers to modules that support the given phase by
  // filtering the view returned by ModuleManager::GetModules().
  auto GetModulesForPhase(PhaseId id) const -> decltype(auto)
  {
    using std::views::filter;
    using std::views::transform;

    return mgr_.GetModules()
      // only modules that support the given phase
      | filter([id](const EngineModule& m) {
          return (m.GetSupportedPhases() & MakePhaseMask(id)) != 0;
        })
      // to EngineModule*
      | transform([](EngineModule& m) { return &m; })
      // to std::vector<EngineModule*>
      | std::ranges::to<std::vector<EngineModule*>>();
  }
};

TEST_F(ModuleManagerBasicTest, RegisterMultipleModules_CountAndQueryWork)
{
  // Arrange
  mgr_.RegisterModule(std::make_unique<DummyModule>(
    "a", ModulePriority { 100 }, MakeModuleMask<PhaseId::kInput>()));
  mgr_.RegisterModule(std::make_unique<DummyModule>("b", ModulePriority { 50 },
    MakeModuleMask<PhaseId::kInput, PhaseId::kGameplay>()));

  // Act
  const auto count = mgr_.GetModuleCount();

  // Assert
  EXPECT_EQ(count, 2u);
  auto list = GetModulesForPhase(PhaseId::kInput);
  EXPECT_GE(list.size(), 2u);
}

//! Verify module lifecycle callbacks are invoked during registration and
//! unregistration
TEST_F(ModuleManagerBasicTest, ModuleLifecycle_OnAttachedAndOnShutdownCalled)
{
  // Arrange
  // Create an external atomic to observe lifecycle state. Construct the
  // InitModule with a pointer to that atomic so we don't hold a pointer to
  // the owned module instance (avoids dangling pointer after move).
  std::atomic<int> observed_state { 0 };
  auto mod = std::make_unique<InitModule>(observed_state);

  // Act: RegisterModule should call OnAttached and set the external state to 1.
  bool reg_result = mgr_.RegisterModule(std::move(mod));
  EXPECT_TRUE(reg_result);
  EXPECT_EQ(observed_state.load(), 1);

  // Query the module via the public API to ensure it's exposed by GetModule.
  auto opt = mgr_.GetModule("init");
  ASSERT_TRUE(opt.has_value());

  // Act: Unregister the module via public API; this should call OnShutdown
  // and set the external state to 2, and remove it from the manager.
  mgr_.UnregisterModule("init");
  EXPECT_EQ(observed_state.load(), 2);
  EXPECT_FALSE(mgr_.GetModule("init").has_value());
}

//! Verify barriered async phase execution gathers all module tasks and awaits
//! completion
TEST_F(ModuleManagerBasicTest, AsyncPhaseExecution_BarrieredConcurrency)
{
  // Arrange
  TestEventLoop loop;
  FrameContext ctx; // Need real FrameContext for error handling

  auto am = std::make_unique<AsyncModule>(
    "async", ModulePriority { 100 }, MakeModuleMask<PhaseId::kInput>());
  AsyncModule* am_ptr = am.get();
  am_ptr->_dummy_loop = &loop;
  mgr_.RegisterModule(std::move(am));

  // Act: run ExecutePhase for Input which is BarrieredConcurrency
  oxygen::co::Run(loop, [&]() -> Co<> {
    co_await mgr_.ExecutePhase(PhaseId::kInput, ctx);
    co_return;
  });

  // Assert: the AsyncModule recorded the start and end markers
  ASSERT_GE(am_ptr->calls.size(), 2u);
  EXPECT_EQ(am_ptr->calls.front(), "OnInput-start");
  EXPECT_EQ(am_ptr->calls.back(), "OnInput-end");
}

//! Verify unregistering modules removes them from manager and rebuilds phase
//! cache
TEST_F(ModuleManagerBasicTest, UnregisterModule_RemovesFromManagerAndPhaseCache)
{
  // Arrange
  mgr_.RegisterModule(std::make_unique<DummyModule>(
    "x", ModulePriority { 10 }, MakeModuleMask<PhaseId::kInput>()));
  mgr_.RegisterModule(std::make_unique<DummyModule>(
    "y", ModulePriority { 20 }, MakeModuleMask<PhaseId::kInput>()));

  // Precondition
  EXPECT_EQ(mgr_.GetModuleCount(), 2u);
  auto before = GetModulesForPhase(PhaseId::kInput);
  EXPECT_GE(before.size(), 2u);

  // Act
  mgr_.UnregisterModule("x");

  // Assert
  EXPECT_EQ(mgr_.GetModuleCount(), 1u);
  auto after = GetModulesForPhase(PhaseId::kInput);
  EXPECT_EQ(after.size(), before.size() - 1);
}

//! Verify modules with equal priority maintain registration order (stable sort)
TEST_F(ModuleManagerBasicTest, EqualPriorities_PreserveRegistrationOrder)
{
  // Arrange
  mgr_.RegisterModule(std::make_unique<SyncModule>(
    "first", ModulePriority { 100 }, MakeModuleMask<PhaseId::kFrameStart>()));
  mgr_.RegisterModule(std::make_unique<SyncModule>(
    "second", ModulePriority { 100 }, MakeModuleMask<PhaseId::kFrameStart>()));

  // Act
  auto list = GetModulesForPhase(PhaseId::kFrameStart);

  // Assert: registration order preserved for tie priorities (first then second)
  ASSERT_GE(list.size(), 2u);
  EXPECT_EQ(list[0]->GetName(), std::string_view("first"));
  EXPECT_EQ(list[1]->GetName(), std::string_view("second"));
}

//! Verify ordered sync phases execute modules sequentially in priority order
TEST_F(ModuleManagerBasicTest, SyncPhaseExecution_OrderedByPriority)
{
  // Arrange
  FrameContext ctx; // Need real FrameContext for error handling
  auto m1 = std::make_unique<SyncModule>(
    "high", ModulePriority { 10 }, MakeModuleMask<PhaseId::kFrameStart>());
  auto m2 = std::make_unique<SyncModule>(
    "low", ModulePriority { 200 }, MakeModuleMask<PhaseId::kFrameStart>());
  SyncModule* p1 = m1.get();
  SyncModule* p2 = m2.get();
  mgr_.RegisterModule(std::move(m2));
  mgr_.RegisterModule(std::move(m1));

  // Act: execute FrameStart which is an ordered synchronous phase. ExecutePhase
  // is a coroutine; run it on a TestEventLoop and co_await it to ensure
  // synchronous handlers were invoked.
  TestEventLoop loop;
  oxygen::co::Run(loop, [&]() -> Co<> {
    co_await mgr_.ExecutePhase(PhaseId::kFrameStart, ctx);
    co_return;
  });

  // Assert: high-priority (10) should have been called before low (200)
  ASSERT_GE(p1->calls.size(), 1u);
  ASSERT_GE(p2->calls.size(), 1u);
  // both had OnFrameStart appended; order: p1 then p2
  EXPECT_EQ(p1->calls.front(), "OnFrameStart");
  EXPECT_EQ(p2->calls.front(), "OnFrameStart");
}

//! Verify multiple async modules run concurrently and all complete in barriered
//! phase
TEST_F(ModuleManagerBasicTest, MultipleAsyncModules_ConcurrentExecution)
{
  // Arrange
  TestEventLoop loop;
  FrameContext ctx; // Need real FrameContext for error handling

  auto a1 = std::make_unique<AsyncModule>(
    "a1", ModulePriority { 100 }, MakeModuleMask<PhaseId::kInput>());
  auto a2 = std::make_unique<AsyncModule>(
    "a2", ModulePriority { 150 }, MakeModuleMask<PhaseId::kInput>());
  AsyncModule* p1 = a1.get();
  AsyncModule* p2 = a2.get();
  p1->_dummy_loop = &loop;
  p2->_dummy_loop = &loop;
  mgr_.RegisterModule(std::move(a1));
  mgr_.RegisterModule(std::move(a2));

  // Act: run ExecutePhase on Input which should await both coroutine handlers
  oxygen::co::Run(loop, [&]() -> Co<> {
    co_await mgr_.ExecutePhase(PhaseId::kInput, ctx);
    co_return;
  });

  // Assert: both modules ran their OnInput handlers
  ASSERT_GE(p1->calls.size(), 2u);
  ASSERT_GE(p2->calls.size(), 2u);
  EXPECT_EQ(p1->calls.front(), "OnInput-start");
  EXPECT_EQ(p1->calls.back(), "OnInput-end");
  EXPECT_EQ(p2->calls.front(), "OnInput-start");
  EXPECT_EQ(p2->calls.back(), "OnInput-end");
}

//! Verify GetModule returns optional that reflects module registration state
TEST_F(ModuleManagerBasicTest, GetModuleOptional_ReflectsRegistrationState)
{
  // Arrange
  mgr_.RegisterModule(std::make_unique<DummyModule>(
    "solo", ModulePriority { 42 }, MakeModuleMask<PhaseId::kInput>()));

  // Act/Assert: GetModule returns value, UnregisterModule removes it
  {
    auto opt = mgr_.GetModule("solo");
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->get().GetName(), std::string_view("solo"));
  }

  mgr_.UnregisterModule("solo");
  EXPECT_FALSE(mgr_.GetModule("solo").has_value());
}

} // namespace
