//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>

#include <Oxygen/Commands/Command.h>

namespace oxygen::commands {

auto to_string(const CommandFlags value) -> std::string
{
  if (value == CommandFlags::kNone) {
    return "None";
  }
  auto result = std::string {};
  auto checked = CommandFlags::kNone;
  const auto append = [&](const CommandFlags flag, const char* name) -> void {
    if ((value & flag) == flag) {
      if (!result.empty()) {
        result += "|";
      }
      result += name;
      checked |= flag;
    }
  };
  append(CommandFlags::kDevOnly, "DevOnly");
  append(CommandFlags::kCheat, "Cheat");
  return checked == value ? result : "__NotSupported__";
}

auto to_string(const CommandSource value) -> const char*
{
  switch (value) {
  case CommandSource::kLocalConsole:
    return "local_console";
  case CommandSource::kConfigFile:
    return "config";
  case CommandSource::kRemote:
    return "remote";
  case CommandSource::kAutomation:
    return "automation";
  case CommandSource::kUi:
    return "ui";
  case CommandSource::kKey:
    return "key";
  }
  return "__NotSupported__";
}

auto to_string(const ExecutionStatus value) -> const char*
{
  switch (value) {
  case ExecutionStatus::kOk:
    return "Ok";
  case ExecutionStatus::kNotFound:
    return "NotFound";
  case ExecutionStatus::kInvalidArguments:
    return "InvalidArguments";
  case ExecutionStatus::kDenied:
    return "Denied";
  case ExecutionStatus::kDisabled:
    return "Disabled";
  case ExecutionStatus::kError:
    return "Error";
  case ExecutionStatus::kCancelled:
    return "Cancelled";
  case ExecutionStatus::kPending:
    return "Pending";
  }
  return "__NotSupported__";
}

} // namespace oxygen::commands
