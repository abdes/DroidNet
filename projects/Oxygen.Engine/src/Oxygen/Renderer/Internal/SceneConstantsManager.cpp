//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "SceneConstantsManager.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/Types/SceneConstants.h>

namespace oxygen::engine::internal {

using graphics::BufferDesc;
using graphics::BufferMemory;
using graphics::BufferUsage;

SceneConstantsManager::SceneConstantsManager(
  observer_ptr<Graphics> gfx, std::uint32_t buffer_size)
  : gfx_(gfx)
  , buffer_size_(buffer_size)
{
}

SceneConstantsManager::~SceneConstantsManager()
{
  // Unmap all buffers
  for (auto& [key, info] : buffers_) {
    if (info.buffer && info.mapped_ptr) {
      info.buffer->UnMap();
      info.mapped_ptr = nullptr;
    }
  }
  buffers_.clear();
}

auto SceneConstantsManager::OnFrameStart(frame::Slot slot) -> void
{
  current_slot_ = slot;
}

auto SceneConstantsManager::GetOrCreateBuffer(ViewId view_id) -> BufferInfo
{
  if (current_slot_ == frame::kInvalidSlot) {
    LOG_F(ERROR,
      "SceneConstantsManager::GetOrCreateBuffer called without valid frame "
      "slot");
    return {};
  }

  const BufferKey key { current_slot_, view_id };

  // Check if buffer already exists
  if (auto it = buffers_.find(key); it != buffers_.end()) {
    return it->second;
  }

  // Create new buffer
  const BufferDesc desc {
    .size_bytes = buffer_size_,
    .usage = BufferUsage::kConstant,
    .memory = BufferMemory::kUpload,
    .debug_name = "SceneConstants_View" + std::to_string(view_id.get())
      + "_Slot" + std::to_string(current_slot_),
  };

  auto buffer = gfx_->CreateBuffer(desc);
  if (!buffer) {
    LOG_F(ERROR, "Failed to create scene constants buffer for view {} slot {}",
      view_id.get(), current_slot_);
    return {};
  }

  buffer->SetName(desc.debug_name);

  // Persistently map the buffer
  void* mapped_ptr = buffer->Map();
  if (!mapped_ptr) {
    LOG_F(ERROR, "Failed to map scene constants buffer for view {} slot {}",
      view_id.get(), current_slot_);
    return {};
  }

  BufferInfo info { buffer, mapped_ptr };
  buffers_[key] = info;

  LOG_F(1,
    "SceneConstantsManager: Created buffer for view {} slot {} (size={} "
    "bytes)",
    view_id.get(), current_slot_, buffer_size_);

  return info;
}

auto SceneConstantsManager::WriteSceneConstants(
  ViewId view_id, const void* snapshot, std::size_t size_bytes) -> BufferInfo
{
  auto info = GetOrCreateBuffer(view_id);
  if (!info.buffer || !info.mapped_ptr) {
    LOG_F(ERROR,
      "SceneConstantsManager::WriteSceneConstants failed for view {} - no "
      "buffer",
      view_id.get());
    return {};
  }

  if (size_bytes > buffer_size_) {
    LOG_F(ERROR,
      "SceneConstantsManager::WriteSceneConstants snapshot size ({}) exceeds "
      "configured buffer size ({})",
      size_bytes, buffer_size_);
    return {};
  }

  // Persistently mapped - write directly
  std::memcpy(info.mapped_ptr, snapshot, size_bytes);
  return info;
}

} // namespace oxygen::engine::internal
