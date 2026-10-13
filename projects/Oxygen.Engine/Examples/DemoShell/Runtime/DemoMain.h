//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/RuntimeCli.h>
#include <Oxygen/Vortex/RendererCapability.h>

namespace oxygen {
class AsyncEngine;
namespace vortex {
  class Renderer;
} // namespace vortex
} // namespace oxygen

namespace oxygen::examples {

//! What a DemoShell example's `MainImpl` hands to `RunDemo`.
struct DemoMain {
  //! Program name and one-line description for the command line help.
  std::string program;
  std::string about;
  //! Engine application name.
  std::string application;
  //! Defaults of the runtime options, before the command line overrides
  //! them.
  host::RuntimeCliState runtime { .target_fps = 100U };
  host::RuntimeOptionSet runtime_options {};
  //! The example's own option groups, each shown or kept for
  //! `help-advanced`.
  std::vector<std::pair<clap::Options::Ptr, bool>> options;
  //! Validates the example's options and adjusts the host configuration;
  //! throws to reject the command line.
  std::function<void(const clap::CommandLineContext&, host::HostConfig&)>
    configure;
  //! Registers the example's modules; `RunHost` already registered input,
  //! ImGui and the command dispatcher.
  std::function<void(AsyncEngine&, host::HostContext&)> register_modules;
};

//! Parses the command line and runs the example on `RunHost`.
/*!
 The command line has the host's runtime, graphics tooling and capture
 options, then the example's groups. The host configuration defaults to the
 workspace with `Examples/Content` as the script source root, the asset
 loader enabled and a 250 microsecond pacing margin; `configure` may change
 any of it.

 Logs the outcome, flushes the log and, in a UI test build, folds the test
 result into the exit code.
*/
auto RunDemo(std::span<const char*> args, DemoMain demo) -> int;

//! The renderer capabilities the DemoShell examples run with.
inline constexpr auto kDemoRendererCapabilities
  = vortex::RendererCapabilityFamily::kScenePreparation
  | vortex::RendererCapabilityFamily::kGpuUploadAndAssetBinding
  | vortex::RendererCapabilityFamily::kLightingData
  | vortex::RendererCapabilityFamily::kShadowing
  | vortex::RendererCapabilityFamily::kEnvironmentLighting
  | vortex::RendererCapabilityFamily::kFinalOutputComposition
  | vortex::RendererCapabilityFamily::kDiagnosticsAndProfiling
  | vortex::RendererCapabilityFamily::kDeferredShading;

//! Registers the Vortex renderer, uploading on the host's transfer queue.
auto RegisterDemoRenderer(AsyncEngine& engine, const host::HostContext& host,
  RendererConfig config = {},
  vortex::CapabilitySet capabilities = kDemoRendererCapabilities)
  -> observer_ptr<vortex::Renderer>;

} // namespace oxygen::examples
