//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Engine/FrameContext.h>
#include <Oxygen/Engine/Modules/EngineModule.h>
#include <Oxygen/Engine/Modules/ModuleManager.h>
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

//=== Base Test Fixture ===---------------------------------------------------//

//! Base test fixture with common helpers for ModuleManager error handling
class ModuleManagerErrorTestBase : public ::testing::Test {
protected:
  ModuleManagerErrorTestBase()
    : mgr_(nullptr)
    , ctx_()
  {
  }

  ModuleManager mgr_;
  FrameContext ctx_;
  TestEventLoop loop_;

  //! Helper to check if a module is still registered
  auto IsModuleRegistered(std::string_view name) const -> bool
  {
    return mgr_.GetModule(name).has_value();
  }

  //! Helper to get module count
  auto GetModuleCount() const -> size_t { return mgr_.GetModuleCount(); }

  //! Helper to check if FrameContext has any errors
  auto HasErrors() const -> bool { return ctx_.HasErrors(); }

  //! Helper to get error count
  auto GetErrorCount() const -> size_t { return ctx_.GetErrors().size(); }

  //! Helper to find error messages containing specific text
  auto FindErrorContaining(std::string_view text) const -> bool
  {
    const auto errors = ctx_.GetErrors();
    return std::ranges::any_of(errors, [text](const auto& error) {
      return error.message.find(text) != std::string::npos;
    });
  }
};

//! Test fixture for synchronous phase error handling scenarios
class SyncModuleErrorTest : public ModuleManagerErrorTestBase { };

//! Test fixture for asynchronous phase error handling scenarios
class AsyncModuleErrorTest : public ModuleManagerErrorTestBase { };

//! Test fixture for multiple module error scenarios
class MultiModuleErrorTest : public ModuleManagerErrorTestBase { };

//! Test fixture for edge case error scenarios
class EdgeCaseErrorTest : public ModuleManagerErrorTestBase { };

//! Test fixture for single module error reporting scenarios
class SingleModuleErrorTest : public ModuleManagerErrorTestBase { };

//=== Synchronous Phase Error Tests ===---------------------------------------//

//! Non-critical sync module throws and gets removed
NOLINT_TEST_F(SyncModuleErrorTest, NonCriticalSyncRemoved)
{
  // Arrange
  auto throwing_module
    = std::make_unique<ThrowingSyncModule>("throwing", ModulePriority { 100 },
      MakeModuleMask<PhaseId::kFrameStart>(), false /* non-critical */);
  auto normal_module = std::make_unique<SyncModule>(
    "normal", ModulePriority { 200 }, MakeModuleMask<PhaseId::kFrameStart>());

  auto* normal_ptr = normal_module.get();

  mgr_.RegisterModule(std::move(throwing_module));
  mgr_.RegisterModule(std::move(normal_module));

  ASSERT_EQ(GetModuleCount(), 2u);
  ASSERT_TRUE(IsModuleRegistered("throwing"));
  ASSERT_TRUE(IsModuleRegistered("normal"));

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    co_await mgr_.ExecutePhase(PhaseId::kFrameStart, ctx_);
    co_return;
  });

  // Assert
  // Non-critical throwing module should be removed
  EXPECT_EQ(GetModuleCount(), 1u);
  EXPECT_FALSE(IsModuleRegistered("throwing"));
  EXPECT_TRUE(IsModuleRegistered("normal"));

  // Error should be reported and then cleared when module was removed
  EXPECT_FALSE(HasErrors()); // Errors cleared when non-critical module removed

  // Normal module should have executed successfully
  ASSERT_GE(normal_ptr->calls.size(), 1u);
  EXPECT_EQ(normal_ptr->calls.front(), "OnFrameStart");

  // NOTE: Cannot check throwing_ptr->calls because the module has been deleted!
}

//! Critical sync module throws but stays registered
NOLINT_TEST_F(SyncModuleErrorTest, CriticalSyncKept)
{
  // Arrange
  auto critical_module
    = std::make_unique<ThrowingSyncModule>("critical", ModulePriority { 100 },
      MakeModuleMask<PhaseId::kFrameStart>(), true /* critical */);
  auto normal_module = std::make_unique<SyncModule>(
    "normal", ModulePriority { 200 }, MakeModuleMask<PhaseId::kFrameStart>());

  auto* critical_ptr = critical_module.get();
  auto* normal_ptr = normal_module.get();

  mgr_.RegisterModule(std::move(critical_module));
  mgr_.RegisterModule(std::move(normal_module));

  ASSERT_EQ(GetModuleCount(), 2u);

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    co_await mgr_.ExecutePhase(PhaseId::kFrameStart, ctx_);
    co_return;
  });

  // Assert
  // Critical module should NOT be removed
  EXPECT_EQ(GetModuleCount(), 2u);
  EXPECT_TRUE(IsModuleRegistered("critical"));
  EXPECT_TRUE(IsModuleRegistered("normal"));

  // Error should remain in context for engine to handle
  EXPECT_TRUE(HasErrors());
  EXPECT_EQ(GetErrorCount(), 1u);
  EXPECT_TRUE(FindErrorContaining("Test exception from OnFrameStart"));

  // Both modules should have attempted execution
  ASSERT_GE(critical_ptr->calls.size(), 1u);
  EXPECT_EQ(critical_ptr->calls.front(), "OnFrameStart-before-throw");

  ASSERT_GE(normal_ptr->calls.size(), 1u);
  EXPECT_EQ(normal_ptr->calls.front(), "OnFrameStart");
}

//=== Concurrent Phase Error Tests ===----------------------------------------//

//! Non-critical async module throws and gets removed
NOLINT_TEST_F(AsyncModuleErrorTest, NonCriticalAsyncRemoved)
{
  // Arrange
  auto throwing_module
    = std::make_unique<ThrowingAsyncModule>("throwing", ModulePriority { 100 },
      MakeModuleMask<PhaseId::kInput>(), &loop_, false /* non-critical */);
  auto normal_module = std::make_unique<AsyncModule>(
    "normal", ModulePriority { 200 }, MakeModuleMask<PhaseId::kInput>());

  auto* normal_ptr = normal_module.get();
  normal_ptr->_dummy_loop = &loop_;

  mgr_.RegisterModule(std::move(throwing_module));
  mgr_.RegisterModule(std::move(normal_module));

  ASSERT_EQ(GetModuleCount(), 2u);

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    co_await mgr_.ExecutePhase(PhaseId::kInput, ctx_);
    co_return;
  });

  // Assert
  // Non-critical throwing module should be removed
  EXPECT_EQ(GetModuleCount(), 1u);
  EXPECT_FALSE(IsModuleRegistered("throwing"));
  EXPECT_TRUE(IsModuleRegistered("normal"));

  // Error should be cleared when non-critical module was removed
  EXPECT_FALSE(HasErrors());

  // Normal module should have completed successfully
  ASSERT_GE(normal_ptr->calls.size(), 2u);
  EXPECT_EQ(normal_ptr->calls.front(), "OnInput-start");
  EXPECT_EQ(normal_ptr->calls.back(), "OnInput-end");

  // NOTE: Cannot check throwing_ptr->calls because the module has been deleted!
}

//! Critical async module throws but stays registered
NOLINT_TEST_F(AsyncModuleErrorTest, CriticalAsyncKept)
{
  // Arrange
  auto critical_module
    = std::make_unique<ThrowingAsyncModule>("critical", ModulePriority { 100 },
      MakeModuleMask<PhaseId::kGameplay>(), &loop_, true /* critical */);
  auto normal_module = std::make_unique<AsyncModule>(
    "normal", ModulePriority { 200 }, MakeModuleMask<PhaseId::kInput>());

  auto* critical_ptr = critical_module.get();
  auto* normal_ptr = normal_module.get();
  normal_ptr->_dummy_loop = &loop_;

  mgr_.RegisterModule(std::move(critical_module));
  mgr_.RegisterModule(std::move(normal_module));

  ASSERT_EQ(GetModuleCount(), 2u);

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    co_await mgr_.ExecutePhase(PhaseId::kGameplay, ctx_);
    co_return;
  });

  // Assert
  // Critical module should NOT be removed
  EXPECT_EQ(GetModuleCount(), 2u);
  EXPECT_TRUE(IsModuleRegistered("critical"));
  EXPECT_TRUE(IsModuleRegistered("normal"));

  // Error should remain in context for engine to handle
  EXPECT_TRUE(HasErrors());
  EXPECT_EQ(GetErrorCount(), 1u);
  EXPECT_TRUE(FindErrorContaining("Test exception from OnGameplay"));

  // Critical module should have attempted execution
  ASSERT_GE(critical_ptr->calls.size(), 1u);
  EXPECT_EQ(critical_ptr->calls.front(), "OnGameplay-before-throw");
}

//=== Multiple Module Error Tests ===-----------------------------------------//

//! Critical module that throws exception is preserved in manager
NOLINT_TEST_F(MultiModuleErrorTest, CriticalModuleThrows_RemainsRegistered)
{
  // Arrange
  auto critical_module
    = std::make_unique<ThrowingAsyncModule>("critical", ModulePriority { 50 },
      MakeModuleMask<PhaseId::kInput>(), &loop_, true /* critical */);

  mgr_.RegisterModule(std::move(critical_module));
  ASSERT_EQ(GetModuleCount(), 1u);

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    co_await mgr_.ExecutePhase(PhaseId::kInput, ctx_);
    co_return;
  });

  // Assert
  EXPECT_EQ(GetModuleCount(), 1u);
  EXPECT_TRUE(IsModuleRegistered("critical"));
  EXPECT_TRUE(HasErrors());
  EXPECT_EQ(GetErrorCount(), 1u);
  EXPECT_TRUE(FindErrorContaining("Test exception from OnInput"));
}

//! Non-critical module that throws exception is removed from manager
NOLINT_TEST_F(MultiModuleErrorTest, NonCriticalModuleThrows_RemovedFromManager)
{
  // Arrange
  auto noncritical_module = std::make_unique<ThrowingAsyncModule>("noncritical",
    ModulePriority { 100 }, MakeModuleMask<PhaseId::kInput>(), &loop_,
    false /* non-critical */);

  mgr_.RegisterModule(std::move(noncritical_module));
  ASSERT_EQ(GetModuleCount(), 1u);

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    co_await mgr_.ExecutePhase(PhaseId::kInput, ctx_);
    co_return;
  });

  // Assert
  EXPECT_EQ(GetModuleCount(), 0u);
  EXPECT_FALSE(IsModuleRegistered("noncritical"));
  EXPECT_FALSE(HasErrors()); // Errors cleared when non-critical module removed
}

//! Critical module error persists while non-critical module error is cleared
NOLINT_TEST_F(MultiModuleErrorTest, MixedCriticality_OnlyCriticalErrorsPersist)
{
  // Arrange
  auto critical_module
    = std::make_unique<ThrowingAsyncModule>("critical", ModulePriority { 50 },
      MakeModuleMask<PhaseId::kInput>(), &loop_, true /* critical */);
  auto noncritical_module = std::make_unique<ThrowingAsyncModule>("noncritical",
    ModulePriority { 100 }, MakeModuleMask<PhaseId::kInput>(), &loop_,
    false /* non-critical */);

  mgr_.RegisterModule(std::move(critical_module));
  mgr_.RegisterModule(std::move(noncritical_module));
  ASSERT_EQ(GetModuleCount(), 2u);

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    co_await mgr_.ExecutePhase(PhaseId::kInput, ctx_);
    co_return;
  });

  // Assert
  // Only non-critical module should be removed
  EXPECT_EQ(GetModuleCount(), 1u);
  EXPECT_TRUE(IsModuleRegistered("critical"));
  EXPECT_FALSE(IsModuleRegistered("noncritical"));

  // Only critical module error should remain
  EXPECT_TRUE(HasErrors());
  EXPECT_EQ(GetErrorCount(), 1u);
  EXPECT_TRUE(FindErrorContaining("Test exception from OnInput"));
}

//! Normal modules continue execution even when other modules throw
NOLINT_TEST_F(
  MultiModuleErrorTest, FailingModules_DoNotInterruptNormalExecution)
{
  // Arrange
  auto throwing_module
    = std::make_unique<ThrowingAsyncModule>("throwing", ModulePriority { 50 },
      MakeModuleMask<PhaseId::kInput>(), &loop_, false /* non-critical */);
  auto normal_module = std::make_unique<AsyncModule>(
    "normal", ModulePriority { 200 }, MakeModuleMask<PhaseId::kInput>());

  auto* normal_ptr = normal_module.get();
  normal_ptr->_dummy_loop = &loop_;

  mgr_.RegisterModule(std::move(throwing_module));
  mgr_.RegisterModule(std::move(normal_module));
  ASSERT_EQ(GetModuleCount(), 2u);

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    co_await mgr_.ExecutePhase(PhaseId::kInput, ctx_);
    co_return;
  });

  // Assert
  // Normal module should have completed successfully despite other module
  // failing
  ASSERT_GE(normal_ptr->calls.size(), 2u);
  EXPECT_EQ(normal_ptr->calls.front(), "OnInput-start");
  EXPECT_EQ(normal_ptr->calls.back(), "OnInput-end");
}

//! Multiple non-critical modules that fail are all removed
NOLINT_TEST_F(MultiModuleErrorTest, MultipleNonCriticalFail_AllRemoved)
{
  // Arrange
  auto module1
    = std::make_unique<ThrowingSyncModule>("fail1", ModulePriority { 100 },
      MakeModuleMask<PhaseId::kFrameStart>(), false /* non-critical */);
  auto module2
    = std::make_unique<ThrowingSyncModule>("fail2", ModulePriority { 200 },
      MakeModuleMask<PhaseId::kFrameStart>(), false /* non-critical */);

  mgr_.RegisterModule(std::move(module1));
  mgr_.RegisterModule(std::move(module2));
  ASSERT_EQ(GetModuleCount(), 2u);

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    co_await mgr_.ExecutePhase(PhaseId::kFrameStart, ctx_);
    co_return;
  });

  // Assert
  EXPECT_EQ(GetModuleCount(), 0u);
  EXPECT_FALSE(IsModuleRegistered("fail1"));
  EXPECT_FALSE(IsModuleRegistered("fail2"));
  EXPECT_FALSE(HasErrors()); // All errors cleared when modules removed
}

//! Critical modules that throw exceptions generate errors with proper TypeId
//! attribution
NOLINT_TEST_F(
  MultiModuleErrorTest, CriticalModuleErrors_IncludeTypeIdAttribution)
{
  // Arrange
  auto critical_module
    = std::make_unique<ThrowingSyncModule>("critical", ModulePriority { 100 },
      MakeModuleMask<PhaseId::kFrameStart>(), true /* critical */);

  const auto expected_type_id = critical_module->GetTypeId();
  mgr_.RegisterModule(std::move(critical_module));
  ASSERT_EQ(GetModuleCount(), 1u);

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    co_await mgr_.ExecutePhase(PhaseId::kFrameStart, ctx_);
    co_return;
  });

  // Assert
  EXPECT_EQ(GetModuleCount(), 1u); // Critical module remains
  EXPECT_TRUE(HasErrors());
  EXPECT_EQ(GetErrorCount(), 1u);

  const auto errors = ctx_.GetErrors();
  ASSERT_EQ(errors.size(), 1u);
  const auto& error = errors[0];
  EXPECT_EQ(error.source_type_id, expected_type_id);
  EXPECT_TRUE(error.message.find("Test exception from OnFrameStart")
    != std::string::npos);
}

//! Critical modules that throw exceptions generate errors with proper
//! source_key (module name)
NOLINT_TEST_F(
  MultiModuleErrorTest, CriticalModuleErrors_IncludeSourceKeyAttribution)
{
  // Arrange
  auto critical_module = std::make_unique<ThrowingSyncModule>("test_module",
    ModulePriority { 100 }, MakeModuleMask<PhaseId::kFrameStart>(),
    true /* critical */);

  mgr_.RegisterModule(std::move(critical_module));
  ASSERT_EQ(GetModuleCount(), 1u);

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    co_await mgr_.ExecutePhase(PhaseId::kFrameStart, ctx_);
    co_return;
  });

  // Assert
  EXPECT_EQ(GetModuleCount(), 1u); // Critical module remains
  EXPECT_TRUE(HasErrors());
  EXPECT_EQ(GetErrorCount(), 1u);

  const auto errors = ctx_.GetErrors();
  ASSERT_EQ(errors.size(), 1u);
  const auto& error = errors[0];
  EXPECT_TRUE(error.source_key.has_value());
  EXPECT_EQ(error.source_key.value(), "test_module");
}

//! Multiple critical modules generate distinct errors with unique TypeId and
//! source_key combinations
NOLINT_TEST_F(MultiModuleErrorTest,
  MultipleCriticalModules_GenerateDistinctErrorAttribution)
{
  // Arrange
  auto module1 = std::make_unique<ThrowingSyncModule>("first_module",
    ModulePriority { 100 }, MakeModuleMask<PhaseId::kFrameStart>(),
    true /* critical */);
  auto module2 = std::make_unique<ThrowingSyncModule>("second_module",
    ModulePriority { 200 }, MakeModuleMask<PhaseId::kFrameStart>(),
    true /* critical */);

  const auto module1_type_id = module1->GetTypeId();
  const auto module2_type_id = module2->GetTypeId();

  mgr_.RegisterModule(std::move(module1));
  mgr_.RegisterModule(std::move(module2));
  ASSERT_EQ(GetModuleCount(), 2u);

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    co_await mgr_.ExecutePhase(PhaseId::kFrameStart, ctx_);
    co_return;
  });

  // Assert
  EXPECT_EQ(GetModuleCount(), 2u); // Both critical modules remain
  EXPECT_TRUE(HasErrors());
  EXPECT_EQ(GetErrorCount(), 2u);

  // Find and verify first module error
  const auto errors = ctx_.GetErrors();
  auto module1_error = std::ranges::find_if(errors, [&](const auto& error) {
    return error.source_type_id == module1_type_id
      && error.source_key.has_value()
      && error.source_key.value() == "first_module";
  });

  ASSERT_NE(module1_error, errors.end());
  EXPECT_TRUE(module1_error->message.find("Test exception from OnFrameStart")
    != std::string::npos);

  // Find and verify second module error
  auto module2_error = std::ranges::find_if(errors, [&](const auto& error) {
    return error.source_type_id == module2_type_id
      && error.source_key.has_value()
      && error.source_key.value() == "second_module";
  });

  ASSERT_NE(module2_error, errors.end());
  EXPECT_TRUE(module2_error->message.find("Test exception from OnFrameStart")
    != std::string::npos);
}

//=== Edge Case Error Tests ===-----------------------------------------------//

//! Executing phase with no modules doesn't crash
NOLINT_TEST_F(EdgeCaseErrorTest, EmptyPhaseExecution)
{
  // Arrange
  mgr_.RegisterModule(
    std::make_unique<DummyModule>("dummy", ModulePriority { 100 },
      MakeModuleMask<PhaseId::kGameplay>())); // Different phase

  ASSERT_EQ(GetModuleCount(), 1u);

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    co_await mgr_.ExecutePhase(PhaseId::kFrameStart, ctx_);
    co_return;
  });

  // Assert
  EXPECT_EQ(GetModuleCount(), 1u);
  EXPECT_FALSE(HasErrors());
}

//! Module throws non-std::exception type
NOLINT_TEST_F(EdgeCaseErrorTest, UnknownExceptionHandling)
{
  // Override OnFrameStart to throw custom exception
  class CustomThrowingModule : public ConditionalThrowingModule {
  public:
    using ConditionalThrowingModule::ConditionalThrowingModule;

    auto OnFrameStart(FrameContext&) -> void override
    {
      calls.push_back("OnFrameStart");
      throw 42; // Non-exception type
    }
  };

  // Arrange - create a module that throws a custom exception type
  mgr_.RegisterModule(
    std::make_unique<CustomThrowingModule>("custom", ModulePriority { 100 },
      MakeModuleMask<PhaseId::kFrameStart>(), nullptr, false));

  ASSERT_EQ(GetModuleCount(), 1u);

  // Act
  oxygen::co::Run(loop_, [&]() -> Co<> {
    co_await mgr_.ExecutePhase(PhaseId::kFrameStart, ctx_);
    co_return;
  });

  // Assert
  // Module should be removed due to unknown exception
  EXPECT_EQ(GetModuleCount(), 0u);
  EXPECT_FALSE(IsModuleRegistered("custom"));
  EXPECT_FALSE(HasErrors()); // Error cleared when module removed
}

//! Module that selectively throws based on configuration
NOLINT_TEST_F(EdgeCaseErrorTest, ConditionalThrowing)
{
  // Arrange
  auto conditional_module = std::make_unique<ConditionalThrowingModule>(
    "conditional", ModulePriority { 100 },
    MakeModuleMask<PhaseId::kFrameStart, PhaseId::kInput>(), &loop_, false);

  auto* conditional_ptr = conditional_module.get();
  mgr_.RegisterModule(std::move(conditional_module));

  ASSERT_EQ(GetModuleCount(), 1u);

  // Act & Assert - First run without errors
  oxygen::co::Run(loop_, [&]() -> Co<> {
    co_await mgr_.ExecutePhase(PhaseId::kFrameStart, ctx_);
    co_return;
  });

  EXPECT_EQ(GetModuleCount(), 1u);
  EXPECT_FALSE(HasErrors());
  ASSERT_GE(conditional_ptr->calls.size(), 1u);
  EXPECT_EQ(conditional_ptr->calls.back(), "OnFrameStart");

  // Act & Assert - Second run with sync error
  conditional_ptr->should_throw_sync = true;
  oxygen::co::Run(loop_, [&]() -> Co<> {
    co_await mgr_.ExecutePhase(PhaseId::kFrameStart, ctx_);
    co_return;
  });

  // Module should be removed after throwing
  EXPECT_EQ(GetModuleCount(), 0u);
  EXPECT_FALSE(HasErrors()); // Error cleared when module removed
}

//! Test proper error reporting using the EngineModule::ReportError helper
NOLINT_TEST_F(SingleModuleErrorTest, ProperErrorReporting)
{
  // Arrange
  auto module = std::make_unique<ErrorReportingModule>("proper_reporting",
    ModulePriority { 100 }, MakeModuleMask<PhaseId::kFrameStart>(),
    true); // Make critical so it doesn't get removed
  auto* module_ptr = module.get();

  mgr_.RegisterModule(std::move(module));

  // Act - Execute phase where module reports error properly
  oxygen::co::Run(loop_,
    [&]() -> Co<> { co_await mgr_.ExecutePhase(PhaseId::kFrameStart, ctx_); });

  // Assert
  // Module should have reported error but not thrown exception
  ASSERT_GE(module_ptr->calls.size(), 2u);
  EXPECT_EQ(module_ptr->calls[0], "OnFrameStart-before-error");
  EXPECT_EQ(module_ptr->calls[1], "OnFrameStart-after-error");

  // Error should be tracked with proper source key (module name)
  EXPECT_TRUE(HasErrors());
  EXPECT_EQ(GetErrorCount(), 1u);
  const auto& errors = ctx_.GetErrors();
  EXPECT_EQ(errors.size(), 1u);

  const auto& error = errors[0];
  EXPECT_TRUE(error.source_key.has_value());
  EXPECT_EQ(error.source_key.value(), "proper_reporting");
  EXPECT_TRUE(
    error.message.find("Test error from OnFrameStart using helper method")
    != std::string::npos);

  // Critical module with error should remain registered
  EXPECT_EQ(GetModuleCount(), 1u);
  EXPECT_TRUE(IsModuleRegistered("proper_reporting"));
}

} // namespace
