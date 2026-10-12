//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/RunHost.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/EventLoop.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Platform/Platform.h>

template <> struct oxygen::co::EventLoopTraits<oxygen::host::HostContext> {
  static auto Run(oxygen::host::HostContext& host) -> void
  {
    while (host.running.load(std::memory_order_relaxed)) {
      host.platform->Async().PollOne();
      if (!host.headless) {
        host.platform->Events().PollOne();
      }
    }
  }
  static auto Stop(oxygen::host::HostContext& host) -> void
  {
    host.running.store(false, std::memory_order_relaxed);
  }
  static auto IsRunning(const oxygen::host::HostContext& host) -> bool
  {
    return host.running.load(std::memory_order_relaxed);
  }
  static auto EventLoopId(oxygen::host::HostContext& host) -> EventLoopID
  {
    return EventLoopID(&host);
  }
};

namespace oxygen::host {

namespace {

  auto RegisterHostModules(HostContext& host) -> void
  {
    auto input
      = std::make_unique<engine::InputSystem>(host.platform->Input().ForRead());
    host.input_system = observer_ptr { input.get() };
    RegisterRequiredModule(*host.engine, std::move(input));
    if (!host.headless) {
      RegisterRequiredModule(
        *host.engine, std::make_unique<imgui::ImGuiModule>());
    }
  }

  auto AsyncMain(HostContext& host, const RegisterModules& register_modules,
    const std::string& application) -> co::Co<int>
  {
    OXCO_WITH_NURSERY(n)
    {
      host.running.store(true, std::memory_order_relaxed);
      host.nursery = observer_ptr { &n };

      co_await n.Start(&Platform::ActivateAsync, std::ref(*host.platform));
      host.platform->Run();

      const auto gfx = host.gfx_weak.lock();
      CHECK_NOTNULL_F(gfx);
      co_await n.Start(&Graphics::ActivateAsync, std::ref(*gfx));
      gfx->Run();

      co_await n.Start(&AsyncEngine::ActivateAsync, std::ref(*host.engine));
      host.engine->Run();

      RegisterHostModules(host);
      if (register_modules) {
        register_modules(*host.engine, host);
      }

      n.Start([&host, &application] -> co::Co<> {
        co_await host.platform->Windows().LastWindowClosed();
        LOG_F(
          INFO, "{}: last window closed -> stopping the engine", application);
        host.engine->Stop();
      });

      co_await host.engine->Completed();
      host.nursery = nullptr;
      co_return co::kCancel;
    };
    co_return EXIT_SUCCESS;
  }

  //! Resolves the settings the host derives from other settings.
  auto Resolve(HostConfig& config) -> void
  {
    const bool headless = config.platform.headless;
    config.engine.graphics.headless = headless;
    config.engine.graphics.enable_imgui = !headless;
    if (headless) {
      config.backend = graphics::BackendType::kHeadless;
    }
    if (const auto cores = std::thread::hardware_concurrency(); cores > 0U) {
      config.platform.thread_pool_size
        = (std::min)(config.platform.thread_pool_size, cores);
    }
  }

  //! Stops and releases everything `RunHost` created, in reverse order.
  auto TearDown(HostContext& host, const GraphicsBackendLoader& loader,
    const bool backend_loaded) -> void
  {
    if (host.engine) {
      host.engine->Stop();
    }
    if (host.platform) {
      host.platform->Stop();
    }
    host.engine.reset();
    if (auto gfx = host.gfx_weak.lock()) {
      gfx->Stop();
    }
    // Nobody may hold the Graphics instance past this point.
    if (backend_loaded) {
      loader.UnloadBackend();
    }
    host.platform.reset();
  }

} // namespace

auto RegisterRequiredModule(
  AsyncEngine& engine, std::unique_ptr<engine::EngineModule> module) -> void
{
  CHECK_NOTNULL_F(module.get());
  const std::string name(module->GetName());
  if (!engine.RegisterModule(std::move(module))) {
    throw std::runtime_error("Module registration failed: " + name);
  }
}

auto RunHost(HostConfig config, const RegisterModules& register_modules) -> int
{
  Resolve(config);
  const auto& application = config.engine.application.name;

  HostContext host {};
  host.headless = config.platform.headless;
  host.window = config.window;

  // The host is a library: the loader's first call does not come from the
  // executable, so it uses the relaxed singleton.
  const auto& loader = GraphicsBackendLoader::GetInstanceRelaxed();
  bool backend_loaded = false;
  int exit_code = EXIT_FAILURE;
  try {
    host.platform = std::make_shared<Platform>(config.platform);
    host.gfx_weak = loader.LoadBackend(
      config.backend, config.engine.graphics, config.engine.path_finder_config);
    backend_loaded = !host.gfx_weak.expired();
    if (!backend_loaded) {
      throw std::runtime_error("Failed to load the graphics backend");
    }
    host.gfx_weak.lock()->CreateCommandQueues(host.queue_strategy);

    host.engine = std::make_shared<AsyncEngine>(host.platform, host.gfx_weak,
      config.engine, std::move(config.startup_cvars));

    exit_code = co::Run(host, AsyncMain(host, register_modules, application));
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "{}: {}", application, ex.what());
    exit_code = EXIT_FAILURE;
  }

  TearDown(host, loader, backend_loaded);
  LOG_F(INFO, "{}: exit code {}", application, exit_code);
  return exit_code;
}

} // namespace oxygen::host
