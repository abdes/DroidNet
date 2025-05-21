//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Allocator/D3D12MemAlloc.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/Renderer.h>

namespace oxygen::graphics {

class CommandQueue;
class SynchronizationCounter;

namespace d3d12 {

    // TODO: add a component to manage descriptor heaps (rtv, dsv, srv, uav)

    class Graphics final : public oxygen::Graphics, public std::enable_shared_from_this<Graphics> {
        using Base = oxygen::Graphics;

    public:
        explicit Graphics(const SerializedBackendConfig& config);

        ~Graphics() override = default;

        OXYGEN_MAKE_NON_COPYABLE(Graphics);
        OXYGEN_MAKE_NON_MOVABLE(Graphics);

        // [[nodiscard]] OXYGEN_D3D12_API auto CreateImGuiModule(
        //     EngineWeakPtr engine,
        //     platform::WindowIdType window_id)
        //     const -> std::unique_ptr<imgui::ImguiModule> override;

        [[nodiscard]] OXYGEN_D3D12_API auto CreateSurface(
            std::weak_ptr<platform::Window> window_weak,
            std::shared_ptr<graphics::CommandQueue> command_queue)
            const -> std::shared_ptr<graphics::Surface> override;

        //! @{
        //! Device Manager API (module internal)
        [[nodiscard]] auto GetFactory() const -> dx::IFactory*;
        [[nodiscard]] auto GetCurrentDevice() const -> dx::IDevice*;
        [[nodiscard]] auto GetAllocator() const -> D3D12MA::Allocator*;
        //! @}

        [[nodiscard]] auto GetShader(std::string_view unique_id) const
            -> std::shared_ptr<IShaderByteCode> override;

        [[nodiscard]] auto GetFormatPlaneCount(DXGI_FORMAT format) const -> uint8_t;

    protected:
        [[nodiscard]] OXYGEN_D3D12_API auto CreateCommandQueue(
            std::string_view name,
            QueueRole role,
            QueueAllocationPreference allocation_preference)
            -> std::shared_ptr<graphics::CommandQueue> override;

        [[nodiscard]] OXYGEN_D3D12_API auto CreateRendererImpl(
            std::string_view name,
            std::weak_ptr<Surface> surface,
            uint32_t frames_in_flight)
            -> std::unique_ptr<graphics::Renderer> override;

        [[nodiscard]] auto CreateCommandListImpl(
            QueueRole role,
            std::string_view command_list_name)
            -> std::unique_ptr<graphics::CommandList> override;

    private:
        mutable std::unordered_map<DXGI_FORMAT, uint8_t> dxgi_format_plane_count_cache_ {};
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
        [[nodiscard]] auto GetGraphics() -> Graphics&;
    } // namespace detail

} // namespace d3d12

} // namespace oxygen::graphics
