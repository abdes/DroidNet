//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <optional>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Engine/Scripting/ScriptCompilationService.h>

namespace oxygen::scripting {
namespace {
  constexpr std::array<char, 8> kPersistentCacheMagic {
    'O',
    'X',
    'S',
    'C',
    'R',
    'P',
    'T',
    '\0',
  };
  constexpr uint32_t kPersistentCacheVersion = 1;

#pragma pack(push, 1)
  struct PersistentCacheHeader final {
    std::array<char, 8> magic {};
    uint32_t version { 0 };
    uint32_t reserved { 0 };
    uint64_t index_offset { 0 };
    uint32_t entry_count { 0 };
    uint32_t reserved2 { 0 };
  };

  struct PersistentCacheEntry final {
    uint64_t key { 0 };
    uint64_t data_offset { 0 };
    uint32_t data_size { 0 };
    uint32_t language { 0 };
    uint32_t compression { 0 };
    uint32_t origin { 0 };
    uint32_t reserved { 0 };
    uint64_t content_hash { 0 };
  };
#pragma pack(pop)

  auto MakePersistentBytecodeBlob(std::vector<uint8_t> payload,
    const uint32_t language, const uint32_t compression,
    const uint64_t content_hash, const uint32_t origin)
    -> std::shared_ptr<const ScriptBytecodeBlob>
  {
    return std::make_shared<const ScriptBytecodeBlob>(
      ScriptBytecodeBlob::FromOwned(std::move(payload),
        static_cast<data::pak::scripting::ScriptLanguage>(language),
        static_cast<data::pak::scripting::ScriptCompression>(compression),
        content_hash, static_cast<ScriptBlobOrigin>(origin),
        ScriptBlobCanonicalName { "persistent-cache" }));
  }

  auto ReadPayload(std::ifstream& input, const uint64_t data_offset,
    const uint32_t data_size) -> std::optional<std::vector<uint8_t>>
  {
    input.clear();
    input.seekg(static_cast<std::streamoff>(data_offset), std::ios::beg);
    if (!input) {
      return std::nullopt;
    }
    std::vector<uint8_t> payload(data_size);
    input.read(reinterpret_cast<char*>(payload.data()),
      static_cast<std::streamsize>(payload.size()));
    if (!input) {
      return std::nullopt;
    }
    return payload;
  }
} // namespace

ScriptCompilationService::ScriptCompilationService(
  const observer_ptr<co::ThreadPool> thread_pool,
  std::filesystem::path persistent_cache_path) noexcept
  : thread_pool_(thread_pool)
  , l1_cache_(kL1ByteBudget)
  , persistent_cache_path_(std::move(persistent_cache_path))
{
  l1_cache_.GetPolicy().SetCostFunction(
    [](const std::shared_ptr<void>& value, const TypeId type_id) -> size_t {
      if (!value) {
        return 1;
      }
      if (type_id == ScriptBytecodeBlob::ClassTypeId()) {
        const auto blob
          = std::static_pointer_cast<const ScriptBytecodeBlob>(value);
        return std::max<size_t>(1, blob->Size());
      }
      return 1;
    });

  if (!persistent_cache_path_.empty()) {
    auto load_failed = false;
    {
      std::error_code ec {};
      if (!std::filesystem::exists(persistent_cache_path_, ec)) {
        ec.clear();
      } else if (ec) {
        load_failed = true;
      } else {
        std::ifstream input(persistent_cache_path_, std::ios::binary);
        if (!input) {
          load_failed = true;
        } else {
          PersistentCacheHeader header {};
          input.read(
            reinterpret_cast<char*>(&header), sizeof(PersistentCacheHeader));
          if (!input || header.magic != kPersistentCacheMagic
            || header.version != kPersistentCacheVersion) {
            load_failed = true;
          } else {
            input.seekg(0, std::ios::end);
            const auto file_size = static_cast<uint64_t>(input.tellg());
            if (header.index_offset > file_size) {
              load_failed = true;
            } else {
              input.seekg(static_cast<std::streamoff>(header.index_offset),
                std::ios::beg);
              for (uint32_t i = 0; i < header.entry_count; ++i) {
                PersistentCacheEntry entry {};
                input.read(reinterpret_cast<char*>(&entry),
                  sizeof(PersistentCacheEntry));
                if (!input) {
                  load_failed = true;
                  break;
                }
                if ((entry.data_offset + entry.data_size) > file_size) {
                  load_failed = true;
                  break;
                }
                persistent_index_.insert_or_assign(CompileKey { entry.key },
                  PersistentIndexEntry {
                    .data_offset = entry.data_offset,
                    .data_size = entry.data_size,
                    .language = entry.language,
                    .compression = entry.compression,
                    .origin = entry.origin,
                    .content_hash = entry.content_hash,
                  });
                input.seekg(static_cast<std::streamoff>(header.index_offset
                              + (static_cast<uint64_t>(i + 1)
                                * sizeof(PersistentCacheEntry))),
                  std::ios::beg);
              }
            }
          }
        }
      }
    }
    if (load_failed) {
      LOG_F(WARNING, "persistent cache invalid, falling back to empty cache");
      persistent_index_.clear();
    } else if (!persistent_index_.empty()) {
      LOG_F(
        INFO, "persistent cache loaded (entries={})", persistent_index_.size());
    }
  }

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

  // Ensure all pending compilations are persisted before stopping
  FlushPersistentCache();

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
    std::lock_guard lock(l1_cache_mutex_);
    l1_cache_.Clear();
  }
  {
    std::lock_guard lock(compilers_mutex_);
    compilers_.clear();
  }
  if (nursery_ != nullptr) {
    nursery_->Cancel();
  }

  const auto l1_hits = l1_hits_.load(std::memory_order_relaxed);
  const auto l2_hits = l2_hits_.load(std::memory_order_relaxed);
  const auto compile_started = compile_started_.load(std::memory_order_relaxed);
  const auto compile_succeeded
    = compile_succeeded_.load(std::memory_order_relaxed);
  const auto compile_failed = compile_failed_.load(std::memory_order_relaxed);
  const auto latency_samples
    = compile_latency_samples_.load(std::memory_order_relaxed);
  const auto latency_total_us
    = compile_latency_total_us_.load(std::memory_order_relaxed);
  const auto latency_max_us
    = compile_latency_max_us_.load(std::memory_order_relaxed);
  const auto latency_avg_us
    = latency_samples == 0 ? 0 : (latency_total_us / latency_samples);

  LOG_SCOPE_F(INFO, "Compilation Statistics");
  LOG_F(INFO, "subscribers cleared       : {}", subscribers_cleared);
  LOG_F(INFO, "completions cleared       : {}", completions_cleared);
  LOG_F(INFO, "l1 hits                   : {}", l1_hits);
  LOG_F(INFO, "l2 hits                   : {}", l2_hits);
  LOG_F(INFO, "compile started           : {}", compile_started);
  LOG_F(INFO, "compile succeeded         : {}", compile_succeeded);
  LOG_F(INFO, "compile failed            : {}", compile_failed);
  LOG_F(INFO, "compile latency samples   : {}", latency_samples);
  LOG_F(INFO, "compile latency avg       : {} us", latency_avg_us);
  LOG_F(INFO, "compile latency max       : {} us", latency_max_us);
  LOG_F(INFO, "compile latency total     : {} us", latency_total_us);
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
  const data::pak::scripting::ScriptLanguage language) -> bool
{
  std::lock_guard lock(compilers_mutex_);
  const auto erased = compilers_.erase(language) > 0;
  if (erased) {
    LOG_F(INFO, "compiler unregistered (language={})", language);
  }
  return erased;
}

auto ScriptCompilationService::HasCompiler(
  const data::pak::scripting::ScriptLanguage language) const -> bool
{
  std::lock_guard lock(compilers_mutex_);
  return compilers_.contains(language);
}

auto ScriptCompilationService::CompileAsync(Request request) -> co::Co<Result>
{
  if (!active_.load(std::memory_order_acquire)) {
    co_return Result {
      .success = false,
      .bytecode = nullptr,
      .diagnostics = "ScriptCompilationService is shut down",
    };
  }

  const auto compile_key = request.compile_key;
  if (auto cached_bytecode = TryGetCachedBytecode(compile_key);
    cached_bytecode != nullptr) {
    l1_hits_.fetch_add(1, std::memory_order_relaxed);
    Result cached_result {};
    cached_result.success = true;
    cached_result.bytecode = std::move(cached_bytecode);
    DLOG_F(2, "compile cache hit (key={}, bytecode_size={})", compile_key,
      cached_result.bytecode->Size());
    EnqueueCompletion(compile_key, cached_result);
    co_return cached_result;
  }

  if (auto cached_bytecode = TryGetPersistentBytecode(compile_key);
    cached_bytecode != nullptr) {
    l2_hits_.fetch_add(1, std::memory_order_relaxed);
    Result persisted_result {};
    persisted_result.success = true;
    persisted_result.bytecode = std::move(cached_bytecode);
    StoreCachedBytecode(compile_key, persisted_result.bytecode);
    DLOG_F(2, "persistent cache hit (key={}, bytecode_size={})", compile_key,
      persisted_result.bytecode->Size());
    EnqueueCompletion(compile_key, persisted_result);
    co_return persisted_result;
  }
  DLOG_F(3, "compile cache miss (key={})", compile_key);

  co::Shared<co::Co<Result>> shared;
  {
    std::lock_guard lock(in_flight_mutex_);
    if (const auto it = in_flight_.find(compile_key); it != in_flight_.end()) {
      shared = it->second;
      DLOG_F(2, "joining in-flight compile request (key={})", compile_key);
    } else {
      const auto compile_mode = request.compile_mode;
      auto source = std::move(request.source);
      compile_started_.fetch_add(1, std::memory_order_relaxed);
      LOG_F(INFO,
        "Script change detected, recompiling (key={}, language={}, mode={}, "
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

auto ScriptCompilationService::GetCounters() const noexcept -> Counters
{
  return Counters {
    .l1_hits = l1_hits_.load(std::memory_order_relaxed),
    .l2_hits = l2_hits_.load(std::memory_order_relaxed),
    .compile_started = compile_started_.load(std::memory_order_relaxed),
    .compile_succeeded = compile_succeeded_.load(std::memory_order_relaxed),
    .compile_failed = compile_failed_.load(std::memory_order_relaxed),
    .compile_latency_samples
    = compile_latency_samples_.load(std::memory_order_relaxed),
    .compile_latency_total_us
    = compile_latency_total_us_.load(std::memory_order_relaxed),
    .compile_latency_max_us
    = compile_latency_max_us_.load(std::memory_order_relaxed),
  };
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
      if (result.success && result.bytecode != nullptr) {
        if (callbacks.on_ready) {
          callbacks.on_ready(result.bytecode);
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

auto ScriptCompilationService::SetCacheBudget(const size_t budget_bytes) -> void
{
  std::lock_guard lock(l1_cache_mutex_);
  l1_cache_.SetBudget(budget_bytes);
}

auto ScriptCompilationService::SetDeferredPersistence(const bool enabled)
  -> void
{
  const bool was_enabled = deferred_persistence_enabled_.exchange(
    enabled, std::memory_order_acq_rel);
  if (was_enabled && !enabled) {
    FlushPersistentCache();
  }
}

auto ScriptCompilationService::RegisterConsoleBindings(
  const observer_ptr<console::Console> console) -> void
{
  if (!console) {
    return;
  }

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = "scripting.cache_budget_mb",
    .help = "Script bytecode L1 memory cache budget in MB",
    .default_value = static_cast<int64_t>(kL1ByteBudget / (1024 * 1024)),
    .flags = console::CVarFlags::kArchive,
  });

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = "scripting.deferred_persistence",
    .help = "If true, bytecode cache writes are batched and deferred",
    .default_value = true,
    .flags = console::CVarFlags::kArchive,
  });

  (void)console->RegisterCommand(
    console::CommandDefinition { .name = "scripting.stats",
      .help = "Print scripting compilation and cache statistics",
      .handler = [this](const auto&, const auto&) -> console::ExecutionResult {
        const auto counters = GetCounters();
        LOG_F(INFO, "Scripting Statistics:");
        LOG_F(INFO, "  L1 Hits: {}", counters.l1_hits);
        LOG_F(INFO, "  L2 Hits: {}", counters.l2_hits);
        LOG_F(INFO, "  Compile Started: {}", counters.compile_started);
        LOG_F(INFO, "  Compile Succeeded: {}", counters.compile_succeeded);
        LOG_F(INFO, "  Compile Failed: {}", counters.compile_failed);
        LOG_F(INFO, "  Avg Latency: {} us",
          counters.compile_latency_samples == 0
            ? 0
            : counters.compile_latency_total_us
              / counters.compile_latency_samples);
        LOG_F(INFO, "  Max Latency: {} us", counters.compile_latency_max_us);
        return { .status = console::ExecutionStatus::kOk };
      } });

  (void)console->RegisterCommand(
    console::CommandDefinition { .name = "scripting.flush_cache",
      .help = "Manually trigger a flush of the persistent bytecode cache",
      .handler = [this](const auto&, const auto&) -> console::ExecutionResult {
        FlushPersistentCache();
        return { .status = console::ExecutionStatus::kOk,
          .output = "Script cache flushed" };
      } });
}

auto ScriptCompilationService::ApplyConsoleCVars(
  const console::Console& console) -> void
{
  int64_t budget_mb = kL1ByteBudget / (1024 * 1024);
  if (console.TryGetCVarValue<int64_t>(
        "scripting.cache_budget_mb", budget_mb)) {
    SetCacheBudget(static_cast<size_t>(budget_mb) * 1024 * 1024);
  }

  bool deferred = true;
  if (console.TryGetCVarValue<bool>(
        "scripting.deferred_persistence", deferred)) {
    SetDeferredPersistence(deferred);
  }
}

auto ScriptCompilationService::ExecuteCompileRequest(
  const CompileKey compile_key, ScriptSourceBlob source,
  const CompileMode compile_mode) -> co::Co<Result>
{
  auto erase = ScopeGuard([this, compile_key]() noexcept {
    std::lock_guard lock(in_flight_mutex_);
    in_flight_.erase(compile_key);
  });

  const auto started_at = std::chrono::steady_clock::now();
  auto result = co_await CompileOnWorkerThread(std::move(source), compile_mode);
  const auto finished_at = std::chrono::steady_clock::now();
  const auto latency_us = static_cast<uint64_t>(
    std::chrono::duration_cast<std::chrono::microseconds>(
      finished_at - started_at)
      .count());
  compile_latency_samples_.fetch_add(1, std::memory_order_relaxed);
  compile_latency_total_us_.fetch_add(latency_us, std::memory_order_relaxed);
  auto observed_max = compile_latency_max_us_.load(std::memory_order_relaxed);
  while (latency_us > observed_max
    && !compile_latency_max_us_.compare_exchange_weak(
      observed_max, latency_us, std::memory_order_relaxed)) { }

  if (result.success && result.bytecode != nullptr) {
    compile_succeeded_.fetch_add(1, std::memory_order_relaxed);
    StorePersistentBytecode(compile_key, result.bytecode);
    StoreCachedBytecode(compile_key, result.bytecode);
    LOG_F(INFO, "compile succeeded (key={}, bytecode_size={}, latency_us={})",
      compile_key, result.bytecode->Size(), latency_us);
  } else {
    compile_failed_.fetch_add(1, std::memory_order_relaxed);
    LOG_F(ERROR,
      "compile failed (key={}, diagnostics_size={}, latency_us={}, "
      "diagnostics={})",
      compile_key, result.diagnostics.size(), latency_us, result.diagnostics);
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
      DLOG_F(2, "dispatching compile to worker (language={}, source_size={})",
        language, source.Size());
      co_return co_await thread_pool_->Run(
        [compiler, compile_mode](ScriptSourceBlob source_blob) -> Result {
          DLOG_F(
            3, "worker invoking compiler (source_size={})", source_blob.Size());
          return compiler->Compile(std::move(source_blob), compile_mode);
        },
        std::move(source));
    }

    DLOG_F(2, "thread pool unavailable, compiling inline");
    LOG_F(INFO, "invoking compiler inline (language={}, source_size={})",
      language, source.Size());
    co_return compiler->Compile(std::move(source), compile_mode);
  }

  LOG_F(ERROR, "no compiler registered for language={}", language);
  Result result {};
  result.success = false;
  result.diagnostics = "No compiler registered for script language";
  co_return result;
}

auto ScriptCompilationService::TryGetCachedBytecode(
  const CompileKey compile_key) -> std::shared_ptr<const ScriptBytecodeBlob>
{
  std::lock_guard lock(l1_cache_mutex_);
  auto cached = l1_cache_.CheckOut<const ScriptBytecodeBlob>(
    compile_key.get(), oxygen::CheckoutOwner::kInternal);
  if (cached == nullptr) {
    return nullptr;
  }
  l1_cache_.CheckIn(compile_key.get());
  return cached;
}

auto ScriptCompilationService::StoreCachedBytecode(const CompileKey compile_key,
  std::shared_ptr<const ScriptBytecodeBlob> bytecode) -> void
{
  if (bytecode == nullptr || bytecode->IsEmpty()) {
    return;
  }

  auto mutable_view = std::const_pointer_cast<ScriptBytecodeBlob>(bytecode);

  std::lock_guard lock(l1_cache_mutex_);
  if (!l1_cache_.Store(compile_key.get(), mutable_view)) {
    if (!l1_cache_.Replace(compile_key.get(), mutable_view)) {
      LOG_F(WARNING, "cache store skipped (key={})", compile_key);
      return;
    }
  }
}

auto ScriptCompilationService::TryGetPersistentBytecode(
  const CompileKey compile_key) -> std::shared_ptr<const ScriptBytecodeBlob>
{
  if (persistent_cache_path_.empty()) {
    return nullptr;
  }

  PersistentIndexEntry entry {};
  {
    std::lock_guard lock(persistent_cache_mutex_);
    const auto it = persistent_index_.find(compile_key);
    if (it == persistent_index_.end()) {
      return nullptr;
    }
    entry = it->second;
  }

  std::ifstream input(persistent_cache_path_, std::ios::binary);
  if (!input) {
    LOG_F(WARNING, "persistent cache unavailable on read");
    return nullptr;
  }
  auto payload = ReadPayload(input, entry.data_offset, entry.data_size);
  if (!payload.has_value()) {
    LOG_F(WARNING, "persistent cache entry invalidated (key={})", compile_key);
    std::lock_guard lock(persistent_cache_mutex_);
    persistent_index_.erase(compile_key);
    return nullptr;
  }

  return MakePersistentBytecodeBlob(std::move(*payload), entry.language,
    entry.compression, entry.content_hash, entry.origin);
}

auto ScriptCompilationService::StorePersistentBytecode(
  const CompileKey compile_key,
  std::shared_ptr<const ScriptBytecodeBlob> bytecode) -> void
{
  if (persistent_cache_path_.empty() || bytecode == nullptr
    || bytecode->IsEmpty()) {
    return;
  }

  if (deferred_persistence_enabled_.load(std::memory_order_acquire)) {
    std::lock_guard lock(persistent_cache_mutex_);
    pending_persistence_.insert_or_assign(compile_key, std::move(bytecode));
    cache_dirty_.store(true, std::memory_order_relaxed);
    DLOG_F(2, "bytecode queued for persistence (key={})", compile_key);
  } else {
    // Immediate persistence
    std::vector<
      std::pair<CompileKey, std::shared_ptr<const ScriptBytecodeBlob>>>
      snapshot {};
    {
      std::lock_guard lock(persistent_cache_mutex_);
      snapshot.reserve(persistent_index_.size() + 1);
      std::ifstream input(persistent_cache_path_, std::ios::binary);
      for (const auto& [key, entry] : persistent_index_) {
        if (key == compile_key) {
          continue;
        }
        if (!input) {
          break;
        }
        auto payload = ReadPayload(input, entry.data_offset, entry.data_size);
        if (!payload.has_value()) {
          continue;
        }
        snapshot.emplace_back(key,
          MakePersistentBytecodeBlob(std::move(*payload), entry.language,
            entry.compression, entry.content_hash, entry.origin));
      }
    }
    snapshot.emplace_back(compile_key, std::move(bytecode));
    PersistCacheSnapshot(snapshot);
  }
}

auto ScriptCompilationService::FlushPersistentCache() -> void
{
  if (!cache_dirty_.load(std::memory_order_relaxed)
    || persistent_cache_path_.empty()) {
    return;
  }

  LOG_F(INFO, "flushing persistent script cache to disk");

  std::vector<std::pair<CompileKey, std::shared_ptr<const ScriptBytecodeBlob>>>
    snapshot {};
  {
    std::lock_guard lock(persistent_cache_mutex_);
    snapshot.reserve(persistent_index_.size() + pending_persistence_.size());

    std::ifstream input(persistent_cache_path_, std::ios::binary);
    // Load all current entries that aren't being overridden by pending ones
    for (const auto& [key, entry] : persistent_index_) {
      if (pending_persistence_.contains(key)) {
        continue;
      }
      if (!input) {
        break;
      }
      auto payload = ReadPayload(input, entry.data_offset, entry.data_size);
      if (!payload.has_value()) {
        continue;
      }
      snapshot.emplace_back(key,
        MakePersistentBytecodeBlob(std::move(*payload), entry.language,
          entry.compression, entry.content_hash, entry.origin));
    }

    // Add all newly compiled bytecodes
    for (auto& [key, bytecode] : pending_persistence_) {
      snapshot.emplace_back(key, std::move(bytecode));
    }
    pending_persistence_.clear();
    cache_dirty_.store(false, std::memory_order_relaxed);
  }

  PersistCacheSnapshot(snapshot);
}

auto ScriptCompilationService::PersistCacheSnapshot(const std::vector<
  std::pair<CompileKey, std::shared_ptr<const ScriptBytecodeBlob>>>& snapshot)
  -> void
{
  if (persistent_cache_path_.empty()) {
    return;
  }

  const auto temp_path = persistent_cache_path_.string().append(".tmp");

  auto sorted_snapshot = snapshot;

  std::ranges::sort(sorted_snapshot,
    [](const auto& a, const auto& b) { return a.first.get() < b.first.get(); });

  std::error_code ec {};
  const auto parent_path = persistent_cache_path_.parent_path();
  if (!parent_path.empty()) {
    std::filesystem::create_directories(parent_path, ec);
    if (ec) {
      LOG_F(
        WARNING, "failed to create persistent cache directory: {}", ec.value());
      return;
    }
  }

  std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
  if (!output) {
    LOG_F(WARNING, "failed to open temporary persistent cache file");
    return;
  }

  PersistentCacheHeader header {};
  header.magic = kPersistentCacheMagic;
  header.version = kPersistentCacheVersion;
  output.write(reinterpret_cast<const char*>(&header), sizeof(header));

  std::vector<PersistentCacheEntry> index_entries;
  index_entries.reserve(sorted_snapshot.size());
  for (const auto& [key, payload] : sorted_snapshot) {
    if (payload == nullptr) {
      continue;
    }
    const auto payload_bytes = payload->BytesView();
    const auto data_offset = static_cast<uint64_t>(output.tellp());
    output.write(reinterpret_cast<const char*>(payload_bytes.data()),
      static_cast<std::streamsize>(payload_bytes.size()));
    index_entries.push_back(PersistentCacheEntry {
      .key = key.get(),
      .data_offset = data_offset,
      .data_size = static_cast<uint32_t>(payload_bytes.size()),
      .language = static_cast<uint32_t>(payload->Language()),
      .compression = static_cast<uint32_t>(payload->Compression()),
      .origin = static_cast<uint32_t>(payload->GetOrigin()),
      .reserved = 0U,
      .content_hash = payload->ContentHash(),
    });
  }

  header.index_offset = static_cast<uint64_t>(output.tellp());
  header.entry_count = static_cast<uint32_t>(index_entries.size());
  for (const auto& entry : index_entries) {
    output.write(
      reinterpret_cast<const char*>(&entry), sizeof(PersistentCacheEntry));
  }

  output.seekp(0, std::ios::beg);
  output.write(reinterpret_cast<const char*>(&header), sizeof(header));
  output.flush();
  output.close();

  std::filesystem::remove(persistent_cache_path_, ec);
  ec.clear();
  std::filesystem::rename(temp_path, persistent_cache_path_, ec);
  if (ec) {
    LOG_F(WARNING, "failed to publish persistent cache file: {}", ec.value());
    std::filesystem::remove(temp_path, ec);
    return;
  }

  std::lock_guard lock(persistent_cache_mutex_);
  persistent_index_.clear();
  for (const auto& entry : index_entries) {
    persistent_index_.insert_or_assign(CompileKey { entry.key },
      PersistentIndexEntry {
        .data_offset = entry.data_offset,
        .data_size = entry.data_size,
        .language = entry.language,
        .compression = entry.compression,
        .origin = entry.origin,
        .content_hash = entry.content_hash,
      });
  }
}

auto ScriptCompilationService::EnqueueCompletion(
  const CompileKey compile_key, const Result& result) -> void
{
  if (!active_.load(std::memory_order_acquire)) {
    return;
  }
  std::lock_guard lock(completions_mutex_);
  DLOG_F(
    3, "queue completion (key={}, success={})", compile_key, result.success);
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
