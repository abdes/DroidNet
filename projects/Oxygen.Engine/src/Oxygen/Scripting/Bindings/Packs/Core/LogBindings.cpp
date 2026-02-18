//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>
#include <string_view>
#include <vector>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/LogBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  constexpr int kLuaArgMessage = 1;
  constexpr std::string_view kConsoleLuaLogCommand = "__oxgn_lua_log";
  constexpr size_t kConsoleLogCommandOverheadBytes = 6;
  constexpr console::CommandContext kLuaConsoleContext {
    .source = console::CommandSource::kAutomation,
    .shipping_build = false,
  };

  enum class LuaLogLevel : uint8_t {
    kTrace,
    kDebug,
    kInfo,
    kWarn,
    kError,
  };

  struct ConsoleLogAccess {
    observer_ptr<AsyncEngine> engine;
    observer_ptr<console::Console> console;
  };

  auto RequireConsoleLogAccess(lua_State* state) -> ConsoleLogAccess
  {
    const auto engine = GetActiveEngine(state);
    if (engine == nullptr) {
      (void)luaL_error(state, "oxygen.log requires active AsyncEngine");
      return {};
    }

    return ConsoleLogAccess {
      .engine = engine,
      .console = observer_ptr<console::Console> { &engine->GetConsole() },
    };
  }

  auto EscapeConsoleQuotedArg(std::string_view text) -> std::string
  {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char c : text) {
      if (c == '\\' || c == '"') {
        escaped.push_back('\\');
      }
      escaped.push_back(c);
    }
    return escaped;
  }

  auto EnsureLuaLogCommandRegistered(console::Console& console_service) -> void
  {
    (void)console_service.RegisterCommand(console::CommandDefinition {
      .name = std::string(kConsoleLuaLogCommand),
      .help = "internal lua log sink",
      .flags = console::CommandFlags::kNone,
      .handler = [](const std::vector<std::string>& args,
                   const console::CommandContext&) -> console::ExecutionResult {
        if (args.size() < 2) {
          return {
            .status = console::ExecutionStatus::kInvalidArguments,
            .exit_code = 2,
            .output = {},
            .error = "usage: __oxgn_lua_log <level> <message>",
          };
        }

        std::string message = args[1];
        for (size_t i = 2; i < args.size(); ++i) {
          message.push_back(' ');
          message.append(args[i]);
        }

        return {
          .status = console::ExecutionStatus::kOk,
          .exit_code = 0,
          .output = "[lua." + args[0] + "] " + message,
          .error = {},
        };
      },
    });
  }

  auto DispatchToConsole(console::Console& console_service,
    std::string_view level, std::string_view message) -> void
  {
    EnsureLuaLogCommandRegistered(console_service);

    std::string line;
    line.reserve(kConsoleLuaLogCommand.size() + level.size() + message.size()
      + kConsoleLogCommandOverheadBytes);
    line.append(kConsoleLuaLogCommand);
    line.push_back(' ');
    line.append(level);
    line.append(" \"");
    line.append(EscapeConsoleQuotedArg(message));
    line.push_back('"');
    (void)console_service.Execute(line, kLuaConsoleContext);
  }

  auto EmitEngineLog(LuaLogLevel level, std::string_view message,
    std::string_view level_name) -> void
  {
    switch (level) {
    case LuaLogLevel::kTrace:
      DLOG_F(1, "[lua.{}] {}", level_name, message);
      break;
    case LuaLogLevel::kDebug:
      DLOG_F(1, "[lua.{}] {}", level_name, message);
      break;
    case LuaLogLevel::kInfo:
      LOG_F(INFO, "[lua.{}] {}", level_name, message);
      break;
    case LuaLogLevel::kWarn:
      LOG_F(WARNING, "[lua.{}] {}", level_name, message);
      break;
    case LuaLogLevel::kError:
      LOG_F(ERROR, "[lua.{}] {}", level_name, message);
      break;
    }
  }

  auto LogThroughEngine(
    lua_State* state, LuaLogLevel level, std::string_view level_name) -> int
  {
    size_t message_size = 0;
    const auto* message_text
      = lua_tolstring(state, kLuaArgMessage, &message_size);
    if (message_text == nullptr) {
      luaL_error(state, "oxygen.log.%s expects a message string",
        std::string(level_name).c_str());
      return 0;
    }

    const std::string_view message { message_text, message_size };
    const auto access = RequireConsoleLogAccess(state);
    if (access.engine == nullptr || access.console == nullptr) {
      luaL_error(state, "oxygen.log requires active AsyncEngine");
      return 0;
    }

    EmitEngineLog(level, message, level_name);
    DispatchToConsole(*access.console, level_name, message);
    return 0;
  }

  auto LuaLogTrace(lua_State* state) -> int
  {
    return LogThroughEngine(state, LuaLogLevel::kTrace, "trace");
  }

  auto LuaLogDebug(lua_State* state) -> int
  {
    return LogThroughEngine(state, LuaLogLevel::kDebug, "debug");
  }

  auto LuaLogInfo(lua_State* state) -> int
  {
    return LogThroughEngine(state, LuaLogLevel::kInfo, "info");
  }

  auto LuaLogWarn(lua_State* state) -> int
  {
    return LogThroughEngine(state, LuaLogLevel::kWarn, "warn");
  }

  auto LuaLogError(lua_State* state) -> int
  {
    return LogThroughEngine(state, LuaLogLevel::kError, "error");
  }
} // namespace

auto RegisterLogBindings(lua_State* state, const int oxygen_table_index) -> void
{
  const int module_index = PushOxygenSubtable(state, oxygen_table_index, "log");

  lua_pushcfunction(state, LuaLogTrace, "log.trace");
  lua_setfield(state, module_index, "trace");

  lua_pushcfunction(state, LuaLogDebug, "log.debug");
  lua_setfield(state, module_index, "debug");

  lua_pushcfunction(state, LuaLogInfo, "log.info");
  lua_setfield(state, module_index, "info");

  lua_pushcfunction(state, LuaLogWarn, "log.warn");
  lua_setfield(state, module_index, "warn");

  lua_pushcfunction(state, LuaLogError, "log.error");
  lua_setfield(state, module_index, "error");

  lua_pop(state, 1);
}

} // namespace oxygen::scripting::bindings
