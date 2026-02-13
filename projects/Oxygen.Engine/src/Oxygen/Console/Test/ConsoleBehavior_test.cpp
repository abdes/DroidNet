//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Console/Parser.h>

namespace {

using oxygen::console::CommandContext;
using oxygen::console::CommandDefinition;
using oxygen::console::CommandFlags;
using oxygen::console::CommandSource;
using oxygen::console::Console;
using oxygen::console::CVarDefinition;
using oxygen::console::CVarFlags;
using oxygen::console::ExecutionResult;
using oxygen::console::ExecutionStatus;
using oxygen::console::Parser;

NOLINT_TEST(ConsoleParser, HandlesQuotesEscapesAndWhitespace)
{
  const auto tokens = Parser::Tokenize(
    R"(cmd  "arg with space"  'single quoted' escaped\ value "a\"b"  )");

  ASSERT_EQ(tokens.size(), 5);
  EXPECT_EQ(tokens[0], "cmd");
  EXPECT_EQ(tokens[1], "arg with space");
  EXPECT_EQ(tokens[2], "single quoted");
  EXPECT_EQ(tokens[3], "escaped value");
  EXPECT_EQ(tokens[4], "a\"b");
}

NOLINT_TEST(ConsoleParser, EmptyInputYieldsNoTokens)
{
  const auto tokens = Parser::Tokenize("   \t \r\n ");
  EXPECT_TRUE(tokens.empty());
}

NOLINT_TEST(ConsoleParser, PreservesWindowsPaths)
{
  const auto tokens = Parser::Tokenize(R"(exec C:\temp\console.cfg)");
  ASSERT_EQ(tokens.size(), 2);
  EXPECT_EQ(tokens[0], "exec");
  EXPECT_EQ(tokens[1], R"(C:\temp\console.cfg)");
}

NOLINT_TEST(ConsoleCVarValidation, RejectsInvalidTypeAndClampsBounds)
{
  Console console {};

  const auto int_handle = console.RegisterCVar(CVarDefinition {
    .name = "sys.max_lights",
    .help = "Maximum visible lights",
    .default_value = int64_t { 4 },
    .flags = CVarFlags::kNone,
    .min_value = 1.0,
    .max_value = 8.0,
  });
  ASSERT_TRUE(int_handle.IsValid());

  const auto float_handle = console.RegisterCVar(CVarDefinition {
    .name = "r.exposure",
    .help = "Exposure compensation",
    .default_value = 1.0,
    .flags = CVarFlags::kNone,
    .min_value = 0.25,
    .max_value = 4.0,
  });
  ASSERT_TRUE(float_handle.IsValid());

  {
    const auto result = console.Execute("sys.max_lights 999");
    EXPECT_EQ(result.status, ExecutionStatus::kOk);
    const auto snapshot = console.FindCVar("sys.max_lights");
    ASSERT_NE(snapshot, nullptr);
    ASSERT_TRUE(std::holds_alternative<int64_t>(snapshot->current_value));
    EXPECT_EQ(std::get<int64_t>(snapshot->current_value), 8);
  }

  {
    const auto result = console.Execute("sys.max_lights -5");
    EXPECT_EQ(result.status, ExecutionStatus::kOk);
    const auto snapshot = console.FindCVar("sys.max_lights");
    ASSERT_NE(snapshot, nullptr);
    ASSERT_TRUE(std::holds_alternative<int64_t>(snapshot->current_value));
    EXPECT_EQ(std::get<int64_t>(snapshot->current_value), 1);
  }

  {
    const auto result = console.Execute("sys.max_lights nope");
    EXPECT_EQ(result.status, ExecutionStatus::kInvalidArguments);
  }

  {
    const auto result = console.Execute("r.exposure no_float");
    EXPECT_EQ(result.status, ExecutionStatus::kInvalidArguments);
  }
}

NOLINT_TEST(ConsoleCommandPolicy, EnforcesShippingAndRemotePolicies)
{
  Console console {};

  const auto dev_handle = console.RegisterCommand(CommandDefinition {
    .name = "sys.dev_only",
    .help = "Dev only command",
    .flags = CommandFlags::kDevOnly,
    .handler = [](const std::vector<std::string>&,
                 const CommandContext&) -> ExecutionResult {
      return {
        .status = ExecutionStatus::kOk,
        .exit_code = 0,
        .output = "dev-ok",
        .error = {},
      };
    },
  });
  ASSERT_TRUE(dev_handle.IsValid());

  const auto remote_handle = console.RegisterCommand(CommandDefinition {
    .name = "sys.remote_allowed",
    .help = "Remote safe command",
    .flags = CommandFlags::kRemoteAllowed,
    .handler = [](const std::vector<std::string>&,
                 const CommandContext&) -> ExecutionResult {
      return {
        .status = ExecutionStatus::kOk,
        .exit_code = 0,
        .output = "remote-ok",
        .error = {},
      };
    },
  });
  ASSERT_TRUE(remote_handle.IsValid());

  {
    const auto result = console.Execute("sys.dev_only",
      CommandContext {
        .source = CommandSource::kLocalConsole, .shipping_build = true });
    EXPECT_EQ(result.status, ExecutionStatus::kDenied);
  }

  {
    const auto result = console.Execute("sys.remote_allowed",
      CommandContext {
        .source = CommandSource::kRemote, .shipping_build = false });
    EXPECT_EQ(result.status, ExecutionStatus::kOk);
    EXPECT_EQ(result.output, "remote-ok");
  }

  {
    const auto local_only_handle = console.RegisterCommand(CommandDefinition {
      .name = "sys.local_only",
      .help = "Local only command",
      .flags = CommandFlags::kNone,
      .handler = [](const std::vector<std::string>&,
                   const CommandContext&) -> ExecutionResult {
        return {
          .status = ExecutionStatus::kOk,
          .exit_code = 0,
          .output = "local-ok",
          .error = {},
        };
      },
    });
    ASSERT_TRUE(local_only_handle.IsValid());

    const auto result = console.Execute("sys.local_only",
      CommandContext {
        .source = CommandSource::kRemote, .shipping_build = false });
    EXPECT_EQ(result.status, ExecutionStatus::kDenied);
  }
}

NOLINT_TEST(ConsoleCompletion, RanksByFrequencyAndRecency)
{
  Console console {};

  const auto first_handle = console.RegisterCommand(CommandDefinition {
    .name = "r.reset",
    .help = "Reset rendering state",
    .flags = CommandFlags::kNone,
    .handler = [](const std::vector<std::string>&,
                 const CommandContext&) -> ExecutionResult {
      return {
        .status = ExecutionStatus::kOk,
        .exit_code = 0,
        .output = "ok",
        .error = {},
      };
    },
  });
  ASSERT_TRUE(first_handle.IsValid());

  const auto second_handle = console.RegisterCommand(CommandDefinition {
    .name = "r.reload",
    .help = "Reload rendering state",
    .flags = CommandFlags::kNone,
    .handler = [](const std::vector<std::string>&,
                 const CommandContext&) -> ExecutionResult {
      return {
        .status = ExecutionStatus::kOk,
        .exit_code = 0,
        .output = "ok",
        .error = {},
      };
    },
  });
  ASSERT_TRUE(second_handle.IsValid());

  EXPECT_EQ(console.Execute("r.reload").status, ExecutionStatus::kOk);
  EXPECT_EQ(console.Execute("r.reset").status, ExecutionStatus::kOk);
  EXPECT_EQ(console.Execute("r.reload").status, ExecutionStatus::kOk);

  const auto completions = console.Complete("r.re");
  ASSERT_GE(completions.size(), 2);
  EXPECT_EQ(completions[0].token, "r.reload");
  EXPECT_EQ(completions[1].token, "r.reset");
}

NOLINT_TEST(ConsoleCompletion, SupportsCyclingState)
{
  Console console {};

  ASSERT_TRUE(console
      .RegisterCommand(CommandDefinition {
        .name = "gfx.reload",
        .help = "Reload gfx",
        .flags = CommandFlags::kNone,
        .handler = [](const std::vector<std::string>&,
                     const CommandContext&) -> ExecutionResult {
          return {
            .status = ExecutionStatus::kOk,
            .exit_code = 0,
            .output = "ok",
            .error = {},
          };
        },
      })
      .IsValid());
  ASSERT_TRUE(console
      .RegisterCommand(CommandDefinition {
        .name = "gfx.reset",
        .help = "Reset gfx",
        .flags = CommandFlags::kNone,
        .handler = [](const std::vector<std::string>&,
                     const CommandContext&) -> ExecutionResult {
          return {
            .status = ExecutionStatus::kOk,
            .exit_code = 0,
            .output = "ok",
            .error = {},
          };
        },
      })
      .IsValid());

  const auto start = console.BeginCompletionCycle("gfx.re");
  ASSERT_NE(start, nullptr);
  ASSERT_NE(console.CurrentCompletion(), nullptr);

  const auto next = console.NextCompletion();
  ASSERT_NE(next, nullptr);
  const auto wrapped = console.NextCompletion();
  ASSERT_NE(wrapped, nullptr);
  const auto previous = console.PreviousCompletion();
  ASSERT_NE(previous, nullptr);

  EXPECT_EQ(previous->token, next->token);
}

NOLINT_TEST(ConsoleBuiltins, ProvidesHelpFindAndList)
{
  Console console {};

  ASSERT_TRUE(console
      .RegisterCommand(CommandDefinition {
        .name = "sys.custom",
        .help = "Custom command",
        .flags = CommandFlags::kNone,
        .handler = [](const std::vector<std::string>&,
                     const CommandContext&) -> ExecutionResult {
          return {
            .status = ExecutionStatus::kOk,
            .exit_code = 0,
            .output = "ok",
            .error = {},
          };
        },
      })
      .IsValid());

  const auto list_result = console.Execute("list commands");
  EXPECT_EQ(list_result.status, ExecutionStatus::kOk);
  EXPECT_NE(list_result.output.find("sys.custom"), std::string::npos);

  const auto help_result = console.Execute("help sys.custom");
  EXPECT_EQ(help_result.status, ExecutionStatus::kOk);
  EXPECT_NE(help_result.output.find("Custom command"), std::string::npos);

  const auto find_result = console.Execute("find custom");
  EXPECT_EQ(find_result.status, ExecutionStatus::kOk);
  EXPECT_NE(find_result.output.find("sys.custom"), std::string::npos);
}

NOLINT_TEST(ConsoleExecution, SupportsCommandChaining)
{
  Console console {};
  bool was_called = false;

  ASSERT_TRUE(console
      .RegisterCVar(CVarDefinition {
        .name = "sys.max_lights",
        .help = "Maximum visible lights",
        .default_value = int64_t { 1 },
        .flags = CVarFlags::kNone,
        .min_value = 1.0,
        .max_value = 8.0,
      })
      .IsValid());

  ASSERT_TRUE(console
      .RegisterCommand(CommandDefinition {
        .name = "sys.mark",
        .help = "Mark execution",
        .flags = CommandFlags::kNone,
        .handler = [&was_called](const std::vector<std::string>&,
                     const CommandContext&) -> ExecutionResult {
          was_called = true;
          return {
            .status = ExecutionStatus::kOk,
            .exit_code = 0,
            .output = "marked",
            .error = {},
          };
        },
      })
      .IsValid());

  const auto result = console.Execute("sys.max_lights 4; sys.mark");
  EXPECT_EQ(result.status, ExecutionStatus::kOk);
  EXPECT_TRUE(was_called);

  const auto snapshot = console.FindCVar("sys.max_lights");
  ASSERT_NE(snapshot, nullptr);
  ASSERT_TRUE(std::holds_alternative<int64_t>(snapshot->current_value));
  EXPECT_EQ(std::get<int64_t>(snapshot->current_value), 4);
}

NOLINT_TEST(ConsoleExecution, SupportsScriptExecution)
{
  Console console {};

  ASSERT_TRUE(console
      .RegisterCVar(CVarDefinition {
        .name = "r.exposure",
        .help = "Exposure compensation",
        .default_value = 1.0,
        .flags = CVarFlags::kNone,
        .min_value = 0.25,
        .max_value = 4.0,
      })
      .IsValid());

  const auto script_path
    = std::filesystem::temp_directory_path() / "oxygen_console_script.cfg";
  {
    std::ofstream script { script_path };
    script << "# comment\n";
    script << "r.exposure 2.5\n";
  }

  const auto result
    = console.Execute("exec \"" + script_path.generic_string() + "\"");
  EXPECT_EQ(result.status, ExecutionStatus::kOk);

  const auto snapshot = console.FindCVar("r.exposure");
  ASSERT_NE(snapshot, nullptr);
  ASSERT_TRUE(std::holds_alternative<double>(snapshot->current_value));
  EXPECT_DOUBLE_EQ(std::get<double>(snapshot->current_value), 2.5);

  std::filesystem::remove(script_path);
}

NOLINT_TEST(ConsolePersistence, PersistsAndLoadsArchiveCVars)
{
  Console writer {};

  ASSERT_TRUE(writer
      .RegisterCVar(CVarDefinition {
        .name = "r.vsync",
        .help = "VSync mode",
        .default_value = int64_t { 1 },
        .flags = CVarFlags::kArchive,
        .min_value = 0.0,
        .max_value = 1.0,
      })
      .IsValid());
  ASSERT_TRUE(writer
      .RegisterCVar(CVarDefinition {
        .name = "sys.ephemeral",
        .help = "Non archived setting",
        .default_value = int64_t { 10 },
        .flags = CVarFlags::kNone,
        .min_value = 0.0,
        .max_value = 100.0,
      })
      .IsValid());

  EXPECT_EQ(writer.Execute("r.vsync 0").status, ExecutionStatus::kOk);
  EXPECT_EQ(writer.Execute("sys.ephemeral 42").status, ExecutionStatus::kOk);

  const auto temp_root = std::filesystem::temp_directory_path();
  const auto config = oxygen::PathFinderConfig::Create()
                        .WithWorkspaceRoot(temp_root)
                        .WithCVarsArchivePath("oxygen_console/cvars_test.json")
                        .BuildShared();
  const oxygen::PathFinder path_finder(config, temp_root);

  const auto save_result = writer.SaveArchiveCVars(path_finder);
  ASSERT_EQ(save_result.status, ExecutionStatus::kOk);

  Console reader {};
  ASSERT_TRUE(reader
      .RegisterCVar(CVarDefinition {
        .name = "r.vsync",
        .help = "VSync mode",
        .default_value = int64_t { 1 },
        .flags = CVarFlags::kArchive,
        .min_value = 0.0,
        .max_value = 1.0,
      })
      .IsValid());
  ASSERT_TRUE(reader
      .RegisterCVar(CVarDefinition {
        .name = "sys.ephemeral",
        .help = "Non archived setting",
        .default_value = int64_t { 10 },
        .flags = CVarFlags::kNone,
        .min_value = 0.0,
        .max_value = 100.0,
      })
      .IsValid());

  const auto load_result = reader.LoadArchiveCVars(path_finder);
  ASSERT_EQ(load_result.status, ExecutionStatus::kOk);

  const auto archived = reader.FindCVar("r.vsync");
  ASSERT_NE(archived, nullptr);
  EXPECT_EQ(std::get<int64_t>(archived->current_value), 0);

  const auto non_archived = reader.FindCVar("sys.ephemeral");
  ASSERT_NE(non_archived, nullptr);
  EXPECT_EQ(std::get<int64_t>(non_archived->current_value), 10);

  std::filesystem::remove(path_finder.CVarsArchivePath());
}

NOLINT_TEST(ConsoleOverrides, AppliesCommandLineOverrides)
{
  Console console {};
  ASSERT_TRUE(console
      .RegisterCVar(CVarDefinition {
        .name = "r.quality",
        .help = "Quality level",
        .default_value = int64_t { 1 },
        .flags = CVarFlags::kNone,
        .min_value = 1.0,
        .max_value = 5.0,
      })
      .IsValid());
  ASSERT_TRUE(console
      .RegisterCVar(CVarDefinition {
        .name = "sys.profile",
        .help = "Runtime profile",
        .default_value = std::string("default"),
        .flags = CVarFlags::kNone,
      })
      .IsValid());

  const std::array<std::string_view, 3> args {
    "+r.quality=4",
    "+sys.profile",
    "shipping",
  };
  const auto result = console.ApplyCommandLineOverrides(
    std::span<const std::string_view>(args));
  ASSERT_EQ(result.status, ExecutionStatus::kOk);

  const auto quality = console.FindCVar("r.quality");
  ASSERT_NE(quality, nullptr);
  EXPECT_EQ(std::get<int64_t>(quality->current_value), 4);

  const auto profile = console.FindCVar("sys.profile");
  ASSERT_NE(profile, nullptr);
  EXPECT_EQ(std::get<std::string>(profile->current_value), "shipping");
}

NOLINT_TEST(ConsolePolicy, EnforcesCVarSourceAndShippingPolicies)
{
  Console console {};
  ASSERT_TRUE(console
      .RegisterCVar(CVarDefinition {
        .name = "sys.dev_budget",
        .help = "Development-only budget",
        .default_value = int64_t { 1 },
        .flags = CVarFlags::kDevOnly,
        .min_value = 0.0,
        .max_value = 10.0,
      })
      .IsValid());
  ASSERT_TRUE(console
      .RegisterCVar(CVarDefinition {
        .name = "sys.cheat_speed",
        .help = "Cheat speed multiplier",
        .default_value = 1.0,
        .flags = CVarFlags::kCheat,
        .min_value = 1.0,
        .max_value = 4.0,
      })
      .IsValid());

  EXPECT_EQ(console.Execute("sys.cheat_speed 2.0").status, ExecutionStatus::kOk);
  EXPECT_EQ(console.Execute("sys.dev_budget 2",
              CommandContext {
                .source = CommandSource::kLocalConsole,
                .shipping_build = true,
              })
              .status,
    ExecutionStatus::kDenied);
  EXPECT_EQ(console.Execute("sys.cheat_speed 3.0",
              CommandContext {
                .source = CommandSource::kRemote,
                .shipping_build = false,
              })
              .status,
    ExecutionStatus::kDenied);
}

NOLINT_TEST(ConsolePolicy, SupportsRequiresRestartSemantics)
{
  Console console {};
  ASSERT_TRUE(console
      .RegisterCVar(CVarDefinition {
        .name = "gfx.backend",
        .help = "Backend selection",
        .default_value = std::string("d3d12"),
        .flags = CVarFlags::kRequiresRestart,
      })
      .IsValid());

  const auto set_result = console.Execute("gfx.backend vulkan");
  EXPECT_EQ(set_result.status, ExecutionStatus::kOk);
  EXPECT_NE(set_result.output.find("restart required"), std::string::npos);

  const auto snapshot = console.FindCVar("gfx.backend");
  ASSERT_NE(snapshot, nullptr);
  EXPECT_EQ(std::get<std::string>(snapshot->current_value), "d3d12");
  ASSERT_TRUE(snapshot->restart_value.has_value());
  EXPECT_EQ(std::get<std::string>(*snapshot->restart_value), "vulkan");

  EXPECT_EQ(console.ApplyLatchedCVars(), 0U);
  EXPECT_EQ(std::get<std::string>(snapshot->current_value), "d3d12");
}

NOLINT_TEST(ConsoleVisibility, FiltersHiddenCVarsFromPublicListings)
{
  Console console {};
  ASSERT_TRUE(console
      .RegisterCVar(CVarDefinition {
        .name = "sys.visible",
        .help = "Visible CVar",
        .default_value = int64_t { 1 },
        .flags = CVarFlags::kNone,
        .min_value = 0.0,
        .max_value = 1.0,
      })
      .IsValid());
  ASSERT_TRUE(console
      .RegisterCVar(CVarDefinition {
        .name = "sys.secret",
        .help = "Hidden CVar",
        .default_value = int64_t { 1 },
        .flags = CVarFlags::kHidden,
        .min_value = 0.0,
        .max_value = 1.0,
      })
      .IsValid());

  const auto complete = console.Complete("sys.");
  for (const auto& entry : complete) {
    EXPECT_NE(entry.token, "sys.secret");
  }

  const auto list_result = console.Execute("list cvars");
  EXPECT_EQ(list_result.status, ExecutionStatus::kOk);
  EXPECT_NE(list_result.output.find("sys.visible"), std::string::npos);
  EXPECT_EQ(list_result.output.find("sys.secret"), std::string::npos);

  const auto help_hidden = console.Execute("help sys.secret");
  EXPECT_EQ(help_hidden.status, ExecutionStatus::kNotFound);
  EXPECT_EQ(console.Execute("sys.secret").status, ExecutionStatus::kOk);
}

NOLINT_TEST(ConsolePolicy, EnforcesRemoteAllowlistAndEmitsAuditHooks)
{
  Console console {};
  ASSERT_TRUE(console
      .RegisterCommand(CommandDefinition {
        .name = "net.ping",
        .help = "Ping command",
        .flags = CommandFlags::kRemoteAllowed,
        .handler = [](const std::vector<std::string>&,
                     const CommandContext&) -> ExecutionResult {
          return {
            .status = ExecutionStatus::kOk,
            .exit_code = 0,
            .output = "pong",
            .error = {},
          };
        },
      })
      .IsValid());
  ASSERT_TRUE(console
      .RegisterCommand(CommandDefinition {
        .name = "net.reset",
        .help = "Reset command",
        .flags = CommandFlags::kRemoteAllowed,
        .handler = [](const std::vector<std::string>&,
                     const CommandContext&) -> ExecutionResult {
          return {
            .status = ExecutionStatus::kOk,
            .exit_code = 0,
            .output = "reset",
            .error = {},
          };
        },
      })
      .IsValid());

  std::vector<oxygen::console::Registry::AuditEvent> events;
  console.SetAuditHook([&events](const oxygen::console::Registry::AuditEvent& event) {
    events.push_back(event);
  });
  console.SetRemoteAllowlist({ "net.ping" });

  const auto allowed = console.Execute("net.ping",
    CommandContext {
      .source = CommandSource::kRemote,
      .shipping_build = false,
    });
  const auto blocked = console.Execute("net.reset",
    CommandContext {
      .source = CommandSource::kRemote,
      .shipping_build = false,
    });

  EXPECT_EQ(allowed.status, ExecutionStatus::kOk);
  EXPECT_EQ(blocked.status, ExecutionStatus::kDenied);
  ASSERT_GE(events.size(), 2);
  EXPECT_EQ(events[events.size() - 2].subject, "net.ping");
  EXPECT_FALSE(events[events.size() - 2].denied_by_policy);
  EXPECT_EQ(events.back().subject, "net.reset");
  EXPECT_TRUE(events.back().denied_by_policy);
}

NOLINT_TEST(ConsolePolicy, AppliesSourcePolicyMatrixToCommands)
{
  Console console {};
  ASSERT_TRUE(console
      .RegisterCommand(CommandDefinition {
        .name = "sys.echo",
        .help = "Echo command",
        .flags = CommandFlags::kNone,
        .handler = [](const std::vector<std::string>&,
                     const CommandContext&) -> ExecutionResult {
          return {
            .status = ExecutionStatus::kOk,
            .exit_code = 0,
            .output = "echo",
            .error = {},
          };
        },
      })
      .IsValid());

  console.SetSourcePolicy(CommandSource::kAutomation,
    oxygen::console::Registry::SourcePolicy {
      .allow_commands = false,
      .allow_cvars = true,
      .allow_dev_only = true,
      .allow_cheat = false,
    });

  const auto result = console.Execute("sys.echo",
    CommandContext {
      .source = CommandSource::kAutomation,
      .shipping_build = false,
    });
  EXPECT_EQ(result.status, ExecutionStatus::kDenied);
}

NOLINT_TEST(ConsoleHistory, PersistsAcrossSessions)
{
  Console writer {};
  EXPECT_EQ(writer.Execute("help").status, ExecutionStatus::kOk);
  EXPECT_EQ(writer.Execute("list commands").status, ExecutionStatus::kOk);

  const auto temp_root
    = std::filesystem::temp_directory_path() / "oxygen_console_history_test";
  std::error_code ec;
  std::filesystem::remove_all(temp_root, ec);
  std::filesystem::create_directories(temp_root, ec);
  ASSERT_FALSE(ec);

  auto config = oxygen::PathFinderConfig::Create()
                  .WithWorkspaceRoot(temp_root)
                  .WithCVarsArchivePath("console/cvars.json")
                  .Build();
  oxygen::PathFinder path_finder {
    std::make_shared<const oxygen::PathFinderConfig>(std::move(config)),
    temp_root,
  };

  const auto save = writer.SaveHistory(path_finder);
  EXPECT_EQ(save.status, ExecutionStatus::kOk);

  Console reader {};
  const auto load = reader.LoadHistory(path_finder);
  EXPECT_EQ(load.status, ExecutionStatus::kOk);
  const auto& entries = reader.GetHistory().Entries();
  ASSERT_EQ(entries.size(), 2);
  EXPECT_EQ(entries[0], "help");
  EXPECT_EQ(entries[1], "list commands");
}

NOLINT_TEST(ConsoleCapture, ExposesDeterministicExecutionRecordsAndSymbolCatalog)
{
  Console console {};
  ASSERT_TRUE(console
      .RegisterCommand(CommandDefinition {
        .name = "sys.echo",
        .help = "Echo test",
        .flags = CommandFlags::kNone,
        .handler = [](const std::vector<std::string>&,
                     const CommandContext&) -> ExecutionResult {
          return {
            .status = ExecutionStatus::kOk,
            .exit_code = 0,
            .output = "echo",
            .error = {},
          };
        },
      })
      .IsValid());

  EXPECT_EQ(console.Execute("sys.echo").status, ExecutionStatus::kOk);
  EXPECT_EQ(console.Execute("missing.symbol").status, ExecutionStatus::kNotFound);

  const auto& records = console.GetExecutionRecords();
  ASSERT_GE(records.size(), 2);
  EXPECT_EQ(records[records.size() - 2].line, "sys.echo");
  EXPECT_EQ(
    records[records.size() - 2].result.status, ExecutionStatus::kOk);
  EXPECT_EQ(records.back().line, "missing.symbol");
  EXPECT_EQ(records.back().result.status, ExecutionStatus::kNotFound);

  const auto symbols = console.ListSymbols(false);
  const auto it = std::find_if(symbols.begin(), symbols.end(),
    [](const oxygen::console::ConsoleSymbol& symbol) {
      return symbol.token == "sys.echo"
        && symbol.kind == oxygen::console::CompletionKind::kCommand;
    });
  EXPECT_NE(it, symbols.end());

  console.ClearExecutionRecords();
  EXPECT_TRUE(console.GetExecutionRecords().empty());
}

} // namespace
