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
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Fluent/OptionValueBuilder.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Headless/Graphics.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/EventLoop.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/OxCo/asio.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Renderer/Renderer.h>

#include "MainModule.h"

using namespace oxygen;
using namespace oxygen::engine;
using namespace oxygen::graphics;
using namespace std::chrono_literals;

namespace {

// Wrap engine plus running flag to model an event loop subject.
struct AsyncEngineApp {
  bool headless { false };
  std::shared_ptr<Platform> platform;
  std::weak_ptr<Graphics> gfx_weak;
  std::shared_ptr<AsyncEngine> engine;
  //! Flag toggled to request loop continue/stop
  std::atomic_bool running { false };
};

//! Event loop tick: drives the engine's asio context (if supplied) and
//! applies frame pacing + cooperative sleep when idle to avoid busy spinning.
auto EventLoopRun(const AsyncEngineApp& app) -> void
{
  while (app.running.load(std::memory_order_relaxed)) {

    app.platform->Async().PollOne();
    if (!app.headless) {
      // Input Events (only if not headless platform)
      app.platform->Events().PollOne();
    }

    if (!app.running.load(std::memory_order_relaxed)) {
      // Additional gentle backoff before running starts.
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
}
} // namespace

template <> struct co::EventLoopTraits<AsyncEngineApp> {
  static auto Run(AsyncEngineApp& app) -> void { EventLoopRun(app); }
  static auto Stop(AsyncEngineApp& app) -> void
  {
    app.running.store(false, std::memory_order_relaxed);
  }
  static auto IsRunning(const AsyncEngineApp& app) -> bool
  {
    return app.running.load(std::memory_order_relaxed);
  }
  static auto EventLoopId(AsyncEngineApp& app) -> EventLoopID
  {
    return EventLoopID(&app);
  }
};

namespace {
auto AsyncMain(AsyncEngineApp& app, uint32_t frames) -> co::Co<int>
{
  // Structured concurrency scope.
  OXCO_WITH_NURSERY(n)
  {
    app.running.store(true, std::memory_order_relaxed);

    co_await n.Start(&Platform::ActivateAsync, std::ref(*app.platform));
    app.platform->Run();

    DCHECK_F(!app.gfx_weak.expired());
    auto gfx = app.gfx_weak.lock();
    co_await n.Start(&Graphics::ActivateAsync, std::ref(*gfx));
    gfx->Run();

    co_await n.Start(&AsyncEngine::ActivateAsync, std::ref(*app.engine));
    app.engine->Run();

    co_await app.engine->Completed();

    co_return co::kCancel;
  };

  co_return EXIT_SUCCESS;
}
} // namespace

extern "C" auto MainImpl(std::span<const char*> args) -> void
{
  using namespace oxygen::clap; // NOLINT

  uint32_t frames = 0U;
  uint32_t target_fps = 100U; // desired frame pacing
  bool headless = false;
  bool fullscreen = false;
  bool enable_vsync = true;

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
    default_command.WithOption(Option::WithKey("headless")
        .About("Run the engine in headless mode")
        .Short("d")
        .Long("headless")
        .WithValue<bool>()
        .DefaultValue(false)
        .UserFriendlyName("headless")
        .StoreTo(&headless)
        .Build());
    default_command.WithOption(Option::WithKey("fullscreen")
        .About("Run the application in full-screen mode")
        .Short("F")
        .Long("fullscreen")
        .WithValue<bool>()
        .DefaultValue(false)
        .UserFriendlyName("fullscreen")
        .StoreTo(&fullscreen)
        .Build());
    default_command.WithOption(Option::WithKey("vsync")
        .About("Enable vertical synchronization (limits FPS to monitor refresh "
               "rate)")
        .Short("s")
        .Long("vsync")
        .WithValue<bool>()
        .DefaultValue(true)
        .UserFriendlyName("vsync")
        .StoreTo(&enable_vsync)
        .Build());

    auto cli = CliBuilder()
                 .ProgramName("async-sim")
                 .Version("0.1")
                 .About("Async engine frame orchestration engine")
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
    LOG_F(INFO, "Parsed fullscreen option = {}", fullscreen);
    LOG_F(INFO, "Parsed vsync option = {}", enable_vsync);
    LOG_F(INFO, "Starting async engine engine for {} frames (target {} fps)",
      frames, target_fps);

    AsyncEngineApp app {};

    // Create the platform
    app.platform = std::make_shared<Platform>(PlatformConfig {
      .headless = headless,
      .thread_pool_size = (std::min)(4u, std::thread::hardware_concurrency()),
    });

    // Load the graphics backend
    const GraphicsConfig gfx_config {
      .enable_debug = true,
      .enable_validation = false,
      .preferred_card_name = std::nullopt,
      .headless = headless,
      .enable_vsync = enable_vsync,
      .extra = {},
    };
    const auto queue_strategy = graphics::SingleQueueStrategy();
    const auto& loader = GraphicsBackendLoader::GetInstance();
    app.gfx_weak = loader.LoadBackend(
      headless ? BackendType::kHeadless : BackendType::kDirect3D12, gfx_config);
    CHECK_F(
      !app.gfx_weak.expired()); // Expect a valid graphics backend, or abort
    app.gfx_weak.lock()->CreateCommandQueues(queue_strategy);

    app.engine = std::make_shared<AsyncEngine>(
      app.platform,
      app.gfx_weak,
      EngineProps {
        .application = { .name = "Async Example", .version = 1u, },
        .target_fps = target_fps,
        .frame_count = frames,
      }
    );

    // Register engine modules
    LOG_F(INFO, "Registering engine modules...");

    // Helper lambda to register modules with error checking
    auto register_module = [&](auto&& module) {
      const bool registered
        = app.engine->RegisterModule(std::forward<decltype(module)>(module));
      if (!registered) {
        LOG_F(ERROR, "Failed to register module");
        throw std::runtime_error("Module registration failed");
      }
    };

    // Register built-in engine modules (one-time)
    {
      oxygen::RendererConfig renderer_config {
        .upload_queue_key = queue_strategy.KeyFor(QueueRole::kTransfer).get(),
      };
      // Create the Renderer - we need unique_ptr for registration and
      // observer_ptr for MainModule
      auto renderer_unique
        = std::make_unique<engine::Renderer>(app.gfx_weak, renderer_config);
      auto renderer_observer = observer_ptr { renderer_unique.get() };

      // Register as module
      register_module(std::move(renderer_unique));

      // Graphics main module (replaces RenderController/RenderThread pattern)
      register_module(std::make_unique<oxygen::examples::async::MainModule>(
        app.platform, app.gfx_weak, fullscreen, renderer_observer));
    }

    const auto rc = co::Run(app, AsyncMain(app, frames));

    app.engine->Stop();
    app.platform->Stop();
    app.engine.reset();
    if (!app.gfx_weak.expired()) {
      auto gfx = app.gfx_weak.lock();
      gfx->Stop();
      gfx.reset();
    }
    // Make sure no one holds a reference to the Graphics instance at this
    // point.
    loader.UnloadBackend();
    app.platform.reset();

    LOG_F(INFO, "exit code: {}", rc);
  } catch (const CmdLineArgumentsError& e) {
    LOG_F(ERROR, "CLI parse error: {}", e.what());
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Unhandled exception: {}", e.what());
  }
}
