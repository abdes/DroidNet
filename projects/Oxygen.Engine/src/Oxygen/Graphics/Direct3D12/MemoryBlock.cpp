//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/MemoryBlock.h>

#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Base/logging.h>
#include <Oxygen/Graphics/Direct3D12/Allocator/D3D12MemAlloc.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>

using oxygen::graphics::d3d12::MemoryBlock;
using oxygen::graphics::d3d12::detail::GetAllocator;
using oxygen::graphics::d3d12::detail::GetRenderer;
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
    if (desc.size == 0u) {
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

    D3D12MA::Allocation* allocation_raw_ptr = nullptr; // Local variable to store the raw pointer
    ThrowOnFailed(GetAllocator().AllocateMemory(
        &alloc_desc,
        &allocation_info,
        &allocation_raw_ptr));
    allocation_ = allocation_raw_ptr; // Assign the raw pointer to the unique_ptr

    size_ = desc.size;
    alignment_ = desc.alignment;
}
