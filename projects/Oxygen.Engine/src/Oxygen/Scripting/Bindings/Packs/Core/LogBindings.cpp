//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <lua.h>
#include <lualib.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Scripting/Bindings/LuaBindingCommon.h>
#include <Oxygen/Scripting/Bindings/Packs/Core/LogBindings.h>

namespace oxygen::scripting::bindings {

namespace {
  constexpr std::string_view kConsoleLuaLogCommand = "__oxgn_lua_log";
  constexpr size_t kConsoleLogCommandOverheadBytes = 6;
  auto LuaConsoleContext() -> commands::CommandContext
  {
    return { .source = commands::CommandSource::kAutomation };
  }

  enum class LuaLogLevel : uint8_t {
    kTrace,
    kDebug,
    kInfo,
    kWarn,
    kError,
  };

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

  auto EnsureLuaLogCommandRegistered(commands::CommandRegistry& registry)
    -> void
  {
    if (registry.Find(kConsoleLuaLogCommand) != nullptr) {
      return;
    }
    static_cast<void>(registry.Register(commands::CommandDefinition {
      .name = std::string(kConsoleLuaLogCommand),
      .label = "Script Log",
      .help = "Internal sink that echoes oxygen.log output to the console",
      .params = {
        commands::CommandParam {
          .name = "level",
          .type = commands::ParamType::kString,
          .required = true,
        },
        commands::CommandParam {
          .name = "message",
          .type = commands::ParamType::kString,
          .required = true,
          .rest_of_line = true,
        },
      },
      .handler = [](const commands::CommandArgs& args,
                   const commands::CommandContext&) -> commands::CommandResult {
        return commands::CommandResult::Success("[lua."
          + args.GetString("level") + "] " + args.GetString("message"));
      },
    }));
  }

  auto DispatchToConsole(IAsyncEngine& engine, std::string_view level,
    std::string_view message) -> void
  {
    auto& console_service = engine.GetConsole();
    EnsureLuaLogCommandRegistered(engine.GetCommands());

    std::string line;
    line.reserve(kConsoleLuaLogCommand.size() + level.size() + message.size()
      + kConsoleLogCommandOverheadBytes);
    line.append(kConsoleLuaLogCommand);
    line.push_back(' ');
    line.append(level);
    line.append(" \"");
    line.append(EscapeConsoleQuotedArg(message));
    line.push_back('"');
    (void)console_service.Execute(line, LuaConsoleContext());
  }

  auto EmitEngineLog(LuaLogLevel level, std::string_view message,
    std::string_view level_name) -> void
  {
    switch (level) {
    case LuaLogLevel::kTrace:
      DLOG_F(3, "[lua.{}] {}", level_name, message);
      break;
    case LuaLogLevel::kDebug:
      DLOG_F(2, "[lua.{}] {}", level_name, message);
      break;
    case LuaLogLevel::kInfo:
      // Script output, so it reaches release builds and log listeners.
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

  auto LogImpl(lua_State* state, LuaLogLevel level, const char* level_name)
    -> int
  {
    const int n = lua_gettop(state);
    std::string buffer;

    // Variadic concatenation using protected tostring calls.
    lua_getglobal(state, "tostring");
    if (lua_isfunction(state, -1) == 0) {
      lua_pop(state, 1);
      luaL_error(state, "global tostring must be a function");
      return 0;
    }

    for (int i = 1; i <= n; ++i) {
      lua_pushvalue(state, -1); // copy the tostring function
      lua_pushvalue(state, i); // copy the argument

      const int status = lua_pcall(state, 1, 1, 0);
      if (status != LUA_OK) {
        // an error occurred
        lua_pop(state, 1); // pop error message
        if (i > 1) {
          buffer.push_back(' ');
        }
        buffer.append("<tostring error>");
      } else {
        // success
        size_t len = 0;
        const char* s = lua_tolstring(state, -1, &len);
        if (s != nullptr) {
          if (i > 1) {
            buffer.push_back(' ');
          }
          buffer.append(s, len);
        }
        lua_pop(state, 1); // pop result
      }
    }

    lua_pop(
      state, 1); // pop the tostring function from the stack out of the loop

    EmitEngineLog(level, buffer, level_name);

    auto engine = GetActiveEngine(state);
    if (engine) {
      DispatchToConsole(*engine, level_name, buffer);
    }
    return 0;
  }

  auto LuaLogTrace(lua_State* state) -> int
  {
    return LogImpl(state, LuaLogLevel::kTrace, "trace");
  }

  auto LuaLogDebug(lua_State* state) -> int
  {
    return LogImpl(state, LuaLogLevel::kDebug, "debug");
  }

  auto LuaLogInfo(lua_State* state) -> int
  {
    return LogImpl(state, LuaLogLevel::kInfo, "info");
  }

  auto LuaLogWarn(lua_State* state) -> int
  {
    return LogImpl(state, LuaLogLevel::kWarn, "warn");
  }

  auto LuaLogError(lua_State* state) -> int
  {
    return LogImpl(state, LuaLogLevel::kError, "error");
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
