//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <wrl/client.h>

#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Direct3D12/Allocator/D3D12MemAlloc.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>

namespace oxygen::graphics::d3d12 {
class DebugLayer;
//! Device allocations and heaps survive the Graphics facade until last use.
struct NativeLifetime {
  // Report live objects only after all retained native allocations are
  // released.
  std::shared_ptr<DebugLayer> debug_layer;
  // Destruction order: descriptors, memory allocator, device, then debug layer.
  Microsoft::WRL::ComPtr<dx::IDevice> device;
  Microsoft::WRL::ComPtr<D3D12MA::Allocator> memory_allocator;
  std::shared_ptr<graphics::DescriptorAllocator> descriptors;
};
} // namespace oxygen::graphics::d3d12
