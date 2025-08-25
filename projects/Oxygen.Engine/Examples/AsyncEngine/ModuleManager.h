//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include <algorithm>
#include <memory>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>

#include "FrameContext.h"
#include "IEngineModule.h"

namespace oxygen::examples::asyncsim {

//! Manages engine modules and orchestrates their execution during frame phases
//!
//! Responsibilities:
//! - Module registration and lifecycle management
//! - Ordered execution based on priorities within each phase
//! - Parallel execution for Category B phases
//! - Error handling and module isolation
class ModuleManager final {
public:
  ModuleManager() = default;
  ~ModuleManager() = default;

  OXYGEN_MAKE_NON_COPYABLE(ModuleManager)
  OXYGEN_MAKE_NON_MOVABLE(ModuleManager)

  // === MODULE REGISTRATION ===

  //! Register a module with the engine
  void RegisterModule(std::unique_ptr<IEngineModule> module)
  {
    if (!module) {
      LOG_F(WARNING, "Attempted to register null module");
      return;
    }

    LOG_F(INFO, "Registering module '{}' with priority {} and phases 0x{:X}",
      module->GetName(), module->GetPriority(),
      static_cast<uint32_t>(module->GetSupportedPhases()));

    modules_.emplace_back(std::move(module));

    // Sort modules by priority for deterministic execution order
    std::sort(
      modules_.begin(), modules_.end(), [](const auto& a, const auto& b) {
        return a->GetPriority() < b->GetPriority();
      });
  }

  //! Get count of registered modules
  [[nodiscard]] size_t GetModuleCount() const noexcept
  {
    return modules_.size();
  }

  //! Get module by name (for debugging/inspection)
  [[nodiscard]] IEngineModule* GetModule(std::string_view name) const noexcept
  {
    auto it = std::find_if(modules_.begin(), modules_.end(),
      [name](const auto& module) { return module->GetName() == name; });
    return (it != modules_.end()) ? it->get() : nullptr;
  }

  // === LIFECYCLE MANAGEMENT ===

  //! Initialize all modules with engine reference
  auto InitializeModules(AsyncEngineSimulator& engine) -> co::Co<>
  {
    LOG_SCOPE_F(
      INFO, fmt::format("Initializing {} modules", modules_.size()).c_str());

    for (auto& module : modules_) {
      LOG_SCOPE_F(INFO, fmt::format("{}", module->GetName()).c_str());
      try {
        // Pass engine reference to Initialize - modules can store as
        // observer_ptr engine lifetime > module lifetime
        co_await module->Initialize(engine);
      } catch (const std::exception& e) {
        LOG_F(ERROR, " -failed- to initialize module '{}': {}",
          module->GetName(), e.what());
        // Continue with other modules - don't let one failure stop everything
      }
    }
  }

  //! Shutdown all modules (in reverse order)
  auto ShutdownModules(AsyncEngineSimulator& engine) -> co::Co<>
  {
    LOG_SCOPE_F(
      INFO, fmt::format("Shutting down {} modules", modules_.size()).c_str());

    // Shutdown in reverse order to handle dependencies
    for (auto it = modules_.rbegin(); it != modules_.rend(); ++it) {
      auto& module = *it;
      try {
        LOG_SCOPE_F(INFO, fmt::format("{}", module->GetName()).c_str());
        co_await module->Shutdown();
        // Clear engine reference after shutdown
        module->SetEngine(nullptr);
      } catch (const std::exception& e) {
        LOG_F(ERROR, "-failed- to shutdown module '{}': {}", module->GetName(),
          e.what());
        // Continue shutdown process despite errors
      }
    }
  }

  // === PHASE EXECUTION METHODS ===

#define DEFINE_ORDERED_PHASE_METHOD(PhaseName, PhaseEnum, MethodName)          \
  void Execute##PhaseName(FrameContext& context)                               \
  {                                                                            \
    for (auto& module : modules_) {                                            \
      if (HasPhase(module->GetSupportedPhases(), ModulePhases::PhaseEnum)) {   \
        try {                                                                  \
          LOG_F(                                                               \
            2, "[{}] Executing module '{}'", #PhaseName, module->GetName());   \
          module->MethodName(context);                                         \
          LOG_F(                                                               \
            2, "[{}] Module '{}' completed", #PhaseName, module->GetName());   \
        } catch (const std::exception& e) {                                    \
          LOG_F(ERROR, "[{}] Module '{}' failed: {}", #PhaseName,              \
            module->GetName(), e.what());                                      \
        }                                                                      \
      }                                                                        \
    }                                                                          \
  }
  // clang-format off
  DEFINE_ORDERED_PHASE_METHOD(FrameStart, FrameStart, OnFrameStart)
  DEFINE_ORDERED_PHASE_METHOD(FrameEnd, FrameEnd, OnFrameEnd)
  // clang-format on
#undef DEFINE_ORDERED_PHASE_METHOD

#define DEFINE_ORDERED_PHASE_ASYNC_METHOD(PhaseName, PhaseEnum, MethodName)    \
  auto Execute##PhaseName(FrameContext& context) -> co::Co<>                   \
  {                                                                            \
    context.SetCurrentPhase(                                                   \
      FrameContext::FramePhase::PhaseName, internal::EngineTagFactory::Get()); \
    co_await ExecuteOrderedPhase(                                              \
      ModulePhases::PhaseEnum,                                                 \
      [](IEngineModule& module, FrameContext& ctx) {                           \
        return module.MethodName(ctx);                                         \
      },                                                                       \
      context, #PhaseName);                                                    \
  }
  // clang-format off
  DEFINE_ORDERED_PHASE_ASYNC_METHOD(Input, Input, OnInput)
  DEFINE_ORDERED_PHASE_ASYNC_METHOD(FixedSimulation, FixedSimulation, OnFixedSimulation)
  DEFINE_ORDERED_PHASE_ASYNC_METHOD(Gameplay, Gameplay, OnGameplay)
  DEFINE_ORDERED_PHASE_ASYNC_METHOD(NetworkReconciliation, NetworkReconciliation, OnNetworkReconciliation)
  DEFINE_ORDERED_PHASE_ASYNC_METHOD(SceneMutation, SceneMutation, OnSceneMutation)
  DEFINE_ORDERED_PHASE_ASYNC_METHOD(TransformPropagation, TransformPropagation, OnTransformPropagation)
  DEFINE_ORDERED_PHASE_ASYNC_METHOD(PostParallel, PostParallel, OnPostParallel)
  DEFINE_ORDERED_PHASE_ASYNC_METHOD(FrameGraph, FrameGraph, OnFrameGraph)
  DEFINE_ORDERED_PHASE_ASYNC_METHOD(CommandRecord, CommandRecord, OnCommandRecord)
  // NOTE: Present is engine-only and not exposed to modules
  // clang-format on
#undef DEFINE_ORDERED_PHASE_ASYNC_METHOD

  //! Execute parallel work phase (Category B) - concurrent execution
  auto ExecuteParallelWork(FrameContext& context) -> co::Co<>
  {
    std::vector<co::Co<>> parallel_tasks;

    for (auto& module : modules_) {
      if (HasPhase(module->GetSupportedPhases(), ModulePhases::ParallelWork)) {
        // Create parallel task for this module
        parallel_tasks.emplace_back(ExecuteModuleTask(
          *module,
          [](IEngineModule& mod, FrameContext& ctx) {
            return mod.OnParallelWork(ctx);
          },
          context, "ParallelWork"));
      }
    }

    if (!parallel_tasks.empty()) {
      LOG_F(1, "Executing {} parallel work tasks", parallel_tasks.size());
      co_await co::AllOf(std::move(parallel_tasks));
      LOG_F(1, "Parallel work phase complete");
    }
  }

  //! Execute async work phase (Category C) - fire-and-forget
  auto ExecuteAsyncWork(FrameContext& context) -> co::Co<>
  {
    co_await ExecuteOrderedPhase(
      ModulePhases::AsyncWork,
      [](IEngineModule& module, FrameContext& ctx) {
        return module.OnAsyncWork(ctx);
      },
      context, "AsyncWork");
  }

  //! Execute detached work phase (Category D) - background services
  auto ExecuteDetachedWork(FrameContext& context) -> co::Co<>
  {
    // Detached work doesn't block - fire and forget
    for (auto& module : modules_) {
      if (HasPhase(module->GetSupportedPhases(), ModulePhases::DetachedWork)) {
        try {
          // Start detached work but don't await it
          co_await module->OnDetachedWork(context);
        } catch (const std::exception& e) {
          LOG_F(WARNING, "Detached work failed for module '{}': {}",
            module->GetName(), e.what());
        }
      }
    }
  }

private:
  std::vector<std::unique_ptr<IEngineModule>> modules_;

  //! Execute ordered phase with sequential module execution
  template <typename MethodInvoker>
  auto ExecuteOrderedPhase(ModulePhases phase, MethodInvoker&& invoker,
    FrameContext& context, const char* phase_name) -> co::Co<>
  {
    for (auto& module : modules_) {
      if (HasPhase(module->GetSupportedPhases(), phase)) {
        co_await ExecuteModuleTask(
          *module, std::forward<MethodInvoker>(invoker), context, phase_name);
      }
    }
  }

  //! Execute individual module task with error handling
  template <typename MethodInvoker>
  auto ExecuteModuleTask(IEngineModule& module, MethodInvoker&& invoker,
    FrameContext& context, const char* phase_name) -> co::Co<>
  {
    try {
      LOG_F(2, "[{}] Executing module '{}'", phase_name, module.GetName());
      co_await invoker(module, context);
      LOG_F(2, "[{}] Module '{}' completed", phase_name, module.GetName());
    } catch (const std::exception& e) {
      LOG_F(ERROR, "[{}] Module '{}' failed: {}", phase_name, module.GetName(),
        e.what());
      // Don't rethrow - isolate module failures
    }
  }
};

} // namespace oxygen::examples::asyncsim
