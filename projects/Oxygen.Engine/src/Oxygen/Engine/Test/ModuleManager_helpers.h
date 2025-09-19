//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>

namespace oxygen::engine::test {

using namespace oxygen::core;
using namespace oxygen::engine;
using oxygen::co::testing::TestEventLoop;
using namespace oxygen::co;

//! Basic dummy module for registration and ordering tests
class DummyModule : public EngineModule {
  OXYGEN_TYPED(DummyModule)

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

//! Simple dummy synchronous module used for ordered phases
class SyncModule : public EngineModule {
  OXYGEN_TYPED(SyncModule)

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

//! Async module that records coroutine handlers executed
class AsyncModule : public EngineModule {
  OXYGEN_TYPED(AsyncModule)

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

//! Initialization test module must be a non-local type so coroutine member
//! functions can be properly instantiated by the compiler. We allow an
//! external atomic<int>* to be supplied so tests can observe lifecycle state
//! without keeping a pointer to the owned module instance.
struct InitModule : EngineModule {
  OXYGEN_TYPED(InitModule)

public:
  explicit InitModule(std::atomic<int>& external)
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

//! Module that throws exceptions in synchronous handlers
class ThrowingSyncModule : public EngineModule {
  OXYGEN_TYPED(ThrowingSyncModule)

public:
  ThrowingSyncModule(std::string name, ModulePriority p, ModulePhaseMask mask,
    bool is_critical = false)
    : name_(std::move(name))
    , priority_(p)
    , mask_(mask)
    , is_critical_(is_critical)
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
  auto IsCritical() const noexcept -> bool override { return is_critical_; }

  auto OnFrameStart(FrameContext&) -> void override
  {
    calls.push_back("OnFrameStart-before-throw");
    throw std::runtime_error("Test exception from OnFrameStart");
  }

  auto OnFrameEnd(FrameContext&) -> void override
  {
    calls.push_back("OnFrameEnd-before-throw");
    throw std::logic_error("Test exception from OnFrameEnd");
  }

  std::vector<std::string> calls;

private:
  std::string name_;
  ModulePriority priority_;
  ModulePhaseMask mask_;
  bool is_critical_;
};

//! Module that throws exceptions in async handlers
class ThrowingAsyncModule : public EngineModule {
  OXYGEN_TYPED(ThrowingAsyncModule)

public:
  ThrowingAsyncModule(std::string name, ModulePriority p, ModulePhaseMask mask,
    TestEventLoop* loop, bool is_critical = false)
    : name_(std::move(name))
    , priority_(p)
    , mask_(mask)
    , dummy_loop_(loop)
    , is_critical_(is_critical)
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
  auto IsCritical() const noexcept -> bool override { return is_critical_; }

  auto OnInput(FrameContext&) -> Co<> override
  {
    calls.push_back("OnInput-before-throw");
    co_await dummy_loop_->Sleep(milliseconds(0));
    throw std::runtime_error("Test exception from OnInput");
  }

  auto OnGameplay(FrameContext&) -> Co<> override
  {
    calls.push_back("OnGameplay-before-throw");
    co_await dummy_loop_->Sleep(milliseconds(0));
    throw std::invalid_argument("Test exception from OnGameplay");
  }

  std::vector<std::string> calls;

private:
  std::string name_;
  ModulePriority priority_;
  ModulePhaseMask mask_;
  TestEventLoop* dummy_loop_;
  bool is_critical_;
};

//! Module that can optionally throw based on configuration
class ConditionalThrowingModule : public EngineModule {
  OXYGEN_TYPED(ConditionalThrowingModule)

public:
  ConditionalThrowingModule(std::string name, ModulePriority p,
    ModulePhaseMask mask, TestEventLoop* loop = nullptr,
    bool is_critical = false)
    : name_(std::move(name))
    , priority_(p)
    , mask_(mask)
    , dummy_loop_(loop)
    , is_critical_(is_critical)
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
  auto IsCritical() const noexcept -> bool override { return is_critical_; }

  auto OnFrameStart(FrameContext&) -> void override
  {
    calls.push_back("OnFrameStart");
    if (should_throw_sync) {
      throw std::runtime_error("Conditional sync exception");
    }
  }

  auto OnInput(FrameContext&) -> Co<> override
  {
    calls.push_back("OnInput-start");
    if (dummy_loop_) {
      co_await dummy_loop_->Sleep(milliseconds(0));
    }
    if (should_throw_async) {
      throw std::runtime_error("Conditional async exception");
    }
    calls.push_back("OnInput-end");
    co_return;
  }

  bool should_throw_sync = false;
  bool should_throw_async = false;
  std::vector<std::string> calls;

private:
  std::string name_;
  ModulePriority priority_;
  ModulePhaseMask mask_;
  TestEventLoop* dummy_loop_;
  bool is_critical_;
};

//! Module that properly reports errors using the protected helper method
class ErrorReportingModule : public EngineModule {
  OXYGEN_TYPED(ErrorReportingModule)

public:
  ErrorReportingModule(std::string name, ModulePriority p, ModulePhaseMask mask,
    bool is_critical = false)
    : name_(std::move(name))
    , priority_(p)
    , mask_(mask)
    , is_critical_(is_critical)
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
  auto IsCritical() const noexcept -> bool override { return is_critical_; }

  auto OnFrameStart(FrameContext& context) -> void override
  {
    calls.push_back("OnFrameStart-before-error");
    ReportError(context, "Test error from OnFrameStart using helper method");
    calls.push_back("OnFrameStart-after-error");
  }

  auto OnInput(FrameContext& context) -> Co<> override
  {
    calls.push_back("OnInput-before-error");
    ReportError(context, "Test error from OnInput using helper method");
    calls.push_back("OnInput-after-error");
    co_return;
  }

  std::vector<std::string> calls;

private:
  std::string name_;
  ModulePriority priority_;
  ModulePhaseMask mask_;
  bool is_critical_;
};

} // namespace oxygen::engine::test
