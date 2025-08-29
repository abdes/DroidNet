//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstring>

#include <Oxygen/Graphics/Headless/Buffer.h>

namespace oxygen::graphics::headless {

Buffer::Buffer(const BufferDesc& desc)
  : graphics::Buffer("HeadlessBuffer")
  , desc_(desc)
{
  if (desc_.size_bytes > 0) {
    data_.resize(desc_.size_bytes);
  }
}

[[nodiscard]] auto Buffer::GetDescriptor() const noexcept -> BufferDesc
{
  return desc_;
}

[[nodiscard]] auto Buffer::GetNativeResource() const -> NativeObject
{
  return NativeObject(const_cast<Buffer*>(this), ClassTypeId());
}

auto Buffer::Map(size_t /*offset*/, size_t /*size*/) -> void*
{
  if (!initialized_) {
    LOG_F(WARNING, "Headless Buffer::Map called on uninitialized resource");
    DCHECK_F(initialized_, "Mapping uninitialized buffer");
    return nullptr;
  }
  mapped_ = true;
  if (data_.empty()) {
    return nullptr;
  }
  return data_.data();
}

auto Buffer::UnMap() -> void { mapped_ = false; }

auto Buffer::Update(const void* data, size_t size, size_t offset) -> void
{
  if (!initialized_) {
    LOG_F(WARNING, "Headless Buffer::Update called on uninitialized resource");
    DCHECK_F(initialized_, "Updating uninitialized buffer");
    return;
  }
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

auto Buffer::ReadBacking(void* dst, size_t src_offset, size_t size) const
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

auto Buffer::WriteBacking(const void* src, size_t dst_offset, size_t size)
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

[[nodiscard]] auto Buffer::GetSize() const noexcept -> size_t
{
  return desc_.size_bytes;
}

[[nodiscard]] auto Buffer::GetUsage() const noexcept -> BufferUsage
{
  return desc_.usage;
}

[[nodiscard]] auto Buffer::GetMemoryType() const noexcept -> BufferMemory
{
  return desc_.memory;
}

[[nodiscard]] auto Buffer::IsMapped() const noexcept -> bool { return mapped_; }

[[nodiscard]] auto Buffer::GetGPUVirtualAddress() const -> uint64_t
{
  return 0;
}

[[nodiscard]] auto Buffer::CreateConstantBufferView(
  const DescriptorHandle& /*view_handle*/, const BufferRange& /*range*/) const
  -> NativeObject
{
  return {};
}

[[nodiscard]] auto Buffer::CreateShaderResourceView(
  const DescriptorHandle& /*view_handle*/, Format /*format*/,
  BufferRange /*range*/, uint32_t /*stride*/) const -> NativeObject
{
  return {};
}

[[nodiscard]] auto Buffer::CreateUnorderedAccessView(
  const DescriptorHandle& /*view_handle*/, Format /*format*/,
  BufferRange /*range*/, uint32_t /*stride*/) const -> NativeObject
{
  return {};
}

} // namespace oxygen::graphics::headless
