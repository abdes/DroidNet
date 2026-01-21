//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <string>
#include <thread>

#include <Oxygen/Clap/Fluent/CliBuilder.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Content/Tools/ImportTool/CliBuilder.h>
#include <Oxygen/Content/Tools/ImportTool/GlobalOptions.h>
#include <Oxygen/Content/Tools/ImportTool/ImportCommand.h>

namespace oxygen::content::import::tool {

namespace {

  constexpr std::string_view kProgramName = "Oxygen.Content.ImportTool";
  constexpr std::string_view kVersion = "0.1";

  using oxygen::clap::Option;
  using oxygen::clap::Options;
  using oxygen::content::import::tool::GlobalOptions;

  auto DefaultMaxInFlight() -> uint32_t
  {
    const auto hardware
      = static_cast<uint32_t>(std::thread::hardware_concurrency());
    return std::max<uint32_t>(1U, hardware);
  }

  auto BuildGlobalOptions(GlobalOptions& options)
    -> std::shared_ptr<Options>
  {
    auto group = std::make_shared<Options>("Global Options");

    group->Add(Option::WithKey("quiet")
           .About("Suppress non-error output")
           .Short("q")
           .Long("quiet")
           .WithValue<bool>()
           .DefaultValue(false)
           .StoreTo(&options.quiet)
           .Build());
    group->Add(Option::WithKey("diagnostics-file")
                 .About("Write structured diagnostics to file")
                 .Long("diagnostics-file")
                 .WithValue<std::string>()
                 .StoreTo(&options.diagnostics_file)
                 .Build());

    group->Add(Option::WithKey("cooked-root")
                 .About("Default output directory for all jobs")
                 .Long("cooked-root")
                 .WithValue<std::string>()
                 .StoreTo(&options.cooked_root)
                 .Build());

    group->Add(Option::WithKey("max-in-flight")
                 .About("Maximum concurrent import jobs")
                 .Long("max-in-flight")
                 .WithValue<uint32_t>()
                 .DefaultValue(DefaultMaxInFlight())
                 .StoreTo(&options.max_in_flight)
                 .Build());

    group->Add(Option::WithKey("fail-fast")
                 .About("Stop on first job failure")
                 .Long("fail-fast")
                 .WithValue<bool>()
                 .DefaultValue(false)
                 .StoreTo(&options.fail_fast)
                 .Build());

    group->Add(Option::WithKey("no-color")
                 .About("Disable ANSI color codes")
                 .Long("no-color")
                 .WithValue<bool>()
                 .DefaultValue(false)
                 .StoreTo(&options.no_color)
                 .Build());

    group->Add(Option::WithKey("no-tui")
           .About("Disable curses UI")
           .Long("no-tui")
           .WithValue<bool>()
           .DefaultValue(false)
           .StoreTo(&options.no_tui)
           .Build());

    group->Add(Option::WithKey("theme")
                 .About("Select output theme: plain, dark, light")
                 .Long("theme")
                 .WithValue<oxygen::clap::CliThemeKind>()
                 .DefaultValue(oxygen::clap::CliThemeKind::kDark, "dark")
                 .StoreTo(&options.theme)
                 .Build());

    return group;
  }

} // namespace

auto BuildCli(std::span<ImportCommand* const> commands,
  GlobalOptions& global_options)
  -> std::unique_ptr<clap::Cli>
{
  clap::CliBuilder builder;
  builder.ProgramName(std::string(kProgramName))
    .Version(std::string(kVersion))
    .About("Invoke async import jobs for standalone assets")
    .WithHelpCommand()
    .WithVersionCommand();

  builder.WithGlobalOptions(BuildGlobalOptions(global_options));

  for (auto* command : commands) {
    builder.WithCommand(command->BuildCommand());
  }

  return builder.Build();
}

} // namespace oxygen::content::import::tool
