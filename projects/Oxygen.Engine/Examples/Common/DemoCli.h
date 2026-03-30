//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Fluent/OptionValueBuilder.h>
#include <Oxygen/Clap/Option.h>

namespace oxygen::examples::cli {

inline constexpr auto kAdvancedHelpCommand = "help-advanced";

struct RuntimeOptionBindings {
  uint32_t* frames = nullptr;
  uint32_t* target_fps = nullptr;
  bool* headless = nullptr;
  bool* fullscreen = nullptr;
  bool* vsync = nullptr;
};

inline auto MakeRuntimeOptions(const RuntimeOptionBindings& bindings)
  -> clap::Options::Ptr
{
  auto options = std::make_shared<clap::Options>("Runtime options");

  if (bindings.frames != nullptr) {
    options->Add(clap::Option::WithKey("frames")
        .About("Number of frames to run before exiting")
        .Short("f")
        .Long("frames")
        .WithValue<uint32_t>()
        .UserFriendlyName("count")
        .StoreTo(bindings.frames)
        .Build());
  }
  if (bindings.target_fps != nullptr) {
    options->Add(clap::Option::WithKey("fps")
        .About("Target frames per second for frame pacing")
        .Short("r")
        .Long("fps")
        .WithValue<uint32_t>()
        .UserFriendlyName("rate")
        .StoreTo(bindings.target_fps)
        .Build());
  }
  if (bindings.headless != nullptr) {
    options->Add(clap::Option::WithKey("headless")
        .About("Run without creating a visible window")
        .Short("d")
        .Long("headless")
        .WithValue<bool>()
        .DefaultValue(false)
        .UserFriendlyName("headless")
        .StoreTo(bindings.headless)
        .Build());
  }
  if (bindings.fullscreen != nullptr) {
    options->Add(clap::Option::WithKey("fullscreen")
        .About("Start in full-screen mode")
        .Short("F")
        .Long("fullscreen")
        .WithValue<bool>()
        .DefaultValue(false)
        .UserFriendlyName("fullscreen")
        .StoreTo(bindings.fullscreen)
        .Build());
  }
  if (bindings.vsync != nullptr) {
    options->Add(clap::Option::WithKey("vsync")
        .About("Enable vertical synchronization")
        .Short("s")
        .Long("vsync")
        .WithValue<bool>()
        .DefaultValue(true)
        .UserFriendlyName("vsync")
        .StoreTo(bindings.vsync)
        .Build());
  }

  return options;
}

inline auto MakeAdvancedHelpCommand() -> clap::Command::Ptr
{
  return clap::CommandBuilder(std::string(kAdvancedHelpCommand))
    .About("Display help including advanced and development-only options.");
}

inline auto BuildCli(std::string program_name, std::string about,
  clap::Command::Ptr default_command) -> std::unique_ptr<clap::Cli>
{
  const bool has_advanced_options
    = default_command && default_command->HasHiddenOptionGroups();

  clap::CliBuilder builder;
  builder.ProgramName(program_name)
    .Version("0.1")
    .About(about)
    .WithHelpCommand()
    .WithVersionCommand()
    .WithCommand(std::move(default_command));

  if (has_advanced_options) {
    builder
      .Footer("Advanced and development-only options are hidden from the "
              "default help. Run '"
        + program_name + " " + std::string(kAdvancedHelpCommand)
        + "' to show them.")
      .WithCommand(MakeAdvancedHelpCommand());
  }

  return builder.Build();
}

inline auto HandleMetaCommand(const clap::CommandLineContext& context,
  const clap::Command::Ptr& default_command) -> bool
{
  if (context.active_command->PathAsString() == clap::Command::HELP
    || context.active_command->PathAsString() == clap::Command::VERSION
    || context.ovm.HasOption(clap::Command::HELP)) {
    return true;
  }

  if (context.active_command->PathAsString() != kAdvancedHelpCommand) {
    return false;
  }

  default_command->Print(context, context.output_width);
  if (default_command->HasHiddenOptionGroups()) {
    context.out << "\nADVANCED OPTIONS\n\n";
    default_command->PrintHiddenOptions(context, context.output_width);
    context.out << "\n";
  }
  return true;
}

} // namespace oxygen::examples::cli
