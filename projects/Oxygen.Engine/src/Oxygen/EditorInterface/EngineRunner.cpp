//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <memory>
#include <stdexcept>
#include <thread>

#if defined(_WIN32)
#  include <Windows.h>
#else
#  include <unistd.h>
#endif

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/EditorInterface/Api.h>
#include <Oxygen/EditorInterface/EngineContext.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/EventLoop.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Renderer/Renderer.h>

namespace {

//! Event loop tick: drives the engine's asio context (if supplied) and
//! applies frame pacing + cooperative sleep when idle to avoid busy spinning.
auto EventLoopRun(const oxygen::engine::interop::EngineContext& ctx) -> void
{
  while (ctx.running.load(std::memory_order_relaxed)) {

    ctx.platform->Async().PollOne();
    ctx.platform->Events().PollOne();

    if (!ctx.running.load(std::memory_order_relaxed)) {
      // Additional gentle backoff before running starts.
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
}
} // namespace

template <>
struct oxygen::co::EventLoopTraits<oxygen::engine::interop::EngineContext> {
  static auto Run(oxygen::engine::interop::EngineContext& ctx) -> void
  {
    EventLoopRun(ctx);
  }
  static auto Stop(oxygen::engine::interop::EngineContext& ctx) -> void
  {
    ctx.running.store(false, std::memory_order_relaxed);
  }
  static auto IsRunning(const oxygen::engine::interop::EngineContext& ctx)
    -> bool
  {
    return ctx.running.load(std::memory_order_relaxed);
  }
  static auto EventLoopId(oxygen::engine::interop::EngineContext& ctx)
    -> EventLoopID
  {
    return EventLoopID(&ctx);
  }
};

namespace {

auto RegisterEngineModules(oxygen::engine::interop::EngineContext& ctx) -> void
{
  using namespace oxygen;

  // Register engine modules
  LOG_F(INFO, "Registering engine modules...");

  // Helper lambda to register modules with error checking
  auto register_module
    = [&](std::unique_ptr<oxygen::engine::EngineModule> module) {
        const bool registered = ctx.engine->RegisterModule(std::move(module));
        if (!registered) {
          LOG_F(ERROR, "Failed to register module");
          throw std::runtime_error("Module registration failed");
        }
      };

  // Register built-in engine modules (one-time)
  {
    auto input_sys = std::make_unique<oxygen::engine::InputSystem>(
      ctx.platform->Input().ForRead());
    ctx.input_system = observer_ptr { input_sys.get() };
    register_module(std::move(input_sys));

#if 0
    oxygen::RendererConfig renderer_config {
      .upload_queue_key = ctx.queue_strategy.KeyFor(QueueRole::kTransfer).get(),
    };
    // Create the Renderer - we need unique_ptr for registration and
    // observer_ptr for MainModule
    auto renderer_unique
      = std::make_unique<engine::Renderer>(ctx.gfx_weak, renderer_config);

    // Graphics main module (replaces RenderController/RenderThread pattern)
    ctx.renderer = observer_ptr { renderer_unique.get() };
    register_module(std::make_unique<oxygen::examples::async::MainModule>(ctx));

    // Register as module
    register_module(std::move(renderer_unique));

    // ImGui module (last): only when not headless and when a graphics backend
    // exists
    if (!ctx.headless) {
      auto imgui_backend = std::make_unique<
        oxygen::graphics::d3d12::D3D12ImGuiGraphicsBackend>();
      auto imgui_module = std::make_unique<oxygen::imgui::ImGuiModule>(
        ctx.platform, std::move(imgui_backend));
      register_module(std::move(imgui_module));
    }
#endif
  }
}

auto AsyncMain(oxygen::engine::interop::EngineContext& ctx)
  -> oxygen::co::Co<int>
{
  using namespace oxygen;

  // Structured concurrency scope.
  OXCO_WITH_NURSERY(n)
  {
    ctx.running.store(true, std::memory_order_relaxed);

    // PLatform started and running is a prerequisite for many of the modules
    // and the other subsystems.
    co_await n.Start(&Platform::ActivateAsync, std::ref(*ctx.platform));
    ctx.platform->Run();

    DCHECK_F(!ctx.gfx_weak.expired());
    auto gfx = ctx.gfx_weak.lock();
    co_await n.Start(&Graphics::ActivateAsync, std::ref(*gfx));
    gfx->Run();

    co_await n.Start(&AsyncEngine::ActivateAsync, std::ref(*ctx.engine));
    ctx.engine->Run();

    // Everything is started, now register modules
    RegisterEngineModules(ctx);

    co_await ctx.engine->Completed();

    co_return co::kCancel;
  };

  co_return EXIT_SUCCESS;
}
} // namespace

namespace oxygen::engine::interop {

auto CreateEngine(const EngineConfig& config) -> std::unique_ptr<EngineContext>
{
  // Pre-allocate static error messages when we are handling critical failures
  constexpr std::string_view kUnhandledException
    = "Error: Out of memory or other critical failure when logging unhandled "
      "exception\n";
  constexpr std::string_view kUnknownUnhandledException
    = "Error: Out of memory or other critical failure when logging unhandled "
      "exception of unknown type\n";

  // Low-level error reporting function that won't allocate memory
  auto report_error = [](std::string_view message) noexcept {
#if defined(_WIN32)
    HANDLE stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
    DWORD bytes_written { 0UL };
    WriteFile(stderr_handle, message.data(), static_cast<DWORD>(message.size()),
      &bytes_written, nullptr);
#else
    write(STDERR_FILENO, message.data(), message.size());
#endif
  };

  try {
    auto ctx = std::make_unique<EngineContext>();

    // Create the platform
    ctx->platform = std::make_shared<Platform>(PlatformConfig {
      .headless = false,
      .thread_pool_size = (std::min)(4u, std::thread::hardware_concurrency()),
    });

    // Load the graphics backend
    const GraphicsConfig gfx_config {
      .enable_debug = true,
      .enable_validation = false,
      .preferred_card_name = std::nullopt,
      .headless = false,
      .enable_vsync = false,
      .extra = {},
    };
    const auto& loader = GraphicsBackendLoader::GetInstanceRelaxed();
    ctx->gfx_weak
      = loader.LoadBackend(graphics::BackendType::kDirect3D12, gfx_config);
    CHECK_F(
      !ctx->gfx_weak.expired()); // Expect a valid graphics backend, or abort
    ctx->gfx_weak.lock()->CreateCommandQueues(ctx->queue_strategy);

    // Create the async engine
    ctx->engine
      = std::make_shared<AsyncEngine>(ctx->platform, ctx->gfx_weak, config);

    return ctx;
  } catch (const std::exception& ex) {
    try {
      LOG_F(ERROR, "Unhandled exception: {}", ex.what());
    } catch (...) {
      report_error(kUnhandledException);
    }
  } catch (...) {
    // Catch any other exceptions
    try {
      LOG_F(ERROR, "Unhandled exception of unknown type");
    } catch (...) {
      // Cannot do anything if ex.what() throws
      report_error(kUnknownUnhandledException);
    }
  }

  return {};
}

auto RunEngine(std::shared_ptr<EngineContext> ctx) -> void
{
  const auto rc = co::Run(*ctx, AsyncMain(*ctx));

  try {
    ctx->platform->Stop();
    ctx->engine.reset();
    if (!ctx->gfx_weak.expired()) {
      auto gfx = ctx->gfx_weak.lock();
      gfx->Stop();
      gfx.reset();
    }
    // Make sure no one holds a reference to the Graphics instance at this
    // point.
    const auto& loader = GraphicsBackendLoader::GetInstanceRelaxed();
    loader.UnloadBackend();
    ctx->platform.reset();
    ctx->running.store(false, std::memory_order_relaxed);
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "Unhandled exception during shutdown: {}", ex.what());
  } catch (...) {
    // Catch any other exceptions
    LOG_F(ERROR, "Unhandled exception of unknown type during shutdown");
  }
  LOG_F(INFO, "engine exit code: {}", rc);
  loguru::flush();
  loguru::shutdown();
}

auto StopEngine(std::shared_ptr<EngineContext> ctx) -> void
{
  ctx->engine->Stop();
}

 auto SetTargetFps(std::shared_ptr<EngineContext> ctx, uint32_t fps) -> void
 {
   if (!ctx || !ctx->engine) {
     return; // nothing to do
   }

   try {
     ctx->engine->SetTargetFps(fps);
   }
   catch (...) {
     // keep interop boundary robust — swallow internal errors
   }
 }

 auto GetEngineConfig(std::shared_ptr<EngineContext> ctx) -> EngineConfig
 {
   if (!ctx || !ctx->engine) {
     return EngineConfig{};
   }
   // GetEngineConfig returns a const reference; copy it for safe return
   return ctx->engine->GetEngineConfig();
 }

} // namespace oxygen::engine::interop
