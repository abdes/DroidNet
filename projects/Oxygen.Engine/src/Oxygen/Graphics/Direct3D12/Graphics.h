//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Allocator/D3D12MemAlloc.h>
#include <Oxygen/Graphics/Direct3D12/Forward.h>
#include <Oxygen/Graphics/Direct3D12/Renderer.h>

#include <wrl/client.h>

namespace oxygen::graphics::d3d12 {

class Graphics final : public oxygen::Graphics {
    using Base = oxygen::Graphics;

public:
    Graphics(const SerializedBackendConfig& config);

    ~Graphics() override;

    OXYGEN_MAKE_NON_COPYABLE(Graphics);
    OXYGEN_MAKE_NON_MOVABLE(Graphics);

    [[nodiscard]] OXYGEN_D3D12_API auto GetFactory() const -> FactoryType*;
    [[nodiscard]] OXYGEN_D3D12_API auto GetMainDevice() const -> DeviceType*;
    [[nodiscard]] OXYGEN_D3D12_API auto GetAllocator() const -> D3D12MA::Allocator* { return allocator_; }

    [[nodiscard]] OXYGEN_D3D12_API auto CreateImGuiModule(EngineWeakPtr engine, platform::WindowIdType window_id) const -> std::unique_ptr<imgui::ImguiModule> override;

protected:
    // void InitializeGraphicsBackend(const SerializedBackendConfig& props) override;
    // void ShutdownGraphicsBackend() override;
    auto CreateRenderer() -> std::unique_ptr<graphics::Renderer> override;

private:
    Microsoft::WRL::ComPtr<FactoryType> factory_ {};
    Microsoft::WRL::ComPtr<DeviceType> main_device_ {};
    D3D12MA::Allocator* allocator_ { nullptr };
};

namespace detail {
    //! Get references to the Direct3D12 Renderer global objects for internal use within the
    //! renderer implementation module.
    /*!
      \note These functions are not part of the public API and should not be used. For application
      needs, use the GetRenderer() function from the renderer loader API and use the Renderer class.

      \note These functions will __abort__ when called while the renderer instance is not yet
      initialized or has been destroyed.
    */
    //! @{
    [[nodiscard]] OXYGEN_D3D12_API auto Graphics() -> Graphics&;
    [[nodiscard]] OXYGEN_D3D12_API auto GetFactory() -> FactoryType*;
    [[nodiscard]] OXYGEN_D3D12_API auto GetMainDevice() -> DeviceType*;
    [[nodiscard]] OXYGEN_D3D12_API auto GetRenderer() -> Renderer&;
    [[nodiscard]] OXYGEN_D3D12_API auto GetPerFrameResourceManager() -> graphics::PerFrameResourceManager&;
    [[nodiscard]] OXYGEN_D3D12_API auto GetAllocator() -> D3D12MA::Allocator&;
    //! Get the backend memory allocator
    // TODO: Add the allocator
    //! @}
} // namespace oxygen::graphics::d3d12::detail

} // namespace oxygen::graphics::d3d12
