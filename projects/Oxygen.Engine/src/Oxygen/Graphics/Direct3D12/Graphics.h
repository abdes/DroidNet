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
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/RenderController.h>

// ReSharper disable once CppInconsistentNaming
namespace D3D12MA {
class Allocator;
} // namespace D3D12MA

namespace oxygen::graphics::d3d12 {

class Graphics : public oxygen::Graphics,
                 public std::enable_shared_from_this<Graphics> {
  using Base = oxygen::Graphics;

public:
  OXYGEN_D3D12_API explicit Graphics(const SerializedBackendConfig& config);

  OXYGEN_D3D12_API ~Graphics() override = default;

  OXYGEN_MAKE_NON_COPYABLE(Graphics)
  OXYGEN_MAKE_NON_MOVABLE(Graphics)

  //=== D3D12 specific factories ===----------------------------------------//

  [[nodiscard]] OXYGEN_D3D12_API auto CreateSurface(
    std::weak_ptr<platform::Window> window_weak,
    std::shared_ptr<graphics::CommandQueue> command_queue) const
    -> std::shared_ptr<Surface> override;

  [[nodiscard]] OXYGEN_D3D12_API auto CreateFramebuffer(
    const FramebufferDesc& desc, graphics::RenderController& renderer)
    -> std::shared_ptr<graphics::Framebuffer> override;

  [[nodiscard]] OXYGEN_D3D12_API auto CreateTexture(
    const TextureDesc& desc) const
    -> std::shared_ptr<graphics::Texture> override;

  [[nodiscard]] OXYGEN_D3D12_API auto CreateTextureFromNativeObject(
    const TextureDesc& desc, const NativeObject& native) const
    -> std::shared_ptr<graphics::Texture> override;

  [[nodiscard]] OXYGEN_D3D12_API auto CreateBuffer(const BufferDesc& desc) const
    -> std::shared_ptr<graphics::Buffer> override;

  [[nodiscard]] OXYGEN_D3D12_API auto GetShader(
    std::string_view unique_id) const
    -> std::shared_ptr<IShaderByteCode> override;

  //=== Device Manager Internal API ===-------------------------------------//

  //! @{
  //! Device Manager API (module internal)
  [[nodiscard]] OXYGEN_D3D12_API virtual auto GetFactory() const
    -> dx::IFactory*;
  [[nodiscard]] OXYGEN_D3D12_API virtual auto GetCurrentDevice() const
    -> dx::IDevice*;
  [[nodiscard]] OXYGEN_D3D12_API virtual auto GetAllocator() const
    -> D3D12MA::Allocator*;
  //! @}

  //=== D3D12 Helpers ===---------------------------------------------------//

  [[nodiscard]] OXYGEN_D3D12_API auto GetFormatPlaneCount(
    DXGI_FORMAT format) const -> uint8_t;

protected:
  // Default constructor that does not initialize the backend. Used for testing
  // purposes.
  Graphics()
    : Base("Dummy Graphics Backend")
  {
  }

  [[nodiscard]] OXYGEN_D3D12_API auto CreateCommandQueue(std::string_view name,
    QueueRole role, QueueAllocationPreference allocation_preference)
    -> std::shared_ptr<graphics::CommandQueue> override;

  [[nodiscard]] OXYGEN_D3D12_API auto CreateRendererImpl(std::string_view name,
    std::weak_ptr<Surface> surface, uint32_t frames_in_flight)
    -> std::unique_ptr<graphics::RenderController> override;

  [[nodiscard]] OXYGEN_D3D12_API auto CreateCommandListImpl(
    QueueRole role, std::string_view command_list_name)
    -> std::unique_ptr<graphics::CommandList> override;

private:
  mutable std::unordered_map<DXGI_FORMAT, uint8_t>
    dxgi_format_plane_count_cache_ {};
};

}
