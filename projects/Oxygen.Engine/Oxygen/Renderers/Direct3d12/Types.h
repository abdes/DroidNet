//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <memory>

#include "Oxygen/Renderers/Direct3d12/api_export.h"

namespace oxygen::renderer::d3d12 {

  //! Type aliases for Direct3d12 interfaces with versions used in the module.
  //! @{

  using DeviceType = ID3D12Device9;
  using FactoryType = IDXGIFactory7;
  using GraphicsCommandListType = ID3D12GraphicsCommandList7;
  using CommandQueueType = ID3D12CommandQueue;
  using DescriptorHeapType = ID3D12DescriptorHeap;
  using FenceType = ID3D12Fence;

  //! @}

  //! Forward declarations of renderer types and associated smart pointers.
  //! @{

  class CommandList;
  class CommandQueue;
  class CommandRecorder;
  class Fence;
  class Renderer;
  class WindowSurface;

  using FencePtr = std::unique_ptr<Fence>;
  //! @}

  //! Getters for the global Direct3d12 objects used by the renderer.
  //! @{

  //! Get the IDXGIFactory interface used by the renderer.
  OXYGEN_D3D12_API [[nodiscard]] FactoryType* GetFactory();

  //! Get the ID3D12Device interface for the main device used in the renderer.
  OXYGEN_D3D12_API [[nodiscard]] DeviceType* GetMainDevice();

  //! @}

  // These are only for internal use.
  namespace detail {
    class IDeferredReleaseController;
    class WindowSurfaceImpl;

    using DeferredReleaseControllerPtr = std::weak_ptr<IDeferredReleaseController>;
    using WindowSurfaceImplPtr = std::shared_ptr<WindowSurfaceImpl>;
  }

}  // namespace oxygen::renderer::d3d12
