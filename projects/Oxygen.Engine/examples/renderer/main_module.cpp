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
#include "Oxygen/Renderers/Direct3d12/Surface.h"
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
using oxygen::renderer::d3d12::CreateWindowSurface;
using oxygen::renderer::d3d12::DestroyWindowSurface;
using oxygen::renderer::d3d12::GetSurface;

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

    auto surface = CreateWindowSurface(my_window);
    surface_id_ = surface.GetId();
    renderer->CreateSwapChain(surface_id_);
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
    renderer->Render(surface_id_);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(distribution(gen)));
}

void MainModule::Shutdown() noexcept
{
  if (auto surface = GetSurface(surface_id_); surface.IsValid())
  {
    surface.Release();
    DestroyWindowSurface(surface_id_);
  }
  surface_id_.Invalidate();

  renderer_.reset();
  platform_.reset();
}
