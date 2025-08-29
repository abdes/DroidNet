//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <Oxygen/Graphics/Common/Buffer.h>

namespace oxygen::graphics::headless {

class Buffer final : public graphics::Buffer {
public:
  explicit Buffer(const BufferDesc& desc);
  ~Buffer() override = default;

  // Mark this buffer as initialized by the creator/registry. Headless uses an
  // internal flag to avoid depending on the global ResourceRegistry.
  auto MarkInitialized() noexcept -> void { initialized_ = true; }

  // Buffer interface
  [[nodiscard]] auto GetDescriptor() const noexcept -> BufferDesc override;
  [[nodiscard]] auto GetNativeResource() const -> NativeObject override;
  auto Map(size_t offset = 0, size_t size = 0) -> void* override;
  auto UnMap() -> void override;
  auto Update(const void* data, size_t size, size_t offset = 0)
    -> void override;
  // Headless-only helpers to access the CPU backing store. These are safe
  // no-ops for non-headless buffers and perform bounds-checked copies for
  // headless buffers.
  auto ReadBacking(void* dst, size_t src_offset, size_t size) const -> void;
  auto WriteBacking(const void* src, size_t dst_offset, size_t size) -> void;
  [[nodiscard]] auto GetSize() const noexcept -> size_t override;
  [[nodiscard]] auto GetUsage() const noexcept -> BufferUsage override;
  [[nodiscard]] auto GetMemoryType() const noexcept -> BufferMemory override;
  [[nodiscard]] auto IsMapped() const noexcept -> bool override;
  [[nodiscard]] auto GetGPUVirtualAddress() const -> uint64_t override;

protected:
  [[nodiscard]] auto CreateConstantBufferView(
    const DescriptorHandle& view_handle, const BufferRange& range = {}) const
    -> NativeObject override;
  [[nodiscard]] auto CreateShaderResourceView(
    const DescriptorHandle& view_handle, Format format, BufferRange range = {},
    uint32_t stride = 0) const -> NativeObject override;
  [[nodiscard]] auto CreateUnorderedAccessView(
    const DescriptorHandle& view_handle, Format format, BufferRange range = {},
    uint32_t stride = 0) const -> NativeObject override;

private:
  BufferDesc desc_ {};
  bool mapped_ = false;
  // CPU-side backing storage for headless emulation. Lazily allocated in ctor
  // when size_bytes > 0.
  std::vector<std::uint8_t> data_;
  // Whether the buffer has been initialized/registered by the creator.
  bool initialized_ = false;
};

} // namespace oxygen::graphics::headless
