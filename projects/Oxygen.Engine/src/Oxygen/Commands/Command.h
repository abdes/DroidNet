//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Commands/api_export.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::commands {

enum class CommandFlags : uint8_t {
  kNone = 0,
  kDevOnly = OXYGEN_FLAG(0),
  kCheat = OXYGEN_FLAG(1),
};
OXYGEN_DEFINE_FLAGS_OPERATORS(CommandFlags)

//! Who started an invocation.
enum class CommandSource : uint8_t {
  kLocalConsole,
  kConfigFile,
  kRemote,
  kAutomation,
  //! A toolbar button, menu item or other widget.
  kUi,
  //! A key binding.
  kKey,
};

enum class ExecutionStatus : uint8_t {
  kOk,
  kNotFound,
  kInvalidArguments,
  kDenied,
  //! The command's availability said no; the error holds the reason.
  kDisabled,
  kError,
  kCancelled,
  //! An asynchronous command that a synchronous caller cannot complete.
  kPending,
};

//! An invocation running under the engine's dispatcher.
using JobId = NamedType<uint64_t, struct JobIdTag, DefaultInitialized,
  Comparable, Hashable, Printable>;

struct CommandContext {
  CommandSource source { CommandSource::kLocalConsole };
  bool shipping_build { false };
  //! A remote client's name, for audit; empty for local sources.
  std::string client {}; // NOLINT(*-member-init)
  //! The job running this invocation; 0 outside the dispatcher.
  JobId job {};
};

struct CommandResult {
  ExecutionStatus status { ExecutionStatus::kOk };
  //! Human-readable text, shown by the console.
  std::string output {}; // NOLINT(*-member-init)
  std::string error {}; // NOLINT(*-member-init)
  //! Structured result for programs; null when the command has none.
  nlohmann::json data {}; // NOLINT(*-member-init)

  [[nodiscard]] static auto Success(
    std::string output = {}, nlohmann::json data = {}) -> CommandResult
  {
    return {
      .status = ExecutionStatus::kOk,
      .output = std::move(output),
      .error = {},
      .data = std::move(data),
    };
  }

  [[nodiscard]] static auto Failure(
    const ExecutionStatus status, std::string error) -> CommandResult
  {
    return {
      .status = status,
      .output = {},
      .error = std::move(error),
      .data = {},
    };
  }
};

struct CommandAvailability {
  bool enabled { true };
  //! Why the command is disabled, for tooltips and remote errors.
  std::string reason {}; // NOLINT(*-member-init)
};

using CommandHandler = std::function<
  auto(const CommandArgs& args, const CommandContext& context)->CommandResult>;
using AsyncCommandHandler = std::function<
  auto(CommandArgs args, CommandContext context)->co::Co<CommandResult>>;
using AvailabilityPredicate
  = std::function<auto(const CommandContext& context)->CommandAvailability>;

//! A command: the only way an interaction changes engine, host or UI state.
struct CommandDefinition {
  //! Dotted name, for example `ngin.step`; also the remote method name.
  std::string name;
  //! Short UI text, for example "Step".
  std::string label;
  std::string help;
  std::vector<CommandParam> params {}; // NOLINT(*-member-init)
  CommandFlags flags { CommandFlags::kNone };
  //! Absent means always available.
  AvailabilityPredicate availability {}; // NOLINT(*-member-init)
  //! Exactly one of `handler` (completes at once) and `async_handler` (may
  //! span frames).
  CommandHandler handler {}; // NOLINT(*-member-init)
  AsyncCommandHandler async_handler {}; // NOLINT(*-member-init)
  //! JSON Schema of `CommandResult::data`, when the command returns data.
  std::optional<nlohmann::json> result_schema {}; // NOLINT(*-member-init)
};

OXGN_CMDS_NDAPI constexpr auto HasFlag(
  const CommandFlags value, const CommandFlags flag) noexcept -> bool
{
  return (value & flag) != CommandFlags::kNone;
}

OXGN_CMDS_NDAPI auto to_string(CommandFlags value) -> std::string;
OXGN_CMDS_NDAPI auto to_string(CommandSource value) -> const char*;
OXGN_CMDS_NDAPI auto to_string(ExecutionStatus value) -> const char*;

} // namespace oxygen::commands
