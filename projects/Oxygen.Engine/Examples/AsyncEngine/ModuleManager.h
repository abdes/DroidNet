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

#include "IEngineModule.h"
#include "ModuleContext.h"

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
      module->GetName(), static_cast<uint32_t>(module->GetPriority()),
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

  //! Initialize all modules
  auto InitializeModules(ModuleContext& context) -> co::Co<>
  {
    LOG_F(INFO, "Initializing {} modules", modules_.size());

    for (auto& module : modules_) {
      try {
        LOG_F(1, "Initializing module '{}'", module->GetName());
        co_await module->Initialize(context);
        LOG_F(1, "Module '{}' initialized successfully", module->GetName());
      } catch (const std::exception& e) {
        LOG_F(ERROR, "Failed to initialize module '{}': {}", module->GetName(),
          e.what());
        // Continue with other modules - don't let one failure stop everything
      }
    }

    LOG_F(INFO, "Module initialization complete");
  }

  //! Shutdown all modules (in reverse order)
  auto ShutdownModules(ModuleContext& context) -> co::Co<>
  {
    LOG_F(INFO, "Shutting down {} modules", modules_.size());

    // Shutdown in reverse order to handle dependencies
    for (auto it = modules_.rbegin(); it != modules_.rend(); ++it) {
      auto& module = *it;
      try {
        LOG_F(1, "Shutting down module '{}'", module->GetName());
        co_await module->Shutdown(context);
        LOG_F(1, "Module '{}' shutdown successfully", module->GetName());
      } catch (const std::exception& e) {
        LOG_F(ERROR, "Failed to shutdown module '{}': {}", module->GetName(),
          e.what());
        // Continue shutdown process despite errors
      }
    }

    LOG_F(INFO, "Module shutdown complete");
  }

  // === PHASE EXECUTION METHODS ===
  // Each method calls modules that support the corresponding phase

#define DEFINE_ORDERED_PHASE_METHOD(PhaseName, PhaseEnum, MethodName)          \
  auto Execute##PhaseName(ModuleContext& context) -> co::Co<>                  \
  {                                                                            \
    context.SetCurrentPhase(ModuleContext::FramePhase::PhaseName);             \
    co_await ExecuteOrderedPhase(                                              \
      ModulePhases::PhaseEnum,                                                 \
      [](IEngineModule& module, ModuleContext& ctx) {                          \
        return module.MethodName(ctx);                                         \
      },                                                                       \
      context, #PhaseName);                                                    \
  }
  // clang-format off
  DEFINE_ORDERED_PHASE_METHOD(Input, Input, OnInput)
  DEFINE_ORDERED_PHASE_METHOD(FixedSimulation, FixedSimulation, OnFixedSimulation)
  DEFINE_ORDERED_PHASE_METHOD(Gameplay, Gameplay, OnGameplay)
  DEFINE_ORDERED_PHASE_METHOD(NetworkReconciliation, NetworkReconciliation, OnNetworkReconciliation)
  DEFINE_ORDERED_PHASE_METHOD(SceneMutation, SceneMutation, OnSceneMutation)
  DEFINE_ORDERED_PHASE_METHOD(TransformPropagation, TransformPropagation, OnTransformPropagation)
  DEFINE_ORDERED_PHASE_METHOD(SnapshotBuild, SnapshotBuild, OnSnapshotBuild)
  DEFINE_ORDERED_PHASE_METHOD(PostParallel, PostParallel, OnPostParallel)
  DEFINE_ORDERED_PHASE_METHOD(FrameGraph, FrameGraph, OnFrameGraph)
  DEFINE_ORDERED_PHASE_METHOD(DescriptorPublication, DescriptorPublication, OnDescriptorPublication)
  DEFINE_ORDERED_PHASE_METHOD(ResourceTransitions, ResourceTransitions, OnResourceTransitions)
  DEFINE_ORDERED_PHASE_METHOD(CommandRecord, CommandRecord, OnCommandRecord)
  DEFINE_ORDERED_PHASE_METHOD(Present, Present, OnPresent)
  // clang-format on
#undef DEFINE_ORDERED_PHASE_METHOD

  //! Execute parallel work phase (Category B) - concurrent execution
  auto ExecuteParallelWork(ModuleContext& context) -> co::Co<>
  {
    context.SetCurrentPhase(ModuleContext::FramePhase::ParallelWork);

    std::vector<co::Co<>> parallel_tasks;

    for (auto& module : modules_) {
      if (HasPhase(module->GetSupportedPhases(), ModulePhases::ParallelWork)) {
        // Create parallel task for this module
        parallel_tasks.emplace_back(ExecuteModuleTask(
          *module,
          [](IEngineModule& mod, ModuleContext& ctx) {
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
  auto ExecuteAsyncWork(ModuleContext& context) -> co::Co<>
  {
    context.SetCurrentPhase(ModuleContext::FramePhase::AsyncWork);
    co_await ExecuteOrderedPhase(
      ModulePhases::AsyncWork,
      [](IEngineModule& module, ModuleContext& ctx) {
        return module.OnAsyncWork(ctx);
      },
      context, "AsyncWork");
  }

  //! Execute detached work phase (Category D) - background services
  auto ExecuteDetachedWork(ModuleContext& context) -> co::Co<>
  {
    context.SetCurrentPhase(ModuleContext::FramePhase::DetachedWork);
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
    ModuleContext& context, const char* phase_name) -> co::Co<>
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
    ModuleContext& context, const char* phase_name) -> co::Co<>
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
