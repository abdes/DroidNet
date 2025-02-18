//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <memory>

namespace oxygen::graphics::d3d12 {

//! Type aliases for Direct3d12 interfaces with versions used in the module.
//! @{

namespace dx {
    using IDevice = ID3D12Device9;
    using IGraphicsCommandList = ID3D12GraphicsCommandList7;
    using ICommandQueue = ID3D12CommandQueue;
    using IDescriptorHeap = ID3D12DescriptorHeap;
    using IFence = ID3D12Fence;
    using IDebug = ID3D12Debug6;
} // namespace dx

namespace dxgi {
    using IFactory = IDXGIFactory7;
    using IDebug = IDXGIDebug1;
    using InfoQueue = IDXGIInfoQueue;
} // namespace dxgi

//! @}

//! Type aliases for Direct3d12 interfaces with versions used in the module.
//! @{

using DeviceType = ID3D12Device9;
using FactoryType = IDXGIFactory7;
using GraphicsCommandListType = ID3D12GraphicsCommandList7;
using CommandQueueType = ID3D12CommandQueue;
using DescriptorHeapType = ID3D12DescriptorHeap;
using ID3DFenceV = ID3D12Fence;

//! @}

//! Forward declarations of renderer types and associated smart pointers.
//! @{

class CommandList;
class CommandQueue;
class CommandRecorder;
class Fence;
class Renderer;
class WindowSurface;
class RenderTarget;

using FencePtr = std::unique_ptr<Fence>;
//! @}

// These are only for internal use.
namespace detail {
    class IDeferredReleaseController;
    class WindowSurface;
    struct DescriptorHandle;
    class DescriptorHeap;
    class PerFrameResourceManager;

    using WindowSurfaceImplPtr = std::shared_ptr<WindowSurface>;
    using DescriptorHandlePtr = std::shared_ptr<DescriptorHandle>;
} // namespace oxygen::graphics::d3d12::detail

} // namespace oxygen::graphics::d3d12
