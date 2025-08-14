//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class RenderController;
} // namespace oxygen::graphics

namespace oxygen::engine::detail {

//! Invalid descriptor heap slot constant for bindless rendering.
constexpr std::uint32_t kInvalidHeapSlot
  = (std::numeric_limits<std::uint32_t>::max)();

//! Manages GPU structured buffer lifecycle for bindless rendering with CPU-side
//! data, dirty tracking, and descriptor heap slot assignment. Always uses
//! std::vector<DataType> for CPU storage to support per-draw arrays.
template <typename DataType> class BindlessStructuredBuffer {
public:
  BindlessStructuredBuffer() = default;
  ~BindlessStructuredBuffer() = default;

  OXYGEN_DEFAULT_COPYABLE(BindlessStructuredBuffer)
  OXYGEN_DEFAULT_MOVABLE(BindlessStructuredBuffer)

  //! Returns mutable reference to CPU-side data vector.
  [[nodiscard]] auto GetCpuData() -> std::vector<DataType>&
  {
    return cpu_data_;
  }

  //! Returns immutable reference to CPU-side data vector.
  [[nodiscard]] auto GetCpuData() const -> const std::vector<DataType>&
  {
    return cpu_data_;
  }

  //! Marks buffer as needing GPU upload.
  auto MarkDirty() -> void { dirty_ = true; }

  //! Returns true if buffer needs GPU upload.
  [[nodiscard]] auto IsDirty() const -> bool { return dirty_; }

  //! Clears dirty flag after successful upload.
  auto ClearDirty() -> void { dirty_ = false; }

  //! Returns the GPU structured buffer.
  [[nodiscard]] auto GetBuffer() const -> std::shared_ptr<graphics::Buffer>
  {
    return buffer_;
  }

  //! Returns the bindless descriptor heap slot for the structured buffer SRV.
  [[nodiscard]] auto GetHeapSlot() const -> std::uint32_t { return heap_slot_; }

  //! Returns true if a valid descriptor heap slot has been assigned.
  [[nodiscard]] auto IsSlotAssigned() const -> bool
  {
    return heap_slot_ != kInvalidHeapSlot;
  }

  //! Ensures structured buffer exists, uploads if dirty, and registers SRV if
  //! needed. Returns true if any changes were made that might affect scene
  //! constants.
  OXGN_RNDR_API auto EnsureAndUpload(
    graphics::RenderController& rc, const std::string& debug_name) -> bool;

  //! Returns true if CPU data vector contains elements.
  [[nodiscard]] auto HasData() const -> bool { return !cpu_data_.empty(); }

private:
  //! Creates or resizes the GPU structured buffer if needed.
  auto CreateOrResizeBuffer(graphics::RenderController& rc,
    const std::string& debug_name, std::size_t size_bytes) -> void;

  //! Registers structured buffer SRV and caches heap slot.
  auto RegisterStructuredBufferSrv(graphics::RenderController& rc) -> void;

  //! Uploads CPU data to GPU structured buffer.
  auto UploadData() -> void;

  //! Calculates required structured buffer size in bytes.
  [[nodiscard]] auto CalculateBufferSize() const -> std::size_t
  {
    return cpu_data_.size() * sizeof(DataType);
  }

  std::vector<DataType> cpu_data_;
  std::shared_ptr<graphics::Buffer> buffer_;
  bool dirty_ { false };
  std::uint32_t heap_slot_ { kInvalidHeapSlot };
};

} // namespace oxygen::engine::detail
