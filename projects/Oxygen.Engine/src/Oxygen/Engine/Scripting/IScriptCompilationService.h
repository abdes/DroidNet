//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Engine/Scripting/IScriptCompiler.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::scripting {

class IScriptCompilationService {
public:
  using CompileKey = NamedType<uint64_t, struct CompileKeyTag,
    // clang-format off
    DefaultInitialized,
    Comparable,
    Hashable,
    Printable
  >; // clang-format on

  using SubscriberId = NamedType<uint64_t, struct SubscriberIdTag,
    // clang-format off
    DefaultInitialized,
    Comparable,
    Hashable,
    Printable,
    Incrementable
  >; // clang-format on

  struct Request {
    CompileKey compile_key;
    ScriptSourceBlob source;
    CompileMode compile_mode { CompileMode::kDebug };
  };

  using Result = ScriptCompileResult;
  using CompletionSubscriber = std::function<void(const Result&)>;

  struct SubscriptionHandle {
    CompileKey compile_key;
    SubscriberId subscriber_id;
  };

  struct SlotAcquireCallbacks {
    std::function<void(std::shared_ptr<const ScriptBytecodeBlob>)> on_ready;
    std::function<void(std::string)> on_failed;
  };

  struct SlotAcquireHandle {
    std::shared_ptr<const void> placeholder;
    SubscriptionHandle subscription;
    // Populated only when the service is not active and kickoff was not
    // started. When active, ownership is transferred to the internal kickoff
    // path and this request may be empty.
    std::optional<Request> request;
  };

  IScriptCompilationService() = default;
  virtual ~IScriptCompilationService() = default;

  OXYGEN_MAKE_NON_COPYABLE(IScriptCompilationService)
  OXYGEN_MAKE_NON_MOVABLE(IScriptCompilationService)

  [[nodiscard]] virtual auto RegisterCompiler(
    std::shared_ptr<const IScriptCompiler> compiler) -> bool
    = 0;
  [[nodiscard]] virtual auto UnregisterCompiler(
    data::pak::ScriptLanguage language) -> bool
    = 0;
  [[nodiscard]] virtual auto HasCompiler(
    data::pak::ScriptLanguage language) const -> bool
    = 0;

  [[nodiscard]] virtual auto CompileAsync(Request request) -> co::Co<Result>
    = 0;
  [[nodiscard]] virtual auto InFlightCount() const -> size_t = 0;
  [[nodiscard]] virtual auto Subscribe(CompileKey compile_key,
    CompletionSubscriber subscriber) -> SubscriptionHandle
    = 0;
  virtual auto Unsubscribe(const SubscriptionHandle& handle) -> bool = 0;
  [[nodiscard]] virtual auto AcquireForSlot(
    Request request, SlotAcquireCallbacks callbacks) -> SlotAcquireHandle
    = 0;

  virtual auto OnFrameStart(engine::EngineTag) -> void = 0;

  //! Updates the L1 memory cache budget.
  virtual auto SetCacheBudget(size_t budget_bytes) -> void = 0;

  //! Toggles between deferred (batched) and immediate persistence.
  virtual auto SetDeferredPersistence(bool enabled) -> void = 0;

  //! Manually triggers a flush of the persistent cache.
  virtual auto FlushPersistentCache() -> void = 0;
};

} // namespace oxygen::scripting
