//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <nlohmann/json.hpp>

#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Direct3D12/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/CommandList.h>
#include <Oxygen/Graphics/Direct3D12/CommandQueue.h>
#include <Oxygen/Graphics/Direct3D12/Detail/WindowSurface.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DeviceManager.h>
#include <Oxygen/Graphics/Direct3D12/Framebuffer.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/RenderController.h>
#include <Oxygen/Graphics/Direct3D12/Shaders/EngineShaders.h>
#include <Oxygen/Graphics/Direct3D12/Texture.h>

using oxygen::graphics::FramebufferDesc;

//===----------------------------------------------------------------------===//
// Internal implementation of the graphics backend module API.

namespace {

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

using oxygen::graphics::d3d12::Graphics;

auto Graphics::GetFactory() const -> dx::IFactory*
{
    auto* factory = GetComponent<DeviceManager>().Factory();
    CHECK_NOTNULL_F(factory, "graphics backend not properly initialized");
    return factory;
}

auto Graphics::GetCurrentDevice() const -> dx::IDevice*
{
    auto* device = GetComponent<DeviceManager>().Device();
    CHECK_NOTNULL_F(device, "graphics backend not properly initialized");
    return device;
}

auto Graphics::GetAllocator() const -> D3D12MA::Allocator*
{
    auto* allocator = GetComponent<DeviceManager>().Allocator();
    CHECK_NOTNULL_F(allocator, "graphics backend not properly initialized");
    return allocator;
}

Graphics::Graphics(const SerializedBackendConfig& config)
    : Base("D3D12 Backend")
{
    LOG_SCOPE_FUNCTION(INFO);

    // Parse JSON configuration
    nlohmann::json jsonConfig = nlohmann::json::parse(config.json_data, config.json_data + config.size);

    DeviceManagerDesc desc {};
    if (jsonConfig.contains("enable_debug")) {
        desc.enable_debug = jsonConfig["enable_debug"].get<bool>();
    }
    AddComponent<DeviceManager>(desc);
    AddComponent<EngineShaders>();
}

auto Graphics::CreateCommandQueue(
    std::string_view name,
    QueueRole role,
    [[maybe_unused]] QueueAllocationPreference allocation_preference)
    -> std::shared_ptr<graphics::CommandQueue>
{
    return std::make_shared<CommandQueue>(name, role, this);
}

auto Graphics::CreateRendererImpl(
    const std::string_view name,
    std::weak_ptr<Surface> surface,
    uint32_t frames_in_flight)
    -> std::unique_ptr<graphics::RenderController>
{
    return std::make_unique<RenderController>(
        name, weak_from_this(), std::move(surface), frames_in_flight);
}

auto Graphics::CreateCommandListImpl(QueueRole role, std::string_view command_list_name)
    -> std::unique_ptr<graphics::CommandList>
{
    return std::make_unique<CommandList>(command_list_name, role, this);
}

auto Graphics::GetFormatPlaneCount(DXGI_FORMAT format) const -> uint8_t
{
    uint8_t& plane_count = dxgi_format_plane_count_cache_[format];
    if (plane_count == 0) {
        D3D12_FEATURE_DATA_FORMAT_INFO format_info = { format, 1 };
        if (FAILED(GetCurrentDevice()->CheckFeatureSupport(
                D3D12_FEATURE_FORMAT_INFO, &format_info, sizeof(format_info)))) {
            // Format is not supported - store a special value in the cache to avoid querying later
            plane_count = 255;
        } else {
            // Format supported - store the plane count in the cache
            plane_count = format_info.PlaneCount;
        }
    }

    if (plane_count == 255) {
        return 0;
    }

    return plane_count;
}

// auto Graphics::CreateImGuiModule(EngineWeakPtr engine, platform::WindowIdType window_id) const -> std::unique_ptr<imgui::ImguiModule>
// {
//     return std::make_unique<ImGuiModule>(std::move(engine), window_id);
// }

auto Graphics::CreateSurface(
    std::weak_ptr<platform::Window> window_weak,
    const std::shared_ptr<graphics::CommandQueue> command_queue) const
    -> std::shared_ptr<Surface>
{
    DCHECK_F(!window_weak.expired());
    DCHECK_NOTNULL_F(command_queue);
    DCHECK_EQ_F(command_queue->GetTypeId(), graphics::d3d12::CommandQueue::ClassTypeId(), "Invalid command queue class");

    // NOLINTNEXTLINE(*-pro-type-static-cast-downcast)
    const auto* queue = static_cast<CommandQueue*>(command_queue.get());
    const auto surface = std::make_shared<detail::WindowSurface>(window_weak, queue->GetCommandQueue());
    CHECK_NOTNULL_F(surface, "Failed to create surface");
    return std::static_pointer_cast<Surface>(surface);
}

auto Graphics::GetShader(const std::string_view unique_id) const -> std::shared_ptr<IShaderByteCode>
{
    return GetComponent<EngineShaders>().GetShader(unique_id);
}

auto Graphics::CreateFramebuffer(const FramebufferDesc& desc, const graphics::RenderController& renderer)
    -> std::shared_ptr<graphics::Framebuffer>
{
    return std::make_shared<Framebuffer>(
        desc,
        // NOLINTNEXTLINE(*-pro-type-static-cast-downcast)
        static_cast<const RenderController*>(&renderer));
}

auto Graphics::CreateTexture(const TextureDesc& desc) const
    -> std::shared_ptr<graphics::Texture>
{
    return std::make_shared<Texture>(desc, this);
}

auto Graphics::CreateTextureFromNativeObject(
    const TextureDesc& desc,
    const NativeObject& native) const
    -> std::shared_ptr<graphics::Texture>
{
    return std::make_shared<Texture>(desc, native, this);
}

auto Graphics::CreateBuffer(const BufferDesc& desc) const -> std::shared_ptr<graphics::Buffer>
{
    return std::make_shared<Buffer>(desc, this);
}
