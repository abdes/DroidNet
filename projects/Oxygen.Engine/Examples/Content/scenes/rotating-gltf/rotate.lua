local script = oxygen.script {
  params = {
    rotation_angle = oxygen.param.float { default = 0.9, min = 0,
      unit = "rad/s", description = "Initial rotation speed" },
    speed_up = oxygen.param.asset { kind = "input_action",
      description = "Action that speeds the rotation up" },
    speed_down = oxygen.param.asset { kind = "input_action",
      description = "Action that slows the rotation down" },
  },
}

local input = oxygen.input
local log = oxygen.log
local omath = oxygen.math

local kSpeedStep = 0.25

local function on_triggered(action, handler)
  if action ~= nil then
    input.on_action(action, input.edges.triggered, handler)
  end
end

function script.on_start(self)
  self.speed = self.params.rotation_angle
  on_triggered(self.params.speed_up, function()
    self.speed = self.speed + kSpeedStep
    log.debug(string.format("rotate.lua speed=%.3f", self.speed))
  end)
  on_triggered(self.params.speed_down, function()
    self.speed = math.max(0.0, self.speed - kSpeedStep)
    log.debug(string.format("rotate.lua speed=%.3f", self.speed))
  end)
end

function script.on_gameplay(self, dt_seconds)
  local angle = self.speed * dt_seconds
  self.node:rotate(omath.quat_from_euler_xyz(0.0, 0.0, angle), true)
end

return script
