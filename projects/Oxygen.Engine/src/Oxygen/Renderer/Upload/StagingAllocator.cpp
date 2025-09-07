//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Upload/StagingAllocator.h>

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>

namespace oxygen::engine::upload {

auto StagingAllocator::Allocate(const Bytes size, const std::string_view name)
  -> Allocation
{
  oxygen::graphics::BufferDesc desc;
  desc.size_bytes = size.get();
  desc.usage = oxygen::graphics::BufferUsage::kNone; // match Renderer.cpp
  desc.memory = oxygen::graphics::BufferMemory::kUpload;
  desc.debug_name = std::string(name);

  auto buffer = gfx_->CreateBuffer(desc);
  auto* p = static_cast<std::byte*>(buffer->Map()); // persistently mapped

  Allocation alloc;
  alloc.buffer = std::move(buffer);
  alloc.offset = 0;
  alloc.size = desc.size_bytes;
  alloc.ptr = p;
  return alloc;
}

auto StagingAllocator::RetireCompleted(const FenceValue /*completed*/) -> void
{
  // v1: per-allocation buffers rely on RAII, nothing to recycle.
}

} // namespace oxygen::engine::upload
