//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/Internal/EnvironmentDynamicDataManager.h>

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
  // These are contractual, their absence would indicate a serious logic error,
  // and will abort. The lifetime of the environment dynamic data manager is
  // managed by the renderer, which ensures a valid Graphics pointer for as long
  // as the manager is being used.
  CHECK_NOTNULL_F(gfx_, "expecting valid Graphics pointer");
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

auto EnvironmentDynamicDataManager::GetExposure(ViewId view_id) const -> float
{
  const auto it = view_states_.find(view_id);
  if (it == view_states_.end()) {
    return 1.0F;
  }
  return it->second.data.exposure;
}

// SetLightCullingData now contains the logic previously in SetCullingData.
auto EnvironmentDynamicDataManager::SetLightCullingData(
  ViewId view_id, const LightCullingData& data) -> void
{
  auto& state = view_states_[view_id];
  bool dirty = false;
  dirty |= (state.data.bindless_cluster_grid_slot
    != data.bindless_cluster_grid_slot);
  dirty |= (state.data.bindless_cluster_index_list_slot
    != data.bindless_cluster_index_list_slot);
  dirty |= (state.data.cluster_dim_x != data.cluster_dim_x);
  dirty |= (state.data.cluster_dim_y != data.cluster_dim_y);
  dirty |= (state.data.cluster_dim_z != data.cluster_dim_z);
  dirty |= (state.data.tile_size_px != data.tile_size_px);

  if (dirty) {
    state.data.bindless_cluster_grid_slot = data.bindless_cluster_grid_slot;
    state.data.bindless_cluster_index_list_slot
      = data.bindless_cluster_index_list_slot;
    state.data.cluster_dim_x = data.cluster_dim_x;
    state.data.cluster_dim_y = data.cluster_dim_y;
    state.data.cluster_dim_z = data.cluster_dim_z;
    state.data.tile_size_px = data.tile_size_px;
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

auto EnvironmentDynamicDataManager::SetSunState(
  ViewId view_id, const SunState& sun) -> void
{
  auto& state = view_states_[view_id];
  bool dirty = false;
  dirty
    |= (glm::vec3(state.data.sun_direction_ws_illuminance) != sun.direction_ws);
  dirty |= (state.data.sun_direction_ws_illuminance.w != sun.illuminance);
  dirty |= (glm::vec3(state.data.sun_color_rgb_intensity) != sun.color_rgb);
  dirty |= (state.data.sun_color_rgb_intensity.w != sun.intensity);
  const auto enabled_flag = sun.enabled ? 1u : 0u;
  dirty |= (state.data.sun_enabled != enabled_flag);

  if (dirty) {
    state.data.sun_direction_ws_illuminance
      = glm::vec4(sun.direction_ws, sun.illuminance);
    state.data.sun_color_rgb_intensity
      = glm::vec4(sun.color_rgb, sun.intensity);
    state.data.sun_enabled = enabled_flag;
    MarkAllSlotsDirty(view_id);
  }
}

auto EnvironmentDynamicDataManager::SetAtmosphereScattering(ViewId view_id,
  const float aerial_distance_scale, const float aerial_scattering_strength)
  -> void
{
  auto& state = view_states_[view_id];
  bool dirty = false;
  dirty
    |= (state.data.aerial_perspective_distance_scale != aerial_distance_scale);
  dirty
    |= (state.data.aerial_scattering_strength != aerial_scattering_strength);

  if (dirty) {
    state.data.aerial_perspective_distance_scale = aerial_distance_scale;
    state.data.aerial_scattering_strength = aerial_scattering_strength;
    MarkAllSlotsDirty(view_id);
  }
}

auto EnvironmentDynamicDataManager::SetAtmosphereFrameContext(ViewId view_id,
  const glm::vec3& planet_center_ws, const glm::vec3& planet_up_ws,
  const float camera_altitude_m, const float sky_view_lut_slice,
  const float planet_to_sun_cos_zenith) -> void
{
  auto& state = view_states_[view_id];
  bool dirty = false;
  dirty |= (glm::vec3(state.data.planet_center_ws_pad) != planet_center_ws);
  dirty
    |= (glm::vec3(state.data.planet_up_ws_camera_altitude_m) != planet_up_ws);
  dirty |= (state.data.planet_up_ws_camera_altitude_m.w != camera_altitude_m);
  dirty |= (state.data.sky_view_lut_slice_cos_zenith.x != sky_view_lut_slice);
  dirty
    |= (state.data.sky_view_lut_slice_cos_zenith.y != planet_to_sun_cos_zenith);

  if (dirty) {
    state.data.planet_center_ws_pad
      = glm::vec4(planet_center_ws, 0.0F); // w is padding
    state.data.planet_up_ws_camera_altitude_m
      = glm::vec4(planet_up_ws, camera_altitude_m);
    state.data.sky_view_lut_slice_cos_zenith
      = glm::vec4(sky_view_lut_slice, planet_to_sun_cos_zenith, 0.0F, 0.0F);
    MarkAllSlotsDirty(view_id);
  }
}

auto EnvironmentDynamicDataManager::SetAtmosphereFlags(
  ViewId view_id, const std::uint32_t atmosphere_flags) -> void
{
  auto& state = view_states_[view_id];
  if (state.data.atmosphere_flags != atmosphere_flags) {
    state.data.atmosphere_flags = atmosphere_flags;
    MarkAllSlotsDirty(view_id);
  }
}

auto EnvironmentDynamicDataManager::SetAtmosphereSunOverride(
  ViewId view_id, const SunState& sun) -> void
{
  auto& state = view_states_[view_id];
  bool dirty = false;
  dirty |= (glm::vec3(state.data.override_sun_direction_ws_illuminance)
    != sun.direction_ws);
  dirty
    |= (state.data.override_sun_direction_ws_illuminance.w != sun.illuminance);
  dirty |= (glm::vec3(state.data.override_sun_color_rgb_intensity)
    != sun.color_rgb);
  dirty |= (state.data.override_sun_color_rgb_intensity.w != sun.intensity);
  const auto enabled_flag = sun.enabled ? 1u : 0u;
  dirty |= (state.data.override_sun_flags.x != enabled_flag);

  if (dirty) {
    state.data.override_sun_direction_ws_illuminance
      = glm::vec4(sun.direction_ws, sun.illuminance);
    state.data.override_sun_color_rgb_intensity
      = glm::vec4(sun.color_rgb, sun.intensity);
    state.data.override_sun_flags.x = enabled_flag;
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
  DCHECK_F(current_slot_ != frame::kInvalidSlot,
    "proper use of the environment dynamic data manager requires calling its "
    "OnFrameStart() method every frame, and before any use");

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
