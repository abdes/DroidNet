//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <type_traits>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Renderer.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/Window.h>

#include <MainModule.h>

using oxygen::examples::MainModule;
using WindowProps = oxygen::platform::window::Properties;
using WindowEvent = oxygen::platform::window::Event;
using oxygen::graphics::Scissors;
using oxygen::graphics::ViewPort;

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

    // Flush command queues used for the surface
    if (!gfx_weak_.expired()) {
        auto gfx = gfx_weak_.lock();
        oxygen::graphics::SingleQueueStrategy queues;
        gfx->GetCommandQueue(queues.GraphicsQueueName())->Flush();
    }

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
            co_await gfx->OnRenderStart();
            // Submit the render task to the renderer
            renderer_->Submit([this]() -> oxygen::co::Co<> {
                co_await RenderScene();
            });
        }
    });
}

void MainModule::SetupCommandQueues() const
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

void MainModule::SetupMainWindow()
{
    // Setup the main window
    WindowProps props("Oxygen Graphics Example");
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

    nursery_->Start([this]() -> oxygen::co::Co<> {
        while (!window_weak_.expired()) {
            auto window = window_weak_.lock();
            if (const auto [from, to] = co_await window->Events().UntilChanged();
                to == WindowEvent::kResized) {
                LOG_F(INFO, "Main window was resized");
                surface_->ShouldResize(true);
            } else {
                if (to == WindowEvent::kExposed) {
                    LOG_F(INFO, "My window is exposed");
                }
            }
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
    renderer_ = gfx->CreateRenderer("Main Window Renderer", surface_, kFrameBufferCount - 1);
    CHECK_NOTNULL_F(renderer_, "Failed to create renderer for main window");
}

// FIXME: replace the reference to renderer with a shared_ptr or std::ref
auto MainModule::RenderScene() -> oxygen::co::Co<>
{
    if (gfx_weak_.expired()) {
        co_return;
    }

    DLOG_F(1, "Rendering scene");

    auto gfx = gfx_weak_.lock();
    auto recorder = renderer_->AcquireCommandRecorder(
        oxygen::graphics::SingleQueueStrategy().GraphicsQueueName(),
        "Main Window Command List");

    recorder->SetRenderTarget(surface_->GetRenderTarget());

    ViewPort viewport {
        .width = static_cast<float>(surface_->Width()),
        .height = static_cast<float>(surface_->Height()),
    };

    Scissors scissors {
        .right = static_cast<int32_t>(surface_->Width()),
        .bottom = static_cast<int32_t>(surface_->Height())
    };

    recorder->SetViewport(viewport);
    recorder->SetScissors(scissors);

    constexpr glm::vec4 clear_color = { 0.4F, 0.4F, .8f, 1.0F }; // Violet color
    recorder->Clear(oxygen::graphics::kClearFlagsColor, 1, nullptr, &clear_color, 0.0F, 0);

    co_return;
}
