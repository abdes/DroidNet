//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Graphics/Common/Graphics.h"
#include "Oxygen/Graphics/Direct3D12/Renderer.h"
#include "Oxygen/Graphics/Direct3D12/Types.h"

#include <wrl/client.h>

namespace oxygen::graphics::d3d12 {

class Graphics final : public oxygen::Graphics
{
  using Base = oxygen::Graphics;

 public:
  Graphics()
    : Base("D3D12 Backend")
  {
  }

  ~Graphics() override = default;

  OXYGEN_MAKE_NON_COPYABLE(Graphics);
  OXYGEN_MAKE_NON_MOVEABLE(Graphics);

  OXYGEN_D3D12_API [[nodiscard]] auto GetFactory() const -> renderer::d3d12::FactoryType*;
  OXYGEN_D3D12_API [[nodiscard]] auto GetMainDevice() const -> renderer::d3d12::DeviceType*;
  OXYGEN_D3D12_API [[nodiscard]] auto GetAllocator() const -> D3D12MA::Allocator* { return allocator_; }

 protected:
  void InitializeGraphicsBackend(PlatformPtr platform, const GraphicsBackendProperties& props) override;
  void ShutdownGraphicsBackend() override;
  auto CreateRenderer() -> std::unique_ptr<Renderer> override;

 private:
  Microsoft::WRL::ComPtr<renderer::d3d12::FactoryType> factory_ {};
  Microsoft::WRL::ComPtr<renderer::d3d12::DeviceType> main_device_ {};
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
  OXYGEN_D3D12_API [[nodiscard]] auto Graphics() -> Graphics&;
  OXYGEN_D3D12_API [[nodiscard]] auto GetFactory() -> renderer::d3d12::FactoryType*;
  OXYGEN_D3D12_API [[nodiscard]] auto GetMainDevice() -> renderer::d3d12::DeviceType*;
  OXYGEN_D3D12_API [[nodiscard]] auto GetRenderer() -> renderer::d3d12::Renderer&;
  OXYGEN_D3D12_API [[nodiscard]] auto GetPerFrameResourceManager() -> renderer::PerFrameResourceManager&;
  OXYGEN_D3D12_API [[nodiscard]] auto GetAllocator() -> D3D12MA::Allocator&;
  //! Get the backend memory allocator
  // TODO: Add the allocator
  //! @}
} // namespace oxygen::graphics::d3d12::detail

} // namespace oxygen::graphics::d3d12
