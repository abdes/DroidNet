//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstring>

#include <Oxygen/Graphics/Headless/Buffer.h>

namespace oxygen::graphics::headless {

// View payloads are small PODs owned by the Buffer. Public nested types
// (CBV/SRV/UAV) are declared in Buffer.h and used below.

Buffer::Buffer(const BufferDesc& desc)
  : graphics::Buffer("HeadlessBuffer")
  , desc_(desc)
{
  if (desc_.size_bytes > 0) {
    data_.resize(desc_.size_bytes);
  }
  // Headless buffers are ready to be used after construction.
}

auto Buffer::GetDescriptor() const noexcept -> BufferDesc { return desc_; }

auto Buffer::GetNativeResource() const -> NativeResource
{
  return NativeResource(const_cast<Buffer*>(this), ClassTypeId());
}

auto Buffer::DoMap(uint64_t /*offset*/, uint64_t /*size*/) -> void*
{
  DCHECK_F(!IsMapped()); // Guaranteed by the base class

  std::lock_guard lk(data_mutex_);
  // Mapping when empty returns nullptr (no backing allocated) - allowed.
  if (data_.empty()) {
    mapped_ = true;
    return nullptr;
  }
  // Map entire buffer by default: return base pointer.
  mapped_ = true;
  return data_.data();
}

auto Buffer::DoUnMap() -> void
{
  DCHECK_F(IsMapped()); // Guaranteed by the base class
  mapped_ = false;
}

auto Buffer::Update(const void* data, uint64_t size, uint64_t offset) -> void
{
  std::lock_guard lk(data_mutex_);
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
  const size_t write_size = std::min(size, data_.size() - offset);
  std::memcpy(data_.data() + offset, data, write_size);
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
  const size_t read_size = std::min(size, data_.size() - src_offset);
  std::memcpy(dst, data_.data() + src_offset, read_size);
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
  const size_t write_size = std::min(size, data_.size() - dst_offset);
  std::memcpy(data_.data() + dst_offset, src, write_size);
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
  const auto ptr = reinterpret_cast<uintptr_t>(this);
  return ptr;
}

//! View payloads created here are owned by the Buffer instance.
/*!
 The returned `NativeObject` is a non-owning pointer into the owned payload
 storage inside the `Buffer`. The `ResourceRegistry` may cache the
 `NativeObject` value, but it must not assume ownership of the payload memory.
 Unregister views before destroying the buffer or transfer ownership to the
 registry if views must outlive the resource.
*/
auto Buffer::CreateConstantBufferView(const DescriptorHandle& /*view_handle*/,
  const BufferRange& /*range*/) const -> NativeView
{
  // Allocate raw memory for payload and construct in-place. Use a
  // function-pointer deleter that calls operator delete for the raw
  // allocation to avoid deleting an incomplete type with default deleter.
  void* raw = operator new(sizeof(CBV));
  auto typed = new (raw) CBV { this, BufferRange {}, Format::kUnknown, 0 };
  const void* payload_ptr = typed;
  owned_view_payloads_.emplace_back(raw, [](void* p) {
    if (p) {
      // Explicitly call destructor and free raw memory
      static_cast<CBV*>(p)->~CBV();
      operator delete(p);
    }
  });
  return NativeView(const_cast<void*>(payload_ptr), ClassTypeId());
}

auto Buffer::CreateShaderResourceView(const DescriptorHandle& /*view_handle*/,
  Format /*format*/, BufferRange /*range*/, uint32_t /*stride*/) const
  -> NativeView
{
  void* raw = operator new(sizeof(SRV));
  auto typed = new (raw) SRV { this, BufferRange {}, Format::kUnknown, 0 };
  const void* payload_ptr = typed;
  owned_view_payloads_.emplace_back(raw, [](void* p) {
    if (p) {
      static_cast<SRV*>(p)->~SRV();
      operator delete(p);
    }
  });
  return NativeView(const_cast<void*>(payload_ptr), ClassTypeId());
}

auto Buffer::CreateUnorderedAccessView(const DescriptorHandle& /*view_handle*/,
  Format /*format*/, BufferRange /*range*/, uint32_t /*stride*/) const
  -> NativeView
{
  void* raw = operator new(sizeof(UAV));
  auto typed = new (raw) UAV { this, BufferRange {}, Format::kUnknown, 0 };
  const void* payload_ptr = typed;
  owned_view_payloads_.emplace_back(raw, [](void* p) {
    if (p) {
      static_cast<UAV*>(p)->~UAV();
      operator delete(p);
    }
  });
  return NativeView(const_cast<void*>(payload_ptr), ClassTypeId());
}

} // namespace oxygen::graphics::headless
