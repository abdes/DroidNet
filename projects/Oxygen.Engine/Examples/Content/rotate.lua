local script = {}

local scene = oxygen.scene
local input = oxygen.input
local log = oxygen.log
local omath = oxygen.math

local kDefaultRadiansPerSecond = 0.9
local kSpeedParamKey = "rotation_angle"
local kSpeedStep = 0.25

local current_speed = kDefaultRadiansPerSecond
local speed_initialized = false
local events_wired = false

local function resolve_node(ctx)
  local node = scene.current_node(ctx)
  if node == nil or not node:is_alive() then
    return nil
  end
  return node
end

local function resolve_speed(ctx)
  local value = scene.param(ctx, kSpeedParamKey)
  if type(value) ~= "number" then
    return kDefaultRadiansPerSecond
  end
  return value
end

local function clamp_speed(value)
  if value < 0.0 then
    return 0.0
  end
  return value
end

local function wire_input_once()
  if events_wired then
    return
  end

  input.on_action("RotateSpeedUpAction", input.edges.triggered, function()
    current_speed = current_speed + kSpeedStep
    log.debug(string.format("rotate.lua speed=%.3f", current_speed))
  end)

  input.on_action("RotateSpeedDownAction", input.edges.triggered, function()
    current_speed = clamp_speed(current_speed - kSpeedStep)
    log.debug(string.format("rotate.lua speed=%.3f", current_speed))
  end)

  events_wired = true
end

function script.on_gameplay(ctx, dt_seconds)
  local node = resolve_node(ctx)
  if node == nil then
    return
  end

  if not speed_initialized then
    current_speed = resolve_speed(ctx)
    speed_initialized = true
  end

  wire_input_once()

  local radians_per_second = current_speed
  local angle = radians_per_second * dt_seconds
  node:rotate(omath.quat_from_euler_xyz(0.0, 0.0, angle), true)
end

return script
