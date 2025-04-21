//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <nlohmann/json.hpp>
#include <wrl/client.h>

#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/Graphics/Direct3D12/CommandQueue.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DeviceManager.h>
#include <Oxygen/Graphics/Direct3D12/Forward.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/ImGui/ImGuiModule.h>
#include <Oxygen/Graphics/Direct3D12/Renderer.h>
#include <Oxygen/Graphics/Direct3D12/Shaders/EngineShaders.h>

//===----------------------------------------------------------------------===//
// Internal implementation of the graphics backend module API.

namespace {

//! Utility function to map QueueRole to D3D12_COMMAND_LIST_TYPE
auto ToD3D12CommandListType(oxygen::graphics::QueueRole role)
{
    using oxygen::graphics::QueueRole;
    switch (role) {
    case QueueRole::kGraphics:
    case QueueRole::kPresent: // D3D12 uses graphics queue for present
        return D3D12_COMMAND_LIST_TYPE_DIRECT;
    case QueueRole::kCompute:
        return D3D12_COMMAND_LIST_TYPE_COMPUTE;
    case QueueRole::kTransfer:
        return D3D12_COMMAND_LIST_TYPE_COPY;
    default:
        throw std::invalid_argument("Unknown or unsupported QueueRole for D3D12");
    }
}

auto GetBackendInternal() -> std::shared_ptr<oxygen::graphics::d3d12::Graphics>&
{
    static std::shared_ptr<oxygen::graphics::d3d12::Graphics> graphics;
    return graphics;
}

auto CreateBackend(const oxygen::SerializedBackendConfig& config) -> void*
{
    auto& backend = GetBackendInternal();
    if (!backend) {
        backend = std::make_shared<oxygen::graphics::d3d12::Graphics>(config);
    }
    return backend.get();
}

void DestroyBackend()
{
    LOG_SCOPE_F(INFO, "DestroyBackend");
    auto& renderer = GetBackendInternal();
    renderer.reset();
}

} // namespace

//===----------------------------------------------------------------------===//
// Implementation of the helper functions to access the important graphics
// backend objects.

namespace oxygen::graphics::d3d12::detail {

auto Graphics() -> d3d12::Graphics&
{
    CHECK_NOTNULL_F(GetBackendInternal());
    return *GetBackendInternal();
}

auto GetFactory() -> FactoryType*
{
    CHECK_NOTNULL_F(GetBackendInternal());
    return GetBackendInternal()->GetFactory();
}

auto GetMainDevice() -> DeviceType*
{
    CHECK_NOTNULL_F(GetBackendInternal());
    return GetBackendInternal()->GetMainDevice();
}

auto GetRenderer() -> Renderer&
{
    CHECK_NOTNULL_F(GetBackendInternal());
    auto* const renderer = static_cast<Renderer*>(GetBackendInternal()->GetRenderer());
    CHECK_NOTNULL_F(renderer);
    return *renderer;
}

auto GetAllocator() -> D3D12MA::Allocator&
{
    auto* allocator = Graphics().GetAllocator();
    DCHECK_NOTNULL_F(allocator);
    return *allocator;
}

} // namespace oxygen::graphics::d3d12::detail

//===----------------------------------------------------------------------===//
// Public implementation of the graphics backend API.

extern "C" __declspec(dllexport) auto GetGraphicsModuleApi() -> void*
{
    static oxygen::graphics::GraphicsModuleApi render_module;
    render_module.CreateBackend = CreateBackend;
    render_module.DestroyBackend = DestroyBackend;
    return &render_module;
}

//===----------------------------------------------------------------------===//
// The Graphics class methods

using oxygen::graphics::d3d12::DeviceType;
using oxygen::graphics::d3d12::FactoryType;
using oxygen::graphics::d3d12::Graphics;

auto Graphics::GetFactory() const -> FactoryType*
{
    auto* factory = GetComponent<DeviceManager>().Factory();
    CHECK_NOTNULL_F(factory, "graphics backend not properly initialized");
    return factory;
}

auto Graphics::GetMainDevice() const -> DeviceType*
{
    auto* device = GetComponent<DeviceManager>().Device();
    CHECK_NOTNULL_F(device, "graphics backend not properly initialized");
    return device;
}

auto oxygen::graphics::d3d12::Graphics::GetAllocator() const -> D3D12MA::Allocator*
{
    auto* allocator = GetComponent<DeviceManager>().Allocator();
    CHECK_NOTNULL_F(allocator, "graphics backend not properly initialized");
    return allocator;
}

auto Graphics::GetShader(const std::string_view unique_id) const -> std::shared_ptr<graphics::IShaderByteCode>
{
    return GetComponent<EngineShaders>().GetShader(unique_id);
}

Graphics::Graphics(const SerializedBackendConfig& config)
    : Base("D3D12 Backend")
{
    LOG_SCOPE_FUNCTION(INFO);

    // Parse JSON configuration
    nlohmann::json jsonConfig = nlohmann::json::parse(config.json_data, config.json_data + config.size);

    DeviceManagerDesc desc {};
    if (auto& enable_debug = jsonConfig["enable_debug"]) {
        desc.enable_debug = enable_debug.get<bool>();
    }

    AddComponent<DeviceManager>(desc);
    AddComponent<EngineShaders>();
}

auto Graphics::CreateRenderer() -> std::unique_ptr<graphics::Renderer>
{
    return std::make_unique<Renderer>();
}

auto Graphics::CreateCommandQueue(graphics::QueueRole role, [[maybe_unused]] graphics::QueueAllocationPreference allocation_preference) -> std::shared_ptr<graphics::CommandQueue>
{
    auto queue = std::make_shared<CommandQueue>(role);
    CHECK_NOTNULL_F(queue, "Failed to create command queue");
    return queue;
}

auto Graphics::CreateImGuiModule(EngineWeakPtr engine, platform::WindowIdType window_id) const -> std::unique_ptr<imgui::ImguiModule>
{
    return std::make_unique<ImGuiModule>(std::move(engine), window_id);
}

auto Graphics::CreateSurface(const platform::Window& window) const -> std::unique_ptr<graphics::Surface>
{
    DCHECK_F(window.Valid());

    // TODO: implement CreateSurface
    throw std::runtime_error("CreateSurface not implemented in D3D12 backend");
}
