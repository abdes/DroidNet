//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Engine/Scripting/ScriptCompilationService.h>

namespace oxygen::scripting {

ScriptCompilationService::ScriptCompilationService(
  const observer_ptr<co::ThreadPool> thread_pool) noexcept
  : thread_pool_(thread_pool)
{
  LOG_F(INFO, "compilation service initialized (thread_pool={})",
    thread_pool_ ? "yes" : "no");
}

auto ScriptCompilationService::ActivateAsync(co::TaskStarted<> started)
  -> co::Co<>
{
  return co::OpenNursery(nursery_, std::move(started));
}

auto ScriptCompilationService::Run() -> void
{
  active_.store(true, std::memory_order_release);
}

auto ScriptCompilationService::Stop() -> void
{
  active_.store(false, std::memory_order_release);
  LOG_F(INFO, "shutdown requested");

  size_t subscribers_cleared = 0;
  size_t completions_cleared = 0;
  {
    std::lock_guard lock(subscribers_mutex_);
    for (const auto& [_, list] : subscribers_) {
      subscribers_cleared += list.size();
    }
    subscribers_.clear();
  }
  {
    std::lock_guard lock(completions_mutex_);
    completions_cleared = pending_completions_.size();
    pending_completions_.clear();
  }
  {
    std::lock_guard lock(in_flight_mutex_);
    in_flight_.clear();
  }
  {
    std::lock_guard lock(compilers_mutex_);
    compilers_.clear();
  }
  if (nursery_ != nullptr) {
    nursery_->Cancel();
  }

  LOG_F(INFO,
    "shutdown complete (subscribers_cleared={}, completions_cleared={})",
    subscribers_cleared, completions_cleared);
}

auto ScriptCompilationService::IsRunning() const -> bool
{
  return nursery_ != nullptr;
}

auto ScriptCompilationService::RegisterCompiler(
  std::shared_ptr<const IScriptCompiler> compiler) -> bool
{
  if (!compiler) {
    return false;
  }
  std::lock_guard lock(compilers_mutex_);
  const auto language = compiler->Language();
  const auto inserted
    = compilers_.insert_or_assign(language, std::move(compiler)).second;
  LOG_F(INFO, "compiler registered (language={})", language);
  return inserted;
}

auto ScriptCompilationService::UnregisterCompiler(
  const data::pak::ScriptLanguage language) -> bool
{
  std::lock_guard lock(compilers_mutex_);
  const auto erased = compilers_.erase(language) > 0;
  if (erased) {
    LOG_F(INFO, "compiler unregistered (language={})", language);
  }
  return erased;
}

auto ScriptCompilationService::HasCompiler(
  const data::pak::ScriptLanguage language) const -> bool
{
  std::lock_guard lock(compilers_mutex_);
  return compilers_.contains(language);
}

auto ScriptCompilationService::CompileAsync(Request request) -> co::Co<Result>
{
  if (!active_.load(std::memory_order_acquire)) {
    co_return Result {
      .success = false,
      .bytecode = {},
      .diagnostics = "ScriptCompilationService is shut down",
    };
  }

  const auto compile_key = request.compile_key;
  co::Shared<co::Co<Result>> shared;
  {
    std::lock_guard lock(in_flight_mutex_);
    if (const auto it = in_flight_.find(compile_key); it != in_flight_.end()) {
      shared = it->second;
      DLOG_F(2, "joining in-flight compile request (key={})", compile_key);
    } else {
      const auto compile_mode = request.compile_mode;
      auto source = std::move(request.source);
      LOG_F(INFO,
        "starting compile request (key={}, language={}, mode={}, "
        "source_size={})",
        compile_key, source.Language(), compile_mode, source.Size());
      auto op
        = ExecuteCompileRequest(compile_key, std::move(source), compile_mode);

      shared = co::Shared(std::move(op));
      in_flight_.insert_or_assign(compile_key, shared);
    }
  }

  co_return co_await shared;
}

auto ScriptCompilationService::InFlightCount() const -> size_t
{
  std::lock_guard lock(in_flight_mutex_);
  return in_flight_.size();
}

auto ScriptCompilationService::Subscribe(const CompileKey compile_key,
  CompletionSubscriber subscriber) -> SubscriptionHandle
{
  if (!subscriber) {
    LOG_F(WARNING, "ignoring empty compilation subscriber");
    return {};
  }
  std::lock_guard lock(subscribers_mutex_);
  const auto subscriber_id = next_subscriber_id_++;
  subscribers_[compile_key].emplace_back(subscriber_id, std::move(subscriber));
  LOG_F(INFO, "subscriber attached (key={}, subscriber={})", compile_key,
    subscriber_id);
  return SubscriptionHandle {
    .compile_key = compile_key,
    .subscriber_id = subscriber_id,
  };
}

auto ScriptCompilationService::Unsubscribe(const SubscriptionHandle& handle)
  -> bool
{
  std::lock_guard lock(subscribers_mutex_);
  if (const auto it = subscribers_.find(handle.compile_key);
    it != subscribers_.end()) {
    auto& list = it->second;
    const auto before = list.size();
    std::erase_if(list, [&handle](const auto& entry) {
      return entry.first == handle.subscriber_id;
    });
    const auto removed = (list.size() != before);
    if (list.empty()) {
      subscribers_.erase(it);
    }
    if (removed) {
      LOG_F(INFO, "subscriber detached (key={}, subscriber={})",
        handle.compile_key, handle.subscriber_id);
    }
    return removed;
  }
  return false;
}

auto ScriptCompilationService::AcquireForSlot(
  Request request, SlotAcquireCallbacks callbacks) -> SlotAcquireHandle
{
  const auto compile_key = request.compile_key;
  auto subscription = Subscribe(
    compile_key, [callbacks = std::move(callbacks)](const Result& result) {
      if (result.success) {
        if (callbacks.on_ready) {
          callbacks.on_ready(std::vector<uint8_t>(result.bytecode));
        }
        return;
      }

      if (callbacks.on_failed) {
        callbacks.on_failed(result.diagnostics);
      }
    });

  SlotAcquireHandle handle {
    .placeholder = {},
    .subscription = subscription,
    .request = std::nullopt,
  };

  if (nursery_ != nullptr) {
    nursery_->Start(&ScriptCompilationService::KickoffCompileRequest, this,
      std::move(request));
  } else {
    LOG_F(ERROR, "compile request not started because service is not active");
    handle.request = std::optional<Request> { std::move(request) };
  }

  return handle;
}

auto ScriptCompilationService::OnFrameStart(engine::EngineTag /*tag*/) -> void
{
  if (!active_.load(std::memory_order_acquire)) {
    return;
  }
  DrainCompletions();
}

auto ScriptCompilationService::ExecuteCompileRequest(
  const CompileKey compile_key, ScriptSourceBlob source,
  const CompileMode compile_mode) -> co::Co<Result>
{
  auto erase = ScopeGuard([this, compile_key]() noexcept {
    std::lock_guard lock(in_flight_mutex_);
    in_flight_.erase(compile_key);
  });

  const auto result
    = co_await CompileOnWorkerThread(std::move(source), compile_mode);
  if (result.success) {
    LOG_F(INFO, "compile succeeded (key={}, bytecode_size={})", compile_key,
      result.bytecode.size());
  } else {
    LOG_F(ERROR, "compile failed (key={}, diagnostics_size={}, diagnostics={})",
      compile_key, result.diagnostics.size(), result.diagnostics);
  }
  EnqueueCompletion(compile_key, result);
  co_return result;
}

auto ScriptCompilationService::KickoffCompileRequest(Request request)
  -> co::Co<>
{
  (void)co_await CompileAsync(std::move(request));
  co_return;
}

auto ScriptCompilationService::CompileOnWorkerThread(ScriptSourceBlob source,
  const CompileMode compile_mode) const -> co::Co<Result>
{
  const auto language = source.Language();
  std::shared_ptr<const IScriptCompiler> compiler;
  {
    std::lock_guard lock(compilers_mutex_);
    if (const auto it = compilers_.find(language); it != compilers_.end()) {
      compiler = it->second;
    }
  }
  if (compiler) {
    if (thread_pool_) {
      LOG_F(INFO, "dispatching compile to worker (language={}, source_size={})",
        language, source.Size());
      co_return co_await thread_pool_->Run(
        [compiler, compile_mode](ScriptSourceBlob source_blob) -> Result {
          LOG_F(INFO, "worker invoking compiler (source_size={})",
            source_blob.Size());
          return compiler->Compile(source_blob.BytesView(), compile_mode);
        },
        std::move(source));
    }

    DLOG_F(2, "thread pool unavailable, compiling inline");
    LOG_F(INFO, "invoking compiler inline (language={}, source_size={})",
      language, source.Size());
    co_return compiler->Compile(source.BytesView(), compile_mode);
  }

  LOG_F(ERROR, "no compiler registered for language={}", language);
  Result result {};
  result.success = false;
  result.diagnostics = "No compiler registered for script language";
  co_return result;
}

auto ScriptCompilationService::EnqueueCompletion(
  const CompileKey compile_key, const Result& result) -> void
{
  if (!active_.load(std::memory_order_acquire)) {
    return;
  }
  std::lock_guard lock(completions_mutex_);
  pending_completions_.emplace_back(compile_key, result);
}

auto ScriptCompilationService::DrainCompletions() -> void
{
  std::vector<std::pair<CompileKey, Result>> completions;
  {
    std::lock_guard lock(completions_mutex_);
    completions.swap(pending_completions_);
  }
  if (!completions.empty()) {
    LOG_F(
      INFO, "dispatching compile completions (count={})", completions.size());
  }

  for (const auto& [compile_key, result] : completions) {
    std::vector<CompletionSubscriber> subscribers;
    {
      std::lock_guard lock(subscribers_mutex_);
      if (const auto it = subscribers_.find(compile_key);
        it != subscribers_.end()) {
        subscribers.reserve(it->second.size());
        for (auto& entry : it->second) {
          subscribers.push_back(std::move(entry.second));
        }
        subscribers_.erase(it);
      }
    }
    for (const auto& subscriber : subscribers) {
      subscriber(result);
    }
  }
}

} // namespace oxygen::scripting
