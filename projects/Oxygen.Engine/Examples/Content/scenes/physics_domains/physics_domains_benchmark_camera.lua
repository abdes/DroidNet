local script = {}

local omath = oxygen.math
local scene = oxygen.scene

local state = {
  frame_index = 0,
  initialized = false,
  base_position = nil,
  base_rotation = nil,
}

local function is_node_alive(node)
  return node ~= nil and node:is_alive()
end

local function scene_number_param(ctx, key, default_value)
  local value = scene.param(ctx, key)
  if type(value) == "number" then
    return value
  end
  return default_value
end

local function scene_bool_param(ctx, key, default_value)
  local value = scene.param(ctx, key)
  if type(value) == "boolean" then
    return value
  end
  return default_value
end

function script.on_gameplay(ctx, _dt_seconds)
  local node = scene.current_node(ctx)
  if not is_node_alive(node) then
    return
  end

  if not scene_bool_param(ctx, "enabled", true) then
    return
  end

  if not state.initialized then
    state.base_position = node:get_local_position()
    state.base_rotation = node:get_local_rotation()
    state.initialized = state.base_position ~= nil and state.base_rotation ~= nil
  end

  if not state.initialized then
    return
  end

  state.frame_index = state.frame_index + 1

  local warmup_frames = math.max(0, math.floor(scene_number_param(ctx, "warmup_frames", 0)))
  local fixed_dt_seconds = scene_number_param(ctx, "fixed_dt_seconds", 1.0 / 60.0)
  local sweep_speed_radians = scene_number_param(ctx, "sweep_speed_radians", 0.9)
  local vertical_speed_ratio = scene_number_param(ctx, "vertical_speed_ratio", 0.5)
  local vertical_phase_radians = scene_number_param(ctx, "vertical_phase_radians", 0.0)
  local start_angle_radians = scene_number_param(ctx, "start_angle_radians", 0.0)
  local lateral_amplitude = scene_number_param(ctx, "lateral_amplitude", 3.0)
  local depth_amplitude = scene_number_param(ctx, "depth_amplitude", 5.0)
  local height_amplitude = scene_number_param(ctx, "height_amplitude", 1.25)

  local motion_frame_index = math.max(0, state.frame_index - 1 - warmup_frames)
  local motion_time = motion_frame_index * fixed_dt_seconds
  local orbit_angle = start_angle_radians + (motion_time * sweep_speed_radians)
  local height_angle
    = vertical_phase_radians + (motion_time * sweep_speed_radians * vertical_speed_ratio)

  local local_offset = omath.vec3(
    lateral_amplitude * math.sin(orbit_angle),
    depth_amplitude * (math.cos(orbit_angle) - 1.0),
    height_amplitude * math.sin(height_angle))
  local position = state.base_position + local_offset

  node:set_local_position(position)
  node:set_local_rotation(state.base_rotation)
end

return script
