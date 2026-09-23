//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <charconv>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Fluent/OptionValueBuilder.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Console/StartupPlan.h>

namespace oxygen::examples::cli {

inline constexpr auto kAdvancedHelpCommand = "help-advanced";

class GraphicsToolingCliError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct RuntimeOptionBindings {
  uint32_t* frames = nullptr;
  uint32_t* target_fps = nullptr;
  bool* headless = nullptr;
  bool* fullscreen = nullptr;
  bool* vsync = nullptr;
  std::string* resolution = nullptr;
};

inline auto ParseWindowResolution(const std::string_view value)
  -> Extent<std::uint32_t>
{
  constexpr auto maximum = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
  const auto dimension = [](const std::string_view text) -> std::uint32_t {
    std::uint32_t result {};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size()
      || result == 0U || result > maximum) {
      throw std::invalid_argument("--resolution requires WIDTHxHEIGHT with positive pixel dimensions");
    }
    return result;
  };
  const auto separator = value.find_first_of("xX");
  if (separator == std::string_view::npos) {
    throw std::invalid_argument("--resolution requires WIDTHxHEIGHT, for example 1920x1080");
  }
  return { .width = dimension(value.substr(0, separator)),
    .height = dimension(value.substr(separator + 1U)) };
}

inline auto ResolveWindowResolution(const clap::CommandLineContext& context,
  const std::string_view value, const bool headless)
  -> std::optional<Extent<std::uint32_t>>
{
  if (!context.ovm.HasOption("resolution")) return std::nullopt;
  if (headless) {
    throw std::invalid_argument("--resolution selects a window framebuffer and cannot be used with --headless");
  }
  return ParseWindowResolution(value);
}

struct GraphicsToolingCliState {
  bool enable_debug_layer { oxygen::DefaultGraphicsDebugLayerEnabled() };
  bool enable_aftermath { oxygen::DefaultGraphicsAftermathEnabled() };
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

  if (bindings.resolution != nullptr) {
    options->Add(clap::Option::WithKey("resolution")
        .About("Framebuffer resolution in pixels; fullscreen requires an exact display mode")
        .Long("resolution")
        .WithValue<std::string>()
        .UserFriendlyName("WIDTHxHEIGHT")
        .StoreTo(bindings.resolution)
        .Build());
  }

  return options;
}

inline auto SeedCommonRuntimeStartupCVars(
  const clap::CommandLineContext& context, const uint32_t target_fps,
  const bool enable_vsync, console::ConsoleStartupPlan& startup_plan) -> void
{
  if (context.ovm.HasOption("fps")) {
    startup_plan.Set(
      "ngin.target_fps", int64_t { static_cast<int64_t>(target_fps) });
  }
  if (context.ovm.HasOption("vsync")) {
    startup_plan.Set("gfx.vsync", enable_vsync);
  }
}

inline auto MakeAdvancedHelpCommand() -> clap::Command::Ptr
{
  return clap::CommandBuilder(std::string(kAdvancedHelpCommand))
    .About("Display help including advanced and development-only options.");
}

inline auto MakeGraphicsToolingOptions(GraphicsToolingCliState& state)
  -> clap::Options::Ptr
{
  auto options = std::make_shared<clap::Options>("Graphics tooling options");
  options->Add(clap::Option::WithKey("debug-layer")
      .About("Enable the D3D12 debug layer. Mutually exclusive with "
             "--aftermath")
      .Long("debug-layer")
      .WithValue<bool>()
      .DefaultValue(oxygen::DefaultGraphicsDebugLayerEnabled())
      .UserFriendlyName("enabled")
      .StoreTo(&state.enable_debug_layer)
      .Build());
  options->Add(clap::Option::WithKey("aftermath")
      .About("Enable Nsight Aftermath. Mutually exclusive with "
             "--debug-layer")
      .Long("aftermath")
      .WithValue<bool>()
      .DefaultValue(oxygen::DefaultGraphicsAftermathEnabled())
      .UserFriendlyName("enabled")
      .StoreTo(&state.enable_aftermath)
      .Build());
  return options;
}

inline auto ValidateGraphicsToolingOptions(const GraphicsToolingCliState& state)
  -> void
{
  if (!oxygen::AreGraphicsToolingOptionsMutuallyExclusive(
        state.enable_debug_layer, state.enable_aftermath)) {
    LOG_F(ERROR,
      "Rejected graphics tooling CLI options: debug_layer={} aftermath={} "
      "(mutually exclusive)",
      state.enable_debug_layer, state.enable_aftermath);
    throw GraphicsToolingCliError(
      "--debug-layer and --aftermath are mutually exclusive");
  }
}

inline auto LogGraphicsToolingOptions(const GraphicsToolingCliState& state)
  -> void
{
  LOG_F(INFO, "Graphics tooling CLI resolved: debug_layer={} aftermath={}",
    state.enable_debug_layer, state.enable_aftermath);
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
