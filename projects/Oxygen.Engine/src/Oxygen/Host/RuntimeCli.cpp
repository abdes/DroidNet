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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <fmt/format.h>

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
#include <Oxygen/Clap/OptionValue.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Control/ControlServer.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/RuntimeCli.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>

namespace oxygen::host {

namespace {

  constexpr std::string_view kAdvancedHelpCommand = "help-advanced";
  constexpr std::string_view kListGpusOption = "list-gpus";

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

  //! A PCI device id, decimal or `0x` hexadecimal.
  auto ParseGpuDeviceId(std::string_view value) -> DeviceId
  {
    auto base = 10;
    if (value.starts_with("0x") || value.starts_with("0X")) {
      value.remove_prefix(2U);
      base = 16;
    }
    auto id = DeviceId { 0 };
    const auto* const begin = std::to_address(value.begin());
    const auto* const end = std::to_address(value.end());
    const auto parsed = std::from_chars(begin, end, id, base);
    if (value.empty() || parsed.ec != std::errc {} || parsed.ptr != end
      || id < 0) {
      throw RuntimeCliError(
        "--gpu-id expects a PCI device id, decimal or 0x hexadecimal");
    }
    return id;
  }

  //! `--list-gpus` was given and is true.
  auto ListGpusRequested(const clap::CommandLineContext& context) -> bool
  {
    const auto key = std::string(kListGpusOption);
    if (!context.ovm.HasOption(key)) {
      return false;
    }
    return std::ranges::any_of(
      context.ovm.ValuesOf(key), [](const clap::OptionValue& value) -> bool {
        return !value.IsDefaulted() && value.GetAs<bool>();
      });
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
        .Short("w")
        .Long("resolution")
        .WithValue<std::string>()
        .UserFriendlyName("WIDTHxHEIGHT")
        .StoreTo(&state.resolution)
        .Build());
  }
  if (selection.control) {
    options->Add(clap::Option::WithKey("control")
        .About("Start the JSON-RPC control server on a loopback port (0 picks "
               "one); clients find it in the discovery file")
        .Long("control")
        .WithValue<int>()
        .DefaultValue(state.control_port)
        .ImplicitValue(0)
        .UserFriendlyName("port")
        .StoreTo(&state.control_port)
        .Build());
    options->Add(clap::Option::WithKey("control-name")
        .About("Label of this instance for control clients and the window "
               "title; needs --control")
        .Long("control-name")
        .WithValue<std::string>()
        .UserFriendlyName("label")
        .StoreTo(&state.control_name)
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

auto MakeGraphicsAdapterOptions(RuntimeCliState& state) -> clap::Options::Ptr
{
  auto options = std::make_shared<clap::Options>("Graphics adapter options");
  options->Add(clap::Option::WithKey("gpu")
      .About("Prefer the GPU whose name contains this text; mutually "
             "exclusive with --gpu-id")
      .Long("gpu")
      .WithValue<std::string>()
      .UserFriendlyName("name")
      .StoreTo(&state.gpu)
      .Build());
  options->Add(clap::Option::WithKey("gpu-id")
      .About("Prefer the GPU with this PCI device id, decimal or 0x "
             "hexadecimal; mutually exclusive with --gpu")
      .Long("gpu-id")
      .WithValue<std::string>()
      .UserFriendlyName("id")
      .StoreTo(&state.gpu_id)
      .Build());
  options->Add(clap::Option::WithKey("require-display")
      .About("Choose automatically only among GPUs that drive a display; a "
             "preferred GPU is exempt")
      .Long("require-display")
      .WithValue<bool>()
      .DefaultValue(state.require_display)
      .UserFriendlyName("required")
      .StoreTo(&state.require_display)
      .Build());
  options->Add(clap::Option::WithKey(std::string(kListGpusOption))
      .About("List the GPUs with their names and ids, then exit")
      .Long(std::string(kListGpusOption))
      .WithValue<bool>()
      .DefaultValue(state.list_gpus)
      .UserFriendlyName("list")
      .StoreTo(&state.list_gpus)
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
  if (ListGpusRequested(context)) {
    const auto adapters
      = GraphicsBackendLoader::GetInstanceRelaxed().ListAdapters(
        graphics::BackendType::kDirect3D12);
    *context.out << FormatGraphicsAdapters(adapters);
    return true;
  }
  if (command != kAdvancedHelpCommand) {
    return false;
  }
  default_command->Print(context, context.output_width);
  if (default_command->HasHiddenOptionGroups()) {
    *context.out << "\nADVANCED OPTIONS\n\n";
    default_command->PrintHiddenOptions(context, context.output_width);
    *context.out << "\n";
  }
  return true;
}

auto FormatGraphicsAdapters(
  const std::span<const loader::GraphicsAdapter> adapters) -> std::string
{
  if (adapters.empty()) {
    return "No hardware GPU found.\n";
  }
  constexpr std::string_view kNameHeader = "GPU";
  auto name_width = kNameHeader.size();
  for (const auto& adapter : adapters) {
    name_width = std::max(name_width, adapter.name.size());
  }

  constexpr double kBytesPerGiB = 1024.0 * 1024.0 * 1024.0;
  auto text
    = fmt::format("{:<{}}  {:<6}  {:<6}  {:>9}  {:<7}  {}\n", kNameHeader,
      name_width, "Id", "Vendor", "Memory", "Display", "Feature level");
  for (const auto& adapter : adapters) {
    text
      += fmt::format("{:<{}}  0x{:04X}  0x{:04X}  {:>6.2f} GB  {:<7}  {}{}\n",
        adapter.name, name_width, adapter.device_id, adapter.vendor_id,
        static_cast<double>(adapter.dedicated_memory) / kBytesPerGiB,
        adapter.has_display ? "yes" : "no",
        adapter.usable ? adapter.feature_level : "unsupported",
        adapter.default_choice ? "  (default)" : "");
  }
  text += "\nSelect one with --gpu \"<part of the name>\" or --gpu-id <id>.\n";
  return text;
}

auto WasPassed(
  const clap::CommandLineContext& context, const std::string& option) -> bool
{
  return std::ranges::any_of(
    context.ovm.ValuesOf(option), [](const clap::OptionValue& value) -> bool {
      return !value.IsDefaulted();
    });
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

auto ApplyGraphicsAdapterCli(
  const RuntimeCliState& state, GraphicsConfig& config) -> void
{
  if (!state.gpu.empty() && !state.gpu_id.empty()) {
    throw RuntimeCliError("--gpu and --gpu-id are mutually exclusive");
  }
  config.preferred_card_name
    = state.gpu.empty() ? std::nullopt : std::optional { state.gpu };
  config.preferred_card_device_id = state.gpu_id.empty()
    ? std::nullopt
    : std::optional { ParseGpuDeviceId(state.gpu_id) };
  config.require_display = state.require_display;
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
  ApplyGraphicsAdapterCli(state, config.engine.graphics);
  config.engine.graphics.frame_capture = BuildFrameCaptureConfig(state);
  config.platform.headless = state.headless;
  config.window.fullscreen = state.fullscreen;
  if (!state.resolution.empty()) {
    config.window.resolution = ParseWindowResolution(state.resolution);
  }
  if (std::cmp_greater(
        state.control_port, std::numeric_limits<std::uint16_t>::max())) {
    throw RuntimeCliError("--control expects a port up to 65535");
  }
  if (!state.control_name.empty() && state.control_port < 0) {
    throw RuntimeCliError("--control-name needs --control");
  }
  if (state.control_port >= 0) {
    // The port wins; an ownership rule the application set stays.
    auto control = config.control.value_or(control::ControlServerConfig {
      .application = config.engine.application.name,
    });
    control.port = static_cast<std::uint16_t>(state.control_port);
    if (!state.control_name.empty()) {
      control.name = state.control_name;
    }
    config.control = std::move(control);
  }

  // Explicit options win over persisted console values.
  if (WasPassed(context, "fps")) {
    config.startup_cvars.Set(
      "ngin.target_fps", static_cast<std::int64_t>(state.target_fps));
  }
  if (WasPassed(context, "vsync")) {
    config.startup_cvars.Set("gfx.vsync", state.vsync);
  }

  LOG_F(INFO,
    "Runtime options: frames={} fps={} headless={} fullscreen={} vsync={} "
    "resolution='{}' debug_layer={} aftermath={} gpu='{}' gpu_id='{}' "
    "require_display={} capture_provider={}",
    state.frames, state.target_fps, state.headless, state.fullscreen,
    state.vsync, state.resolution, state.enable_debug_layer,
    state.enable_aftermath, state.gpu, state.gpu_id, state.require_display,
    state.capture_provider);
}

} // namespace oxygen::host
