//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Clap/CliTheme.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Tools/ImportTool/CliBuilder.h>
#include <Oxygen/Cooker/Tools/ImportTool/GlobalOptions.h>

namespace oxygen::content::import::tool {

namespace {

  using oxygen::clap::Option;
  using oxygen::clap::Options;
  using oxygen::content::import::tool::GlobalOptions;

  auto BuildGlobalOptions(GlobalOptions& options) -> std::shared_ptr<Options>
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
        .Short("o")
        .Long("cooked-root")
        .WithValue<std::string>()
        .StoreTo(&options.cooked_root)
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

  auto BuildServiceOptions() -> std::shared_ptr<Options>
  {
    auto group = std::make_shared<Options>("Service Options");

    group->Add(Option::WithKey("thread-pool-size")
        .About("Override import ThreadPool worker count")
        .Long("thread-pool-size")
        .WithValue<uint32_t>()
        .Build());

    group->Add(Option::WithKey("concurrency")
        .About("Override pipeline concurrency (t,b,m,h,g,s as workers/queue)")
        .Long("concurrency")
        .WithValue<std::string>()
        .Build());

    return group;
  }

} // namespace

auto BuildGlobalOptionGroups(GlobalOptions& global_options)
  -> std::vector<std::shared_ptr<clap::Options>>
{
  return { BuildGlobalOptions(global_options), BuildServiceOptions() };
}

} // namespace oxygen::content::import::tool
