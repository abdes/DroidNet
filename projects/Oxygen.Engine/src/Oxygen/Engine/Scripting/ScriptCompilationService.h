//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Engine/Scripting/IScriptCompilationService.h>
#include <Oxygen/OxCo/LiveObject.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Shared.h>
#include <Oxygen/OxCo/ThreadPool.h>

namespace oxygen::scripting {

class ScriptCompilationService final : public co::LiveObject,
                                       public IScriptCompilationService {
public:
  OXGN_NGIN_API explicit ScriptCompilationService(
    observer_ptr<co::ThreadPool> thread_pool) noexcept;
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
  OXGN_NGIN_NDAPI auto Subscribe(CompileKey compile_key,
    CompletionSubscriber subscriber) -> SubscriptionHandle override;
  OXGN_NGIN_API auto Unsubscribe(const SubscriptionHandle& handle)
    -> bool override;
  OXGN_NGIN_NDAPI auto AcquireForSlot(Request request,
    SlotAcquireCallbacks callbacks) -> SlotAcquireHandle override;

  OXGN_NGIN_API auto OnFrameStart(engine::EngineTag) -> void override;

private:
  OXGN_NGIN_NDAPI auto ExecuteCompileRequest(CompileKey compile_key,
    ScriptSourceBlob source, CompileMode compile_mode) -> co::Co<Result>;
  OXGN_NGIN_NDAPI auto KickoffCompileRequest(Request request) -> co::Co<>;
  OXGN_NGIN_NDAPI auto CompileOnWorkerThread(
    ScriptSourceBlob source, CompileMode compile_mode) const -> co::Co<Result>;
  OXGN_NGIN_API auto EnqueueCompletion(
    CompileKey compile_key, const Result& result) -> void;
  OXGN_NGIN_API auto DrainCompletions() -> void;

  co::Nursery* nursery_ {};
  observer_ptr<co::ThreadPool> thread_pool_ {};
  std::atomic<bool> active_ { true };

  mutable std::mutex compilers_mutex_;
  std::unordered_map<data::pak::ScriptLanguage,
    std::shared_ptr<const IScriptCompiler>>
    compilers_;

  mutable std::mutex in_flight_mutex_;
  std::unordered_map<CompileKey, co::Shared<co::Co<Result>>> in_flight_;

  mutable std::mutex subscribers_mutex_;
  std::unordered_map<CompileKey,
    std::vector<std::pair<SubscriberId, CompletionSubscriber>>>
    subscribers_;

  mutable std::mutex completions_mutex_;
  std::vector<std::pair<CompileKey, Result>> pending_completions_;
  SubscriberId next_subscriber_id_ { 1 };
};

} // namespace oxygen::scripting
