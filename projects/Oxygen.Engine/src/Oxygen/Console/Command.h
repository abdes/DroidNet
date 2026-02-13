//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Console/api_export.h>

namespace oxygen::console {

enum class CommandFlags : uint32_t { // NOLINT(*-enum-size)
  kNone = 0,
  kDevOnly = OXYGEN_FLAG(0),
  kCheat = OXYGEN_FLAG(1),
  kRemoteAllowed = OXYGEN_FLAG(2),
};
OXYGEN_DEFINE_FLAGS_OPERATORS(CommandFlags)

enum class CommandSource : uint8_t {
  kLocalConsole,
  kConfigFile,
  kRemote,
  kAutomation,
};

enum class ExecutionStatus : uint8_t {
  kOk,
  kNotFound,
  kInvalidArguments,
  kDenied,
  kError,
};

struct CommandContext {
  CommandSource source { CommandSource::kLocalConsole };
  bool shipping_build { false };
};

struct ExecutionResult {
  ExecutionStatus status { ExecutionStatus::kOk };
  int exit_code { 0 };
  std::string output;
  std::string error;
};

using CommandHandler = std::function<auto(
  const std::vector<std::string>& args, const CommandContext& context)
    ->ExecutionResult>;

struct CommandDefinition {
  std::string name;
  std::string help;
  CommandFlags flags { CommandFlags::kNone };
  CommandHandler handler;
};

struct CommandHandle {
  uint32_t id { 0 };

  OXGN_CONS_NDAPI auto IsValid() const noexcept -> bool { return id != 0; }
};

OXGN_CONS_NDAPI constexpr auto HasFlag(
  const CommandFlags value, const CommandFlags flag) noexcept -> bool
{
  return (value & flag) != CommandFlags::kNone;
}

OXGN_CONS_NDAPI auto to_string(CommandFlags value) -> std::string;
OXGN_CONS_NDAPI auto to_string(CommandSource value) -> const char*;
OXGN_CONS_NDAPI auto to_string(ExecutionStatus value) -> const char*;

} // namespace oxygen::console
