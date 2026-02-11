//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>

#include <string>

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
    if (info.buffer && info.mapped_ptr != nullptr) {
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
  // (e.g., zero light count) instead of stale data from
  // previous frames.
  for (auto& [view_id, state] : view_states_) {
    state.data = EnvironmentDynamicData {};
    MarkAllSlotsDirty(view_id);
  }
}

auto EnvironmentDynamicDataManager::SetLightCullingConfig(
  ViewId view_id, const LightCullingConfig& config) -> void
{
  auto& state = view_states_[view_id];
  bool dirty = false;
  dirty |= (state.data.light_culling.bindless_cluster_grid_slot
    != config.bindless_cluster_grid_slot);
  dirty |= (state.data.light_culling.bindless_cluster_index_list_slot
    != config.bindless_cluster_index_list_slot);
  dirty |= (state.data.light_culling.cluster_dim_x != config.cluster_dim_x);
  dirty |= (state.data.light_culling.cluster_dim_y != config.cluster_dim_y);
  dirty |= (state.data.light_culling.cluster_dim_z != config.cluster_dim_z);
  dirty |= (state.data.light_culling.tile_size_px != config.tile_size_px);
  dirty |= (state.data.light_culling.z_near != config.z_near);
  dirty |= (state.data.light_culling.z_far != config.z_far);
  dirty |= (state.data.light_culling.z_scale != config.z_scale);
  dirty |= (state.data.light_culling.z_bias != config.z_bias);
  dirty |= (state.data.light_culling.max_lights_per_cluster
    != config.max_lights_per_cluster);

  if (dirty) {
    state.data.light_culling = config;
    MarkAllSlotsDirty(view_id);
  }
}

auto EnvironmentDynamicDataManager::SetSunState(
  ViewId view_id, const SyntheticSunData& sun) -> void
{
  constexpr auto kEpsilon = 0.001F;
  auto& state = view_states_[view_id];
  if (!state.data.sun.ApproxEquals(sun, kEpsilon)) {
    state.data.sun = sun;
    MarkAllSlotsDirty(view_id);
  }
}

auto EnvironmentDynamicDataManager::SetAtmosphereScattering(ViewId view_id,
  const float aerial_distance_scale, const float aerial_scattering_strength)
  -> void
{
  auto& state = view_states_[view_id];
  bool dirty = false;
  dirty |= (state.data.atmosphere.aerial_perspective_distance_scale
    != aerial_distance_scale);
  dirty |= (state.data.atmosphere.aerial_scattering_strength
    != aerial_scattering_strength);

  if (dirty) {
    state.data.atmosphere.aerial_perspective_distance_scale
      = aerial_distance_scale;
    state.data.atmosphere.aerial_scattering_strength
      = aerial_scattering_strength;
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
  dirty |= (glm::vec3(state.data.atmosphere.planet_center_ws_pad)
    != planet_center_ws);
  dirty |= (glm::vec3(state.data.atmosphere.planet_up_ws_camera_altitude_m)
    != planet_up_ws);
  dirty |= (state.data.atmosphere.planet_up_ws_camera_altitude_m.w
    != camera_altitude_m);
  dirty |= (state.data.atmosphere.sky_view_lut_slice != sky_view_lut_slice);
  dirty |= (state.data.atmosphere.planet_to_sun_cos_zenith
    != planet_to_sun_cos_zenith);

  if (dirty) {
    state.data.atmosphere.planet_center_ws_pad
      = glm::vec4(planet_center_ws, 0.0F); // w is padding
    state.data.atmosphere.planet_up_ws_camera_altitude_m
      = glm::vec4(planet_up_ws, camera_altitude_m);
    state.data.atmosphere.sky_view_lut_slice = sky_view_lut_slice;
    state.data.atmosphere.planet_to_sun_cos_zenith = planet_to_sun_cos_zenith;
    MarkAllSlotsDirty(view_id);
  }
}

auto EnvironmentDynamicDataManager::UpdateIfNeeded(ViewId view_id) -> void
{
  if (current_slot_ == frame::kInvalidSlot) {
    return;
  }

  auto [it, inserted] = view_states_.try_emplace(view_id);
  auto& state = it->second;
  if (inserted) {
    // This view has never been written via setters yet. Mark it dirty so the
    // first use uploads a deterministic default (zero-initialized) struct.
    MarkAllSlotsDirty(view_id);
  }
  const auto u_current_slot = current_slot_.get();
  const auto slot_index = static_cast<std::size_t>(u_current_slot);
  if (!state.slot_dirty_[slot_index]) {
    return;
  }

  auto info = GetOrCreateBuffer(view_id);
  if (info.buffer && info.mapped_ptr != nullptr) {
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

auto EnvironmentDynamicDataManager::DebugFormat(ViewId view_id) -> std::string
{
  const auto it = view_states_.find(view_id);
  if (it == view_states_.end()) {
    return fmt::format(
      "<no EnvironmentDynamicData for view {}>", nostd::to_string(view_id));
  }

  const auto& d = it->second.data;
  const auto cluster_grid = d.light_culling.bindless_cluster_grid_slot;
  const auto light_list = d.light_culling.bindless_cluster_index_list_slot;

  std::string result
    = fmt::format("view={} slot={} [EnvironmentDynamicData]:\n",
      nostd::to_string(view_id), current_slot_.get());

  result += fmt::format(
    "  [LightCulling]: grid_slot={} list_slot={} dims=({}x{}x{}) tile={}px\n",
    cluster_grid.IsValid() ? fmt::to_string(cluster_grid.value.get())
                           : "invalid",
    light_list.IsValid() ? fmt::to_string(light_list.value.get()) : "invalid",
    d.light_culling.cluster_dim_x, d.light_culling.cluster_dim_y,
    d.light_culling.cluster_dim_z, d.light_culling.tile_size_px);

  result += fmt::format(
    "  [Z-Binning]: near={:.4g} far={:.4g} scale={:.4g} bias={:.4g}\n",
    d.light_culling.z_near, d.light_culling.z_far, d.light_culling.z_scale,
    d.light_culling.z_bias);

  result
    += fmt::format("  [Atmosphere]: flags=0x{:x} sky_view_lut_slice={:.4g} "
                   "cos_zenith={:.4g}\n",
      d.atmosphere.flags, d.atmosphere.sky_view_lut_slice,
      d.atmosphere.planet_to_sun_cos_zenith);

  result += fmt::format("  [AerialPerspective]: dist_scale={:.4g} "
                        "scat_strength={:.4g}\n",
    d.atmosphere.aerial_perspective_distance_scale,
    d.atmosphere.aerial_scattering_strength);

  result += fmt::format("  [PlanetContext]: center=({:.4g}, {:.4g}, {:.4g}) "
                        "up=({:.4g}, {:.4g}, {:.4g}) alt={:.4g}m\n",
    d.atmosphere.planet_center_ws_pad.x, d.atmosphere.planet_center_ws_pad.y,
    d.atmosphere.planet_center_ws_pad.z,
    d.atmosphere.planet_up_ws_camera_altitude_m.x,
    d.atmosphere.planet_up_ws_camera_altitude_m.y,
    d.atmosphere.planet_up_ws_camera_altitude_m.z,
    d.atmosphere.planet_up_ws_camera_altitude_m.w);

  result += fmt::format("  [Sun]: enabled={} cos_zenith={:.4g} "
                        "dir=({:.4g}, {:.4g}, {:.4g}) "
                        "illuminance={:.4g}lx color=({:.4g}, {:.4g}, {:.4g})\n",
    d.sun.enabled, d.sun.cos_zenith, d.sun.direction_ws_illuminance.x,
    d.sun.direction_ws_illuminance.y, d.sun.direction_ws_illuminance.z,
    d.sun.direction_ws_illuminance.w, d.sun.color_rgb_intensity.x,
    d.sun.color_rgb_intensity.y, d.sun.color_rgb_intensity.z);

  return result;
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
  if (mapped_ptr == nullptr) {
    LOG_F(ERROR,
      "Failed to map environment dynamic data buffer for view {} slot {}",
      view_id, current_slot_);
    return {};
  }

  // Ensure deterministic contents even if UpdateIfNeeded is not called before
  // the buffer is bound (or if the view never receives setter updates).
  std::memset(mapped_ptr, 0, kBufferSize);

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
