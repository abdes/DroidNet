//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <asio/signal_set.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/DSL.h> // pulls in fluent builders
#include <Oxygen/Clap/Fluent/OptionValueBuilder.h> // value builder template
#include <Oxygen/Clap/Option.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/EventLoop.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/OxCo/asio.h>

#include "AsyncEngineSimulator.h"
#include "Examples/GameModule.h"
#include "Modules/ConsoleModule.h"
#include "Modules/DebugOverlayModule.h"
#include "Modules/GeometryRenderModule.h"
#include "Modules/RenderGraphModule.h"

using namespace oxygen::examples::asyncsim;
using namespace std::chrono_literals;

// Wrap simulator plus running flag to model an event loop subject.
struct AsyncEngineApp {
  AsyncEngineSimulator* simulator {
    nullptr
  }; //! Non-owning simulator reference
  asio::io_context* io { nullptr }; //! Non-owning pointer to shared io_context
  std::atomic_bool running {
    false
  }; //! Flag toggled to request loop continue/stop
};
//! Event loop tick: drives the simulator's asio context (if supplied) and
//! applies frame pacing + cooperative sleep when idle to avoid busy spinning.
auto EventLoopRun(AsyncEngineApp& app) -> void
{
  while (app.running.load(std::memory_order_relaxed)) {
    // Drive asio without blocking: run at most one handler. If there's no work,
    // poll_one returns immediately. When idle we sleep briefly to avoid a hot
    // spin (could switch to waiter mechanism later if needed).
    if (app.io) {
      (void)app.io->poll_one();
    }

    if (!app.running.load(std::memory_order_relaxed)) {
      // Additional gentle backoff before running starts.
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
}

template <> struct oxygen::co::EventLoopTraits<AsyncEngineApp> {
  static auto Run(AsyncEngineApp& app) -> void { EventLoopRun(app); }
  static auto Stop(AsyncEngineApp& app) -> void
  {
    app.running.store(false, std::memory_order_relaxed);
  }
  static auto IsRunning(AsyncEngineApp& app) -> bool
  {
    return app.running.load(std::memory_order_relaxed);
  }
  static auto EventLoopId(AsyncEngineApp& app) -> oxygen::co::EventLoopID
  {
    return oxygen::co::EventLoopID(&app);
  }
};

namespace {
auto AsyncMain(AsyncEngineApp& app, uint32_t frames) -> oxygen::co::Co<int>
{
  // Structured concurrency scope.
  OXCO_WITH_NURSERY(n)
  {
    app.running.store(true, std::memory_order_relaxed);

    co_await n.Start(
      &AsyncEngineSimulator::StartAsync, std::ref(*app.simulator));
    app.simulator->Run(frames);

    const auto user_termination = [&]() -> oxygen::co::Co<> {
      asio::signal_set signals(app.io->get_executor(), SIGINT, SIGTERM);
      co_await signals.async_wait(oxygen::co::asio_awaitable);
    };

    co_await oxygen::co::AnyOf(user_termination(), app.simulator->Completed());

    co_return oxygen::co::kCancel;
  };

  co_return EXIT_SUCCESS;
}
} // namespace

extern "C" auto MainImpl(std::span<const char*> args) -> void
{
  using namespace oxygen::clap; // NOLINT

  uint32_t frames = 5U;
  uint32_t target_fps = 60U; // desired frame pacing

  try {
    CommandBuilder default_command(Command::DEFAULT);
    default_command.WithOption(Option::WithKey("frames")
        .About("Number of frames to simulate")
        .Short("f")
        .Long("frames")
        .WithValue<uint32_t>()
        .UserFriendlyName("count")
        .StoreTo(&frames)
        .Build());
    default_command.WithOption(Option::WithKey("fps")
        .About("Target frames per second for pacing the event loop")
        .Short("r")
        .Long("fps")
        .WithValue<uint32_t>()
        .UserFriendlyName("rate")
        .StoreTo(&target_fps)
        .Build());

    auto cli = CliBuilder()
                 .ProgramName("async-sim")
                 .Version("0.1")
                 .About("Async engine frame orchestration simulator")
                 .WithHelpCommand()
                 .WithVersionCommand()
                 .WithCommand(default_command)
                 .Build();

    const int argc = static_cast<int>(args.size());
    const char** argv = args.data();
    auto context = cli->Parse(argc, argv);
    if (context.active_command->PathAsString() == Command::HELP
      || context.active_command->PathAsString() == Command::VERSION
      || context.ovm.HasOption(Command::HELP)) {
      return;
    }

    LOG_F(INFO, "Parsed frames option = {}", frames);
    LOG_F(INFO, "Parsed fps option = {}", target_fps);
    LOG_F(INFO, "Starting async engine simulator for {} frames (target {} fps)",
      frames, target_fps);

    asio::io_context io_ctx; // local context for thread pool
    oxygen::co::ThreadPool pool(
      io_ctx, std::max(1u, std::thread::hardware_concurrency()));
    AsyncEngineSimulator engine { pool, EngineProps { target_fps } };

    // Register engine modules
    LOG_F(INFO, "Registering engine modules...");

    // Core render graph module (priority: High=100 - sets up render graph
    // infrastructure)
    engine.GetModuleManager().RegisterModule(
      std::make_unique<RenderGraphModule>());

    // Core game module (priority: High=100 - main game logic)
    engine.GetModuleManager().RegisterModule(std::make_unique<GameModule>());

    // Geometry rendering module (priority: Normal=500 - geometry rendering via
    // render graph)
    engine.GetModuleManager().RegisterModule(
      std::make_unique<GeometryRenderModule>());

    // Debug overlay module (priority: Low=800 - non-critical debug
    // visualization)
    engine.GetModuleManager().RegisterModule(
      std::make_unique<DebugOverlayModule>());

    // Console module (priority: Normal=500 - development console commands)
    engine.GetModuleManager().RegisterModule(std::make_unique<ConsoleModule>());

    LOG_F(INFO, "Registered {} modules",
      engine.GetModuleManager().GetModuleCount());

    AsyncEngineApp app { &engine, &io_ctx };

    const auto rc = oxygen::co::Run(app, AsyncMain(app, frames));

    LOG_F(INFO, "Simulation completed rc={}", rc);
  } catch (const oxygen::clap::CmdLineArgumentsError& e) {
    LOG_F(ERROR, "CLI parse error: {}", e.what());
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Unhandled exception: {}", e.what());
  }
}
