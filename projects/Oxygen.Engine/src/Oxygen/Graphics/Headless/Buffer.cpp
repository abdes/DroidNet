//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Headless/Buffer.h>

namespace oxygen::graphics::headless {

// View payloads are small PODs owned by the Buffer. Public nested types
// (CBV/SRV/UAV) are declared in Buffer.h and used below.

Buffer::Buffer(BufferDesc desc)
  : graphics::Buffer("HeadlessBuffer")
  , desc_(std::move(desc))
{
  if (desc_.allocation_budget.owner) {
    budget_reservation_ = desc_.allocation_budget.owner->Reserve(
      SizeBytes { desc_.size_bytes }, desc_.allocation_budget.category);
  }
  if (desc_.size_bytes > 0) {
    data_.resize(desc_.size_bytes);
  }
  // Headless buffers are ready to be used after construction.
}

auto Buffer::GetDescriptor() const noexcept -> BufferDesc { return desc_; }

auto Buffer::GetNativeResource() const -> NativeResource
{
  return { this, ClassTypeId() };
}

auto Buffer::DoMap(uint64_t /*offset*/, uint64_t /*size*/) -> void*
{
  DCHECK_F(!IsMapped()); // Guaranteed by the base class

  const auto lock = std::scoped_lock(data_mutex_);
  // Mapping when empty returns nullptr (no backing allocated) - allowed.
  if (data_.empty()) {
    mapped_ = true;
    return nullptr;
  }
  // Map entire buffer by default: return base pointer.
  mapped_ = true;
  return data_.data();
}

auto Buffer::DoUnMap() noexcept -> void
{
  DCHECK_F(IsMapped()); // Guaranteed by the base class
  mapped_ = false;
}

auto Buffer::Update(const void* data, uint64_t size, uint64_t offset) -> void
{
  const auto lock = std::scoped_lock(data_mutex_);
  // Update validates parameters and writes into the backing when present.
  if (data == nullptr || size == 0) {
    return;
  }
  if (offset >= data_.size()) {
    LOG_F(WARNING,
      "Headless Buffer::Update out-of-range offset={} size={} "
      "buffer_size={}",
      offset, size, data_.size());
    return;
  }
  const auto target
    = std::span(data_).subspan(offset, std::min(size, data_.size() - offset));
  std::memcpy(target.data(), data, target.size());
}

auto Buffer::ReadBacking(void* dst, uint64_t src_offset, uint64_t size) const
  -> void
{
  if (dst == nullptr || size == 0) {
    return;
  }
  if (src_offset >= data_.size()) {
    LOG_F(WARNING,
      "Headless Buffer::ReadBacking out-of-range src_offset={} size={} "
      "buffer_size={}",
      src_offset, size, data_.size());
    return;
  }
  const auto source = std::span(data_).subspan(
    src_offset, std::min(size, data_.size() - src_offset));
  std::memcpy(dst, source.data(), source.size());
}

auto Buffer::WriteBacking(const void* src, uint64_t dst_offset, uint64_t size)
  -> void
{
  if (src == nullptr || size == 0) {
    return;
  }
  if (dst_offset >= data_.size()) {
    LOG_F(WARNING,
      "Headless Buffer::WriteBacking out-of-range dst_offset={} size={} "
      "buffer_size={}",
      dst_offset, size, data_.size());
    return;
  }
  const auto target = std::span(data_).subspan(
    dst_offset, std::min(size, data_.size() - dst_offset));
  std::memcpy(target.data(), src, target.size());
}

auto Buffer::GetSize() const noexcept -> uint64_t { return desc_.size_bytes; }

auto Buffer::GetUsage() const noexcept -> BufferUsage { return desc_.usage; }

auto Buffer::GetMemoryType() const noexcept -> BufferMemory
{
  return desc_.memory;
}

auto Buffer::IsMapped() const noexcept -> bool { return mapped_; }

auto Buffer::GetGPUVirtualAddress() const -> uint64_t
{
  // Return a stable fake GPU virtual address for headless testing. Use the
  // pointer value of the object as a deterministic unique address.
  return std::bit_cast<std::uintptr_t>(this);
}

//! View payloads created here are owned by the Buffer instance.
/*!
 The returned `NativeObject` is a non-owning pointer into the owned payload
 storage inside the `Buffer`. The `ResourceRegistry` may cache the
 `NativeObject` value, but it must not assume ownership of the payload memory.
 Unregister views before destroying the buffer or transfer ownership to the
 registry if views must outlive the resource.
*/
auto Buffer::CreateConstantBufferView(
  const DescriptorAllocationHandle& /*view_handle*/,
  const BufferRange& /*range*/) const -> NativeView
{
  return OwnView(CBV {
    {
      .buffer = this,
      .range = {},
      .format = Format::kUnknown,
      .stride = 0,
    },
  });
}

auto Buffer::CreateShaderResourceView(
  const DescriptorAllocationHandle& /*view_handle*/, Format /*format*/,
  BufferRange /*range*/, uint32_t /*stride*/) const -> NativeView
{
  return OwnView(SRV {
    {
      .buffer = this,
      .range = {},
      .format = Format::kUnknown,
      .stride = 0,
    },
  });
}

auto Buffer::CreateUnorderedAccessView(
  const DescriptorAllocationHandle& /*view_handle*/, Format /*format*/,
  BufferRange /*range*/, uint32_t /*stride*/) const -> NativeView
{
  return OwnView(UAV {
    {
      .buffer = this,
      .range = {},
      .format = Format::kUnknown,
      .stride = 0,
    },
  });
}

} // namespace oxygen::graphics::headless
