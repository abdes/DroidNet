//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <source_location>
#include <span>
#include <utility>

#include "DemoShell/Runtime/DemoMain.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/RunHost.h>
#include <Oxygen/Host/RuntimeCli.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>

#ifdef OXYGEN_BUILD_UI_TESTS
#  include "DemoShell/test/UiTestSession.h"
#endif

namespace oxygen::examples {

namespace {

  auto WorkspaceRoot() -> std::filesystem::path
  {
    // Examples/DemoShell/Runtime/DemoMain.cpp -> the engine workspace.
    return std::filesystem::path(std::source_location::current().file_name())
      .parent_path()
      .parent_path()
      .parent_path()
      .parent_path();
  }

  auto Finish(const int exit_code) -> int
  {
#ifdef OXYGEN_BUILD_UI_TESTS
    const int result = testing::UiTestSession::ExitCode(exit_code);
#else
    const int result = exit_code;
#endif
    loguru::flush();
    return result;
  }

} // namespace

auto RunDemo(std::span<const char*> args, DemoMain demo) -> int
{
  using namespace std::chrono_literals;
  try {
    clap::CommandBuilder builder(clap::Command::DEFAULT);
    builder
      .WithOptions(host::MakeRuntimeOptions(demo.runtime, demo.runtime_options))
      .WithOptions(host::MakeGraphicsToolingOptions(demo.runtime))
      .WithOptions(host::MakeCaptureOptions(demo.runtime));
    for (auto& [options, hidden] : demo.options) {
      builder.WithOptions(std::move(options), hidden);
    }
    builder.WithOptions(host::MakeAdvancedCaptureOptions(demo.runtime), true);
    const clap::Command::Ptr default_command = builder;
    const auto cli = host::BuildCli(demo.program, demo.about, default_command);
    const auto context = cli->Parse(args);
    if (host::HandleMetaCommand(context, default_command)) {
      return EXIT_SUCCESS;
    }

    const auto workspace_root = WorkspaceRoot();
    host::HostConfig config {};
    config.engine.renderer.implementation = RendererImplementation::kVortex;
    config.engine.application = { .name = demo.application, .version = 1U };
    config.engine.enable_asset_loader = true;
    config.engine.path_finder_config
      = PathFinderConfig::Create()
          .WithWorkspaceRoot(workspace_root)
          .WithScriptSourceRoots({ workspace_root / "Examples" / "Content" })
          // Each example keeps its own console variables and history, so a
          // setting changed in one does not leak into the others.
          .WithCVarsArchivePath(
            std::filesystem::path("bin/Oxygen") / demo.program / "cvars.json")
          .Build();
    config.engine.timing.pacing_safety_margin = 250us;
    host::ApplyRuntimeCli(context, demo.runtime, config);
    if (demo.configure) {
      demo.configure(context, config);
    }

    const auto exit_code
      = host::RunHost(std::move(config), demo.register_modules);
    return Finish(exit_code);
  } catch (const host::RuntimeCliError& e) {
    LOG_F(ERROR, "CLI error: {}", e.what());
  } catch (const clap::CmdLineArgumentsError& e) {
    LOG_F(ERROR, "CLI parse error: {}", e.what());
  } catch (const std::exception& e) {
    LOG_F(ERROR, "{}: {}", demo.application, e.what());
  }
  return Finish(EXIT_FAILURE);
}

auto RegisterDemoRenderer(AsyncEngine& engine, const host::HostContext& host,
  RendererConfig config, const vortex::CapabilitySet capabilities)
  -> observer_ptr<vortex::Renderer>
{
  config.upload_queue_key
    = host.queue_strategy.KeyFor(graphics::QueueRole::kTransfer).get();
  auto renderer
    = std::make_unique<vortex::Renderer>(host.gfx_weak, config, capabilities);
  const auto registered = observer_ptr { renderer.get() };
  host::RegisterRequiredModule(engine, std::move(renderer));
  return registered;
}

} // namespace oxygen::examples
