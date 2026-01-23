//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>

namespace oxygen::graphics::d3d12 {

//! Type aliases for Direct3d12 interfaces with versions used in the module.
//! @{

namespace dx {
  using IFactory = IDXGIFactory7;
  using IDevice = ID3D12Device9;
  using IGraphicsCommandList = ID3D12GraphicsCommandList7;
  using ICommandQueue = ID3D12CommandQueue;
  using IDescriptorHeap = ID3D12DescriptorHeap;
  using IFence = ID3D12Fence;
  using IDebug = ID3D12Debug6;
  using IPipelineState = ID3D12PipelineState;
  using IRootSignature = ID3D12RootSignature;
} // namespace dx

namespace dxgi {
  using IAdapter = IDXGIAdapter1;
  using IDebug = IDXGIDebug1;
  using InfoQueue = IDXGIInfoQueue;
} // namespace dxgi

} // namespace oxygen::graphics::d3d12
