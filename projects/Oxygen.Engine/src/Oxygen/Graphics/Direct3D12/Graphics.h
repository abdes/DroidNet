//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Allocator/D3D12MemAlloc.h>
#include <Oxygen/Graphics/Direct3D12/Forward.h>
#include <Oxygen/Graphics/Direct3D12/Renderer.h>

namespace oxygen::graphics::d3d12 {

namespace detail {
    class DescriptorHeaps;
} // namespace detail

// TODO: add a component to manage descriptor heaps (rtv, dsv, srv, uav)

class Graphics final : public oxygen::Graphics {
    using Base = oxygen::Graphics;

public:
    explicit Graphics(const SerializedBackendConfig& config);

    ~Graphics() override = default;

    OXYGEN_MAKE_NON_COPYABLE(Graphics);
    OXYGEN_MAKE_NON_MOVABLE(Graphics);

    [[nodiscard]] OXYGEN_D3D12_API auto CreateImGuiModule(
        EngineWeakPtr engine,
        platform::WindowIdType window_id)
        const -> std::unique_ptr<imgui::ImguiModule> override;

    [[nodiscard]] OXYGEN_D3D12_API auto CreateSurface(
        std::weak_ptr<platform::Window> window_weak,
        std::shared_ptr<graphics::CommandQueue> command_queue)
        const -> std::shared_ptr<graphics::Surface> override;

    //! @{
    //! Device Manager API (module internal)
    [[nodiscard]] auto GetFactory() const -> FactoryType*;
    [[nodiscard]] auto GetCurrentDevice() const -> DeviceType*;
    [[nodiscard]] auto GetAllocator() const -> D3D12MA::Allocator*;
    //! @}

    [[nodiscard]] OXYGEN_D3D12_API auto Descriptors() const -> const detail::DescriptorHeaps&;

    [[nodiscard]] auto GetShader(std::string_view unique_id) const
        -> std::shared_ptr<graphics::IShaderByteCode> override;

protected:
    [[nodiscard]] OXYGEN_D3D12_API auto CreateCommandQueue(
        graphics::QueueRole role,
        graphics::QueueAllocationPreference allocation_preference)
        -> std::shared_ptr<graphics::CommandQueue> override;

    [[nodiscard]] OXYGEN_D3D12_API auto CreateRendererImpl(
        const std::string_view name,
        std::shared_ptr<graphics::Surface> surface,
        uint32_t frames_in_flight)
        -> std::shared_ptr<graphics::Renderer> override;

    [[nodiscard]] auto CreateCommandList(
        graphics::QueueRole role,
        std::string_view command_list_name)
        -> std::shared_ptr<graphics::CommandList> override;

    [[nodiscard]] auto CreateCommandRecorder(graphics::CommandList* command_list)
        -> std::unique_ptr<graphics::CommandRecorder> override;
};

namespace detail {
    //! Get a reference to the Direct3D12 Graphics backend for internal use
    //! within the module.
    /*!
      \note These functions are not part of the public API and should not be
      used. For application needs, use the `GetBackend()` API from the
      `GraphicsBackendLoader` API.

      \note These functions will __abort__ when called while the graphics
      backend instance is not yet initialized or has been destroyed.
    */
    [[nodiscard]] auto GetGraphics() -> oxygen::graphics::d3d12::Graphics&;
} // namespace detail

} // namespace oxygen::graphics::d3d12
