//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <dxgi1_6.h>
#include <memory>

namespace oxygen::renderer::d3d12 {

  using DeviceType = ID3D12Device9;
  using GraphicsCommandListType = ID3D12GraphicsCommandList7;
  using CommandQueueType = ID3D12CommandQueue;
  using DescriptorHeapType = ID3D12DescriptorHeap;
  using FenceType = ID3D12Fence;
  using FactoryType = IDXGIFactory7;

  /**
   * Forward declarations of renderer types and associated smart pointers.
   * @{
   */

  class WindowSurface;
  class Renderer;
  class IDeferredReleaseController;

  using DeferredReleaseControllerPtr = std::weak_ptr<IDeferredReleaseController>;
  /**@}*/

}  // namespace oxygen::renderer::d3d12
