//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//
// ModuleManager - Module Execution and Error Handling
//
// BEHAVIOR:
// 1. Synchronous phases (FrameStart, Snapshot, FrameEnd):
//    - Errors are handled immediately as each module executes
//    - Failed modules are processed right after the exception is caught
//
// 2. Concurrent phases (Input, Gameplay, FrameGraph, etc.):
//    - Module execution happens in parallel using AllOf()
//    - Errors are collected during execution but NOT processed immediately
//    - After AllOf() completes, all errors get processed together
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

#include <algorithm>
#include <concepts>
#include <type_traits>
#include <utility>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Engine/ModuleManager.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Renderer.h>

using oxygen::observer_ptr;
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
  , alive_token_(std::make_shared<int>(0))
{
}

// Subscription helpers (move/dtor/Cancel)
ModuleManager::Subscription::Subscription(Subscription&& other) noexcept
  : id_(other.id_)
  , owner_(other.owner_)
  , alive_token_(std::move(other.alive_token_))
{
  other.id_ = 0;
  other.owner_ = nullptr;
  other.alive_token_.reset();
}

ModuleManager::Subscription& ModuleManager::Subscription::operator=(
  Subscription&& other) noexcept
{
  if (this != &other) {
    Cancel();
    id_ = other.id_;
    owner_ = other.owner_;
    alive_token_ = std::move(other.alive_token_);
    other.id_ = 0;
    other.owner_ = nullptr;
    other.alive_token_.reset();
  }
  return *this;
}

ModuleManager::Subscription::~Subscription() noexcept { Cancel(); }

void ModuleManager::Subscription::Cancel() noexcept
{
  if (id_ == 0 || !owner_) {
    return;
  }
  if (alive_token_.expired()) {
    id_ = 0;
    owner_ = nullptr;
    return;
  }
  // owner_->UnsubscribeSubscription is private; Subscription is a friend.
  owner_->UnsubscribeSubscription(id_);
  id_ = 0;
  owner_ = nullptr;
}

// Subscribe and unsubscribe implementations
auto ModuleManager::SubscribeModuleAttached(
  ModuleAttachedCallback cb, const bool replay_existing) -> Subscription
{
  // Insert the subscriber and obtain id under lock.
  uint64_t id;
  {
    std::scoped_lock lock(subscribers_mutex_);
    id = next_subscriber_id_++;
    attached_subscribers_.emplace(id, std::move(cb));
  }

  Subscription s;
  s.id_ = id;
  s.owner_ = observer_ptr<ModuleManager> { this };
  s.alive_token_ = alive_token_;

  if (replay_existing) {
    // Capture a snapshot of existing modules in attach order and invoke the
    // newly-registered callback synchronously. We deliberately copy the
    // callback so it remains valid even if the subscriber cancels itself
    // during replay.
    ModuleAttachedCallback callback_copy;
    {
      std::scoped_lock lock(subscribers_mutex_);
      auto it = attached_subscribers_.find(id);
      if (it == attached_subscribers_.end()) {
        // Subscription removed in-between (should be rare)
        return s;
      }
      callback_copy = it->second;
    }

    // Build snapshot and invoke
    for (const auto& up : modules_) {
      if (!up) {
        continue;
      }
      ModuleEvent ev { up->GetTypeId(), std::string { up->GetName() },
        observer_ptr<EngineModule> { up.get() } };

      try {
        callback_copy(ev);
      } catch (const std::exception& e) {
        LOG_F(ERROR, "Subscriber callback threw during replay: {}", e.what());
      } catch (...) {
        LOG_F(
          ERROR, "Subscriber callback threw unknown exception during replay");
      }
    }
  }

  return s;
}

void ModuleManager::UnsubscribeSubscription(uint64_t id) noexcept
{
  std::scoped_lock lock(subscribers_mutex_);
  attached_subscribers_.erase(id);
}

ModuleManager::~ModuleManager()
{
  LOG_SCOPE_FUNCTION(INFO);

  {
    std::scoped_lock lock(subscribers_mutex_);
    attached_subscribers_.clear();
  }
  alive_token_.reset();

  // Reverse-order shutdown with immediate destruction: remove each module
  // from the container before shutting it down, so resources are released
  // right after OnShutdown returns.
  while (!modules_.empty()) {
    auto up = std::move(modules_.back());
    modules_.pop_back();
    if (up) {
      const auto name = up->GetName();
      LOG_SCOPE_F(INFO, "Module Shutdown");
      LOG_F(INFO, "module: '{}'", name);
      // Protect against exceptions in OnShutdown() so we can continue
      // shutting down remaining modules. We also protect the actual
      // destruction (unique_ptr reset) in case a destructor throws.
      try {
        up->OnShutdown();
      } catch (const std::exception& e) {
        LOG_F(ERROR, "Module '{}' OnShutdown() threw exception: {}", name,
          e.what());
      } catch (...) {
        LOG_F(ERROR, "Module '{}' OnShutdown() threw unknown exception", name);
      }

      try {
        up.reset();
      } catch (const std::exception& e) {
        LOG_F(
          ERROR, "Module '{}' destructor threw exception: {}", name, e.what());
      } catch (...) {
        LOG_F(ERROR, "Module '{}' destructor threw unknown exception", name);
      }
    }
  }

  LOG_F(
    INFO, "ModuleManager::~ModuleManager finished shutting down all modules");
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

  if (engine_ != nullptr) {
    module->RegisterConsoleBindings(observer_ptr { &engine_->GetConsole() });
  }

  // Insert module and rebuild caches before notifying subscribers
  modules_.push_back(std::move(module));

  RebuildPhaseCache();

  // Notify any synchronous subscribers about this new module. Create a
  // lightweight ModuleEvent and call subscribers without holding the
  // subscribers_mutex_ to avoid deadlocks or reentrancy issues.
  {
    ModuleEvent ev { modules_.back()->GetTypeId(),
      std::string { modules_.back()->GetName() },
      observer_ptr<EngineModule> { modules_.back().get() } };

    // Snapshot callbacks under lock
    std::vector<ModuleAttachedCallback> cbs;
    {
      std::scoped_lock lock(subscribers_mutex_);
      cbs.reserve(attached_subscribers_.size());
      for (auto const& kv : attached_subscribers_) {
        cbs.push_back(kv.second);
      }
    }

    for (auto const& cb : cbs) {
      try {
        cb(ev);
      } catch (const std::exception& e) {
        LOG_F(ERROR, "Subscriber callback threw during module attach: {}",
          e.what());
      } catch (...) {
        LOG_F(ERROR,
          "Subscriber callback threw unknown exception during module attach");
      }
    }
  }
  return true;
}

auto ModuleManager::UnregisterModule(std::string_view name) noexcept -> void
{
  auto it = std::ranges::find_if(
    modules_, [&](const auto& e) { return e->GetName() == name; });
  if (it == modules_.end()) {
    return;
  }

  // Extract and erase first so destruction happens immediately after
  // OnShutdown returns and to avoid leaving the module in the list during
  // shutdown.
  std::unique_ptr<EngineModule> victim = std::move(*it);
  modules_.erase(it);

  // Not allowed to fail
  if (victim) {
    const auto module_name = victim->GetName();
    try {
      victim->OnShutdown();
    } catch (const std::exception& e) {
      LOG_F(ERROR, "Module '{}' OnShutdown threw exception: {}", module_name,
        e.what());
    } catch (...) {
      LOG_F(
        ERROR, "Module '{}' OnShutdown threw unknown exception", module_name);
    }

    try {
      // Explicitly destroy while guarded to catch destructor exceptions.
      victim.reset();
    } catch (const std::exception& e) {
      LOG_F(ERROR, "Module '{}' destructor threw exception: {}", module_name,
        e.what());
    } catch (...) {
      LOG_F(
        ERROR, "Module '{}' destructor threw unknown exception", module_name);
    }
  }
  RebuildPhaseCache();
}

auto ModuleManager::GetModule(std::string_view name) const noexcept
  -> std::optional<std::reference_wrapper<EngineModule>>
{
  auto it = std::ranges::find_if(
    modules_, [&](const auto& e) { return e->GetName() == name; });
  if (it == modules_.end()) {
    return std::nullopt;
  }
  // `it` is an iterator to std::unique_ptr<EngineModule>. Get the raw
  // EngineModule pointer and construct an optional reference_wrapper to the
  // const EngineModule to match the function return type.
  EngineModule* ptr = it->get();
  DCHECK_NOTNULL_F(ptr);
  return std::ref(*ptr);
}

auto ModuleManager::ApplyConsoleCVars(
  const observer_ptr<const oxygen::console::Console> console) noexcept -> void
{
  if (console == nullptr) {
    return;
  }

  // Deterministic order: ascending priority, stable for equal priorities.
  std::vector<EngineModule*> ordered_modules;
  ordered_modules.reserve(modules_.size());
  for (const auto& module : modules_) {
    ordered_modules.push_back(module.get());
  }

  std::ranges::stable_sort(
    ordered_modules, [](const EngineModule* lhs, const EngineModule* rhs) {
      return lhs->GetPriority().get() < rhs->GetPriority().get();
    });

  for (auto* module : ordered_modules) {
    DCHECK_NOTNULL_F(module);
    try {
      module->ApplyConsoleCVars(console);
    } catch (const std::exception& e) {
      LOG_F(ERROR, "Module '{}' ApplyConsoleCVars threw: {}", module->GetName(),
        e.what());
    } catch (...) {
      LOG_F(ERROR, "Module '{}' ApplyConsoleCVars threw unknown exception",
        module->GetName());
    }
  }
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

  // Sort each phase bucket by ascending priority for execution ordering,
  // leaving 'modules_' untouched to preserve attach order for shutdown.
  for (auto p = PhaseIndex::begin(); p < PhaseIndex::end(); ++p) {
    auto& bucket = phase_cache_[p];
    std::ranges::sort(bucket, [](const EngineModule* a, const EngineModule* b) {
      return a->GetPriority().get() < b->GetPriority().get();
    });
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
  observer_ptr<FrameContext> ctx, PhaseId /*phase*/) noexcept -> void
{
  const auto& errors = ctx->GetErrors();
  if (errors.empty()) {
    return;
  }

  // Normalize module errors: find modules by key or type_id and set proper
  // source_key
  auto normalized_errors = errors
    | std::views::transform([this](auto error) { // Copy error to modify it
        EngineModule* module;

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
  std::ranges::for_each(normalized_errors, [this, ctx](const auto& pair) {
    const auto& [error, module] = pair;
    if (error.source_key.has_value()
      && error.source_key.value() == "__bad_module__") {
      // Clear the original badly reported error
      ctx->ClearErrorsFromSource(error.source_type_id);
      // Report normalized critical error
      ctx->ReportError(
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
  std::ranges::for_each(modules_to_remove, [this, ctx](const auto& pair) {
    const auto& [error, module] = pair;
    const auto& module_name = error.source_key.value();
    const auto module_type_id = error.source_type_id;
    UnregisterModule(module_name);

    // Clear only errors from this specific module (by TypeId AND source_key)
    ctx->ClearErrorsFromSource(module_type_id, error.source_key);
  });
}

// Lightweight RunHandler: accept a callable that either returns co::Co<> or is
// synchronous void. We adapt synchronous calls into coroutines.
namespace {

auto RunHandlerImpl(oxygen::co::Co<> awaitable, const EngineModule* module,
  observer_ptr<FrameContext> ctx) -> oxygen::co::Co<>
{
  try {
    co_await std::move(awaitable);
  } catch (const std::exception& e) {
    const auto error_message = fmt::format(
      "Module '{}' handler threw: {}", module->GetName(), e.what());
    LOG_F(ERROR, "{}", error_message);
    ctx->ReportError(
      module->GetTypeId(), error_message, std::string { module->GetName() });
  } catch (...) {
    const auto error_message = fmt::format(
      "Module '{}' handler threw unknown exception", module->GetName());
    LOG_F(ERROR, "{}", error_message);
    ctx->ReportError(
      module->GetTypeId(), error_message, std::string { module->GetName() });
  }
  co_return;
}

// Overload that accepts a callable and its arguments. If the callable returns
// an awaitable, invoke it and forward the resulting awaitable to the awaitable
// overload above; otherwise just invoke it (synchronous handlers).
template <typename F, typename... Args>
  requires std::invocable<F, Args...>
auto RunHandlerImpl(F&& f, EngineModule* module, observer_ptr<FrameContext> ctx,
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
    ctx->ReportError(
      module->GetTypeId(), error_message, std::string { module->GetName() });
  } catch (...) {
    const auto error_message = fmt::format(
      "Module '{}' handler threw unknown exception", module->GetName());
    LOG_F(ERROR, "{}", error_message);
    ctx->ReportError(
      module->GetTypeId(), error_message, std::string { module->GetName() });
  }
  co_return;
}

// Helpers that execute a phase for a given list of modules. These are kept in
// the anonymous namespace so they can be co_awaited from
// ModuleManager::ExecutePhase and keep the top-level switch on PhaseId.
auto ExecuteSynchronousPhase(const std::vector<EngineModule*>& list,
  const PhaseId phase, observer_ptr<FrameContext> ctx) -> oxygen::co::Co<>
{
  for (auto* m : list) {
    switch (phase) { // NOLINT(clang-diagnostic-switch-enum)
    case PhaseId::kFrameStart:
      co_await RunHandlerImpl(
        [](EngineModule& mm, observer_ptr<FrameContext> c) {
          mm.OnFrameStart(c);
        },
        m, ctx, std::ref(*m), std::ref(ctx));
      break;
    case PhaseId::kSnapshot:
      co_await RunHandlerImpl(
        [](
          EngineModule& mm, observer_ptr<FrameContext> c) { mm.OnSnapshot(c); },
        m, ctx, std::ref(*m), std::ref(ctx));
      break;
    case PhaseId::kPublishViews:
      co_await RunHandlerImpl(m->OnPublishViews(ctx), m, ctx);
      break;
    case PhaseId::kCompositing:
      co_await RunHandlerImpl(m->OnCompositing(ctx), m, ctx);
      break;
    case PhaseId::kFrameEnd:
      co_await RunHandlerImpl(
        [](
          EngineModule& mm, observer_ptr<FrameContext> c) { mm.OnFrameEnd(c); },
        m, ctx, std::ref(*m), std::ref(ctx));
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
  PhaseId phase, observer_ptr<FrameContext> ctx) -> oxygen::co::Co<>
{
  // Special ordering contract for PreRender:
  // run all non-renderer modules first (in parallel), then run Renderer last.
  // This guarantees renderer pre-render consumes fully published per-frame
  // view/graph state from other modules.
  //
  // Why this exists:
  // - kPreRender is a barriered-concurrency phase, so priority sorting alone
  //   does not guarantee the Renderer coroutine starts after other modules.
  // - Pipelines publish/register per-view render graph state during
  //   OnPreRender.
  // - Renderer::OnPreRender requires that state to be ready before it builds
  //   render contexts and scene prep.
  //
  // Therefore we enforce a two-step execution order in this phase:
  // 1) all non-renderer modules complete, 2) renderer runs.
  if (phase == PhaseId::kPreRender) {
    EngineModule* renderer_module = nullptr;
    std::vector<EngineModule*> non_renderer_modules;
    non_renderer_modules.reserve(list.size());

    for (auto* m : list) {
      if (m == nullptr) {
        continue;
      }
      // Typed lookup only (no string matching) to keep this robust across
      // module name changes.
      if (m->GetTypeId() == oxygen::engine::Renderer::ClassTypeId()) {
        renderer_module = m;
        continue;
      }
      non_renderer_modules.push_back(m);
    }

    if (renderer_module == nullptr) {
      LOG_F(ERROR,
        "ExecutePhase(kPreRender): RendererModule not found; skipping "
        "PreRender phase");
      co_return;
    }

    std::vector<oxygen::co::Co<>> tasks;
    tasks.reserve(non_renderer_modules.size());
    for (auto* m : non_renderer_modules) {
      tasks.emplace_back(RunHandlerImpl(m->OnPreRender(ctx), m, ctx));
    }

    if (!tasks.empty()) {
      co_await oxygen::co::AllOf(std::move(tasks));
    }

    co_await RunHandlerImpl(
      renderer_module->OnPreRender(ctx), renderer_module, ctx);
    co_return;
  }

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
    case PhaseId::kGuiUpdate:
      tasks.emplace_back(RunHandlerImpl(m->OnGuiUpdate(ctx), m, ctx));
      break;
    case PhaseId::kPreRender:
      tasks.emplace_back(RunHandlerImpl(m->OnPreRender(ctx), m, ctx));
      break;
    case PhaseId::kRender:
      tasks.emplace_back(RunHandlerImpl(m->OnRender(ctx), m, ctx));
      break;
    case PhaseId::kCompositing:
      tasks.emplace_back(RunHandlerImpl(m->OnCompositing(ctx), m, ctx));
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
  observer_ptr<FrameContext> ctx, const UnifiedSnapshot& snapshot)
  -> oxygen::co::Co<>
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

auto ModuleManager::ExecutePhase(
  const PhaseId phase, observer_ptr<FrameContext> ctx) -> co::Co<>
{
  // Copy the module list for this phase so coroutine lambdas can safely capture
  // module pointers without referencing a temporary container.
  const auto list = phase_cache_[phase];

  // First, switch on the phase id so the compiler can check for exhaustiveness
  // of handled phases. For each phase, determine the phase descriptor and
  // dispatch to a helper based on its execution model.
  switch (phase) {
  case PhaseId::kFrameStart:
  case PhaseId::kPublishViews:
  case PhaseId::kSnapshot:
  case PhaseId::kCompositing:
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
  case PhaseId::kGuiUpdate:
  case PhaseId::kPreRender:
  case PhaseId::kRender:
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
  observer_ptr<FrameContext> ctx, const UnifiedSnapshot& snapshot) -> co::Co<>
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
