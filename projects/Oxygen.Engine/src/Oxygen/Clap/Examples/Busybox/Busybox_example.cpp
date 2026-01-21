//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/DSL.h>

using oxygen::clap::Cli;
using oxygen::clap::CliBuilder;
using oxygen::clap::Command;
using oxygen::clap::CommandBuilder;
using oxygen::clap::Option;
using oxygen::clap::Options;

namespace {

struct AppletInfo {
  std::string name;
  std::string about;
  std::string category;
};

auto BuildApplets() -> std::vector<AppletInfo>
{
  return {
    { "cat", "Concatenate files to stdout.", "File" },
    { "cp", "Copy files and directories.", "File" },
    { "echo", "Print arguments to stdout.", "Shell" },
    { "grep", "Search for PATTERN in each FILE.", "Text" },
    { "head", "Print the first lines of files.", "Text" },
    { "ls", "List directory contents.", "File" },
    { "mkdir", "Create directories.", "File" },
    { "mv", "Move or rename files.", "File" },
    { "rm", "Remove files or directories.", "File" },
    { "tail", "Print the last lines of files.", "Text" },
    { "uname", "Print system information.", "System" },
  };
}

auto FindApplet(const std::vector<AppletInfo>& applets,
  std::string_view name) -> const AppletInfo*
{
  const auto match = std::find_if(applets.begin(), applets.end(),
    [name](const AppletInfo& applet) { return applet.name == name; });
  if (match == applets.end()) {
    return nullptr;
  }
  return &(*match);
}

auto PrintAppletList(const std::vector<AppletInfo>& applets, bool full)
  -> void
{
  if (full) {
    for (const auto& applet : applets) {
      std::cout << applet.name << " - " << applet.about << "\n";
    }
    return;
  }

  constexpr int kColumnWidth = 16;
  int column = 0;
  for (const auto& applet : applets) {
    std::cout << applet.name;
    const auto padding =
      std::max(1, kColumnWidth - static_cast<int>(applet.name.size()));
    std::cout << std::string(static_cast<size_t>(padding), ' ');
    ++column;
    if (column == 4) {
      std::cout << "\n";
      column = 0;
    }
  }
  if (column != 0) {
    std::cout << "\n";
  }
}

auto BuildGlobalOptions() -> std::shared_ptr<Options>
{
  auto globals = std::make_shared<Options>("");
  globals->Add(Option::WithKey("list")
                 .Long("list")
                 .About("List applets and exit.")
                 .WithValue<bool>()
                 .Build());
  globals->Add(Option::WithKey("list_full")
                 .Long("list-full")
                 .About("List applets with descriptions and exit.")
                 .WithValue<bool>()
                 .Build());
  globals->Add(Option::WithKey("install")
                 .Long("install")
                 .About("Install applet links into DIR (default: .).")
                 .WithValue<std::string>()
                 .ImplicitValue(".")
                 .Build());
  globals->Add(Option::WithKey("symlinks")
                 .Short("s")
                 .Long("symlinks")
                 .About("Use symlinks when installing applets.")
                 .WithValue<bool>()
                 .Build());
  globals->Add(Option::WithKey("verbose")
                 .Long("verbose")
                 .About("Enable verbose output.")
                 .WithValue<bool>()
                 .Build());
  globals->Add(Option::WithKey("quiet")
                 .Long("quiet")
                 .About("Suppress non-essential output.")
                 .WithValue<bool>()
                 .Build());
  return globals;
}

auto GetOptionalValue(const oxygen::clap::OptionValuesMap& ovm,
  const std::string& key) -> std::optional<std::string>
{
  const auto& values = ovm.ValuesOf(key);
  if (values.empty()) {
    return std::nullopt;
  }
  return values.front().GetAs<std::string>();
}

} // namespace

auto main(int argc, char** argv) -> int
{
  bool loguru_initialized = false;
  try {
    loguru::g_preamble_date = false;
    loguru::g_preamble_file = true;
    loguru::g_preamble_verbose = false;
    loguru::g_preamble_time = false;
    loguru::g_preamble_uptime = false;
    loguru::g_preamble_thread = true;
    loguru::g_preamble_header = false;
#if !defined(NDEBUG)
    loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;
#else
    loguru::g_stderr_verbosity = loguru::Verbosity_0;
#endif // !NDEBUG

    loguru::init(argc, const_cast<const char**>(argv));
    loguru::set_thread_name("main");
    loguru_initialized = true;

    const auto applets = BuildApplets();
    std::vector<std::string> grep_patterns;

    const std::shared_ptr<Command> ls_command = CommandBuilder("ls")
                  .About("List directory contents.")
                              .WithOption(Option::WithKey("all")
                                            .Short("a")
                                            .Long("all")
                                            .About("Do not ignore entries starting with .")
                                            .WithValue<bool>()
                                            .Build())
                              .WithOption(Option::WithKey("long")
                                            .Short("l")
                                            .Long("long")
                                            .About("Use a long listing format.")
                                            .WithValue<bool>()
                                            .Build())
                              .WithOption(Option::WithKey("human")
                                            .Short("h")
                                            .Long("human-readable")
                                            .About("Print sizes in human-readable format.")
                                            .WithValue<bool>()
                                            .Build())
                  .WithOption(Option::WithKey("width")
                        .Short("w")
                        .Long("width")
                        .About("Set output width in columns.")
                        .WithValue<int>()
                        .Build())
                              .WithOption(Option::WithKey("sort")
                                            .Long("sort")
                                            .About("Sort by: name, size, time.")
                                            .WithValue<std::string>()
                                            .DefaultValue("name")
                                            .Build())
                              .WithPositionalArguments(
                                Option::Rest()
                                  .UserFriendlyName("PATHS")
                                  .About("Zero or more paths to list.")
                                  .WithValue<std::string>()
                                  .Build())
                              .Build();

    const std::shared_ptr<Command> grep_command = CommandBuilder("grep")
                                .About("Search for PATTERN in each FILE.")
                                .WithOption(Option::WithKey("ignore_case")
                                              .Short("i")
                                              .Long("ignore-case")
                                              .About("Ignore case distinctions.")
                                              .WithValue<bool>()
                                              .Build())
                                .WithOption(Option::WithKey("invert")
                                              .Short("v")
                                              .Long("invert-match")
                                              .About("Select non-matching lines.")
                                              .WithValue<bool>()
                                              .Build())
                                .WithOption(Option::WithKey("line_number")
                                              .Short("n")
                                              .Long("line-number")
                                              .About("Print line numbers.")
                                              .WithValue<bool>()
                                              .Build())
                                .WithOption(Option::WithKey("expr")
                                              .Short("e")
                                              .Long("regexp")
                                              .About("Use PATTERN for matching.")
                                              .WithValue<std::string>()
                                              .Repeatable()
                                              .CallOnEachValue([&grep_patterns](const std::string& value) {
                                                grep_patterns.push_back(value);
                                              })
                                              .Build())
                                .WithPositionalArguments(
                                  Option::Positional("PATTERN")
                                    .UserFriendlyName("PATTERN")
                                    .About("Search pattern.")
                                    .Required()
                                    .WithValue<std::string>()
                                    .CallOnEachValue([&grep_patterns](const std::string& value) {
                                      grep_patterns.push_back(value);
                                    })
                                    .Build(),
                                  Option::Rest()
                                    .UserFriendlyName("FILES")
                                    .About("Input files.")
                                    .WithValue<std::string>()
                                    .Build())
                                .Build();

    const std::shared_ptr<Command> echo_command = CommandBuilder("echo")
                                .About("Print arguments to standard output.")
                                .WithOption(Option::WithKey("no_newline")
                                              .Short("n")
                                              .Long("no-newline")
                                              .About("Do not output the trailing newline.")
                                              .WithValue<bool>()
                                              .Build())
                                .WithPositionalArguments(
                                  Option::Rest()
                                    .UserFriendlyName("STRINGS")
                                    .About("Strings to print.")
                                    .WithValue<std::string>()
                                    .Build())
                                .Build();

    const std::shared_ptr<Command> cat_command = CommandBuilder("cat")
                               .About("Concatenate files to standard output.")
                               .WithOption(Option::WithKey("number")
                                             .Short("n")
                                             .Long("number")
                                             .About("Number all output lines.")
                                             .WithValue<bool>()
                                             .Build())
                               .WithOption(Option::WithKey("number_nonblank")
                                             .Short("b")
                                             .Long("number-nonblank")
                                             .About("Number nonempty output lines.")
                                             .WithValue<bool>()
                                             .Build())
                               .WithOption(Option::WithKey("show_ends")
                                             .Short("e")
                                             .Long("show-ends")
                                             .About("Display $ at line endings.")
                                             .WithValue<bool>()
                                             .Build())
                               .WithOption(Option::WithKey("show_tabs")
                                             .Short("t")
                                             .Long("show-tabs")
                                             .About("Display TAB characters as ^I.")
                                             .WithValue<bool>()
                                             .Build())
                               .WithOption(Option::WithKey("show_nonprinting")
                                             .Short("v")
                                             .Long("show-nonprinting")
                                             .About("Display non-printing chars.")
                                             .WithValue<bool>()
                                             .Build())
                               .WithPositionalArguments(
                                 Option::Rest()
                                   .UserFriendlyName("FILES")
                                   .About("Files to print.")
                                   .WithValue<std::string>()
                                   .Build())
                               .Build();

    const std::shared_ptr<Command> head_command = CommandBuilder("head")
                                .About("Print the first lines of files.")
                                .WithOption(Option::WithKey("lines")
                                              .Short("n")
                                              .Long("lines")
                                              .About("Print the first N lines.")
                                              .WithValue<int>()
                                              .DefaultValue(10)
                                              .Build())
                                .WithOption(Option::WithKey("bytes")
                                              .Short("c")
                                              .Long("bytes")
                                              .About("Print the first N bytes.")
                                              .WithValue<int>()
                                              .Build())
                                .WithPositionalArguments(
                                  Option::Rest()
                                    .UserFriendlyName("FILES")
                                    .About("Files to read.")
                                    .WithValue<std::string>()
                                    .Build())
                                .Build();

    const std::shared_ptr<Command> tail_command = CommandBuilder("tail")
                                .About("Print the last lines of files.")
                                .WithOption(Option::WithKey("lines")
                                              .Short("n")
                                              .Long("lines")
                                              .About("Print the last N lines.")
                                              .WithValue<int>()
                                              .DefaultValue(10)
                                              .Build())
                                .WithOption(Option::WithKey("bytes")
                                              .Short("c")
                                              .Long("bytes")
                                              .About("Print the last N bytes.")
                                              .WithValue<int>()
                                              .Build())
                                .WithOption(Option::WithKey("follow")
                                              .Short("f")
                                              .Long("follow")
                                              .About("Output appended data as it grows.")
                                              .WithValue<bool>()
                                              .Build())
                                .WithOption(Option::WithKey("retry")
                                              .Short("F")
                                              .Long("retry")
                                              .About("Follow by name with retry.")
                                              .WithValue<bool>()
                                              .Build())
                                .WithOption(Option::WithKey("quiet")
                                              .Short("q")
                                              .Long("quiet")
                                              .About("Never print headers.")
                                              .WithValue<bool>()
                                              .Build())
                                .WithOption(Option::WithKey("verbose")
                                              .Short("v")
                                              .Long("verbose")
                                              .About("Always print headers.")
                                              .WithValue<bool>()
                                              .Build())
                                .WithPositionalArguments(
                                  Option::Rest()
                                    .UserFriendlyName("FILES")
                                    .About("Files to read.")
                                    .WithValue<std::string>()
                                    .Build())
                                .Build();

    const std::shared_ptr<Command> rm_command = CommandBuilder("rm")
                              .About("Remove files or directories.")
                              .WithOption(Option::WithKey("force")
                                            .Short("f")
                                            .Long("force")
                                            .About("Ignore nonexistent files.")
                                            .WithValue<bool>()
                                            .Build())
                              .WithOption(Option::WithKey("recursive")
                                            .Short("r")
                                            .Long("recursive")
                                            .About("Remove directories recursively.")
                                            .WithValue<bool>()
                                            .Build())
                              .WithPositionalArguments(
                                Option::Rest()
                                  .UserFriendlyName("FILES")
                                  .About("Files to remove.")
                                  .WithValue<std::string>()
                                  .Build())
                              .Build();

    const std::shared_ptr<Command> cp_command = CommandBuilder("cp")
                              .About("Copy files and directories.")
                              .WithOption(Option::WithKey("archive")
                                            .Short("a")
                                            .Long("archive")
                                            .About("Preserve attributes and recurse.")
                                            .WithValue<bool>()
                                            .Build())
                              .WithOption(Option::WithKey("recursive")
                                            .Short("r")
                                            .Long("recursive")
                                            .About("Copy directories recursively.")
                                            .WithValue<bool>()
                                            .Build())
                              .WithOption(Option::WithKey("force")
                                            .Short("f")
                                            .Long("force")
                                            .About("Overwrite existing files.")
                                            .WithValue<bool>()
                                            .Build())
                              .WithOption(Option::WithKey("interactive")
                                            .Short("i")
                                            .Long("interactive")
                                            .About("Prompt before overwrite.")
                                            .WithValue<bool>()
                                            .Build())
                              .WithOption(Option::WithKey("no_clobber")
                                            .Short("n")
                                            .Long("no-clobber")
                                            .About("Do not overwrite existing files.")
                                            .WithValue<bool>()
                                            .Build())
                              .WithOption(Option::WithKey("target_dir")
                                            .Short("t")
                                            .Long("target-directory")
                                            .About("Copy into TARGET directory.")
                                            .WithValue<std::string>()
                                            .Build())
                              .WithPositionalArguments(
                                Option::Rest()
                                  .UserFriendlyName("FILES")
                                  .About("Source(s) and destination.")
                                  .WithValue<std::string>()
                                  .Build())
                              .Build();

    const std::shared_ptr<Command> mv_command = CommandBuilder("mv")
                              .About("Move or rename files.")
                              .WithOption(Option::WithKey("force")
                                            .Short("f")
                                            .Long("force")
                                            .About("Do not prompt before overwrite.")
                                            .WithValue<bool>()
                                            .Build())
                              .WithOption(Option::WithKey("interactive")
                                            .Short("i")
                                            .Long("interactive")
                                            .About("Prompt before overwrite.")
                                            .WithValue<bool>()
                                            .Build())
                              .WithOption(Option::WithKey("no_clobber")
                                            .Short("n")
                                            .Long("no-clobber")
                                            .About("Do not overwrite existing files.")
                                            .WithValue<bool>()
                                            .Build())
                              .WithOption(Option::WithKey("target_dir")
                                            .Short("t")
                                            .Long("target-directory")
                                            .About("Move into TARGET directory.")
                                            .WithValue<std::string>()
                                            .Build())
                              .WithPositionalArguments(
                                Option::Rest()
                                  .UserFriendlyName("FILES")
                                  .About("Source(s) and destination.")
                                  .WithValue<std::string>()
                                  .Build())
                              .Build();

    const std::shared_ptr<Command> mkdir_command = CommandBuilder("mkdir")
                                 .About("Create directories.")
                                 .WithOption(Option::WithKey("parents")
                                               .Short("p")
                                               .Long("parents")
                                               .About("Make parent directories as needed.")
                                               .WithValue<bool>()
                                               .Build())
                                 .WithOption(Option::WithKey("mode")
                                               .Short("m")
                                               .Long("mode")
                                               .About("Set directory permissions.")
                                               .WithValue<std::string>()
                                               .Build())
                                 .WithPositionalArguments(
                                   Option::Rest()
                                     .UserFriendlyName("DIRS")
                                     .About("Directories to create.")
                                     .WithValue<std::string>()
                                     .Build())
                                 .Build();

    const std::shared_ptr<Command> uname_command = CommandBuilder("uname")
                                 .About("Print system information.")
                                 .WithOption(Option::WithKey("all")
                                               .Short("a")
                                               .Long("all")
                                               .About("Print all information.")
                                               .WithValue<bool>()
                                               .Build())
                                 .WithOption(Option::WithKey("kernel")
                                               .Short("s")
                                               .Long("kernel-name")
                                               .About("Print the kernel name.")
                                               .WithValue<bool>()
                                               .Build())
                                 .WithOption(Option::WithKey("node")
                                               .Short("n")
                                               .Long("nodename")
                                               .About("Print the network node name.")
                                               .WithValue<bool>()
                                               .Build())
                                 .WithOption(Option::WithKey("release")
                                               .Short("r")
                                               .Long("kernel-release")
                                               .About("Print the kernel release.")
                                               .WithValue<bool>()
                                               .Build())
                                 .WithOption(Option::WithKey("version")
                                               .Short("v")
                                               .Long("kernel-version")
                                               .About("Print the kernel version.")
                                               .WithValue<bool>()
                                               .Build())
                                 .WithOption(Option::WithKey("machine")
                                               .Short("m")
                                               .Long("machine")
                                               .About("Print the machine hardware name.")
                                               .WithValue<bool>()
                                               .Build())
                                 .WithOption(Option::WithKey("processor")
                                               .Short("p")
                                               .Long("processor")
                                               .About("Print the processor type.")
                                               .WithValue<bool>()
                                               .Build())
                                 .WithOption(Option::WithKey("hardware")
                                               .Short("i")
                                               .Long("hardware-platform")
                                               .About("Print the hardware platform.")
                                               .WithValue<bool>()
                                               .Build())
                                 .WithOption(Option::WithKey("os")
                                               .Short("o")
                                               .Long("operating-system")
                                               .About("Print the operating system.")
                                               .WithValue<bool>()
                                               .Build())
                                 .Build();

    const std::shared_ptr<Command> busybox_command = CommandBuilder("busybox")
                                   .About("Invoke an applet by name.")
                                   .WithPositionalArguments(
                                     Option::Positional("APPLET")
                                       .UserFriendlyName("APPLET")
                                       .About("Applet to run.")
                                       .Required()
                                       .WithValue<std::string>()
                                       .Build(),
                                     Option::Rest()
                                       .UserFriendlyName("ARGS")
                                       .About("Arguments passed to the applet.")
                                       .WithValue<std::string>()
                                       .Build())
                                   .Build();

    const std::unique_ptr<Cli> cli
      = CliBuilder()
          .ProgramName("busybox-lite")
          .Version("1.0.0")
          .About("A BusyBox-inspired CLI example showcasing Clap features.")
          .Footer("This is a demonstration CLI. Commands are not executed.")
          .WithThemeSelectionOption()
          .WithGlobalOptions(BuildGlobalOptions())
          .WithHelpCommand()
          .WithVersionCommand()
          .WithCommand(ls_command)
          .WithCommand(cat_command)
          .WithCommand(head_command)
          .WithCommand(tail_command)
          .WithCommand(grep_command)
          .WithCommand(echo_command)
          .WithCommand(cp_command)
          .WithCommand(mv_command)
          .WithCommand(mkdir_command)
          .WithCommand(rm_command)
          .WithCommand(uname_command)
          .WithCommand(busybox_command);

    const auto context = cli->Parse(argc, const_cast<const char**>(argv));
    const auto command_path = context.active_command->PathAsString();
    const auto& ovm = context.ovm;

    if (ovm.HasOption("list") || ovm.HasOption("list_full")) {
      const auto full = ovm.HasOption("list_full");
      PrintAppletList(applets, full);
      loguru::flush();
      loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
      loguru::shutdown();
      return 0;
    }

    if (const auto install_dir = GetOptionalValue(ovm, "install")) {
      const auto use_symlinks = ovm.HasOption("symlinks");
      if (!ovm.HasOption("quiet")) {
        std::cout << "Would install " << applets.size() << " applet(s) into "
                  << *install_dir << " using "
                  << (use_symlinks ? "symlinks" : "hardlinks") << ".\n";
      }
      loguru::flush();
      loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
      loguru::shutdown();
      return 0;
    }

    if (command_path == Command::VERSION || command_path == Command::HELP
      || ovm.HasOption(Command::HELP)) {
      loguru::flush();
      loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
      loguru::shutdown();
      return 0;
    }

    if (context.active_command->IsDefault()) {
      PrintAppletList(applets, false);
      loguru::flush();
      loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
      loguru::shutdown();
      return 0;
    }

    if (ovm.HasOption("verbose")) {
      std::cout << "[verbose] Command: " << command_path << "\n";
    }

    if (command_path == "ls") {
      const auto& paths = ovm.ValuesOf(Option::key_rest_);
      if (!ovm.HasOption("quiet")) {
        std::cout << "Would list " << paths.size() << " path(s).\n";
      }
    } else if (command_path == "busybox") {
      const auto applet = GetOptionalValue(ovm, "APPLET");
      if (!applet) {
        std::cout << "No applet provided.\n";
      } else if (const auto* info = FindApplet(applets, *applet)) {
        if (!ovm.HasOption("quiet")) {
          std::cout << "Would run applet '" << info->name << "': "
                    << info->about << "\n";
        }
      } else {
        std::cout << "Unknown applet: " << *applet << "\n";
        PrintAppletList(applets, false);
      }
    } else if (command_path == "grep") {
      if (!ovm.HasOption("quiet")) {
        std::cout << "Patterns: " << grep_patterns.size() << "\n";
      }
    } else if (command_path == "echo") {
      const auto& values = ovm.ValuesOf(Option::key_rest_);
      if (!ovm.HasOption("quiet")) {
        std::cout << "Would echo " << values.size() << " token(s).\n";
      }
    } else if (command_path == "rm") {
      const auto& values = ovm.ValuesOf(Option::key_rest_);
      if (!ovm.HasOption("quiet")) {
        std::cout << "Would remove " << values.size() << " item(s).\n";
      }
    } else if (!ovm.HasOption("quiet")) {
      std::cout << "Would run applet: " << command_path << "\n";
    }

    loguru::flush();
    loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
    loguru::shutdown();
    return 0;
  } catch (...) {
    if (loguru_initialized) {
      loguru::flush();
      loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
      loguru::shutdown();
    }
    return 1;
  }
}
