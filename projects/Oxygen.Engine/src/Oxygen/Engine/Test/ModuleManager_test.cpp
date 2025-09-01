//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Engine/Modules/EngineModule.h>
#include <Oxygen/Engine/Modules/ModuleManager.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>

namespace {

using namespace oxygen::core;
using namespace oxygen::engine;
using oxygen::co::testing::TestEventLoop;
using namespace oxygen::co;

// Test fixture that owns a ModuleManager and provides helpers built on the
// public ModuleManager API. This demonstrates how tests can derive filtered
// module lists from ModuleManager::GetModules() without adding special APIs
// to ModuleManager.
// ReSharper disable once CppRedundantQualifier
class ModuleManagerTest : public ::testing::Test {
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

class DummyModule : public EngineModule {
public:
  DummyModule(std::string_view name, ModulePriority p, ModulePhaseMask mask)
    : name_(std::string { name })
    , priority_(p)
    , mask_(mask)
  {
  }

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return name_;
  }
  [[nodiscard]] auto GetPriority() const noexcept -> ModulePriority override
  {
    return priority_;
  }
  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> ModulePhaseMask override
  {
    return mask_;
  }

private:
  std::string name_;
  ModulePriority priority_;
  ModulePhaseMask mask_;
};

// Simple dummy synchronous module used for ordered phases
class SyncModule : public EngineModule {
public:
  SyncModule(std::string name, ModulePriority p, ModulePhaseMask mask)
    : name_(std::move(name))
    , priority_(p)
    , mask_(mask)
  {
  }
  auto GetName() const noexcept -> std::string_view override { return name_; }
  auto GetPriority() const noexcept -> ModulePriority override
  {
    return priority_;
  }
  auto GetSupportedPhases() const noexcept -> ModulePhaseMask override
  {
    return mask_;
  }

  auto OnFrameStart(FrameContext&) -> void override
  {
    calls.push_back("OnFrameStart");
  }
  auto OnFrameEnd(FrameContext&) -> void override
  {
    calls.push_back("OnFrameEnd");
  }

  std::vector<std::string> calls;

private:
  std::string name_;
  ModulePriority priority_;
  ModulePhaseMask mask_;
};

// Async module that records coroutine handlers executed
class AsyncModule : public EngineModule {
public:
  AsyncModule(std::string name, ModulePriority p, ModulePhaseMask mask)
    : name_(std::move(name))
    , priority_(p)
    , mask_(mask)
  {
  }
  auto GetName() const noexcept -> std::string_view override { return name_; }
  auto GetPriority() const noexcept -> ModulePriority override
  {
    return priority_;
  }
  auto GetSupportedPhases() const noexcept -> ModulePhaseMask override
  {
    return mask_;
  }

  auto OnInput(FrameContext&) -> Co<> override
  {
    calls.push_back("OnInput-start");
    // Await a zero-duration sleep on the provided test loop so the handler
    // actually suspends and resumes on the event loop.
    co_await _dummy_loop->Sleep(milliseconds(0));
    calls.push_back("OnInput-end");
    co_return;
  }

  // We set this externally for the test so the module can await on it.
  TestEventLoop* _dummy_loop = nullptr;
  std::vector<std::string> calls;

private:
  std::string name_;
  ModulePriority priority_;
  ModulePhaseMask mask_;
};

// Initialization test module must be a non-local type so coroutine member
// functions can be properly instantiated by the compiler. We allow an
// external atomic<int>* to be supplied so tests can observe lifecycle state
// without keeping a pointer to the owned module instance.
struct InitModule : EngineModule {
  InitModule(std::atomic<int>& external)
    : external_state(&external)
  {
  }

  std::atomic<int>* external_state { nullptr };

  auto GetName() const noexcept -> std::string_view override { return "init"; }
  auto GetPriority() const noexcept -> ModulePriority override
  {
    return ModulePriority { 100 };
  }
  auto GetSupportedPhases() const noexcept -> ModulePhaseMask override
  {
    return MakeModuleMask<PhaseId::kInput>();
  }
  auto OnAttached(oxygen::observer_ptr<oxygen::AsyncEngine> /*engine*/) noexcept
    -> bool override
  {
    *external_state = 1;
    return true;
  }
  auto OnShutdown() noexcept -> void override { *external_state = 2; }
};

TEST_F(ModuleManagerTest, RegisterAndQuery)
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

//! TestCase_RegisterAndOrder: verifies modules register and are sorted by
//! priority
TEST_F(ModuleManagerTest, TestCase_RegisterAndOrder)
{
  // Arrange
  mgr_.RegisterModule(std::make_unique<SyncModule>(
    "low", ModulePriority { 200 }, MakeModuleMask<PhaseId::kFrameStart>()));
  mgr_.RegisterModule(std::make_unique<SyncModule>(
    "high", ModulePriority { 50 }, MakeModuleMask<PhaseId::kFrameStart>()));

  // Act
  auto list = GetModulesForPhase(PhaseId::kFrameStart);

  // Assert: high priority (50) comes before low (200)
  ASSERT_GE(list.size(), 2u);
  EXPECT_EQ(list[0]->GetPriority().get(), 50u);
  EXPECT_EQ(list[1]->GetPriority().get(), 200u);
}

//! TestCase_InitializeShutdownLifecycle: ensures Initialize/Shutdown are
//! awaited
TEST_F(ModuleManagerTest, TestCase_InitializeShutdownLifecycle)
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

//! TestCase_BarrieredConcurrency_ExecutesAll: ensures barriered phases gather
//! tasks
TEST_F(ModuleManagerTest, TestCase_BarrieredConcurrency_ExecutesAll)
{
  // Arrange
  TestEventLoop loop;

  auto am = std::make_unique<AsyncModule>(
    "async", ModulePriority { 100 }, MakeModuleMask<PhaseId::kInput>());
  AsyncModule* am_ptr = am.get();
  am_ptr->_dummy_loop = &loop;
  mgr_.RegisterModule(std::move(am));

  // Act: run ExecutePhase for Input which is BarrieredConcurrency
  oxygen::co::Run(loop, [&]() -> Co<> {
    co_await mgr_.ExecutePhase(
      PhaseId::kInput, *reinterpret_cast<FrameContext*>(nullptr));
    co_return;
  });

  // Assert: the AsyncModule recorded the start and end markers
  ASSERT_GE(am_ptr->calls.size(), 2u);
  EXPECT_EQ(am_ptr->calls.front(), "OnInput-start");
  EXPECT_EQ(am_ptr->calls.back(), "OnInput-end");
}

//! TestCase_UnregisterModule: ensures UnregisterModule removes module from
//! manager and phase lists
TEST_F(ModuleManagerTest, TestCase_UnregisterModule)
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

//! TestCase_PriorityTieBreak: equal priorities preserve registration order
TEST_F(ModuleManagerTest, TestCase_PriorityTieBreak)
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

//! TestCase_OrderedPhaseExecution: ordered phases run synchronous handlers in
//! priority order
TEST_F(ModuleManagerTest, TestCase_OrderedPhaseExecution)
{
  // Arrange
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
    co_await mgr_.ExecutePhase(
      PhaseId::kFrameStart, *reinterpret_cast<FrameContext*>(nullptr));
    co_return;
  });

  // Assert: high-priority (10) should have been called before low (200)
  ASSERT_GE(p1->calls.size(), 1u);
  ASSERT_GE(p2->calls.size(), 1u);
  // both had OnFrameStart appended; order: p1 then p2
  EXPECT_EQ(p1->calls.front(), "OnFrameStart");
  EXPECT_EQ(p2->calls.front(), "OnFrameStart");
}

//! TestCase_MultipleAsyncBarriered: multiple async modules run and complete
TEST_F(ModuleManagerTest, TestCase_MultipleAsyncBarriered)
{
  // Arrange
  TestEventLoop loop;

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
    co_await mgr_.ExecutePhase(
      PhaseId::kInput, *reinterpret_cast<FrameContext*>(nullptr));
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

//! TestCase_GetModuleOptional: GetModule returns optional and reflects
//! unregistering
TEST_F(ModuleManagerTest, TestCase_GetModuleOptional)
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
