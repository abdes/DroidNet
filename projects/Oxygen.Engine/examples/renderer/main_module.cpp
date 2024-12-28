//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "main_module.h"

#include <random>

#include "oxygen/base/logging.h"
#include "oxygen/core/engine.h"
#include "oxygen/input/action_triggers.h"
#include "oxygen/input/types.h"
#include "oxygen/platform/platform.h"
#include "Oxygen/Renderers/Common/Renderer.h"
#include "Oxygen/Renderers/Direct3d12/WindowSurface.h"
#include "Oxygen/Renderers/Loader/RendererLoader.h"

using oxygen::input::Action;
using oxygen::input::ActionTriggerPressed;
using oxygen::input::ActionTriggerTap;
using oxygen::input::ActionValueType;
using oxygen::input::InputActionMapping;
using oxygen::input::InputMappingContext;
using oxygen::input::InputSystem;
using oxygen::platform::InputSlots;
using oxygen::graphics::GetRenderer;

MainModule::MainModule(oxygen::PlatformPtr platform)
  : platform_(std::move(platform)) {
}

MainModule::~MainModule() = default;

void MainModule::Initialize() {
  // Create a window.
  const auto my_window = platform_->MakeWindow(
    "Oxygen Renderer Example",
    { .width = 1200, .height = 800 },
    {
        .hidden = false,
        .always_on_top = false,
        .full_screen = false,
        .maximized = false,
        .minimized = false,
        .resizable = true,
        .borderless = false,
    });

    renderer_ = GetRenderer();
    auto renderer = renderer_.lock();
    DCHECK_NOTNULL_F(renderer, "renderer not initialized");

    surface_ = renderer->CreateWindowSurface(my_window);
    DCHECK_F(surface_->IsValid());
    surface_->Initialize();
}

void MainModule::ProcessInput(const oxygen::platform::InputEvent& event) {
}

void MainModule::Update(const oxygen::Duration delta_time) {
}

void MainModule::FixedUpdate() {
}

void MainModule::Render() {
  // Create a random number core.
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> distribution(4, 8);

  const auto renderer = renderer_.lock();
  DCHECK_NOTNULL_F(renderer, "renderer destroyed before the module is shutdown");
  if (renderer) {
    DCHECK_F(surface_->IsValid());
    //// Get the command list from the renderer
    //auto command_list = renderer->GetCommandList();

    //// Set the render target (backbuffer)
    //auto backbuffer = renderer->GetBackBuffer(surface_id_);
    //command_list->OMSetRenderTargets(1, &backbuffer, FALSE, nullptr);

    //// Define the clear color (RGBA)
    //constexpr float clear_color[4] = { 0.0f, 0.2f, 0.4f, 1.0f }; // Example color: dark blue

    //// Clear the render target (backbuffer)
    //command_list->ClearRenderTargetView(backbuffer, clear_color, 0, nullptr);

    //// Execute the command list
    //renderer->ExecuteCommandList(command_list);

    renderer->Render(surface_->GetId());
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(distribution(gen)));
}

void MainModule::Shutdown() noexcept
{
  surface_.reset();
  renderer_.reset();
  platform_.reset();
}
