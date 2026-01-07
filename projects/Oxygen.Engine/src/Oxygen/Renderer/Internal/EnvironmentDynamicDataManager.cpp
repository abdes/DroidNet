//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "EnvironmentDynamicDataManager.h"

#include <cstring>

#include <Oxygen/Base/Logging.h>

namespace oxygen::engine::internal {

using graphics::BufferDesc;
using graphics::BufferMemory;
using graphics::BufferUsage;

namespace {
  // Buffer size must accommodate EnvironmentDynamicData and be 256-byte aligned
  // for root CBV requirements.
  constexpr std::uint32_t kBufferSize = 256;
  static_assert(sizeof(EnvironmentDynamicData) <= kBufferSize,
    "EnvironmentDynamicData exceeds buffer size");
} // namespace

EnvironmentDynamicDataManager::EnvironmentDynamicDataManager(
  observer_ptr<Graphics> gfx)
  : gfx_(gfx)
{
}

EnvironmentDynamicDataManager::~EnvironmentDynamicDataManager()
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

auto EnvironmentDynamicDataManager::OnFrameStart(frame::Slot slot) -> void
{
  current_slot_ = slot;
  LOG_F(1, "EnvironmentDynamicDataManager::OnFrameStart slot={}", slot);
}

auto EnvironmentDynamicDataManager::GetOrCreateBuffer(ViewId view_id)
  -> BufferInfo
{
  if (current_slot_ == frame::kInvalidSlot) {
    LOG_F(ERROR,
      "EnvironmentDynamicDataManager::GetOrCreateBuffer called without valid "
      "frame slot");
    return {};
  }

  const BufferKey key { current_slot_, view_id };

  // Check if buffer already exists
  if (auto it = buffers_.find(key); it != buffers_.end()) {
    return it->second;
  }

  // Create new buffer
  const BufferDesc desc {
    .size_bytes = kBufferSize,
    .usage = BufferUsage::kConstant,
    .memory = BufferMemory::kUpload,
    .debug_name = "EnvDynamicData_View" + std::to_string(view_id.get())
      + "_Slot" + std::to_string(current_slot_),
  };

  auto buffer = gfx_->CreateBuffer(desc);
  if (!buffer) {
    LOG_F(ERROR,
      "Failed to create environment dynamic data buffer for view {} slot {}",
      view_id.get(), current_slot_);
    return {};
  }

  buffer->SetName(desc.debug_name);

  // Persistently map the buffer
  void* mapped_ptr = buffer->Map();
  if (!mapped_ptr) {
    LOG_F(ERROR,
      "Failed to map environment dynamic data buffer for view {} slot {}",
      view_id.get(), current_slot_);
    return {};
  }

  BufferInfo info { buffer, mapped_ptr };
  buffers_[key] = info;

  LOG_F(1,
    "EnvironmentDynamicDataManager: Created buffer for view {} slot {} "
    "(size={} bytes)",
    view_id.get(), current_slot_, kBufferSize);

  return info;
}

auto EnvironmentDynamicDataManager::WriteEnvironmentData(
  ViewId view_id, const EnvironmentDynamicData& data) -> BufferInfo
{
  auto info = GetOrCreateBuffer(view_id);
  if (!info.buffer || !info.mapped_ptr) {
    LOG_F(ERROR,
      "EnvironmentDynamicDataManager::WriteEnvironmentData failed for view {} "
      "- no buffer",
      view_id.get());
    return {};
  }

  // Persistently mapped - write directly
  std::memcpy(info.mapped_ptr, &data, sizeof(EnvironmentDynamicData));
  return info;
}

} // namespace oxygen::engine::internal
