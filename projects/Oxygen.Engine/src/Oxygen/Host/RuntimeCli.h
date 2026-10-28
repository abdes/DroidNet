//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/api_export.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>

namespace oxygen::host {

//! An invalid or contradictory runtime command-line value.
class RuntimeCliError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

//! Values bound to the runtime, graphics tooling and capture options.
/*!
 Initialize it with the application's defaults before parsing; options the
 user does not pass keep them. `ApplyRuntimeCli` copies every field into the
 host configuration.
*/
struct RuntimeCliState {
  std::uint32_t frames { 0U };
  std::uint32_t target_fps { 0U };
  bool headless { false };
  bool fullscreen { false };
  bool vsync { true };
  std::string resolution;
  //! Control server port; negative leaves the server off, 0 is ephemeral.
  int control_port { -1 };
  //! Launch label of this instance, given with the control server.
  std::string control_name;

  bool enable_debug_layer { DefaultGraphicsDebugLayerEnabled() };
  bool enable_aftermath { DefaultGraphicsAftermathEnabled() };

  //! Preferred GPU: a part of its name, or its PCI device id (decimal or
  //! `0x` hexadecimal). Empty prefers none.
  std::string gpu;
  std::string gpu_id;
  bool require_display { false };
  bool list_gpus { false };
  //! Print the `--list-gpus` output as JSON.
  bool json { false };

  std::string capture_provider { "off" };
  std::string capture_load { "attached" };
  std::string capture_library;
  std::string capture_output;
  std::uint64_t capture_from_frame { 0U };
  std::uint32_t capture_frame_count { 0U };
};

//! Selects the options of the runtime group an application offers.
struct RuntimeOptionSet {
  bool frames { true };
  bool fps { true };
  bool headless { true };
  bool fullscreen { true };
  bool vsync { true };
  bool resolution { true };
  bool control { true };
};

//! Frame count, frame rate, headless, fullscreen, vsync, resolution and
//! the control server port.
OXGN_HOST_NDAPI auto MakeRuntimeOptions(RuntimeCliState& state,
  RuntimeOptionSet selection = {}) -> clap::Options::Ptr;

//! D3D12 debug layer and Nsight Aftermath switches.
OXGN_HOST_NDAPI auto MakeGraphicsToolingOptions(RuntimeCliState& state)
  -> clap::Options::Ptr;

//! GPU selection: a preferred GPU by name or PCI device id, whether the
//! automatic choice needs a GPU that drives a display, and `--list-gpus`.
OXGN_HOST_NDAPI auto MakeGraphicsAdapterOptions(RuntimeCliState& state)
  -> clap::Options::Ptr;

//! The frame capture provider.
OXGN_HOST_NDAPI auto MakeCaptureOptions(RuntimeCliState& state)
  -> clap::Options::Ptr;

//! Capture runtime location, output template and frame range.
OXGN_HOST_NDAPI auto MakeAdvancedCaptureOptions(RuntimeCliState& state)
  -> clap::Options::Ptr;

//! Builds the program's command line, adding `help-advanced` when the
//! default command has hidden option groups.
OXGN_HOST_NDAPI auto BuildCli(const std::string& program_name,
  std::string about, clap::Command::Ptr default_command)
  -> std::unique_ptr<clap::Cli>;

//! Handles help, version, `help-advanced` and `--list-gpus`; true when the
//! program should exit without running.
/*!
 @throw std::runtime_error When `--list-gpus` cannot enumerate the GPUs.
*/
OXGN_HOST_NDAPI auto HandleMetaCommand(const clap::CommandLineContext& context,
  const clap::Command::Ptr& default_command) -> bool;

//! The GPU table `--list-gpus` prints.
/*!
 One row per GPU (index, id, vendor, memory, default choice, display,
 requirements and name), followed by a line with its features and, when it
 falls short, a line with the requirements it misses.
*/
OXGN_HOST_NDAPI auto FormatGraphicsAdapters(
  std::span<const loader::GraphicsAdapter> adapters) -> std::string;

//! The JSON array `--list-gpus --json` prints: one object per GPU with typed
//! fields; unsupported optional features are `null`.
OXGN_HOST_NDAPI auto FormatGraphicsAdaptersJson(
  std::span<const loader::GraphicsAdapter> adapters) -> std::string;

//! True when `option` was given on the command line, not just defaulted.
OXGN_HOST_NDAPI auto WasPassed(
  const clap::CommandLineContext& context, const std::string& option) -> bool;

//! Parses `WIDTHxHEIGHT` with positive dimensions.
OXGN_HOST_NDAPI auto ParseWindowResolution(std::string_view value)
  -> Extent<std::uint32_t>;

//! Validates the GPU selection values and writes them into `config`.
/*!
 @throw RuntimeCliError When both `--gpu` and `--gpu-id` are given, or the
 device id is malformed.
*/
OXGN_HOST_API auto ApplyGraphicsAdapterCli(
  const RuntimeCliState& state, GraphicsConfig& config) -> void;

//! Validates the parsed values and writes them into `config`.
/*!
 Sets the frame count, frame rate, vsync, graphics tooling, GPU selection,
 frame capture, headless and window options and the control server port, and
 seeds the
 `ngin.target_fps` and `gfx.vsync` startup variables when those options were
 passed, so they win over persisted console values.

 @throw RuntimeCliError For contradictory or malformed values.
*/
OXGN_HOST_API auto ApplyRuntimeCli(const clap::CommandLineContext& context,
  const RuntimeCliState& state, HostConfig& config) -> void;

} // namespace oxygen::host
