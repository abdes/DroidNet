//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Headless/api_export.h>

namespace oxygen::graphics::headless {

//! Headless CPU-backed buffer
/*!
 A lightweight CPU-backed implementation of `graphics::Buffer` intended for
 headless runs and unit tests. This class provides an in-memory backing store
 (byte vector) sized from `BufferDesc::size_bytes` and implements a subset of
 GPU buffer behavior sufficient for tests and emulation:

 - Mapping/Unmapping of the backing store via `Map()`/`UnMap()`.
 - Region updates via `Update()` and direct read/write helpers
   (`ReadBacking()` / `WriteBacking()`).
 - Creation of small view payloads (CBV/SRV/UAV) returned as `NativeView`
   pointers that reference payloads owned by the `Buffer` instance.

 ### Key semantics and guarantees:

 - Thread-safety: concurrent access to the CPU backing and mapping state is
   protected by an internal mutex. Callers should still coordinate higher-level
   synchronization when emulating GPU/CPU hazards.
 - Mapping behavior: `Map()` returns a pointer to the backing memory when the
   buffer has storage; if the buffer was created with `size_bytes == 0`,
   `Map()` returns `nullptr` but still sets the mapped state. `UnMap()` clears
   the mapped flag. Mapping is tracked only as a boolean and does not enforce
   exclusive access beyond the internal mutex used by headless helpers.
 - Update/Read semantics: `Update()` / `ReadBacking()` / `WriteBacking()` are
   bounds-checked and will clamp writes/reads to the allocated backing size.
   Invalid parameters (null pointers or zero sizes) are ignored.
 - View payload ownership: view payloads (returned as `NativeView`) are
   small POD structs allocated and owned by the `Buffer` and remain valid for
   the lifetime of the `Buffer` (they are stored in `owned_view_payloads_`).
   The returned `NativeView` is a non-owning pointer into that storage and
   must not be deleted by the caller.

 ### Lifetime and registry notes:

 - The `Buffer` does not attempt to cache or deduplicate views. If a
   `ResourceRegistry` or other system caches `NativeView` pointers to view
   payloads, it must ensure those payloads remain alive for as long as the
   registry expects to reference them (for example, by unregistering views
   before destroying the `Buffer` or by taking ownership of the payload).

 ### Usage example:

 ```cpp
 BufferDesc desc{};
 desc.size_bytes = 1024;
 desc.usage = BufferUsage::kDefault;
 desc.memory = BufferMemory::kCpuWritable;

 auto buf = std::make_shared<headless::Buffer>(desc);
 // Map and write
 void* p = buf->Map();
 if (p) {
   std::memset(p, 0xFF, static_cast<size_t>(buf->GetSize()));
 }
 buf->UnMap();

 // Read backing via helper
 std::vector<uint8_t> tmp(buf->GetSize());
 buf->ReadBacking(tmp.data(), 0, buf->GetSize());
 ```

 @warning Ensure any systems that cache view `NativeView` pointers do not
 hold them past the `Buffer`'s destruction, or transfer ownership of the
 payloads to the cacher. See `Create*View()` methods for details on the
 returned payload type.
 @see ResourceRegistry, NativeView
*/
class Buffer final : public graphics::Buffer {
public:
  // Public nested PODs for buffer view payloads owned by Buffer.
  struct ViewBase {
    const Buffer* buffer;
    BufferRange range;
    Format format;
    uint32_t stride;
  };

  struct CBV : ViewBase { };
  struct SRV : ViewBase { };
  struct UAV : ViewBase { };

  OXGN_HDLS_API explicit Buffer(const BufferDesc& desc);
  ~Buffer() override = default;

  // Buffer interface
  OXGN_HDLS_NDAPI auto GetDescriptor() const noexcept -> BufferDesc override;
  OXGN_HDLS_NDAPI auto GetNativeResource() const -> NativeResource override;
  OXGN_HDLS_NDAPI auto Update(
    const void* data, uint64_t size, uint64_t offset = 0) -> void override;
  OXGN_HDLS_NDAPI auto GetSize() const noexcept -> uint64_t override;
  OXGN_HDLS_NDAPI auto GetUsage() const noexcept -> BufferUsage override;
  OXGN_HDLS_NDAPI auto GetMemoryType() const noexcept -> BufferMemory override;
  OXGN_HDLS_NDAPI auto IsMapped() const noexcept -> bool override;
  OXGN_HDLS_NDAPI auto GetGPUVirtualAddress() const -> uint64_t override;

  // Headless-only helpers to access the CPU backing store. These are safe
  // no-ops for non-headless buffers and perform bounds-checked copies for
  // headless buffers.
  OXGN_HDLS_NDAPI auto ReadBacking(
    void* dst, uint64_t src_offset, uint64_t size) const -> void;
  OXGN_HDLS_NDAPI auto WriteBacking(
    const void* src, uint64_t dst_offset, uint64_t size) -> void;

protected:
  OXGN_HDLS_NDAPI auto DoMap(size_t offset = 0, uint64_t size = 0)
    -> void* override;
  OXGN_HDLS_NDAPI auto DoUnMap() -> void override;

  [[nodiscard]] auto CreateConstantBufferView(
    const DescriptorHandle& view_handle, const BufferRange& range = {}) const
    -> NativeView override;
  [[nodiscard]] auto CreateShaderResourceView(
    const DescriptorHandle& view_handle, Format format, BufferRange range = {},
    uint32_t stride = 0) const -> NativeView override;
  [[nodiscard]] auto CreateUnorderedAccessView(
    const DescriptorHandle& view_handle, Format format, BufferRange range = {},
    uint32_t stride = 0) const -> NativeView override;

private:
  BufferDesc desc_ {};
  bool mapped_ = false;

  // CPU-side backing storage for headless emulation. Lazily allocated in ctor
  // when size_bytes > 0.
  std::vector<std::uint8_t> data_;
  // Protect concurrent access to data_ and mapped_ in tests that may run
  // multi-threaded helpers.
  mutable std::mutex data_mutex_;

  // Owned view payloads to keep NativeView pointers valid without leaking.
  // Use a custom deleter type (function pointer) because deleting an
  // incomplete type with the default deleter is undefined. We allocate the
  // payloads with operator new and destroy with operator delete via the
  // function deleter.
  using ViewPayloadPtr = std::unique_ptr<void, void (*)(void*)>;
  mutable std::deque<ViewPayloadPtr> owned_view_payloads_;
};

} // namespace oxygen::graphics::headless
