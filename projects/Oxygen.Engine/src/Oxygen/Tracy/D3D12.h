//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <source_location>
#include <span>
#include <string_view>

#include <d3d12.h>

#include <Oxygen/Tracy/api_export.h>

namespace oxygen::tracy::d3d12 {

using ContextHandle = void*;

OXGN_TRACY_NDAPI auto CreateContext(
  ID3D12Device* device, ID3D12CommandQueue* queue) -> ContextHandle;
OXGN_TRACY_API auto DestroyContext(ContextHandle context) -> void;
OXGN_TRACY_API auto AdvanceContextFrame(ContextHandle context) -> void;
OXGN_TRACY_API auto CollectContext(ContextHandle context) -> void;
OXGN_TRACY_API auto NameContext(ContextHandle context, std::string_view name)
  -> void;

OXGN_TRACY_NDAPI auto BeginZone(std::span<std::byte> storage,
  ContextHandle context, ID3D12GraphicsCommandList* command_list,
  std::source_location callsite, std::string_view name) -> bool;
OXGN_TRACY_API auto EndZone(std::span<std::byte> storage) -> void;

} // namespace oxygen::tracy::d3d12
