//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>
#include <limits>

#include <glm/mat4x4.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/Detail/BindlessStructuredBuffer.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Types/MaterialConstants.h>

using oxygen::engine::detail::BindlessStructuredBuffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::BufferViewDescription;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::ResourceViewType;

// Explicit instantiation requires a complete type

template <typename DataType>
auto BindlessStructuredBuffer<DataType>::EnsureAndUpload(
  oxygen::Graphics& graphics, const std::string& debug_name) -> bool
{
  if (!HasData()) {
    return false; // No data to upload this frame
  }

  if (buffer_ && !dirty_) {
    return false; // Up-to-date
  }

  const auto size_bytes = CalculateBufferSize();
  const bool need_recreate = !buffer_ || buffer_->GetSize() < size_bytes;
  bool slot_changed = false;

  if (need_recreate) {
    CreateOrResizeBuffer(graphics, debug_name, size_bytes);
    RegisterStructuredBufferSrv(graphics);
    slot_changed = true;
  }

  if (dirty_) {
    UploadData();
    dirty_ = false;
  }

  return slot_changed;
}

template <typename DataType>
auto BindlessStructuredBuffer<DataType>::CreateOrResizeBuffer(
  oxygen::Graphics& graphics, const std::string& debug_name,
  const std::size_t size_bytes) -> void
{
  const BufferDesc desc {
    .size_bytes = size_bytes,
    .usage = BufferUsage::kConstant,
    .memory = BufferMemory::kUpload,
    .debug_name = debug_name,
  };

  buffer_ = graphics.CreateBuffer(desc);
  buffer_->SetName(debug_name);
  graphics.GetResourceRegistry().Register(buffer_);

  // Reset heap slot since we're creating a new buffer
  heap_slot_ = kInvalidHeapSlot;
}

template <typename DataType>
auto BindlessStructuredBuffer<DataType>::RegisterStructuredBufferSrv(
  oxygen::Graphics& graphics) -> void
{
  auto& descriptor_allocator = graphics.GetDescriptorAllocator();
  const BufferViewDescription srv_view_desc {
    .view_type = ResourceViewType::kStructuredBuffer_SRV,
    .visibility = DescriptorVisibility::kShaderVisible,
    .format = Format::kUnknown,
    .stride = sizeof(DataType),
  };

  auto srv_handle
    = descriptor_allocator.Allocate(ResourceViewType::kStructuredBuffer_SRV,
      DescriptorVisibility::kShaderVisible);

  if (!srv_handle.IsValid()) {
    LOG_F(ERROR, "Failed to allocate descriptor for {} structured buffer",
      buffer_->GetName());
    return;
  }

  const auto view = buffer_->GetNativeView(srv_handle, srv_view_desc);
  heap_slot_ = descriptor_allocator.GetShaderVisibleIndex(srv_handle).get();

  graphics.GetResourceRegistry().RegisterView(
    *buffer_, view, std::move(srv_handle), srv_view_desc);

  LOG_F(INFO, "{} structured buffer SRV registered at heap index {}",
    buffer_->GetName(), heap_slot_);
}

template <typename DataType>
auto BindlessStructuredBuffer<DataType>::UploadData() -> void
{
  const auto size_bytes = CalculateBufferSize();
  void* mapped = buffer_->Map();
  std::memcpy(mapped, cpu_data_.data(), size_bytes);
  buffer_->UnMap();
}

// Explicit template instantiation for common types used by the renderer. Placed
// here because this is the only translation unit that sees the entire template
// class definition.
template class BindlessStructuredBuffer<oxygen::engine::DrawMetadata>;
template class BindlessStructuredBuffer<glm::mat4>;
template class BindlessStructuredBuffer<oxygen::engine::MaterialConstants>;
