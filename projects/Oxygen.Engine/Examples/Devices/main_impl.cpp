//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <span>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Fluent/OptionValueBuilder.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DeviceManager.h>

#include "Common/DemoCli.h"

using oxygen::graphics::d3d12::DeviceManager;
using oxygen::graphics::d3d12::DeviceManagerDesc;

namespace {

auto NormalizeCliToken(std::string value) -> std::string
{
  std::ranges::transform(value, value.begin(),
    [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

auto ParseFeatureLevel(std::string value) -> D3D_FEATURE_LEVEL
{
  value = NormalizeCliToken(std::move(value));
  if (value == "12_0" || value == "12.0") {
    return D3D_FEATURE_LEVEL_12_0;
  }
  if (value == "12_1" || value == "12.1") {
    return D3D_FEATURE_LEVEL_12_1;
  }

  throw std::runtime_error(
    "Invalid value for --feature-level. Expected one of: 12_0, 12_1");
}

} // namespace

extern "C" auto MainImpl(std::span<const char*> args) -> int
{
  using namespace oxygen::clap; // NOLINT

  bool enable_debug = true;
  bool enable_validation = false;
  bool require_display = true;
  bool auto_select_adapter = false;
  std::string feature_level = "12_0";

  try {
    const auto device_options = std::make_shared<Options>("Device options");
    device_options->Add(Option::WithKey("debug")
        .About("Enable the D3D12 debug layer")
        .Long("debug")
        .WithValue<bool>()
        .DefaultValue(true)
        .UserFriendlyName("debug")
        .StoreTo(&enable_debug)
        .Build());
    device_options->Add(Option::WithKey("validation")
        .About("Enable GPU validation")
        .Long("validation")
        .WithValue<bool>()
        .DefaultValue(false)
        .UserFriendlyName("validation")
        .StoreTo(&enable_validation)
        .Build());
    device_options->Add(Option::WithKey("require-display")
        .About("Require a display-capable adapter")
        .Long("require-display")
        .WithValue<bool>()
        .DefaultValue(true)
        .UserFriendlyName("require-display")
        .StoreTo(&require_display)
        .Build());
    device_options->Add(Option::WithKey("auto-select-adapter")
        .About("Automatically pick the best adapter")
        .Long("auto-select-adapter")
        .WithValue<bool>()
        .DefaultValue(false)
        .UserFriendlyName("auto-select-adapter")
        .StoreTo(&auto_select_adapter)
        .Build());
    device_options->Add(Option::WithKey("feature-level")
        .About("Minimum D3D feature level: 12_0 or 12_1")
        .Long("feature-level")
        .WithValue<std::string>()
        .DefaultValue(std::string("12_0"))
        .UserFriendlyName("level")
        .StoreTo(&feature_level)
        .Build());

    const Command::Ptr default_command
      = CommandBuilder(Command::DEFAULT).WithOptions(device_options);
    auto cli = oxygen::examples::cli::BuildCli(
      "devices", "D3D12 device removal demo", default_command);

    const int argc = static_cast<int>(args.size());
    const char** argv = args.data();
    auto context = cli->Parse(argc, argv);
    if (oxygen::examples::cli::HandleMetaCommand(context, default_command)) {
      return EXIT_SUCCESS;
    }

    const oxygen::graphics::d3d12::DeviceManagerDesc props {
      .enable_debug = enable_debug,
      .enable_validation = enable_validation,
      .require_display = require_display,
      .auto_select_adapter = auto_select_adapter,
      .minFeatureLevel = ParseFeatureLevel(feature_level),
    };
    DeviceManager device_manager(props);

    device_manager.SelectBestAdapter(
      []() { LOG_F(INFO, "Device removal detected!"); });

    auto* device = device_manager.Device();
    device->GetDeviceRemovedReason();
    device->RemoveDevice();

    device_manager.SelectBestAdapter(
      []() { LOG_F(INFO, "Device removal handler called"); });
    return EXIT_SUCCESS;
  } catch (const CmdLineArgumentsError& e) {
    LOG_F(ERROR, "Devices CLI parse failed: {}", e.what());
    return EXIT_FAILURE;
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Devices example failed: {}", e.what());
    return EXIT_FAILURE;
  }
}
