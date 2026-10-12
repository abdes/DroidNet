//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/CliBuilder.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Fluent/OptionValueBuilder.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/RuntimeCli.h>

namespace oxygen::host {

namespace {

  constexpr std::string_view kAdvancedHelpCommand = "help-advanced";

  auto NormalizeToken(std::string value) -> std::string
  {
    std::ranges::transform(
      value, value.begin(), [](const unsigned char c) -> char {
        return static_cast<char>(std::tolower(c));
      });
    return value;
  }

  auto ParseCaptureProvider(std::string value) -> FrameCaptureProvider
  {
    value = NormalizeToken(std::move(value));
    if (value == "off" || value == "none") {
      return FrameCaptureProvider::kNone;
    }
    if (value == "renderdoc") {
      return FrameCaptureProvider::kRenderDoc;
    }
    if (value == "pix") {
      return FrameCaptureProvider::kPix;
    }
    throw RuntimeCliError("Invalid value for --capture-provider. Expected one "
                          "of: off, renderdoc, pix");
  }

  auto ParseCaptureLoadMode(std::string value) -> FrameCaptureInitMode
  {
    value = NormalizeToken(std::move(value));
    if (value == "attached") {
      return FrameCaptureInitMode::kAttachedOnly;
    }
    if (value == "search") {
      return FrameCaptureInitMode::kSearchPath;
    }
    if (value == "path") {
      return FrameCaptureInitMode::kExplicitPath;
    }
    throw RuntimeCliError("Invalid value for --capture-load. Expected one of: "
                          "attached, search, path");
  }

  auto BuildFrameCaptureConfig(const RuntimeCliState& state)
    -> FrameCaptureConfig
  {
    const auto provider = ParseCaptureProvider(state.capture_provider);
    if (provider == FrameCaptureProvider::kNone) {
      return {};
    }
    if (state.headless) {
      throw RuntimeCliError(
        "--capture-provider requires a Direct3D12 run. Disable --headless or "
        "set --capture-provider off.");
    }
    if (state.capture_from_frame != 0U && state.capture_frame_count == 0U) {
      throw RuntimeCliError("--capture-frame-count must be greater than 0 "
                            "when --capture-from-frame is set");
    }
    const auto init_mode = ParseCaptureLoadMode(state.capture_load);
    if (provider == FrameCaptureProvider::kPix && state.capture_frame_count > 0U
      && state.capture_from_frame == 0U) {
      throw RuntimeCliError(
        "--capture-from-frame must be greater than 0 when "
        "--capture-provider=pix and --capture-frame-count is set");
    }
    if (init_mode == FrameCaptureInitMode::kExplicitPath
      && state.capture_library.empty()) {
      throw RuntimeCliError(
        "--capture-library is required when --capture-load=path");
    }
    return FrameCaptureConfig {
      .provider = provider,
      .init_mode = init_mode,
      .from_frame = state.capture_from_frame,
      .frame_count = state.capture_frame_count,
      .module_path = state.capture_library,
      .capture_file_template = state.capture_output,
    };
  }

  auto MakeAdvancedHelpCommand() -> clap::Command::Ptr
  {
    return clap::CommandBuilder(std::string(kAdvancedHelpCommand))
      .About("Display help including advanced and development-only options.");
  }

} // namespace

auto MakeRuntimeOptions(RuntimeCliState& state,
  const RuntimeOptionSet selection) -> clap::Options::Ptr
{
  auto options = std::make_shared<clap::Options>("Runtime options");
  if (selection.frames) {
    options->Add(clap::Option::WithKey("frames")
        .About("Number of frames to run before exiting")
        .Short("f")
        .Long("frames")
        .WithValue<std::uint32_t>()
        .UserFriendlyName("count")
        .StoreTo(&state.frames)
        .Build());
  }
  if (selection.fps) {
    options->Add(clap::Option::WithKey("fps")
        .About("Target frames per second for frame pacing")
        .Short("r")
        .Long("fps")
        .WithValue<std::uint32_t>()
        .UserFriendlyName("rate")
        .StoreTo(&state.target_fps)
        .Build());
  }
  if (selection.headless) {
    options->Add(clap::Option::WithKey("headless")
        .About("Run without creating a visible window")
        .Short("d")
        .Long("headless")
        .WithValue<bool>()
        .DefaultValue(state.headless)
        .UserFriendlyName("headless")
        .StoreTo(&state.headless)
        .Build());
  }
  if (selection.fullscreen) {
    options->Add(clap::Option::WithKey("fullscreen")
        .About("Start in full-screen mode")
        .Short("F")
        .Long("fullscreen")
        .WithValue<bool>()
        .DefaultValue(state.fullscreen)
        .UserFriendlyName("fullscreen")
        .StoreTo(&state.fullscreen)
        .Build());
  }
  if (selection.vsync) {
    options->Add(clap::Option::WithKey("vsync")
        .About("Enable vertical synchronization")
        .Short("s")
        .Long("vsync")
        .WithValue<bool>()
        .DefaultValue(state.vsync)
        .UserFriendlyName("vsync")
        .StoreTo(&state.vsync)
        .Build());
  }
  if (selection.resolution) {
    options->Add(clap::Option::WithKey("resolution")
        .About("Framebuffer resolution in pixels; fullscreen requires an "
               "exact display mode")
        .Long("resolution")
        .WithValue<std::string>()
        .UserFriendlyName("WIDTHxHEIGHT")
        .StoreTo(&state.resolution)
        .Build());
  }
  return options;
}

auto MakeGraphicsToolingOptions(RuntimeCliState& state) -> clap::Options::Ptr
{
  auto options = std::make_shared<clap::Options>("Graphics tooling options");
  options->Add(clap::Option::WithKey("debug-layer")
      .About("Enable the D3D12 debug layer. Mutually exclusive with "
             "--aftermath")
      .Long("debug-layer")
      .WithValue<bool>()
      .DefaultValue(state.enable_debug_layer)
      .UserFriendlyName("enabled")
      .StoreTo(&state.enable_debug_layer)
      .Build());
  options->Add(clap::Option::WithKey("aftermath")
      .About("Enable Nsight Aftermath. Mutually exclusive with "
             "--debug-layer")
      .Long("aftermath")
      .WithValue<bool>()
      .DefaultValue(state.enable_aftermath)
      .UserFriendlyName("enabled")
      .StoreTo(&state.enable_aftermath)
      .Build());
  return options;
}

auto MakeCaptureOptions(RuntimeCliState& state) -> clap::Options::Ptr
{
  auto options = std::make_shared<clap::Options>("Capture options");
  options->Add(clap::Option::WithKey("capture-provider")
      .About("Frame capture provider to enable: off, renderdoc, or pix")
      .Long("capture-provider")
      .WithValue<std::string>()
      .DefaultValue(state.capture_provider)
      .UserFriendlyName("provider")
      .StoreTo(&state.capture_provider)
      .Build());
  return options;
}

auto MakeAdvancedCaptureOptions(RuntimeCliState& state) -> clap::Options::Ptr
{
  auto options = std::make_shared<clap::Options>("Advanced capture options");
  options->Add(clap::Option::WithKey("capture-load")
      .About("How to locate the capture runtime: attached, search, or path")
      .Long("capture-load")
      .WithValue<std::string>()
      .DefaultValue(state.capture_load)
      .UserFriendlyName("mode")
      .StoreTo(&state.capture_load)
      .Build());
  options->Add(clap::Option::WithKey("capture-library")
      .About("Explicit capture runtime library path used when "
             "--capture-load=path")
      .Long("capture-library")
      .WithValue<std::string>()
      .UserFriendlyName("path")
      .StoreTo(&state.capture_library)
      .Build());
  options->Add(clap::Option::WithKey("capture-output")
      .About("Capture file path template")
      .Long("capture-output")
      .WithValue<std::string>()
      .UserFriendlyName("template")
      .StoreTo(&state.capture_output)
      .Build());
  options->Add(clap::Option::WithKey("capture-from-frame")
      .About("Zero-based first rendered frame to capture. Frame 0 is the "
             "first rendered frame. PIX startup capture requires a value "
             "greater than 0 when --capture-frame-count is set")
      .Long("capture-from-frame")
      .WithValue<std::uint64_t>()
      .DefaultValue(state.capture_from_frame)
      .UserFriendlyName("frame")
      .StoreTo(&state.capture_from_frame)
      .Build());
  options->Add(clap::Option::WithKey("capture-frame-count")
      .About("Number of consecutive frames to capture starting at "
             "--capture-from-frame")
      .Long("capture-frame-count")
      .WithValue<std::uint32_t>()
      .DefaultValue(state.capture_frame_count)
      .UserFriendlyName("count")
      .StoreTo(&state.capture_frame_count)
      .Build());
  return options;
}

auto BuildCli(const std::string& program_name, std::string about,
  clap::Command::Ptr default_command) -> std::unique_ptr<clap::Cli>
{
  const bool has_advanced_options
    = default_command && default_command->HasHiddenOptionGroups();

  clap::CliBuilder builder;
  builder.ProgramName(program_name)
    .Version("0.1")
    .About(std::move(about))
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

auto HandleMetaCommand(const clap::CommandLineContext& context,
  const clap::Command::Ptr& default_command) -> bool
{
  const auto command = context.active_command->PathAsString();
  if (command == clap::Command::HELP || command == clap::Command::VERSION
    || context.ovm.HasOption(clap::Command::HELP)) {
    return true;
  }
  if (command != kAdvancedHelpCommand) {
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

auto ParseWindowResolution(const std::string_view value)
  -> Extent<std::uint32_t>
{
  constexpr auto kMaximum
    = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
  const auto dimension = [](const std::string_view text) -> std::uint32_t {
    std::uint32_t result {};
    const auto* const begin = std::to_address(text.begin());
    const auto* const end = std::to_address(text.end());
    const auto parsed = std::from_chars(begin, end, result);
    if (parsed.ec != std::errc {} || parsed.ptr != end || result == 0U
      || result > kMaximum) {
      throw RuntimeCliError("--resolution requires WIDTHxHEIGHT with positive "
                            "pixel dimensions");
    }
    return result;
  };
  const auto separator = value.find_first_of("xX");
  if (separator == std::string_view::npos) {
    throw RuntimeCliError(
      "--resolution requires WIDTHxHEIGHT, for example 1920x1080");
  }
  return {
    .width = dimension(value.substr(0, separator)),
    .height = dimension(value.substr(separator + 1U)),
  };
}

auto ApplyRuntimeCli(const clap::CommandLineContext& context,
  const RuntimeCliState& state, HostConfig& config) -> void
{
  if (!AreGraphicsToolingOptionsMutuallyExclusive(
        state.enable_debug_layer, state.enable_aftermath)) {
    throw RuntimeCliError(
      "--debug-layer and --aftermath are mutually exclusive");
  }
  if (!state.resolution.empty() && state.headless) {
    throw RuntimeCliError("--resolution selects a window framebuffer and "
                          "cannot be used with --headless");
  }

  config.engine.frame_count = state.frames;
  config.engine.target_fps = state.target_fps;
  config.engine.graphics.enable_vsync = state.vsync;
  config.engine.graphics.enable_debug_layer = state.enable_debug_layer;
  config.engine.graphics.enable_aftermath = state.enable_aftermath;
  config.engine.graphics.frame_capture = BuildFrameCaptureConfig(state);
  config.platform.headless = state.headless;
  config.window.fullscreen = state.fullscreen;
  if (!state.resolution.empty()) {
    config.window.resolution = ParseWindowResolution(state.resolution);
  }

  // Explicit options win over persisted console values.
  if (context.ovm.HasOption("fps")) {
    config.startup_cvars.Set(
      "ngin.target_fps", static_cast<std::int64_t>(state.target_fps));
  }
  if (context.ovm.HasOption("vsync")) {
    config.startup_cvars.Set("gfx.vsync", state.vsync);
  }

  LOG_F(INFO,
    "Runtime options: frames={} fps={} headless={} fullscreen={} vsync={} "
    "resolution='{}' debug_layer={} aftermath={} capture_provider={}",
    state.frames, state.target_fps, state.headless, state.fullscreen,
    state.vsync, state.resolution, state.enable_debug_layer,
    state.enable_aftermath, state.capture_provider);
}

} // namespace oxygen::host
