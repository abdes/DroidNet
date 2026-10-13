//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Commands/api_export.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::commands {

//! A registration; the default (0) is invalid.
class CommandHandle
  : public NamedType<uint32_t, struct CommandHandleTag, oxygen::Comparable,
      oxygen::Hashable, oxygen::Printable> {
public:
  using Base = NamedType<uint32_t, struct CommandHandleTag, oxygen::Comparable,
    oxygen::Hashable, oxygen::Printable>;

  constexpr CommandHandle() noexcept
    : Base { 0U }
  {
  }

  explicit constexpr CommandHandle(const uint32_t value) noexcept
    : Base { value }
  {
  }

  using Base::get;

  OXGN_CMDS_NDAPI auto IsValid() const noexcept -> bool { return get() != 0U; }
};

//! A command name with arguments already bound to its parameters.
struct CommandInvocation {
  std::string name;
  CommandArgs args;
};

//! The registry of every command, and the policy that admits invocations.
/*!
 Registration, lookup and binding are synchronous. `Run` executes any command
 and is what the engine's dispatcher awaits; `Execute` is the synchronous
 path for callers without an event loop and completes only commands with a
 synchronous handler.
*/
class CommandRegistry final {
public:
  struct SourcePolicy {
    bool allow_commands { true };
    bool allow_dev_only { true };
    bool allow_cheat { true };
  };

  struct AuditEvent {
    CommandSource source { CommandSource::kLocalConsole };
    std::string client;
    std::string name;
    ExecutionStatus status { ExecutionStatus::kOk };
    bool denied_by_policy { false };
  };

  using AuditHook = std::function<void(const AuditEvent&)>;

  OXGN_CMDS_API CommandRegistry();
  ~CommandRegistry() = default;

  OXYGEN_MAKE_NON_COPYABLE(CommandRegistry)
  OXYGEN_MAKE_NON_MOVABLE(CommandRegistry)

  //! Register a command; an invalid handle when the name is taken or the
  //! definition is incomplete.
  OXGN_CMDS_NDAPI auto Register(CommandDefinition definition) -> CommandHandle;
  //! A running invocation of the command keeps its definition alive.
  OXGN_CMDS_API auto Unregister(CommandHandle handle) -> bool;

  OXGN_CMDS_NDAPI auto Find(std::string_view name) const
    -> observer_ptr<const CommandDefinition>;
  //! Every command, ordered by name.
  OXGN_CMDS_NDAPI auto List() const
    -> std::vector<observer_ptr<const CommandDefinition>>;

  //! Bind console tokens to the named command; a failure is the result the
  //! caller reports (`kNotFound` or `kInvalidArguments`).
  OXGN_CMDS_NDAPI auto BindText(
    std::string_view name, std::span<const std::string> tokens) const
    -> Result<CommandInvocation, CommandResult>;
  //! Bind a JSON argument object to the named command.
  OXGN_CMDS_NDAPI auto BindJson(
    std::string_view name, const nlohmann::json& arguments) const
    -> Result<CommandInvocation, CommandResult>;

  //! Whether `context` may run the command now: policy, then availability.
  OXGN_CMDS_NDAPI auto Availability(const CommandDefinition& command,
    const CommandContext& context) const -> CommandAvailability;

  //! Run a command with a synchronous handler; an asynchronous one returns
  //! `kPending` without running.
  OXGN_CMDS_NDAPI auto Execute(const CommandInvocation& invocation,
    const CommandContext& context) -> CommandResult;
  //! Run any command to completion. Cancelling the awaiter cancels an
  //! asynchronous handler.
  OXGN_CMDS_NDAPI auto Run(CommandInvocation invocation, CommandContext context)
    -> co::Co<CommandResult>;

  //! Name, label, help, flags, parameter and result schemas, availability.
  OXGN_CMDS_NDAPI auto Describe(const CommandDefinition& command,
    const CommandContext& context) const -> nlohmann::json;

  OXGN_CMDS_API auto SetSourcePolicy(
    CommandSource source, const SourcePolicy& policy) -> void;
  OXGN_CMDS_API auto SetAuditHook(AuditHook hook) -> void;

private:
  struct Entry {
    CommandHandle handle;
    CommandDefinition definition;
  };

  [[nodiscard]] auto FindEntry(std::string_view name) const
    -> std::shared_ptr<Entry>;
  [[nodiscard]] auto IsAllowed(const CommandDefinition& command,
    const CommandContext& context) const -> bool;
  //! The refusal for an invocation that may not run, if any.
  [[nodiscard]] auto Admit(const CommandDefinition& command,
    const CommandContext& context) const -> std::optional<CommandResult>;
  auto Audit(const CommandContext& context, std::string_view name,
    ExecutionStatus status) const -> void;
  auto RegisterBuiltins() -> void;

  std::map<std::string, std::shared_ptr<Entry>, std::less<>> commands_;
  uint32_t next_handle_ { 1U };
  std::map<CommandSource, SourcePolicy> policies_;
  AuditHook audit_hook_;
};

} // namespace oxygen::commands
