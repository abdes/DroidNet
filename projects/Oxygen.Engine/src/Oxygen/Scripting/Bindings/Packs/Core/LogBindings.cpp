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
  constexpr std::string_view kConsoleLuaLogCommand = "__oxgn_lua_log";
  constexpr size_t kConsoleLogCommandOverheadBytes = 6;
  constexpr console::CommandContext kLuaConsoleContext {
    .source = console::CommandSource::kAutomation,
    .shipping_build = false,
    .record_history = false,
  };

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

  auto EnsureLuaLogCommandRegistered(console::Console& console_service) -> void
  {
    // Fast check if already registered?
    // Console service usually handles re-registration or deduplication.
    // For performance, we might want to do this once, but this function is
    // stateless. Rely on Console::RegisterCommand to be efficient or
    // idempotent.
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

  auto DispatchToConsole(const observer_ptr<AsyncEngine> engine,
    std::string_view level, std::string_view message) -> void
  {
    if (engine == nullptr) {
      return;
    }
    auto& console_service = engine->GetConsole();
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
      DLOG_F(3, "[lua.{}] {}", level_name, message);
      break;
    case LuaLogLevel::kDebug:
      DLOG_F(2, "[lua.{}] {}", level_name, message);
      break;
    case LuaLogLevel::kInfo:
      DLOG_F(2, "[lua.{}] {}", level_name, message);
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
    int n = lua_gettop(state); // Number of arguments
    std::string buffer;

    // Variadic concatenation
    lua_getglobal(state, "tostring");
    for (int i = 1; i <= n; ++i) {
      lua_pushvalue(state, -1); // function to be called
      lua_pushvalue(state, i); // value to print
      lua_call(state, 1, 1);
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
    lua_pop(state, 1); // pop tostring

    EmitEngineLog(level, buffer, level_name);

    // Graceful fallback for Console Access
    const auto engine = GetActiveEngine(state); // May be null
    DispatchToConsole(engine, level_name, buffer);

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
