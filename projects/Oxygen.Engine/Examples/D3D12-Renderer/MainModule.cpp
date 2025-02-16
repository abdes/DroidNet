//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "MainModule.h"

#include <random>

#include <glm/glm.hpp>
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Engine.h>
#include <Oxygen/Graphics/Common/Buffer.h> // Must include for the auto deletion of Buffer shared_ptr
#include <Oxygen/Graphics/Common/CommandList.h> // needed for CommandListPtr
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/RenderTarget.h>
#include <Oxygen/Graphics/Common/Renderer.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Direct3D12/DeferredObjectRelease.h> // Needed
#include <Oxygen/Graphics/Direct3D12/WindowSurface.h>
#include <Oxygen/ImGui/ImGuiRenderInterface.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/Types.h>

using oxygen::Engine;
using oxygen::graphics::CommandListPtr;
using oxygen::graphics::CommandLists;
using oxygen::graphics::RenderTarget;
using oxygen::graphics::ShaderType;
using oxygen::graphics::d3d12::detail::DeferredObjectRelease;
using oxygen::input::Action;
using oxygen::input::ActionTriggerPressed;
using oxygen::input::ActionTriggerTap;
using oxygen::input::ActionValueType;
using oxygen::input::InputActionMapping;
using oxygen::input::InputMappingContext;
using oxygen::input::InputSystem;
using oxygen::platform::InputSlots;

void MainModule::OnInitialize(const oxygen::Graphics* gfx)
{
    DCHECK_NOTNULL_F(gfx);
    DCHECK_F(!gfx->IsWithoutRenderer());
    DCHECK_F(!my_window_.expired());

    const auto renderer = gfx->GetRenderer();
    DCHECK_NOTNULL_F(renderer);
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

void MainModule::Render(const oxygen::Graphics* gfx)
{
    DCHECK_NOTNULL_F(gfx);
    const auto renderer = gfx->GetRenderer();
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
        [this, &gfx](const RenderTarget& render_target) {
            return RenderGame(gfx, render_target);
        });
    std::this_thread::sleep_for(std::chrono::milliseconds(distribution(gen)));
}

void MainModule::OnShutdown() noexcept
{
    surface_.reset();
    platform_.reset();
}

auto MainModule::RenderGame(
    const oxygen::Graphics* gfx, const RenderTarget& render_target) const
    -> CommandLists
{
    DCHECK_NOTNULL_F(gfx);
    const auto renderer = gfx->GetRenderer();
    DCHECK_NOTNULL_F(renderer);

    // Pipeline state and Root Signature

    const auto vertex_shader = renderer->GetEngineShader(
        MakeShaderIdentifier(
            ShaderType::kVertex,
            "FullScreenTriangle.hlsl"));

    const auto pixel_shader = renderer->GetEngineShader(
        MakeShaderIdentifier(
            ShaderType::kPixel,
            "FullScreenTriangle.hlsl"));

    //...

    // End Pipeline State and Root signature

    const auto command_recorder = renderer->GetCommandRecorder();
    command_recorder->Begin();
    command_recorder->SetRenderTarget(&render_target);
    // Record commands

    const auto& [TopLeftX, TopLeftY, Width, Height, MinDepth, MaxDepth] = render_target.GetViewPort();
    command_recorder->SetViewport(TopLeftX, Width, TopLeftY, Height, MinDepth, MaxDepth);

    const auto& [left, top, right, bottom] = render_target.GetScissors();
    command_recorder->SetScissors(left, top, right, bottom);

    // Set pipeline state
    command_recorder->SetPipelineState(vertex_shader, pixel_shader);

    constexpr glm::vec4 clear_color = { 0.4f, 0.4f, .8f, 1.0f }; // Violet color
    command_recorder->Clear(oxygen::graphics::kClearFlagsColor, 1, nullptr, &clear_color, 0.0f, 0);

    // Create vertex buffer
    struct Vertex {
        float position[3];
        float color[3];
    };
    Vertex vertices[] = {
        { { 0.0f, 0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
        { { 0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
        { { -0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f } }
    };
    auto vertex_buffer = renderer->CreateVertexBuffer(vertices, sizeof(vertices), sizeof(Vertex));

    //// Set vertex buffer
    // oxygen::graphics::BufferPtr vertex_buffers[] = { vertex_buffer };
    // uint32_t strides[] = { sizeof(Vertex) };
    // uint32_t offsets[] = { 0 };
    // command_recorder->SetVertexBuffers(1, vertex_buffers, strides, offsets);
    DeferredObjectRelease(vertex_buffer);

    command_recorder->Draw(3, 1, 0, 0);
    // command_recorder->Draw();

    //...
    CommandLists command_lists {};
    auto my_command_list = command_recorder->End();
    command_lists.emplace_back(std::move(my_command_list));
    // Initialize the ImGui layer
    if (!GetEngine().HasImGui()) {
        return command_lists;
    }

    const auto imgui = GetEngine().GetImGuiRenderInterface();
    ImGui::SetCurrentContext(imgui.GetContext());
    imgui.NewFrame(gfx);
    ImGui::ShowDemoWindow();
    auto imgui_command_list = imgui.Render(gfx);
    command_lists.emplace_back(std::move(imgui_command_list));

    return command_lists;
}
