//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Console/Command.h>

namespace oxygen::console {

auto to_string(const CommandFlags value) -> std::string
{
  if (value == CommandFlags::kNone) {
    return "None";
  }

  std::string result;
  bool first = true;
  auto checked = CommandFlags::kNone;

  const auto append = [&](const CommandFlags flag, const char* name) {
    if ((value & flag) == flag) {
      if (!first) {
        result += "|";
      }
      result += name;
      first = false;
      checked |= flag;
    }
  };

  append(CommandFlags::kDevOnly, "DevOnly");
  append(CommandFlags::kCheat, "Cheat");
  append(CommandFlags::kRemoteAllowed, "RemoteAllowed");

  return checked == value ? result : "__NotSupported__";
}

auto to_string(const CommandSource value) -> const char*
{
  switch (value) {
  case CommandSource::kLocalConsole:
    return "LocalConsole";
  case CommandSource::kConfigFile:
    return "ConfigFile";
  case CommandSource::kRemote:
    return "Remote";
  case CommandSource::kAutomation:
    return "Automation";
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
  case ExecutionStatus::kError:
    return "Error";
  }
  return "__NotSupported__";
}

} // namespace oxygen::console
