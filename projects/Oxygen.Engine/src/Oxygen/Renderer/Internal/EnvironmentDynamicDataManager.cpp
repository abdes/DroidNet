//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "EnvironmentDynamicDataManager.h"

#include <cstring>

#include <Oxygen/Base/Logging.h>
#include <fmt/format.h>

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

  // Reset the data for each view to ensure a clean slate every frame.
  // This ensures that if a pass (like LightCullingPass) does not run for a view
  // in a given frame, the shading passes will use safe default values
  // (e.g., zero light count, identity exposure) instead of stale data from
  // previous frames.
  for (auto& [view_id, state] : view_states_) {
    state.data = EnvironmentDynamicData {};
    MarkAllSlotsDirty(view_id);
  }
}

auto EnvironmentDynamicDataManager::SetExposure(ViewId view_id, float exposure)
  -> void
{
  auto& state = view_states_[view_id];
  if (state.data.exposure != exposure) {
    state.data.exposure = exposure;
    MarkAllSlotsDirty(view_id);
  }
}

auto EnvironmentDynamicDataManager::SetCullingData(ViewId view_id,
  uint32_t grid_slot, uint32_t index_list_slot, uint32_t dim_x, uint32_t dim_y,
  uint32_t dim_z, uint32_t tile_size_px) -> void
{
  auto& state = view_states_[view_id];
  bool dirty = false;
  dirty |= (state.data.bindless_cluster_grid_slot != grid_slot);
  dirty |= (state.data.bindless_cluster_index_list_slot != index_list_slot);
  dirty |= (state.data.cluster_dim_x != dim_x);
  dirty |= (state.data.cluster_dim_y != dim_y);
  dirty |= (state.data.cluster_dim_z != dim_z);
  dirty |= (state.data.tile_size_px != tile_size_px);

  if (dirty) {
    state.data.bindless_cluster_grid_slot = grid_slot;
    state.data.bindless_cluster_index_list_slot = index_list_slot;
    state.data.cluster_dim_x = dim_x;
    state.data.cluster_dim_y = dim_y;
    state.data.cluster_dim_z = dim_z;
    state.data.tile_size_px = tile_size_px;
    MarkAllSlotsDirty(view_id);
  }
}

auto EnvironmentDynamicDataManager::SetZBinning(ViewId view_id, float z_near,
  float z_far, float z_scale, float z_bias) -> void
{
  auto& state = view_states_[view_id];
  bool dirty = false;
  dirty |= (state.data.z_near != z_near);
  dirty |= (state.data.z_far != z_far);
  dirty |= (state.data.z_scale != z_scale);
  dirty |= (state.data.z_bias != z_bias);

  if (dirty) {
    state.data.z_near = z_near;
    state.data.z_far = z_far;
    state.data.z_scale = z_scale;
    state.data.z_bias = z_bias;
    MarkAllSlotsDirty(view_id);
  }
}

auto EnvironmentDynamicDataManager::UpdateIfNeeded(ViewId view_id) -> void
{
  if (current_slot_ == frame::kInvalidSlot) {
    return;
  }

  auto& state = view_states_[view_id];
  const auto u_current_slot = current_slot_.get();
  const auto slot_index = static_cast<std::size_t>(u_current_slot);
  if (!state.slot_dirty_[slot_index]) {
    return;
  }

  auto info = GetOrCreateBuffer(view_id);
  if (info.buffer && info.mapped_ptr) {
    std::memcpy(info.mapped_ptr, &state.data, sizeof(EnvironmentDynamicData));
    state.slot_dirty_[slot_index] = false;
  }
}

auto EnvironmentDynamicDataManager::GetGpuVirtualAddress(ViewId view_id)
  -> uint64_t
{
  auto info = GetOrCreateBuffer(view_id);
  return info.buffer ? info.buffer->GetGPUVirtualAddress() : 0;
}

auto EnvironmentDynamicDataManager::GetBuffer(ViewId view_id)
  -> std::shared_ptr<graphics::Buffer>
{
  return GetOrCreateBuffer(view_id).buffer;
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
    .debug_name = fmt::format("EnvDynamicData_View{}_Slot{}",
      nostd::to_string(view_id), nostd::to_string(current_slot_)),
  };

  auto buffer = gfx_->CreateBuffer(desc);
  if (!buffer) {
    LOG_F(ERROR,
      "Failed to create environment dynamic data buffer for view {} slot {}",
      view_id, current_slot_);
    return {};
  }

  buffer->SetName(desc.debug_name);

  // Persistently map the buffer
  void* mapped_ptr = buffer->Map();
  if (!mapped_ptr) {
    LOG_F(ERROR,
      "Failed to map environment dynamic data buffer for view {} slot {}",
      view_id, current_slot_);
    return {};
  }

  BufferInfo info { buffer, mapped_ptr };
  buffers_[key] = info;

  return info;
}

auto EnvironmentDynamicDataManager::MarkAllSlotsDirty(ViewId view_id) -> void
{
  // Rationale for marking ALL slots dirty:
  // In a ring-buffered environment (triple buffering), a change made in Frame N
  // must be propagated to the GPU buffers for Slots N, N+1, and N+2 as they
  // each become "current" in subsequent frames. By marking all slots dirty,
  // UpdateIfNeeded will ensure each buffer eventually receives the update
  // exactly once as it enters rotation, avoiding staleness or flickering.
  view_states_[view_id].slot_dirty_.fill(true);
}

} // namespace oxygen::engine::internal
