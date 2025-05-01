//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <type_traits>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Renderer.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/Window.h>

#include <MainModule.h>

using oxygen::examples::MainModule;
using WindowProps = oxygen::platform::window::Properties;

MainModule::MainModule(
    std::shared_ptr<oxygen::Platform> platform,
    std::weak_ptr<Graphics> gfx_weak)
    : platform_(std::move(platform))
    , gfx_weak_(std::move(gfx_weak))
{
    DCHECK_NOTNULL_F(platform_);
    DCHECK_F(!gfx_weak_.expired());
}

MainModule::~MainModule()
{
    LOG_SCOPE_F(INFO, "Destroying MainModule");

    renderer_.reset();
    surface_.reset();
    platform_.reset();
}

void MainModule::Run()
{
    DCHECK_NOTNULL_F(nursery_);

    SetupCommandQueues();
    SetupMainWindow();
    SetupSurface();
    SetupRenderer();

    nursery_->Start([this]() -> oxygen::co::Co<> {
        while (!window_weak_.expired() && !gfx_weak_.expired()) {
            auto gfx = gfx_weak_.lock();
            co_await gfx->RenderStart();
            // Submit the render task to the renderer
            renderer_->Submit([this]() -> oxygen::co::Co<> {
                co_await RenderScene();
            });
        }
    });
}

void MainModule::SetupCommandQueues()
{
    CHECK_F(!gfx_weak_.expired());

    auto gfx = gfx_weak_.lock();
    gfx->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
}

void MainModule::SetupSurface()
{
    CHECK_F(!gfx_weak_.expired());
    CHECK_F(!window_weak_.expired());

    auto gfx = gfx_weak_.lock();

    oxygen::graphics::SingleQueueStrategy queues;
    surface_ = gfx->CreateSurface(window_weak_, gfx->GetCommandQueue(queues.GraphicsQueueName()));
    surface_->SetName("Main Window Surface");
    LOG_F(INFO, "Surface ({}) created for main widnow ({})", surface_->GetName(), window_weak_.lock()->Id());
}

void oxygen::examples::MainModule::SetupMainWindow()
{
    // Setup the main window
    WindowProps props("Oxygen Window Playground");
    props.extent = { .width = 800, .height = 600 };
    props.flags = {
        .hidden = false,
        .always_on_top = false,
        .full_screen = false,
        .maximized = false,
        .minimized = false,
        .resizable = true,
        .borderless = false
    };
    window_weak_ = std::move(platform_->Windows().MakeWindow(props));
    if (const auto window = window_weak_.lock()) {
        LOG_F(INFO, "Main window {} is created", window->Id());
    }

    // Immediately accept the close request for the main window
    nursery_->Start([this]() -> oxygen::co::Co<> {
        while (!window_weak_.expired()) {
            auto window = window_weak_.lock();
            co_await window->CloseRequested();
            // Stop the nursery
            if (nursery_) {
                nursery_->Cancel();
            }
            window_weak_.lock()->VoteToClose();
        }
    });

    // Add a termination signal handler
    nursery_->Start([this]() -> oxygen::co::Co<> {
        co_await platform_->Async().OnTerminate();
        LOG_F(INFO, "terminating...");
        // Terminate the application by requesting the main window to close
        window_weak_.lock()->RequestClose();
    });
}

void MainModule::SetupRenderer()
{
    CHECK_F(!gfx_weak_.expired());

    auto gfx = gfx_weak_.lock();
    renderer_ = gfx->CreateRenderer("Main Window Renderer", surface_);
    CHECK_NOTNULL_F(renderer_, "Failed to create renderer for main window");
}

auto MainModule::RenderScene() -> oxygen::co::Co<>
{
    if (gfx_weak_.expired()) {
        co_return;
    }

    DLOG_F(1, "Rendering scene");

    auto gfx = gfx_weak_.lock();
    auto recorder = gfx->AcquireCommandRecorder(
        oxygen::graphics::SingleQueueStrategy().GraphicsQueueName(),
        "Main Window Command List");
    co_return;
}
