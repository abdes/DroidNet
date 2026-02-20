//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/AnyCache.h>
#include <Oxygen/Engine/Scripting/Detail/LruEviction.h>
#include <Oxygen/Engine/Scripting/IScriptCompilationService.h>
#include <Oxygen/OxCo/LiveObject.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Shared.h>
#include <Oxygen/OxCo/ThreadPool.h>

namespace oxygen::console {
class Console;
}

namespace oxygen::scripting {

//! Core engine service for script compilation and bytecode management.
/*!
  The ScriptCompilationService provides a robust pipeline for transforming
  script source code into Luau bytecode. It features a high-performance two-tier
  caching system:

  ### Caching Tiers
  1. **L1 Memory Cache**: An LRU-based cache for rapid access to recently used
     bytecode. Budget-aware based on bytecode size.
  2. **L2 Persistent Cache**: A binary file (`scripts.bin`) that stores compiled
     scripts across engine restarts, enabling near-instant startup.

  ### Design Contracts
  - **Deduplication**: Simultaneous requests for the same script are joined
    to avoid redundant compilation work.
  - **Asynchronous**: Compilation is dispatched to a background thread pool.
  - **Deferred Persistence**: Disk writes are batched and deferred until engine
    shutdown or idle time to optimize hot-reload iteration speed.
*/
class ScriptCompilationService final : public co::LiveObject,
                                       public IScriptCompilationService {
public:
  struct Counters final {
    uint64_t l1_hits { 0 };
    uint64_t l2_hits { 0 };
    uint64_t compile_started { 0 };
    uint64_t compile_succeeded { 0 };
    uint64_t compile_failed { 0 };
    uint64_t compile_latency_samples { 0 };
    uint64_t compile_latency_total_us { 0 };
    uint64_t compile_latency_max_us { 0 };
  };

  OXGN_NGIN_API explicit ScriptCompilationService(
    observer_ptr<co::ThreadPool> thread_pool,
    std::filesystem::path persistent_cache_path = {}) noexcept;
  OXGN_NGIN_API ~ScriptCompilationService() override = default;

  OXYGEN_MAKE_NON_COPYABLE(ScriptCompilationService)
  OXYGEN_MAKE_NON_MOVABLE(ScriptCompilationService)

  OXGN_NGIN_NDAPI auto ActivateAsync(co::TaskStarted<> started = {})
    -> co::Co<> override;
  OXGN_NGIN_API auto Run() -> void override;
  OXGN_NGIN_API auto Stop() -> void override;
  OXGN_NGIN_NDAPI auto IsRunning() const -> bool override;

  OXGN_NGIN_NDAPI auto RegisterCompiler(
    std::shared_ptr<const IScriptCompiler> compiler) -> bool override;
  OXGN_NGIN_NDAPI auto UnregisterCompiler(data::pak::ScriptLanguage language)
    -> bool override;
  OXGN_NGIN_NDAPI auto HasCompiler(data::pak::ScriptLanguage language) const
    -> bool override;

  OXGN_NGIN_NDAPI auto CompileAsync(Request request) -> co::Co<Result> override;
  OXGN_NGIN_NDAPI auto InFlightCount() const -> size_t override;
  OXGN_NGIN_NDAPI auto GetCounters() const noexcept -> Counters;
  OXGN_NGIN_NDAPI auto Subscribe(CompileKey compile_key,
    CompletionSubscriber subscriber) -> SubscriptionHandle override;
  OXGN_NGIN_API auto Unsubscribe(const SubscriptionHandle& handle)
    -> bool override;
  OXGN_NGIN_NDAPI auto AcquireForSlot(Request request,
    SlotAcquireCallbacks callbacks) -> SlotAcquireHandle override;

  OXGN_NGIN_API auto OnFrameStart(engine::EngineTag) -> void override;

  OXGN_NGIN_API auto SetCacheBudget(size_t budget_bytes) -> void override;
  OXGN_NGIN_API auto SetDeferredPersistence(bool enabled) -> void override;
  OXGN_NGIN_API auto FlushPersistentCache() -> void override;

  OXGN_NGIN_API auto RegisterConsoleBindings(
    observer_ptr<console::Console> console) -> void;
  OXGN_NGIN_API auto ApplyConsoleCVars(const console::Console& console) -> void;

private:
  static constexpr size_t kL1ByteBudget = 8 * 1024 * 1024;

  struct PersistentIndexEntry final {
    uint64_t data_offset { 0 };
    uint32_t data_size { 0 };
    uint32_t language { 0 };
    uint32_t compression { 0 };
    uint32_t origin { 0 };
    uint64_t content_hash { 0 };
  };

  OXGN_NGIN_NDAPI auto ExecuteCompileRequest(CompileKey compile_key,
    ScriptSourceBlob source, CompileMode compile_mode) -> co::Co<Result>;
  OXGN_NGIN_NDAPI auto KickoffCompileRequest(Request request) -> co::Co<>;
  OXGN_NGIN_NDAPI auto CompileOnWorkerThread(
    ScriptSourceBlob source, CompileMode compile_mode) const -> co::Co<Result>;
  OXGN_NGIN_API auto TryGetCachedBytecode(CompileKey compile_key)
    -> std::shared_ptr<const ScriptBytecodeBlob>;
  OXGN_NGIN_API auto StoreCachedBytecode(CompileKey compile_key,
    std::shared_ptr<const ScriptBytecodeBlob> bytecode) -> void;
  OXGN_NGIN_API auto TryGetPersistentBytecode(CompileKey compile_key)
    -> std::shared_ptr<const ScriptBytecodeBlob>;
  OXGN_NGIN_API auto StorePersistentBytecode(CompileKey compile_key,
    std::shared_ptr<const ScriptBytecodeBlob> bytecode) -> void;
  OXGN_NGIN_API auto PersistCacheSnapshot(const std::vector<
    std::pair<CompileKey, std::shared_ptr<const ScriptBytecodeBlob>>>& snapshot)
    -> void;
  OXGN_NGIN_API auto EnqueueCompletion(
    CompileKey compile_key, const Result& result) -> void;
  OXGN_NGIN_API auto DrainCompletions() -> void;

  co::Nursery* nursery_ {};
  observer_ptr<co::ThreadPool> thread_pool_;
  std::atomic<bool> active_ { true };

  mutable std::mutex compilers_mutex_;
  std::unordered_map<data::pak::ScriptLanguage,
    std::shared_ptr<const IScriptCompiler>>
    compilers_;

  mutable std::mutex in_flight_mutex_;
  std::unordered_map<CompileKey, co::Shared<co::Co<Result>>> in_flight_;

  mutable std::mutex l1_cache_mutex_;
  AnyCache<uint64_t, detail::LruEviction<uint64_t>> l1_cache_;
  std::filesystem::path persistent_cache_path_;
  mutable std::mutex persistent_cache_mutex_;
  std::unordered_map<CompileKey, PersistentIndexEntry> persistent_index_;
  std::unordered_map<CompileKey, std::shared_ptr<const ScriptBytecodeBlob>>
    pending_persistence_;
  std::atomic<bool> cache_dirty_ { false };
  std::atomic<bool> deferred_persistence_enabled_ { true };

  mutable std::mutex subscribers_mutex_;
  std::unordered_map<CompileKey,
    std::vector<std::pair<SubscriberId, CompletionSubscriber>>>
    subscribers_;

  mutable std::mutex completions_mutex_;
  std::vector<std::pair<CompileKey, Result>> pending_completions_;
  SubscriberId next_subscriber_id_ { 1 };

  std::atomic<uint64_t> l1_hits_ { 0 };
  std::atomic<uint64_t> l2_hits_ { 0 };
  std::atomic<uint64_t> compile_started_ { 0 };
  std::atomic<uint64_t> compile_succeeded_ { 0 };
  std::atomic<uint64_t> compile_failed_ { 0 };
  std::atomic<uint64_t> compile_latency_samples_ { 0 };
  std::atomic<uint64_t> compile_latency_total_us_ { 0 };
  std::atomic<uint64_t> compile_latency_max_us_ { 0 };
};

} // namespace oxygen::scripting
