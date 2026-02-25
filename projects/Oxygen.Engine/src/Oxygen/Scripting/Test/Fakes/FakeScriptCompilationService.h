//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Engine/Scripting/IScriptCompilationService.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Scripting/Compilers/LuauScriptCompiler.h>

namespace oxygen::scripting::test {

class FakeScriptCompilationService final : public IScriptCompilationService {
public:
  FakeScriptCompilationService()
  {
    const auto luau = std::make_shared<const LuauScriptCompiler>();
    const auto language = luau->Language();
    compilers_.emplace(language, luau);
  }

  ~FakeScriptCompilationService() override = default;

  OXYGEN_MAKE_NON_COPYABLE(FakeScriptCompilationService)
  OXYGEN_MAKE_NON_MOVABLE(FakeScriptCompilationService)

  auto RegisterCompiler(std::shared_ptr<const IScriptCompiler> compiler)
    -> bool override
  {
    if (!compiler) {
      return false;
    }
    compilers_.insert_or_assign(compiler->Language(), std::move(compiler));
    return true;
  }

  auto UnregisterCompiler(const data::pak::ScriptLanguage language)
    -> bool override
  {
    return compilers_.erase(language) > 0;
  }

  [[nodiscard]] auto HasCompiler(const data::pak::ScriptLanguage language) const
    -> bool override
  {
    return compilers_.contains(language);
  }

  auto CompileAsync(Request request) -> co::Co<Result> override
  {
    ++in_flight_count_;
    const auto result = CompileNow(std::move(request));
    --in_flight_count_;
    co_return result;
  }

  [[nodiscard]] auto InFlightCount() const -> size_t override
  {
    return static_cast<size_t>(in_flight_count_);
  }

  auto Subscribe(const CompileKey compile_key, CompletionSubscriber subscriber)
    -> SubscriptionHandle override
  {
    const auto subscriber_id = SubscriberId { ++next_subscriber_id_ };
    subscribers_[compile_key].insert_or_assign(
      subscriber_id, std::move(subscriber));
    return SubscriptionHandle {
      .compile_key = compile_key,
      .subscriber_id = subscriber_id,
    };
  }

  auto Unsubscribe(const SubscriptionHandle& handle) -> bool override
  {
    const auto key_it = subscribers_.find(handle.compile_key);
    if (key_it == subscribers_.end()) {
      return false;
    }
    return key_it->second.erase(handle.subscriber_id) > 0;
  }

  auto AcquireForSlot(Request request, SlotAcquireCallbacks callbacks)
    -> SlotAcquireHandle override
  {
    auto handle = SlotAcquireHandle {
      .placeholder = std::make_shared<int>(0),
      .subscription = Subscribe(request.compile_key,
        [callbacks = std::move(callbacks)](const Result& result) mutable {
          if (result.success && result.HasBytecode()) {
            if (callbacks.on_ready) {
              callbacks.on_ready(result.bytecode);
            }
          } else {
            if (callbacks.on_failed) {
              callbacks.on_failed(result.diagnostics.empty()
                  ? std::string("script compilation failed")
                  : result.diagnostics);
            }
          }
        }),
      .request = std::nullopt,
    };

    const auto result = CompileNow(std::move(request));
    NotifySubscribers(handle.subscription.compile_key, result);
    return handle;
  }

  auto OnFrameStart(engine::EngineTag /*tag*/) -> void override { }

  auto SetCacheBudget(const size_t budget_bytes) -> void override
  {
    cache_budget_bytes_ = budget_bytes;
  }

  auto SetDeferredPersistence(const bool enabled) -> void override
  {
    deferred_persistence_enabled_ = enabled;
  }

  auto FlushPersistentCache() -> void override { }

  [[nodiscard]] auto CacheBudgetBytes() const noexcept -> size_t
  {
    return cache_budget_bytes_;
  }

  [[nodiscard]] auto DeferredPersistenceEnabled() const noexcept -> bool
  {
    return deferred_persistence_enabled_;
  }

private:
  auto CompileNow(Request request) -> Result
  {
    const auto compiler_it = compilers_.find(request.source.Language());
    if (compiler_it == compilers_.end()) {
      return Result {
        .success = false,
        .bytecode = nullptr,
        .diagnostics = "no compiler registered for requested script language",
      };
    }

    const auto result = compiler_it->second->Compile(
      std::move(request.source), request.compile_mode);
    NotifySubscribers(request.compile_key, result);
    return result;
  }

  auto NotifySubscribers(const CompileKey compile_key, const Result& result)
    -> void
  {
    const auto key_it = subscribers_.find(compile_key);
    if (key_it == subscribers_.end()) {
      return;
    }
    for (auto& [_, callback] : key_it->second) {
      if (callback) {
        callback(result);
      }
    }
  }

  using CompilerMap = std::unordered_map<data::pak::ScriptLanguage,
    std::shared_ptr<const IScriptCompiler>>;
  using SubscriberMap = std::unordered_map<SubscriberId, CompletionSubscriber>;

  CompilerMap compilers_;
  std::unordered_map<CompileKey, SubscriberMap> subscribers_;
  uint64_t next_subscriber_id_ { 0 };
  uint64_t in_flight_count_ { 0 };
  size_t cache_budget_bytes_ { 0 };
  bool deferred_persistence_enabled_ { false };
};

} // namespace oxygen::scripting::test
