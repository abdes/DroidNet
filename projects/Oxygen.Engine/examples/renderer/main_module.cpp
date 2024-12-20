//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "main_module.h"

#include <random>

#include "oxygen/core/engine.h"
#include "oxygen/input/action_triggers.h"
#include "oxygen/input/types.h"
#include "oxygen/platform/platform.h"
#include "oxygen/renderer-loader/renderer_loader.h"

using oxygen::input::Action;
using oxygen::input::ActionTriggerPressed;
using oxygen::input::ActionTriggerTap;
using oxygen::input::ActionValueType;
using oxygen::input::InputActionMapping;
using oxygen::input::InputMappingContext;
using oxygen::input::InputSystem;
using oxygen::platform::InputSlots;

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

    constexpr oxygen::RendererProperties renderer_props{
#ifdef _DEBUG
        .enable_debug = true,
#endif
        .enable_validation = false,
    };

    renderer_ = CreateRenderer(oxygen::graphics::GraphicsBackendType::kDirect3D12, platform_, renderer_props);

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
  static std::uniform_int_distribution<> distribution(20, 100);

  renderer_->Render();

  std::this_thread::sleep_for(std::chrono::milliseconds(distribution(gen)));
}

void MainModule::Shutdown() noexcept
{
  renderer_->Shutdown();

  renderer_.reset();
  platform_.reset();
}
