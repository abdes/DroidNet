//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Direct3d12/MemoryBlock.h"

#include "detail/dx12_utils.h"
#include "oxygen/base/logging.h"
#include "Oxygen/Base/Windows/ComError.h"
#include "Oxygen/Renderers/Direct3d12/D3D12MemAlloc.h"
#include "Oxygen/Renderers/Direct3d12/Types.h"
#include "Oxygen/Renderers/Direct3d12/Types.h"
#include "Renderer.h"

using namespace oxygen::renderer::d3d12;
using oxygen::windows::ThrowOnFailed;

MemoryBlock::MemoryBlock()
  : IMemoryBlock()
  , size_(0u)
  , alignment_(0u)
{
}

MemoryBlock::~MemoryBlock() = default;

void MemoryBlock::Init(const MemoryBlockDesc& desc)
{
  if (desc.size == 0u)
  {
    LOG_F(ERROR, "Memory block size must be greater than zero.");
    throw std::invalid_argument("memory block size must be greater than zero");
  }

  constexpr D3D12MA::ALLOCATION_DESC alloc_desc = {
    .Flags = D3D12MA::ALLOCATION_FLAG_NONE,
    .HeapType = D3D12_HEAP_TYPE_DEFAULT,
    .ExtraHeapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES,
    .CustomPool = nullptr,
    .pPrivateData = nullptr,
  };

  const D3D12_RESOURCE_ALLOCATION_INFO allocation_info = {
    .SizeInBytes = desc.size,
    .Alignment = desc.alignment,
  };

  const auto& renderer = detail::GetRenderer();
  D3D12MA::Allocation* allocation_raw_ptr = nullptr; // Local variable to store the raw pointer
  ThrowOnFailed(renderer.GetAllocator()->AllocateMemory(
    &alloc_desc,
    &allocation_info,
    &allocation_raw_ptr));
  allocation_.reset(allocation_raw_ptr); // Assign the raw pointer to the unique_ptr

  size_ = desc.size;
  alignment_ = desc.alignment;
}
