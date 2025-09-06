//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//
// ModuleManager - Module Execution and Error Handling
//
// ERROR HANDLING BEHAVIOR:
// 1. Synchronous phases (FrameStart, Snapshot, FrameEnd):
//    - Errors are handled immediately as each module executes
//    - Failed modules are processed right after the exception is caught
//
// 2. Concurrent phases (Input, Gameplay, FrameGraph, etc.):
//    - Module execution happens in parallel using AllOf()
//    - Errors are collected during execution but NOT processed immediately
//    - After AllOf() completes, all errors are processed together
//    - This ensures we don't modify the module list while coroutines are
//    running
//
// 3. Module failure handling:
//    - Non-critical modules: Removed from ModuleManager, errors cleared
//    - Critical modules: Kept in ModuleManager, errors remain for engine
//    handling
//    - Phase cache is rebuilt when modules are removed
//
// 4. Module handlers are NOT noexcept:
//    - Handlers can throw exceptions which are caught by RunHandlerImpl
//    - Exceptions are converted to error reports in FrameContext
//    - This allows modules to use standard exception handling patterns
//

#include <concepts>
#include <type_traits>
#include <utility>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Engine/FrameContext.h>
#include <Oxygen/Engine/Modules/ModuleManager.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>

using oxygen::core::ExecutionModel;
using oxygen::core::kPhaseRegistry;
using oxygen::core::PhaseId;
using oxygen::core::PhaseIndex;
using oxygen::engine::EngineModule;
using oxygen::engine::FrameContext;
using oxygen::engine::ModuleManager;
using oxygen::engine::UnifiedSnapshot;

ModuleManager::ModuleManager(const observer_ptr<AsyncEngine> engine)
  : engine_(engine)
{
}

ModuleManager::~ModuleManager()
{
  for (const auto& m : modules_) {
    m->OnShutdown();
  }
  modules_.clear();
}

auto ModuleManager::RegisterModule(
  std::unique_ptr<EngineModule> module) noexcept -> bool
{
  if (!module) {
    LOG_F(WARNING, "Attempted to register null module");
    return false;
  }

  const auto name = module->GetName();
  LOG_F(INFO, "Registering module '{}' with priority {}", name,
    module->GetPriority().get());

  auto result = module->OnAttached(engine_);
  if (!result) {
    LOG_F(ERROR, "Module '{}' failed to initialize, and will not be registered",
      name);
    return false;
  }

  modules_.push_back(std::move(module));

  // Sort by priority ascending
  std::ranges::sort(modules_, [](const auto& a, const auto& b) {
    return a->GetPriority().get() < b->GetPriority().get();
  });

  RebuildPhaseCache();
  return true;
}

auto ModuleManager::UnregisterModule(std::string_view name) noexcept -> void
{
  auto it = std::ranges::find_if(
    modules_, [&](const auto& e) { return e->GetName() == name; });
  if (it == modules_.end()) {
    return;
  }

  // Not allowed to fail
  (*it)->OnShutdown();

  modules_.erase(it);
  RebuildPhaseCache();
}

auto ModuleManager::GetModule(std::string_view name) const noexcept
  -> std::optional<std::reference_wrapper<const EngineModule>>
{
  auto it = std::ranges::find_if(
    modules_, [&](const auto& e) { return e->GetName() == name; });
  if (it == modules_.end()) {
    return std::nullopt;
  }
  // `it` is an iterator to std::unique_ptr<EngineModule>. Get the raw
  // EngineModule pointer and construct an optional reference_wrapper to the
  // const EngineModule to match the function return type.
  const EngineModule* ptr = it->get();
  DCHECK_NOTNULL_F(ptr);
  return std::cref(*ptr);
}

auto ModuleManager::RebuildPhaseCache() noexcept -> void
{
  DLOG_F(2, "RebuildPhaseCache: modules_.size() = {}", modules_.size());
  for (size_t i = 0; i < modules_.size(); ++i) {
    DLOG_F(2, "  module[{}] = {} (ptr={})", i, modules_[i]->GetName(),
      static_cast<const void*>(modules_[i].get()));
  }
  // Clear each phase bucket by index. Avoid range-for because the
  // EnumIndexedArray iterator may return elements by value; indexing guarantees
  // we clear the actual stored vectors.
  for (auto p = PhaseIndex::begin(); p < PhaseIndex::end(); ++p) {
    phase_cache_[p].clear();
  }

  for (auto& m : modules_) {
    const auto mask = m->GetSupportedPhases();
    for (auto p = PhaseIndex::begin(); p < PhaseIndex::end(); ++p) {
      if ((mask & MakePhaseMask(p.to_enum())) != 0) {
        DLOG_F(2, "RebuildPhaseCache: adding {} to phase {}", m->GetName(),
          static_cast<int>(p.to_enum()));
        phase_cache_[p].push_back(m.get());
      }
    }
  }
}

auto ModuleManager::FindModuleByTypeId(TypeId type_id) const noexcept
  -> EngineModule*
{
  auto it = std::ranges::find_if(modules_,
    [type_id](const auto& module) { return module->GetTypeId() == type_id; });
  return (it != modules_.end()) ? it->get() : nullptr;
}

auto ModuleManager::HandleModuleErrors(
  FrameContext& ctx, core::PhaseId /*phase*/) noexcept -> void
{
  const auto& errors = ctx.GetErrors();
  if (errors.empty()) {
    return;
  }

  // Normalize module errors: find modules by key or type_id and set proper
  // source_key
  auto normalized_errors = errors
    | std::views::transform([this](auto error) { // Copy error to modify it
        EngineModule* module = nullptr;

        if (error.source_key.has_value()) {
          // Has key - find module by name
          auto module_opt = GetModule(error.source_key.value());
          module = module_opt.has_value()
            ? const_cast<EngineModule*>(&module_opt.value().get())
            : nullptr;
        } else {
          // No key - try to find by type_id and normalize as bad module
          module = FindModuleByTypeId(error.source_type_id);
          if (module) {
            // Normalize bad module with special key
            error.source_key = "__bad_module__";
            error.message = fmt::format("CRITICAL: Module '{}' reported error "
                                        "without proper attribution: {}",
              module->GetName(), error.message);
          }
        }

        return std::make_pair(error, module);
      })
    | std::views::filter([](const auto& pair) {
        return pair.second != nullptr; // Only process module errors
      })
    | std::ranges::to<std::vector>();

  // Handle bad module errors: clear original and report as critical
  std::ranges::for_each(normalized_errors, [this, &ctx](const auto& pair) {
    const auto& [error, module] = pair;
    if (error.source_key.has_value()
      && error.source_key.value() == "__bad_module__") {
      // Clear the original badly reported error
      ctx.ClearErrorsFromSource(error.source_type_id);
      // Report normalized critical error
      ctx.ReportError(
        TypeId {}, error.message); // Already formatted as critical
    }
  });

  // Single pipeline: collect non-critical modules to remove (exclude bad
  // modules)
  auto modules_to_remove = normalized_errors
    | std::views::filter([](const auto& pair) {
        const auto& [error, module] = pair;
        return error.source_key.has_value()
          && error.source_key.value()
          != "__bad_module__" // Don't remove bad modules
          && !module->IsCritical();
      })
    | std::ranges::to<std::vector>();

  // Remove non-critical modules and clear their errors
  std::ranges::for_each(modules_to_remove, [this, &ctx](const auto& pair) {
    const auto& [error, module] = pair;
    const auto& module_name = error.source_key.value();
    const auto module_type_id = error.source_type_id;
    UnregisterModule(module_name);

    // Clear only errors from this specific module (by TypeId AND source_key)
    ctx.ClearErrorsFromSource(module_type_id, error.source_key);
  });
}

// Lightweight RunHandler: accept a callable that either returns co::Co<> or is
// synchronous void. We adapt synchronous calls into coroutines.
namespace {

auto RunHandlerImpl(oxygen::co::Co<> awaitable, EngineModule* module,
  FrameContext& ctx) -> oxygen::co::Co<>
{
  try {
    co_await std::move(awaitable);
  } catch (const std::exception& e) {
    const auto error_message = fmt::format(
      "Module '{}' handler threw: {}", module->GetName(), e.what());
    LOG_F(ERROR, "{}", error_message);
    ctx.ReportError(
      module->GetTypeId(), error_message, std::string { module->GetName() });
  } catch (...) {
    const auto error_message = fmt::format(
      "Module '{}' handler threw unknown exception", module->GetName());
    LOG_F(ERROR, "{}", error_message);
    ctx.ReportError(
      module->GetTypeId(), error_message, std::string { module->GetName() });
  }
  co_return;
}

// Overload that accepts a callable and its arguments. If the callable returns
// an awaitable, invoke it and forward the resulting awaitable to the awaitable
// overload above; otherwise just invoke it (synchronous handlers).
template <typename F, typename... Args>
  requires std::invocable<F, Args...>
auto RunHandlerImpl(F&& f, EngineModule* module, FrameContext& ctx,
  Args&&... args) -> oxygen::co::Co<>
{
  using Ret = std::invoke_result_t<F, Args...>;
  static_assert(!oxygen::co::Awaitable<Ret>,
    "This overload should not be used for coroutines");

  try {
    if constexpr (std::same_as<Ret, void>) {
      std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
    } else {
      // Non-awaitable return value is ignored; keep signature uniform.
      (void)std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
    }
  } catch (const std::exception& e) {
    const auto error_message = fmt::format(
      "Module '{}' handler threw: {}", module->GetName(), e.what());
    LOG_F(ERROR, "{}", error_message);
    ctx.ReportError(
      module->GetTypeId(), error_message, std::string { module->GetName() });
  } catch (...) {
    const auto error_message = fmt::format(
      "Module '{}' handler threw unknown exception", module->GetName());
    LOG_F(ERROR, "{}", error_message);
    ctx.ReportError(
      module->GetTypeId(), error_message, std::string { module->GetName() });
  }
  co_return;
}

// Helpers that execute a phase for a given list of modules. These are kept in
// the anonymous namespace so they can be co_awaited from
// ModuleManager::ExecutePhase and keep the top-level switch on PhaseId.
auto ExecuteSynchronousPhase(const std::vector<EngineModule*>& list,
  const PhaseId phase, FrameContext& ctx) -> oxygen::co::Co<>
{
  for (auto* m : list) {
    switch (phase) { // NOLINT(clang-diagnostic-switch-enum)
    case PhaseId::kFrameStart:
      co_await RunHandlerImpl(
        [](EngineModule& mm, FrameContext& c) { mm.OnFrameStart(c); }, m, ctx,
        std::ref(*m), std::ref(ctx));
      break;
    case PhaseId::kSnapshot:
      co_await RunHandlerImpl(
        [](EngineModule& mm, FrameContext& c) { mm.OnSnapshot(c); }, m, ctx,
        std::ref(*m), std::ref(ctx));
      break;
    case PhaseId::kFrameEnd:
      co_await RunHandlerImpl(
        [](EngineModule& mm, FrameContext& c) { mm.OnFrameEnd(c); }, m, ctx,
        std::ref(*m), std::ref(ctx));
      break;
    default:
      ABORT_F("Check consistency with ExecutePhase() implementation");
    }
    // Note: Synchronous handlers report errors immediately, so no deferred
    // processing needed
  }
  co_return;
}

auto ExecuteBarrieredConcurrencyPhase(const std::vector<EngineModule*>& list,
  PhaseId phase, FrameContext& ctx) -> oxygen::co::Co<>
{
  DLOG_F(2, "ExecutePhase (barriered): phase={} list.size()={}",
    static_cast<int>(phase), list.size());
  for (size_t i = 0; i < list.size(); ++i) {
    DLOG_F(2, "  list[{}] = {} (ptr={})", i,
      list[i] ? list[i]->GetName() : std::string_view("(null)"),
      static_cast<const void*>(list[i]));
  }

  std::vector<oxygen::co::Co<>> tasks;
  tasks.reserve(list.size());
  for (auto* m : list) {
    if (!m) {
      continue;
    }
    switch (phase) { // NOLINT(clang-diagnostic-switch-enum)
    case PhaseId::kInput:
      tasks.emplace_back(RunHandlerImpl(m->OnInput(ctx), m, ctx));
      break;
    case PhaseId::kFixedSimulation:
      tasks.emplace_back(RunHandlerImpl(m->OnFixedSimulation(ctx), m, ctx));
      break;
    case PhaseId::kGameplay:
      tasks.emplace_back(RunHandlerImpl(m->OnGameplay(ctx), m, ctx));
      break;
    case PhaseId::kSceneMutation:
      tasks.emplace_back(RunHandlerImpl(m->OnSceneMutation(ctx), m, ctx));
      break;
    case PhaseId::kTransformPropagation:
      tasks.emplace_back(
        RunHandlerImpl(m->OnTransformPropagation(ctx), m, ctx));
      break;
    case PhaseId::kPostParallel:
      tasks.emplace_back(RunHandlerImpl(m->OnPostParallel(ctx), m, ctx));
      break;
    case PhaseId::kFrameGraph:
      tasks.emplace_back(RunHandlerImpl(m->OnFrameGraph(ctx), m, ctx));
      break;
    case PhaseId::kCommandRecord:
      tasks.emplace_back(RunHandlerImpl(m->OnCommandRecord(ctx), m, ctx));
      break;
    case PhaseId::kAsyncPoll:
      tasks.emplace_back(RunHandlerImpl(m->OnAsyncPoll(ctx), m, ctx));
      break;
    default:
      ABORT_F("Check consistency with ExecutePhase() implementation");
    }
  }

  if (!tasks.empty()) {
    // Wait for all tasks to complete
    co_await oxygen::co::AllOf(std::move(tasks));
  }
  co_return;
}

auto ExecuteDeferredPipelinesPhase(const std::vector<EngineModule*>& list,
  FrameContext& ctx, const UnifiedSnapshot& snapshot) -> oxygen::co::Co<>
{
  std::vector<oxygen::co::Co<>> tasks;
  tasks.reserve(list.size());
  for (auto* m : list) {
    if (!m) {
      continue;
    }
    tasks.emplace_back(RunHandlerImpl(m->OnParallelTasks(snapshot), m, ctx));
  }

  if (!tasks.empty()) {
    // Wait for all tasks to complete
    co_await oxygen::co::AllOf(std::move(tasks));
  }
  co_return;
}
} // namespace

auto ModuleManager::ExecutePhase(const PhaseId phase, FrameContext& ctx)
  -> co::Co<>
{
  // Copy the module list for this phase so coroutine lambdas can safely capture
  // module pointers without referencing a temporary container.
  const auto list = phase_cache_[phase];

  // First, switch on the phase id so the compiler can check for exhaustiveness
  // of handled phases. For each phase, determine the phase descriptor and
  // dispatch to a helper based on its execution model.
  switch (phase) {
  case PhaseId::kFrameStart:
  case PhaseId::kSnapshot:
  case PhaseId::kFrameEnd: {
    const auto& desc = kPhaseRegistry[PhaseIndex { phase }];
    if (desc.category == ExecutionModel::kSynchronousOrdered
      || desc.category == ExecutionModel::kEngineInternal) {
      co_await ExecuteSynchronousPhase(list, phase, ctx);
    }
    break;
  }

  case PhaseId::kInput:
  case PhaseId::kFixedSimulation:
  case PhaseId::kGameplay:
  case PhaseId::kSceneMutation:
  case PhaseId::kTransformPropagation:
  case PhaseId::kPostParallel:
  case PhaseId::kFrameGraph:
  case PhaseId::kCommandRecord:
  case PhaseId::kAsyncPoll: {
    const auto& desc = kPhaseRegistry[PhaseIndex { phase }];
    if (desc.category == ExecutionModel::kBarrieredConcurrency) {
      co_await ExecuteBarrieredConcurrencyPhase(list, phase, ctx);
    }
    break;
  }

  case PhaseId::kDetachedServices: {
    // Detached services are expected to be started elsewhere.
    break;
  }

  case PhaseId::kNetworkReconciliation:
  case PhaseId::kRandomSeedManagement:
  case PhaseId::kPresent:
  case PhaseId::kBudgetAdapt:
    // No modules participate in these engine-only phases.
    break;

  case PhaseId::kParallelTasks:
    ABORT_F("kParallelTasks must be executed via ExecuteParallelTasks()");

  case PhaseId::kCount:
    ABORT_F(
      "kCount is not supposed to be used as a PhaseId for module execution");
  }

  // Handle module errors - remove non-critical failed modules, report critical
  // failures
  HandleModuleErrors(ctx, phase);

  co_return;
}

auto ModuleManager::ExecuteParallelTasks(
  FrameContext& ctx, const UnifiedSnapshot& snapshot) -> co::Co<>
{
  // Copy the module list for this phase so coroutine lambdas can safely capture
  // module pointers without referencing a temporary container.
  const auto list = phase_cache_[PhaseId::kParallelTasks];

  co_await ExecuteDeferredPipelinesPhase(list, ctx, snapshot);

  // Handle module errors - remove non-critical failed modules, report critical
  // failures
  HandleModuleErrors(ctx, PhaseId::kParallelTasks);

  co_return;
}
