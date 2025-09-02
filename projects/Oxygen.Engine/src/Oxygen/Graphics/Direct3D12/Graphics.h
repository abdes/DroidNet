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
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/RenderController.h>

// ReSharper disable once CppInconsistentNaming
namespace D3D12MA {
class Allocator;
} // namespace D3D12MA

namespace oxygen::graphics::d3d12 {

class Graphics : public oxygen::Graphics {
  using Base = oxygen::Graphics;

public:
  OXGN_D3D12_API explicit Graphics(const SerializedBackendConfig& config);

  OXGN_D3D12_API ~Graphics() override = default;

  OXYGEN_MAKE_NON_COPYABLE(Graphics)
  OXYGEN_MAKE_NON_MOVABLE(Graphics)

  OXGN_D3D12_NDAPI auto GetDescriptorAllocator() const
    -> const graphics::DescriptorAllocator& override;

  //=== D3D12 specific factories ===----------------------------------------//

  OXGN_D3D12_NDAPI auto CreateSurface(
    std::weak_ptr<platform::Window> window_weak,
    std::shared_ptr<graphics::CommandQueue> command_queue) const
    -> std::shared_ptr<Surface> override;

  OXGN_D3D12_NDAPI auto CreateTexture(const TextureDesc& desc) const
    -> std::shared_ptr<graphics::Texture> override;

  OXGN_D3D12_NDAPI auto CreateTextureFromNativeObject(
    const TextureDesc& desc, const NativeObject& native) const
    -> std::shared_ptr<graphics::Texture> override;

  OXGN_D3D12_NDAPI auto CreateBuffer(const BufferDesc& desc) const
    -> std::shared_ptr<graphics::Buffer> override;

  OXGN_D3D12_NDAPI auto GetShader(std::string_view unique_id) const
    -> std::shared_ptr<IShaderByteCode> override;

  //=== Device Manager Internal API ===-------------------------------------//

  //! @{
  //! Device Manager API (module internal)
  OXGN_D3D12_NDAPI virtual auto GetFactory() const -> dx::IFactory*;
  OXGN_D3D12_NDAPI virtual auto GetCurrentDevice() const -> dx::IDevice*;
  OXGN_D3D12_NDAPI virtual auto GetAllocator() const -> D3D12MA::Allocator*;
  //! @}

  //=== D3D12 Helpers ===---------------------------------------------------//

  OXGN_D3D12_NDAPI auto GetFormatPlaneCount(DXGI_FORMAT format) const
    -> uint8_t;

protected:
  // Default constructor that does not initialize the backend. Used for testing
  // purposes.
  Graphics()
    : Base("Dummy Graphics Backend")
  {
  }

  OXGN_D3D12_NDAPI auto CreateCommandQueue(const QueueKey& queue_key,
    QueueRole role) -> std::shared_ptr<graphics::CommandQueue> override;

  OXGN_D3D12_NDAPI auto CreateRendererImpl(std::string_view name,
    std::weak_ptr<Surface> surface, frame::SlotCount frames_in_flight)
    -> std::unique_ptr<graphics::RenderController> override;

  OXGN_D3D12_NDAPI auto CreateCommandListImpl(
    QueueRole role, std::string_view command_list_name)
    -> std::unique_ptr<graphics::CommandList> override;

  OXGN_D3D12_NDAPI auto CreateCommandRecorder(
    std::shared_ptr<graphics::CommandList> command_list,
    observer_ptr<graphics::CommandQueue> target_queue)
    -> std::unique_ptr<graphics::CommandRecorder> override;

private:
  mutable std::unordered_map<DXGI_FORMAT, uint8_t>
    dxgi_format_plane_count_cache_ {};
};

}
