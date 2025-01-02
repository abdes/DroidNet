//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "MainModule.h"

#include <random>

#include <glm/glm.hpp>
#include <imgui.h>

#include "Oxygen/Core/Engine.h"
#include "Oxygen/ImGui/ImGuiRenderInterface.h"
#include "Oxygen/Renderers/Common/CommandList.h"
#include "Oxygen/Renderers/Common/CommandRecorder.h"
#include "Oxygen/Renderers/Common/RenderTarget.h"
#include "Oxygen/Renderers/Common/Renderer.h"
#include "Oxygen/Renderers/Direct3d12/WindowSurface.h"
#include "Oxygen/Base/Logging.h"
#include "Oxygen/Input/action_triggers.h"
#include "Oxygen/Input/Types.h"
#include "Oxygen/Platform/Platform.h"

using oxygen::Engine;
using oxygen::input::Action;
using oxygen::input::ActionTriggerPressed;
using oxygen::input::ActionTriggerTap;
using oxygen::input::ActionValueType;
using oxygen::input::InputActionMapping;
using oxygen::input::InputMappingContext;
using oxygen::input::InputSystem;
using oxygen::platform::InputSlots;
using oxygen::renderer::CommandListPtr;
using oxygen::renderer::CommandLists;
using oxygen::renderer::RenderTarget;

void MainModule::OnInitialize(const oxygen::Renderer* renderer)
{
  DCHECK_NOTNULL_F(renderer);
  DCHECK_F(!my_window_.expired());

  surface_ = renderer->CreateWindowSurface(my_window_);
  DCHECK_F(surface_->IsValid());
  surface_->Initialize();
}

void MainModule::ProcessInput(const oxygen::platform::InputEvent& event)
{
}

void MainModule::Update(const oxygen::Duration delta_time)
{
}

void MainModule::FixedUpdate()
{
}

void MainModule::Render(const oxygen::Renderer* renderer)
{
  DCHECK_NOTNULL_F(renderer);

  // Create a random number core.
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> distribution(4, 8);

  DCHECK_F(surface_->IsValid());
  //// Get the command list from the renderer
  // auto command_list = renderer->GetCommandList();

  //// Set the render target (backbuffer)
  // auto backbuffer = renderer->GetBackBuffer(surface_id_);
  // command_list->OMSetRenderTargets(1, &backbuffer, FALSE, nullptr);

  //// Define the clear color (RGBA)
  // constexpr float clear_color[4] = { 0.0f, 0.2f, 0.4f, 1.0f }; // Example color: dark blue

  //// Clear the render target (backbuffer)
  // command_list->ClearRenderTargetView(backbuffer, clear_color, 0, nullptr);

  //// Execute the command list
  // renderer->ExecuteCommandList(command_list);

  renderer->Render(surface_->GetId(),
    [this, &renderer](const RenderTarget& render_target) {
      return RenderGame(renderer, render_target);
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(distribution(gen)));
}

void MainModule::OnShutdown() noexcept
{
  surface_.reset();
  platform_.reset();
}

auto MainModule::RenderGame(
  const oxygen::Renderer* renderer,
  const RenderTarget& render_target) const -> CommandLists
{
  DCHECK_NOTNULL_F(renderer);

  const auto command_recorder = renderer->GetCommandRecorder();
  command_recorder->Begin();
  command_recorder->SetRenderTarget(&render_target);
  // Record commands

  constexpr glm::vec4 clear_color = { 0.4f, 0.4f, .8f, 1.0f }; // Violet color
  command_recorder->Clear(oxygen::renderer::kClearFlagsColor, 1, nullptr, &clear_color, 0.0f, 0);

  const auto& [TopLeftX, TopLeftY, Width, Height, MinDepth, MaxDepth] = render_target.GetViewPort();
  command_recorder->SetViewport(TopLeftX, Width, TopLeftY, Height, MinDepth, MaxDepth);

  const auto& [left, top, right, bottom] = render_target.GetScissors();
  command_recorder->SetScissors(left, top, right, bottom);

  // command_recorder->Draw();

  //...
  CommandLists command_lists {};
  auto my_command_list = command_recorder->End();
  command_lists.emplace_back(std::move(my_command_list));
  // Initialize the ImGui layer
  if (!GetEngine().HasImGui()) {
    return command_lists;
  }

  auto imgui = GetEngine().GetImGuiRenderInterface();
  ImGui::SetCurrentContext(imgui.GetContext());
  imgui.NewFrame(renderer);
  ImGui::ShowDemoWindow();
  auto imgui_command_list = imgui.Render(renderer);
  command_lists.emplace_back(std::move(imgui_command_list));

  return command_lists;
}
