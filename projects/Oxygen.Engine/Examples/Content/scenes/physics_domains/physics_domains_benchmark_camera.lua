local script = oxygen.script {
  params = {
    enabled = oxygen.param.bool { default = true,
      description = "Drive the camera along the benchmark path" },
    fixed_dt_seconds = oxygen.param.float { default = 0.016666667, min = 0,
      unit = "s", description = "Simulated time per frame" },
    warmup_frames = oxygen.param.int { default = 0, min = 0,
      description = "Frames to wait before moving" },
    sweep_speed_radians = oxygen.param.float { default = 0.9,
      unit = "rad/s", description = "Horizontal sweep speed" },
    vertical_speed_ratio = oxygen.param.float { default = 0.5,
      description = "Vertical speed relative to the sweep" },
    vertical_phase_radians = oxygen.param.float { default = 0,
      unit = "rad", description = "Vertical motion phase" },
    start_angle_radians = oxygen.param.float { default = 0,
      unit = "rad", description = "Sweep start angle" },
    lateral_amplitude = oxygen.param.float { default = 3, unit = "m",
      description = "Sideways travel" },
    depth_amplitude = oxygen.param.float { default = 5, unit = "m",
      description = "Forward travel" },
    height_amplitude = oxygen.param.float { default = 1.25, unit = "m",
      description = "Vertical travel" },
  },
}

local omath = oxygen.math

function script.on_start(self)
  self.frame_index = 0
  self.base_position = self.node:get_local_position()
  self.base_rotation = self.node:get_local_rotation()
end

function script.on_gameplay(self, _dt_seconds)
  local params = self.params
  if not params.enabled or not self.node:is_alive() then
    return
  end

  self.frame_index = self.frame_index + 1

  local motion_frame_index
    = math.max(0, self.frame_index - 1 - math.max(0, params.warmup_frames))
  local motion_time = motion_frame_index * params.fixed_dt_seconds
  local orbit_angle
    = params.start_angle_radians + (motion_time * params.sweep_speed_radians)
  local height_angle = params.vertical_phase_radians
    + (motion_time * params.sweep_speed_radians * params.vertical_speed_ratio)

  local local_offset = omath.vec3(
    params.lateral_amplitude * math.sin(orbit_angle),
    params.depth_amplitude * (math.cos(orbit_angle) - 1.0),
    params.height_amplitude * math.sin(height_angle))
  self.node:set_local_position(self.base_position + local_offset)
  self.node:set_local_rotation(self.base_rotation)
end

return script
